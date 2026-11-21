# power.cpp：功率↔PWM 换算（A 级）

> 对应源文件：`source/Core/Src/power.cpp`（共 142 行）
> 头文件：`source/Core/Inc/power.hpp`（共 42 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[02-温度控制环精读](../../../references/firmware/IronOS/Docs/02-温度控制环精读.md)

## 职责

把"我要多少瓦"翻译成"PWM 占空计数"，并在输入电压变化时自动补偿。
它是 PID 与硬件之间的唯一换算层，也是"为什么 9 V 和 20 V 下同样 50 W 的占空不同"
这个问题的答案所在。

## 关键常量

| 常量 | 值 | 含义 |
| --- | --- | --- |
| `powerPWM` | 255（BSP） | 可用占空计数上限 |
| `fastPWMChangeoverPoint` | 128 | 快/慢模式切换点 |
| `fastPWMChangeoverTolerance` | 16 | 迟滞带宽 |
| `wattHistoryFilter` | 24 | 实际功率滑动平均窗口 |

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `shouldBeUsingFastPWMMode` | 40 | 带迟滞的快/慢 PWM 选择 |
| `setTipX10Watts` | 59 | 设定功率：换算 → 下发 → 回读统计 |
| `availableW10` | 78 | 估算当前电源满占空可用功率 |
| `X10WattsToPWM` | 110 | 功率 → 占空计数 |
| `PWMToX10Watts` | 138 | 占空计数 → 实际功率（静态） |

## 核心公式

```cpp
uint32_t availableW10(uint8_t sample) {
  // P = V^2 / R, v*v = v^2 * 100
  //				R = R*10
  // P therefore is in V^2*100/R*10 = W*10.
  uint32_t v             = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), sample); // 100 = 10v
  uint32_t tipResistance = getTipResistanceX10();
```

（`source/Core/Src/power.cpp:78-82`）

单位推导：

```
v 的单位 = 0.1 V
R 的单位 = 0.1 Ω
v²/R = (0.01 V²) / (0.1 Ω) = 0.1 W  → 即 x10 W
```

所以 `(v*v)/tipResistance` 直接就是 x10 W，不需要再乘系数。这是整个文件里
最容易看错的一处：变量名叫 `availableWattsX10`，但计算式里看不到任何 ×10。

占空修正：

```cpp
  availableWattsX10 = availableWattsX10 * powerPWM;
  availableWattsX10 /= totalPWM;
```

（`source/Core/Src/power.cpp:96-97`）

Pinecil 快模式 `255/279 ≈ 91.4%`，慢模式 `255/267 ≈ 95.5%`。慢模式的比例更高，
因为采样窗口与死区都减半——这是"大占空比时反而能多给一点功率"的原因。

## 电压前馈

```cpp
  pwm = (powerPWM * x10Watts) / availableW10(sample);
```

（`source/Core/Src/power.cpp:122`）

同样是 50 W 请求：

| 输入电压 | availableW10 | 计算出的占空 | 占空比 |
| --- | --- | --- | --- |
| 9 V | 99 | 255*500/99 → 夹到 255 | 100% |
| 15 V | 274 | 255*500/274 = 465 → 夹到 255 | 100% |
| 20 V | 487 | 255*500/487 = 262 → 夹到 255 | 100% |

注意：50 W 在 9 V 下根本给不出（最大 9.9 W），所以会被夹到 255 并"尽力而为"。
真正体现前馈的是小功率请求，例如 10 W：

| 电压 | availableW10 | 占空计数 |
| --- | --- | --- |
| 9 V | 99 | 255*100/99 → 255（仍超限） |
| 20 V | 487 | 255*100/487 = 52 |

## 回读统计

```cpp
void setTipX10Watts(int32_t mw) {
  int32_t    outputPWMLevel   = X10WattsToPWM(mw, 1);
  const bool shouldUseFastPWM = shouldBeUsingFastPWMMode(outputPWMLevel);
  setTipPWM(outputPWMLevel, shouldUseFastPWM);
  uint32_t actualMilliWatts = PWMToX10Watts(outputPWMLevel, 0);

  x10WattHistory.update(actualMilliWatts);
}
```

（`source/Core/Src/power.cpp:59-66`）

`x10WattHistory` 是 24 点滑动平均，GUI 的功率条与"实际输出"都读它。
它不是 PID 的目标值，而是"按当前电压估算的真实输出"，所以电池电压下降时
功率条会同步变短。

## 快/慢 PWM 迟滞

```
占空 > 144 → 慢模式（500 Hz）
占空 < 112 → 快模式（1 kHz）
112..144  → 保持
```

迟滞的意义：切换模式要重写 TIMER1 的 `CAR`/`PSC`/`CH0CV`，如果每周期都切，
会不断产生边界上的异常脉冲；16 个计数的带宽足以覆盖 ADC 噪声引起的抖动。

## 陷阱

- `X10WattsToPWM(0)` 仍会读一次输入电压（保持滤波器更新），不是纯空操作。
- `tipResistance == 0` 返回 100（10 W）是失效安全，不是正常值。
- `PWMToX10Watts` 只是估算，不能作为控制反馈（它假设采样瞬间电压稳定）。
- 函数名里的 `mw`/`milliWatts` 是历史命名，实际单位是 0.1 W。

## 相关

- [pid_thread.md](pid_thread.md)：调用方
- [tip_thermo_model.md](tip_thermo_model.md)：温度侧换算
- [bsp_pinecil_irq.md](bsp_pinecil_irq.md)：`powerPWM`/`totalPWM` 的硬件来源
