# LowPassFilter.h —— 定点一阶低通滤波

对应源文件：`src/include/LowPassFilter.h`（共 90 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[03-链路质量与自适应速率](../../../references/firmware/ExpressLRS/Docs/03-链路质量与自适应速率.md)

---

## 1. 文件职责

对 RSSI、相位偏差等抖动大的量做指数滑动平均。纯头文件、无分配、可在 ISR 调用。

---

## 2. 成员变量

| 成员 | 行号 | 默认 | 作用 |
|---|---|---|---|
| `SmoothDataINT` | 31 | — | 整数输出 |
| `SmoothDataFP` | 32 | — | 定点中间值 |
| `Beta` | 33 | 3 | 平滑系数（窗口 ≈ 2^Beta） |
| `FP_Shift` | 34 | 5 | 小数位数 |
| `NeedReset` | 35 | true | 首样本直接灌入 |

---

## 3. 构造函数

| 构造 | 行号 | 用法 |
|---|---|---|
| `LPF(Beta, FP_Shift)` | 38 | 显式指定两者 |
| `LPF(Beta)` | 44 | 只指定 Beta，FP_Shift 默认 5 |
| `LPF()` | 49 | Beta=3、FP_Shift=5 |

ELRS 里的实例：

| 实例 | 文件:行 | Beta | 用途 |
|---|---|---|---|
| `LPF_Offset(2)` | `rx_main.cpp` 170 | 2 | 相位偏差（窗口 4） |
| `LPF_OffsetDx(4)` | `rx_main.cpp` 171 | 4 | 相位变化率（窗口 16） |
| `LPF_UplinkRSSI0(5)` | `rx_main.cpp` 177 | 5 | 天线 0 RSSI（窗口 32） |
| `LPF_UplinkRSSI1(5)` | `rx_main.cpp` 178 | 5 | 天线 1 RSSI |

---

## 4. 算法

```c
    int32_t ICACHE_RAM_ATTR update(int32_t Indata)
    {
        if (NeedReset)
        {
            init(Indata);
            return SmoothDataINT;
        }

        int RawData;
        RawData = Indata;
        RawData <<= FP_Shift; // Shift to fixed point
        SmoothDataFP = (SmoothDataFP << Beta) - SmoothDataFP;
        SmoothDataFP += RawData;
        SmoothDataFP >>= Beta;
        SmoothDataINT = SmoothDataFP >> FP_Shift;
        return SmoothDataINT;
    }
```

数学形式：

```
y[n] = y[n-1] + (x[n] - y[n-1]) / 2^Beta
```

代码用“先放大再移位”实现，避免浮点：

```
SmoothDataFP = (SmoothDataFP * (2^Beta - 1) + x * 2^FP_Shift) / 2^Beta
```

`(SmoothDataFP << Beta) - SmoothDataFP` 等于乘 `2^Beta - 1`，用两次移位一次减法
代替乘法——在 ESP8266 上没有硬件乘法器时这是显著优化。

### 4.1 时间常数

| Beta | 等效窗口（样本） | 500Hz 下的时间 | 50Hz 下的时间 |
|---|---|---|---|
| 2 | 4 | 8 ms | 80 ms |
| 3 | 8 | 16 ms | 160 ms |
| 4 | 16 | 32 ms | 320 ms |
| 5 | 32 | 64 ms | 640 ms |

**注意**：窗口是“样本数”，不是时间。换速率会改变绝对时间常数，
这是调参时容易忽略的耦合。

### 4.2 `init()` 与 `reset()`

```c
    void ICACHE_RAM_ATTR reset()
    {
        NeedReset = true;
    }

    void ICACHE_RAM_ATTR init(int32_t Indata)
    {
        NeedReset = false;

        SmoothDataINT = Indata;
        SmoothDataFP = SmoothDataINT << FP_Shift;
    }
```

`reset()` 只置标志，下一次 `update()` 会用新值直接灌入（阶跃启动）。
`switchAntenna()` 里就调用了它：

```c
        (antenna == 0) ? LPF_UplinkRSSI0.reset() : LPF_UplinkRSSI1.reset(); // discard the outdated value after switching
```

如果不 reset，切换天线后滤波器还带着旧天线的历史值，分集算法会误判。

---

## 5. 精度分析

`FP_Shift = 5` 表示内部保留 1/32 的精度。误差来源：

| 来源 | 大小 | 说明 |
|---|---|---|
| 输入量化 | ±0.5 | 输入是整数 dBm |
| 定点截断 | ±1/32 | 每次 `>> FP_Shift` 截断 |
| 累积误差 | 不超过 ±1 | 反馈结构不会累积 |

对 RSSI（-120..0）来说，1dB 的精度完全够用；对相位偏差（单位是定时器 tick）
也够用。

---

## 6. 设计要点与观察

1. **ISR 安全**：无循环、无分配、无除法。
2. **阶跃启动**：`NeedReset` 避免从 0 缓慢爬升。
3. **`value()` 只读**：不推进滤波器，适合“同一拍多次读”。
4. **两个构造函数重载**：`LPF(2)` 与 `LPF(2, 5)` 语义不同，别写错。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 31-35 行 | 成员是 public，外部可直接改 `Beta` 而不重算 |
| 55 行 | `Indata` 为负时右移是算术移位（实现定义），GCC 下行为正确 |

---

## 7. 互链

- 使用：[rx_main.md](rx_main.md)
- 滤波与功率策略：[03-链路质量与自适应速率](../../../references/firmware/ExpressLRS/Docs/03-链路质量与自适应速率.md)
