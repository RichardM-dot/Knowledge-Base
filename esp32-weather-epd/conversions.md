# conversions.cpp —— 单位换算工具（B级）

对应源文件：`platformio/src/conversions.cpp`（共 153 行）；声明头 `platformio/include/conversions.h`（48 行）
相关文档：[Docs/04-UI布局与绘制管线](../../../references/firmware/esp32-weather-epd/Docs/04-UI布局与绘制管线.md)
互链：[renderer.md](renderer.md) · [api_response.md](api_response.md)

## 0. 文件定位

19 个纯函数（无状态、无副作用），服务 renderer.cpp 的 `UNITS_*` 编译期单位分支。已加 B 级文件头导读块。OWM 原始单位：温度开尔文、风速 m/s、气压 hPa、能见度 m、降水量 mm。

## 1. 逐函数表

### 温度（4）

| 函数 | 行号 | 公式 |
|---|---|---|
| `kelvin_to_celsius` | 46 | K − 273.15 |
| `kelvin_to_fahrenheit` | 51 | K×9/5 − 459.67 |
| `celsius_to_kelvin` | 56 | ℃ + 273.15 |
| `celsius_to_fahrenheit` | 61 | ℃×9/5 + 32 |

### 风速（5）

| 函数 | 行号 | 系数 |
|---|---|---|
| `meterspersecond_to_feetpersecond` | 66 | ×3.281 |
| `meterspersecond_to_kilometersperhour` | 71 | ×3.6（精确） |
| `meterspersecond_to_milesperhour` | 76 | ×2.237 |
| `meterspersecond_to_knots` | 81 | ×1.944 |
| `meterspersecond_to_beaufort` | 86 | 幂函数拟合，见下 |

### 气压（7）

| 函数 | 行号 | 系数（hPa→） |
|---|---|---|
| `hectopascals_to_pascals` | 94 | ×100 |
| `hectopascals_to_millimetersofmercury` | 99 | ×0.7501 |
| `hectopascals_to_inchesofmercury` | 104 | ×0.02953 |
| `hectopascals_to_millibars` | 109 | ×1（等价单位） |
| `hectopascals_to_atmospheres` | 114 | ×9.869e-4 |
| `hectopascals_to_gramspersquarecentimeter` | 119 | ×1.02 |
| `hectopascals_to_poundspersquareinch` | 124 | ×0.0145 |

### 距离（3）与降水（2）

| 函数 | 行号 | 系数 |
|---|---|---|
| `meters_to_kilometers` | 129 | ×0.001 |
| `meters_to_miles` | 134 | ×6.214e-4 |
| `meters_to_feet` | 139 | ×3.281 |
| `millimeters_to_inches` | 144 | ÷25.4 |
| `millimeters_to_centimeters` | 149 | ÷10 |

## 2. 关键摘录：蒲福风级（conversions.cpp:86-92）

```cpp
int meterspersecond_to_beaufort(float meterspersecond)
{
  int beaufort = (int) ((powf( 1 / 0.836f, 2.f/3.f)
                         * powf(meterspersecond, 2.f/3.f))
                        + .5f);
  return beaufort > 12 ? 12 : beaufort;
} // end meterspersecond_to_beaufort
```

`B = (v/0.836)^(2/3)` 是蒲福风级经验公式（0.836 m/s 为 1 级下限），+0.5 后截断即四舍五入；>12 级钳位。

## 3. 关键摘录：华氏换算与气压（conversions.cpp:51-53、104-110）

```cpp
float kelvin_to_fahrenheit(float kelvin)
{
  return kelvin * (9.f / 5.f) - 459.67f;
} // end kelvin_to_fahrenheit
```
```cpp
float hectopascals_to_inchesofmercury(float hectopascals)
{
  return hectopascals * 0.02953f;
} // end hectopascals_to_inchesofmercury

float hectopascals_to_millibars(float hectopascals)
{
  return hectopascals * 1.f;
} // end hectopascals_to_millibars
```

`- 459.67f` 是 273.15×9/5+32 的合并常数；`hPa→mbar` 恒等（×1.f）仍保留成函数，让 renderer 的 8 个单位分支书写一致。

## 4. 深读要点

- **精度定位**：系数取 3~4 位有效数字（如 1 m/s = 2.23694 mph 写成 2.237），为显示服务；不要复用到导航/工程场景。
- **float 传播**：全 float 无 double（ESP32 单精度 FPU，double 慢 10 倍以上）。
- **调用模式**：renderer.cpp 的 `#ifdef UNITS_SPEED_*` 五分支每个分支只调一次对应函数，未选中的分支不编译——运行期零开销。

## 4. 数据流角色

`owm_*.temp/wind_speed/pressure/visibility/rain/snow`（原始单位）→ 本文件 → 渲染字符串。见 [api_response.md](api_response.md) 字段表与 [renderer.md](renderer.md) 各 drawCurrent*。
