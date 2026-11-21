# rxtx_common.cpp —— 延迟执行、I2C 与安全重启

对应源文件：`src/src/rxtx_common.cpp`（共 140 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读 + 本条目逐函数表）
> 相关文档：[01-启动流程与状态机](../../../references/firmware/ExpressLRS/Docs/01-启动流程与状态机.md)、
> [05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)

---

## 1. 文件职责

TX/RX 共用的小工具集：

1. **延迟执行队列**（3 槽位）——把“不能在 ISR 做”的事推到主循环；
2. **I2C 初始化**——按硬件定义或 PWM 配置决定 SCL/SDA；
3. **安全重启**——等配置落盘后再 `ESP.restart()`。

---

## 2. 延迟执行

### 2.1 数据结构（32-44 行）

```c
static const int maxDeferredFunctions = 3;

struct deferred_t {
    unsigned long started;
    unsigned long timeout;
    std::function<void()> function;
};

static deferred_t deferred[maxDeferredFunctions] = {
    {0, 0, nullptr},
    {0, 0, nullptr},
    {0, 0, nullptr},
};
```

3 个槽位、静态数组、无动态分配。`std::function` 对短 lambda 走 SBO（小对象优化），
不一定会堆分配。

### 2.2 `deferExecutionMicros()`（93 行）

```c
void deferExecutionMicros(unsigned long us, std::function<void()> f)
{
    for (int i=0 ; i<maxDeferredFunctions ; i++)
    {
        if (deferred[i].function == nullptr)
        {
            deferred[i].started = micros();
            deferred[i].timeout = us;
            deferred[i].function = f;
            return;
        }
    }

    // Bail out, there are no slots available!
    DBGLN("No more deferred function slots available!");
}
```

槽位满时**丢弃并打日志**，不阻塞、不扩容。这是刻意的“宁可丢也不卡”。

### 2.3 `executeDeferredFunction()`（110 行）

```c
void executeDeferredFunction(unsigned long now)
{
    // execute deferred function if its time has elapsed
    for (int i=0 ; i<maxDeferredFunctions ; i++)
    {
        if (deferred[i].function != nullptr && (now - deferred[i].started) > deferred[i].timeout)
        {
            deferred[i].function();
            deferred[i].function = nullptr;
        }
    }
}
```

注意 `(now - started) > timeout` 用的是无符号减法，天然处理 millis 回绕。
回调执行期间该槽位仍被占用——如果回调里又投递一个任务，会用别的槽位。

### 2.4 实际调用点

| 调用点 | 文件:行 | 用途 |
|---|---|---|
| `deferExecutionMicros(TOA, Radio.TXdoneCallback)` | `tx_main.cpp` LBT 分支 | LBT 拦截后补发 TXdone |
| `deferExecutionMicros(TOA, ...)` | `rx_main.cpp` 强制关遥测 | 同上 |
| `deferExecutionMillis(500, setWifiUpdateMode)` | `rx_main.cpp` 1482 行 | 等 ACK 发完再进 WiFi |
| `deferExecutionMillis(2000, 清上电计数)` | `rx_main.cpp` 1834 行 | 三插拔判定 |

---

## 3. I2C 初始化（49-86 行）

```c
static void setupWire()
{
    int gpio_scl = GPIO_PIN_SCL;
    int gpio_sda = GPIO_PIN_SDA;

#if defined(TARGET_RX)
    for (int ch = 0 ; ch < GPIO_PIN_PWM_OUTPUTS_COUNT ; ++ch)
    {
        auto pin = GPIO_PIN_PWM_OUTPUTS[ch];
        auto pwm = config.GetPwmChannel(ch);
        // if the PWM pin is nominated as SDA or SCL, and it's not configured for I2C then undef the pins
        if ((pin == GPIO_PIN_SCL && pwm->val.mode != somSCL) || (pin == GPIO_PIN_SDA && pwm->val.mode != somSDA))
        {
            gpio_scl = UNDEF_PIN;
            gpio_sda = UNDEF_PIN;
            break;
        }
        // If I2C pins are not defined in the hardware, then look for configured I2C
        if (GPIO_PIN_SCL == UNDEF_PIN && pwm->val.mode == somSCL)
        {
            gpio_scl = pin;
        }
        if (GPIO_PIN_SCL == UNDEF_PIN && pwm->val.mode == somSDA)
        {
            gpio_sda = pin;
        }
    }
#endif
    if(gpio_sda != UNDEF_PIN && gpio_scl != UNDEF_PIN)
    {
        DBGLN("Starting wire on SCL %d, SDA %d", gpio_scl, gpio_sda);
        Wire.begin(gpio_sda, gpio_scl);
        Wire.setClock(400000);
        i2c_enabled = true;
    }
}
```

两条规则：

1. 硬件定义了 SCL/SDA，但用户把该焊盘配成别的功能 → 整条 I2C 关掉；
2. 硬件没定义，但用户把某个 PWM 焊盘配成 `somSCL`/`somSDA` → 用它。

`Wire.begin(sda, scl)` 的参数顺序是 SDA 在前，与常见的 SCL 在前相反——这是
Arduino 的历史约定，写错会“编译通过但通信失败”。

400kHz 是 Fast Mode，ELRS 的 I2C 设备（气压计、OLED）都支持。

---

## 4. 安全重启（126-140 行）

```c
void scheduleRebootTime(unsigned long inMs)
{
    rebootTime_Ms = millis() + inMs;
}

void checkRebootTime(unsigned long now)
{
    // If the reboot time is set and the current time is past the reboot time then reboot.
    // Wait for any pending config change to be committed first
    if (rebootTime_Ms != 0 && !config.IsModified() && now > rebootTime_Ms ) {
        ESP.restart();
    }
}
```

关键条件 `!config.IsModified()`：如果还有未落盘的配置，就一直等。
这避免了“Web UI 点了保存+重启，结果配置丢了”。

`rebootTime_Ms != 0` 是“是否已预约”的标志（0 表示未预约）。

---

## 5. 函数清单

| 函数 | 行号 | 作用 |
|---|---|---|
| `setupWire()` | 49 | 解析引脚并初始化 I2C |
| `setupTargetCommon()` | 88 | 板级公共初始化（TX/RX 都调） |
| `deferExecutionMicros()` | 93 | 投递延迟任务 |
| `executeDeferredFunction()` | 110 | 执行到期任务 |
| `scheduleRebootTime()` | 126 | 预约重启 |
| `checkRebootTime()` | 134 | 到点重启 |

---

## 6. 设计要点与观察

1. **固定槽位**：3 个够用且可预测，避免堆碎片。
2. **时间用微秒**：`deferExecutionMicros` 是基础，毫秒版是封装（`deferred.h`）。
3. **I2C 双来源**：硬件定义优先，PWM 复用兜底。
4. **重启等落盘**：`checkRebootTime()` 与 `config.IsModified()` 耦合。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 93 行 | 槽位满时静默丢弃，只打日志 |
| 110 行 | 回调若抛异常会跳过 `function = nullptr` |
| 71 行 | `GPIO_PIN_SCL == UNDEF_PIN` 时用 `somSDA` 判断，条件写的是 SCL 而非 SDA（疑似笔误，行为上仍可用） |

---

## 7. 互链

- 接口：[deferred_h.md](deferred_h.md)
- 使用：[tx_main.md](tx_main.md)、[rx_main.md](rx_main.md)
- 启动流程：[01-启动流程与状态机](../../../references/firmware/ExpressLRS/Docs/01-启动流程与状态机.md)
