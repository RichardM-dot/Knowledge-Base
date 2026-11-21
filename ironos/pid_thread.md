# PIDThread：控温闭环（A 级）

> 对应源文件：`source/Core/Threads/PIDThread.cpp`（共 495 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[02-温度控制环精读](../../../references/firmware/IronOS/Docs/02-温度控制环精读.md)

## 职责

整个固件的核心：阻塞等待 ADC 完成中断，读温度、算功率、下发 PWM，并在每周期做
热失控与功率限幅检查。任务优先级 `osPriorityRealtime`，是整个系统里唯一的硬实时任务。

## 全局状态

| 名称 | 类型 | 写入方 | 读取方 | 说明 |
| --- | --- | --- | --- | --- |
| `pidTaskNotification` | `TaskHandle_t` | `startPIDTask` | ADC ISR | ISR 唤醒 PID 的通知句柄 |
| `currentTempTargetDegC` | `volatile TemperatureType_t` | GUI 各模式 | PIDTask | 当前设定温度（°C） |
| `powerSupplyWattageLimit` | `int32_t` | USBPD/QC | PIDTask | 协商得到的功率上限（W） |
| `heaterThermalRunawayCounter` | `uint8_t` | PIDTask | GUI | 热失控"积分"计数 |
| `powerPulseWaitUnit` | `TickType_t` | 常量 | 本文件 | 2500 ms（KeepAwakePulse 基准） |
| `powerPulseDurationUnit` | `TickType_t` | 常量 | 本文件 | 250 ms |

`volatile` 只保证单次读写的可见性，不保证原子性；这里所有共享变量都是单字（≤32 位）
且只有一个写者，因此无需临界区。

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `startPIDTask` | 68 | 任务主体：预热滤波器 → 等自检 → 等 PD → 主循环 |
| `struct PID::update` | 187 | 标准位置式 PID（`TIP_CONTROL_PID` 机型） |
| `struct Integrator::update` | 244 | 自衰减积分器（默认控制器） |
| `Integrator::set/get` | 263/266 | 重置/取积分值（可取带符号值） |
| `getPIDResultX10Watts` | 277 | 按实际采样间隔推进控制器 |
| `detectThermalRunaway` | 350 | 两类热失控判据，累加计数器 |
| `getX10WattageLimits` | 409 | 四级功率限制 + 手柄降额 |
| `setOutputx10WattsViaFilters` | 448 | 输出前保护链 + 写 PWM + 喂狗 |

## 主循环

```cpp
  for (;;) {
    x10WattsOut = 0;
    // This is a call to block this thread until the ADC does its samples
    if (ulTaskNotifyTake(pdTRUE, TICKS_SECOND * 2)) {
      // Do the reading here to keep the temp calculations churning along
      TemperatureType_t currentTipTempInC = TipThermoModel::getTipInC(true);
```

（`source/Core/Threads/PIDThread.cpp:115-120`）

三个要点：

1. 每轮先把 `x10WattsOut` 清零，任何提前 `continue` 都不会沿用上一周期的值；
2. 2 s 超时说明 ADC 中断已停，此时走 `setTipPWM(0, false)` 分支；
3. `getTipInC(true)` 会触发一次新的 ADC 读取并经过滑动滤波。

## 控制器数学

默认的 `Integrator::update`：

```cpp
    sum = (sum * (100 - (inertia / rate))) / 100;
    // Add the new value x integration interval ( 1 / rate)
    sum += (gain * val) / rate;
```

（`source/Core/Threads/PIDThread.cpp:248-250`）

```
S[k] = S[k-1] * (1 - inertia/(100*rate)) + gain*val/rate
```

Pinecil：`inertia=65`、`gain=2`、`rate≈10` →

```
S[k] = 0.935*S[k-1] + 13*ΔT
稳态：S_ss = 200*ΔT  （x10 W，即每 1 °C 误差对应 20 W）
```

`val` 由调用方算好：

```cpp
  return powerStore.update(((TemperatureType_t)getTipThermalMass()) * (set_point - current_reading), // the required power
                           getTipInertia(),                                                          // Inertia, smaller numbers increase dominance of the previous value
                           2,                                                                        // gain
                           rate,                                                                     // PID cycle frequency
                           getX10WattageLimits());
```

（`source/Core/Threads/PIDThread.cpp:323-327`）

`rate = TICKS_SECOND / (now - lastCall)`，因此快/慢 PWM 切换导致周期变化时，
增量自动按频率归一化。

## 功率限制的四层

```cpp
int32_t getX10WattageLimits() {
  int32_t limit = availableW10(0);

  if (getSettingValue(SettingsOptions::PowerLimit) && limit > (getSettingValue(SettingsOptions::PowerLimit) * 10)) {
    limit = getSettingValue(SettingsOptions::PowerLimit) * 10;
  }
  if (powerSupplyWattageLimit && limit > powerSupplyWattageLimit * 10) {
    limit = powerSupplyWattageLimit * 10;
  }
```

（`source/Core/Threads/PIDThread.cpp:409-416`）

| 层 | 来源 | 单位换算 |
| --- | --- | --- |
| 电源能力 | `availableW10()` | 已是 x10 W |
| 用户设置 | `PowerLimit`（W） | ×10 |
| PD/QC 协商 | `powerSupplyWattageLimit`（W） | ×10 |
| 硬件额定 | `HARDWARE_MAX_WATTAGE_X10` | 已是 x10 W |
| 手柄降额 | NTC 温度 | 按百分比乘 |

## 热失控判据

```cpp
  if (!thisCycleIsHeating && (getTipRawTemp(0) > (ADC_MAX_READING - 8)) && heaterThermalRunawayCounter < 255) {
    heaterThermalRunawayCounter++;
  }
```

（`source/Core/Threads/PIDThread.cpp:364-366`）

| 判据 | 阈值 | 对应故障 |
| --- | --- | --- |
| 关断时原始 ADC 贴顶 | `> 32768-8` | MOSFET 击穿 |
| 加热 20 s 温升不足 | `< 3 °C` | 热电偶/运放失效 |
| MCU 过温 | `MCU_TEMP_CUTOFF_C` | 输出级过热 |

计数器每秒衰减 1，`>8` 时输出强制 0（`PIDThread.cpp:478-479`），
GUI 侧切到 `ThermalRunaway` 页面。

## 输出前保护

```cpp
  if (getTipRawTemp(0) > (0x7FFF - 32)) {
    x10WattsOut = 0;
  }
  if (heaterThermalRunawayCounter > 8) {
    x10WattsOut = 0;
  }
```

（`source/Core/Threads/PIDThread.cpp:473-479`）

顺序：KeepAwakePulse 抬底 → 贴顶保护 → 热失控保护 → 可选 `SLEW_LIMIT` →
`setTipX10Watts()` → `resetWatchdog()`。

## 陷阱

- **`getX10WattageLimits()` 会触发 ADC 采样**，不要在中断或高频路径里调用。
- **目标为 0 时仍调用 `detectThermalRunaway`**，这是"关机状态下温度仍上升"能被发现的原因。
- **Pinecil V1 不定义 `TIP_CONTROL_PID`**，走 Integrator；V2 定义并给出 `TIP_PID_KP=40`。
- 修改 `TIP_THERMAL_MASS` 等价于同时改比例与积分增益，调参时注意耦合。

## 相关

- [power_cpp.md](power_cpp.md)：W↔PWM 换算与电压前馈
- [tip_thermo_model.md](tip_thermo_model.md)：温度换算链
- [bsp_pinecil_irq.md](bsp_pinecil_irq.md)：ADC/PWM 中断的另一半
