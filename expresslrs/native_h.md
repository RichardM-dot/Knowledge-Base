# native.h —— PC 端 Arduino 环境伪造

对应源文件：`src/include/native.h`（共 219 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)

---

## 1. 文件职责

让 `src/` 与 `lib/` 的业务代码**不修改一行**就能在 x86 上编译运行单元测试。
它伪造了 Arduino 核心提供的一切：引脚宏、Stream、时间函数、数学宏。

只在 `TARGET_NATIVE` 下由 `targets.h` 包含，真实固件里不存在。

---

## 2. 伪造清单

| 类别 | 内容 | 行号 |
|---|---|---|
| 设备名 | `DEVICE_NAME "testing"` | 35 |
| 射频 | `RADIO_SX128X 1` | 36 |
| 引脚 | `GPIO_PIN_BUSY/NSS_2` = `UNDEF_PIN` | 39-40 |
| 选项 | `OPT_USE_HARDWARE_DCDC false` 等 | 41-45 |
| 类型 | `typedef uint8_t byte` | 47 |
| 常量 | `HEX/DEC` | 49-50 |
| `Stream` | 纯虚基类实现 | 52-88 |
| `HardwareSerial` | 空实现 | 90-114 |
| 时间 | `micros()` 用 `gettimeofday` | 121-125 |
| 时钟 | `nativeClockMs()` 可写引用 | 136 |
| 数学 | `min/constrain/bit/itoa` | 132-141 |
| BSD 函数 | `strlcpy/stpcpy/strlcat`（Windows） | 160-215 |

---

## 3. 关键设计：可快进的时钟

```c
// Starts at 0 as it always has. Tests that need to exercise timeouts can advance it, e.g.
// `nativeClockMs() += 1000;`
inline unsigned long &nativeClockMs() { static unsigned long ms = 0; return ms; }
inline unsigned long millis() { return nativeClockMs(); }
```

返回**引用**而不是值，于是测试代码可以：

```cpp
nativeClockMs() += 1000;   // 快进 1 秒
```

所有基于 `millis()` 的超时逻辑（`DisconnectTimeoutMs`、`deferExecutionMillis`）
都能瞬间跑完，不需要真的 sleep。这是整个 native 测试体系能快速运行的基础。

`micros()` 则用真实时间：

```c
inline unsigned long micros() {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
}
```

因为微秒级逻辑（相位、PFD）在测试里很少需要快进。

---

## 4. `Stream` 与 `HardwareSerial`

```c
class Stream
{
public:
    Stream() {}
    virtual ~Stream() {}

    // Stream methods
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual void flush() = 0;

    virtual size_t readBytes(uint8_t *buffer, size_t length)
    {
        size_t count = 0;
        while (count < length && available() > 0)
        {
            const int value = read();
            if (value < 0)
                break;
            buffer[count++] = static_cast<uint8_t>(value);
        }
        return count;
    }
    ...
```

接口与 Arduino 的 `Stream` 完全一致（纯虚 + `readBytes` 默认实现），
所以 `NullStream`（`helpers.h`）能在两种环境下都编译通过。

`HardwareSerial` 提供空实现：

| 方法 | 行为 |
|---|---|
| `begin(baud)` | 空 |
| `available()` | 0 |
| `read()` | -1 |
| `write()` | 返回长度（假装成功） |
| `availableForWrite()` | 256 |

---

## 5. 数学与字符串

```c
#define bit(x) (1 << (x))
// Not a macro, so it does not break the qualified std::min calls in the codebase
inline int min(int a, int b) { return (a < b) ? a : b; }
```

`min` 特意写成**内联函数而不是宏**，因为代码里有 `std::min(...)` 调用，
宏会破坏 `std::` 限定。这个注释是踩坑后的记录。

`constrain` 仍是宏：

```c
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
```

因为它没有命名空间冲突风险。

Windows 上补的三个 BSD 函数（`strlcpy`/`stpcpy`/`strlcat`）来自 OpenBSD，
带完整版权头，用于让 Linux/macOS 上能编译的代码在 MSVC/MinGW 上也能过。

---

## 6. 设计要点与观察

1. **零侵入**：业务代码不需要 `#ifdef UNIT_TEST`，全靠这层伪造。
2. **可快进时钟**：`nativeClockMs()` 是测试速度的关键。
3. **接口等价**：`Stream` 的纯虚列表与 Arduino 一致，保证 `NullStream` 通用。
4. **`min` 用函数不用宏**：避免破坏 `std::min`。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 96 行 | `static HardwareSerial Serial;` 每个 TU 一份（内部链接），可能浪费空间 |
| 121 行 | `micros()` 用 `gettimeofday`，在容器里可能被时钟调整影响 |
| 45 行 | 伪造的 `OPT_*` 只覆盖了部分库的需求，新增库可能要补 |

---

## 7. 互链

- 编译期闸门：[targets_h.md](targets_h.md)
- 工具类：[helpers_h.md](helpers_h.md)
- 构建配置：[05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)
