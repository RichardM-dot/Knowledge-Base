# TipThermoModel：温度换算链（A 级）

> 对应源文件：`source/Core/Drivers/TipThermoModel.cpp`（共 150 行）
> 关联实现：`source/Core/BSP/Pinecil/ThermoModel.cpp`（Hakko 查表）、`source/Core/Drivers/Utils.cpp`（插值）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[02-温度控制环精读](../../../references/firmware/IronOS/Docs/02-温度控制环精读.md)

## 职责

把 ADC 原始码变成"烙铁头绝对温度（°C）"。整条链路四级：

```
raw ADC → mV → uV（除运放增益）→ 减校准偏置 → 查表得温差 → + 冷端温度
```

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `convertTipRawADCTouV` | 52 | ADC → 运放输入 uV，扣除校准偏置 |
| `convertTipRawADCToDegC` | 86 | 一步到 °C（便捷重载） |
| `convertTipRawADCToDegF` | 87 | 一步到 °F |
| `convertuVToDegF` | 89 | uV → °F |
| `convertCtoF` | 96 | 摄氏 → 华氏（整数） |
| `convertFtoC` | 103 | 华氏 → 摄氏，<32 °F 归 0 |
| `getTipInC` | 118 | 取当前温度并加冷端补偿 |
| `getTipInF` | 128 | 同上，返回 °F |
| `getTipMaxInC` | 142 | 当前硬件/校准能测到的上限 |

## 第一级：ADC → uV

```cpp
uint32_t          TipThermoModel::convertTipRawADCTouV(uint16_t rawADC, bool skipCalOffset) {
  // This takes the raw ADC samples, converts these to uV
  // Then divides this down by the gain to convert to the uV on the input to the op-amp (A+B terminals)
  // Then remove the calibration value that is stored as a tip offset
  uint32_t vddRailmVX10 = ADC_VDD_MV * 10; // The vreg is +-2%, but we have no higher accuracy available
```

（`source/Core/Drivers/TipThermoModel.cpp:52-56`）

```cpp
  uint32_t rawInputmVX10 = (rawADC * vddRailmVX10) / (ADC_MAX_READING);

  uint32_t valueuV = rawInputmVX10 * 100; // shift into uV
  // Now to divide this down by the gain
  valueuV /= OP_AMP_GAIN_STAGE;
```

（`source/Core/Drivers/TipThermoModel.cpp:63-69`）

数值示例（Pinecil V1，raw=16000）：

| 步骤 | 计算 | 结果 |
| --- | --- | --- |
| mV×10 | 16000 × 33000 / 32768 | 16113 |
| uV | 16113 × 100 | 1 611 300 |
| 除增益 | 1611300 / 317 | 5082 uV |

`skipCalOffset=true` 时跳过校准偏置，供 CJC 校准读取"未修正"的原始值。

## 第二级：uV → 温差

`convertuVToDegC()` 在板级文件里，用 Hakko K 型热电偶表插值：

```cpp
const int32_t uVtoDegC[] = {
    //
    // uv -> temp in C
    0,     0,   //
    266,   10,  //
    522,   20,  //
```

（`source/Core/BSP/Pinecil/ThermoModel.cpp:12-16`）

表项是 `(uV, °C)` 对，每 10 °C 一点，`Utils::InterpolateLookupTable` 线性插值。
注意 0→10 °C 需要 266 uV，而 300→310 °C 需要 273 uV——Seebeck 系数随温度略增，
所以不能用一个固定斜率。

## 第三级：加冷端

```cpp
TemperatureType_t TipThermoModel::getTipInC(bool sampleNow) {
  TemperatureType_t currentTipTempInC = TipThermoModel::convertTipRawADCToDegC(getTipRawTemp(sampleNow));
  currentTipTempInC += getHandleTemperature(sampleNow) / 10; // Add handle offset
```

（`source/Core/Drivers/TipThermoModel.cpp:118-120`）

热电偶输出只反映"热端 − 冷端"温差，必须加上手柄 NTC 的冷端温度。
`getHandleTemperature()` 返回 0.1 °C，所以除以 10。

## 上限与拔出检测

```cpp
TemperatureType_t TipThermoModel::getTipMaxInC() {
#ifdef CUSTOM_MAX_TEMP_C
  return getCustomTipMaxInC();
#else
  TemperatureType_t maximumTipTemp = TipThermoModel::convertTipRawADCToDegC(ADC_MAX_READING - 1);
  maximumTipTemp += getHandleTemperature(0) / 10; // Add handle offset
  return maximumTipTemp - 1;
#endif
}
```

（`source/Core/Drivers/TipThermoModel.cpp:142-149`）

它有两个用途：

1. 限制设定点（PID 会把目标夹到 `getTipMaxInC()`）；
2. 判断烙铁头是否拔出（读数贴近上限 → `isTipDisconnected()`）。

## 陷阱

- **忘记冷端补偿的症状**：冬天开机温度偏低、夏天偏高，且校准点随环境漂移。
- **`convertFtoC` 对 <32 °F 返回 0**：避免负温度进入 PID（会算出负功率）。
- **`lastuv` 只是调试量**，不参与控制；改它不会影响温度。
- **校准偏置是 uV，不是 ADC 计数**：写 `CalibrationOffset` 时要先换算。

## 相关

- [pid_thread.md](pid_thread.md)：使用温度的地方
- [settings_persistence.md](settings_persistence.md)：`CalibrationOffset` 的存储
- [power_cpp.md](power_cpp.md)：功率侧换算
