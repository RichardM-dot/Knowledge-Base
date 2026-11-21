# dynpower.h —— 动态功率接口与哨兵

对应源文件：`src/include/dynpower.h`（共 42 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[03-链路质量与自适应速率](../../../references/firmware/ExpressLRS/Docs/03-链路质量与自适应速率.md)

---

## 1. 文件职责

声明 TX/RX 两侧的动态功率 API，并定义两个“伪 SNR”哨兵值。

---

## 2. 哨兵值

```c
#define DYNPOWER_UPDATE_NOUPDATE -128
#define DYNPOWER_UPDATE_MISSED   -127
```

| 值 | 名称 | 含义 |
|---|---|---|
| -128 | `DYNPOWER_UPDATE_NOUPDATE` | 本拍没有新遥测，不做判断 |
| -127 | `DYNPOWER_UPDATE_MISSED` | 本拍该有遥测但没收到 |

为什么选 -128/-127：

1. 合法 SNR（定点后）大约在 -20..+30，两者远在范围外；
2. 两者相邻，可以一次比较区分“无更新 / 丢失 / 有效”；
3. `int8_t` 的最小值就是 -128，不额外占空间。

判断代码（`dynpower.cpp` 98 行附近）：

```c
  bool newTlmAvail = snrScaled > DYNPOWER_UPDATE_MISSED;
  bool lastTlmMissed = snrScaled == DYNPOWER_UPDATE_MISSED;
```

`> -127` 即“是有效 SNR 值”（因为 NOUPDATE=-128 < -127）。

---

## 3. 接口

### 3.1 TX 侧

| 函数 | 行号 | 调用时机 | 说明 |
|---|---|---|---|
| `DynamicPower_Init()` | 30 | `setup()` 一次 | 复位滑动平均与状态 |
| `DynamicPower_Update(now)` | 32 | `loop()` 每轮 | 决策树 |
| `DynamicPower_TelemetryUpdate(snr)` | 34 | ISR | 记录 SNR 或 MISSED |

注释明确了调用约定：

```c
// Call DynamicPower_TelemetryUpdate from ISR with DYNPOWER_UPDATE_MISSED or ScaledSNR value
```

### 3.2 RX 侧

| 函数 | 行号 | 调用时机 | 说明 |
|---|---|---|---|
| `DynamicPower_UpdateRx(initialize)` | 41 | `loop()` / `setupRadio` | 固定功率或跟随 TX |

`initialize=true` 时即使 TX 功率没变也会设一次默认功率（启动用）。

---

## 4. 目标裁剪

```c
#if defined(TARGET_TX)
...
#endif // TARGET_TX

#if defined(TARGET_RX)
...
#endif // TARGET_RX
```

TX 编译时看不到 `DynamicPower_UpdateRx`，RX 编译时看不到另外三个。
调用方（`tx_main.cpp`/`rx_main.cpp`）天然在各自目标下编译，不会链接错误。

---

## 5. 设计要点与观察

1. **哨兵相邻**：`-128/-127` 让判断只需一次比较。
2. **ISR 只写一个字节**：`dynpower_updated` 是 `int8_t`，写入原子。
3. **初始化与更新分离**：`Init` 只清状态，`Update` 做决策。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 7-8 行 | 哨兵值是宏而非 enum，无法被调试器识别为符号 |
| 32 行 | `DynamicPower_Update` 的参数是 `now`（millis），与 `executeDeferredFunction` 的 `now` 同名但语义不同 |

---

## 6. 互链

- 实现：[dynpower_cpp.md](dynpower_cpp.md)
- 调用方：[tx_main.md](tx_main.md)、[rx_main.md](rx_main.md)
- 策略：[03-链路质量与自适应速率](../../../references/firmware/ExpressLRS/Docs/03-链路质量与自适应速率.md)
