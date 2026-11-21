# display_utils.cpp —— 渲染辅助函数集（B级）

对应源文件：`platformio/src/display_utils.cpp`（共 1688 行）；声明头 `platformio/include/display_utils.h`（85 行）
相关文档：[Docs/04-UI布局与绘制管线](../../../references/firmware/esp32-weather-epd/Docs/04-UI布局与绘制管线.md) · [Docs/05-低功耗与实测指南](../../../references/firmware/esp32-weather-epd/Docs/05-低功耗与实测指南.md)
互链：[renderer.md](renderer.md) · [main.md](main.md) · [api_response.md](api_response.md)

## 0. 文件定位

"绘图后勤"：天气图标映射（模板 + 4 维判据）、风向罗盘、月相、预警过滤、电池 ADC、错误码→文案。不持有屏幕对象。已加 B 级文件头导读块（函数清单版）。

## 1. 逐函数表（分组）

### 电池

| 函数 | 行号 | 说明 |
|---|---|---|
| `readBatteryVoltage` | 72 | ADC(A2) 采样 → eFuse 校准（11db/12bit）→ ×2 还原 1M+1M 分压 |
| `calcBatPercent` | 123 | S 型拟合 `105-105/(1+(1.724x)^5.5)`，源自 BatterySense（LGPL） |
| `getBatBitmap24` | 138 | 百分比 → 8 档电池图标（93/79/65/50/36/22/8%） |

### 文案与字符串

| 函数 | 行号 | 说明 |
|---|---|---|
| `getDateStr` | 176 | DATE_FORMAT 经 `_strftime`，压缩双空格（%e 补位） |
| `getRefreshTimeStr` | 189 | REFRESH_TIME_FORMAT；对时失败返回 TXT_UNKNOWN |
| `toTitleCase` | 212 | 每词首字母大写（空格/-/( 后） |
| `truncateExtraAlertInfo` | 240 | 在 `,`/`.`/`(` 处截断并去尾空格 |
| `getUVIdesc` | 383 | 0-2/3-5/6-7/8-10/11+ 五档 |
| `getWiFidesc` | 409 | RSSI 0/-50/-60/-70 四档 |
| `getHttpResponsePhrase` | 1420 | HTTP/HTTPClient/JSON/WiFi 四段错误码 → 文案（switch 全展开） |
| `getWifiStatusPhrase` | 1539 | wl_status_t → 文案 |

### 预警

| 函数 | 行号 | 说明 |
|---|---|---|
| `eventUrgency` | 276 | 命中 ALERT_URGENCY（config.h）关键词 → 等级，未命中 -1 |
| `filterAlerts` | 323 | 转小写 → 同 tag 保最紧急 → 保最新 2 条 → 截断；输出 ignore_list |
| `getAlertCategory` | 820 | event 命中 locale 的 TERM_* → 24 类之一 |
| `getAlertBitmap32/48` | 714 / 760 | 类别 → 图标（NOT_FOUND=warning 三角） |
| `containsTerminology` | 803 | 词表匹配（大小写敏感，输入已小写化） |

### 天气图标

| 函数 | 行号 | 说明 |
|---|---|---|
| `isDay` | 461 | icon 尾字符 d/n |
| `isMoonInSky` | 469 | 时间窗 + 非新月 |
| `isCloudy` | 486 | 云量 > 60.25% |
| `isWindy` | 496 | 风速≥32.2 或阵风≥40.2 m/s |
| `getConditionsBitmap<BitmapSize>` | 513（模板） | 天气码×昼夜×月×云×风 → 图标，40+ case + 分组兜底 |
| `getHourlyForecastBitmap32` | 662 | 封装 `<32>` |
| `getDailyForecastBitmap64` | 677 | 封装 `<64>`（恒白天无月） |
| `getCurrentConditionsBitmap196` | 694 | 封装 `<196>` |

### 风向 / 月相 / 其他

| 函数 | 行号 | 说明 |
|---|---|---|
| `wind_direction_icon_arr[]` | 922/929/940/959/994 | 按 WIND_ICONS_* 宏五选一（4/8/16/32/360 向） |
| `getWindBitmap24` | 1361 | 最近邻：`(deg+半格) % 360 ÷ 格宽`，360 向时 ±0.5° |
| `getCompassPointNotation` | 1382 | CPN 文案（32 向全表 COMPASS_POINT_NOTATION，精度宏选 4/8/16/32） |
| `getWiFiBitmap16` | 435 | RSSI → 4 格/3 格/2 格/1 格/断连图标 |
| `disableBuiltinLED` | 1560 | 熄灭 + gpio_hold_en + deep_sleep_hold_en（跨深睡保持） |
| `getMoonPhaseBitmap48` | 1644 | `moon_phase×28+0.5` → 29 项图标数组 |
| `getMoonPhaseStr` | 1652 | 同映射 → 8 种月相名 |

## 2. 关键摘录

### 2.1 电量 S 型拟合（display_utils.cpp:123-134）

```cpp
uint32_t calcBatPercent(uint32_t v, uint32_t minv, uint32_t maxv)
{
  // slow
  //uint32_t p = 110 - (110 / (1 + pow(1.468 * (v - minv)/(maxv - minv), 6)));

  // steep
  //uint32_t p = 102 - (102 / (1 + pow(1.621 * (v - minv)/(maxv - minv), 8.1)));

  // normal
  uint32_t p = 105 - (105 / (1 + pow(1.724 * (v - minv)/(maxv - minv), 5.5)));
  return p >= 100 ? 100 : p;
} // end calcBatPercent
```

三组被注释的系数对应三种曲线形态（slow/steep/normal），函数头注释里给了 desmos 验证链接——调电量显示手感的入口。

### 2.2 图标模板（display_utils.cpp:513-527，节选）

```cpp
template <int BitmapSize>
const uint8_t *getConditionsBitmap(int id, bool day, bool moon, bool cloudy,
                                   bool windy)
{
  switch (id)
  {
  // Group 2xx: Thunderstorm
  case 200: // Thunderstorm  thunderstorm with light rain     11d
  ...
    if (!cloudy && day)          {return getBitmap(wi_day_thunderstorm, BitmapSize);}
    if (!cloudy && !day && moon) {return getBitmap(wi_night_alt_thunderstorm, BitmapSize);}
    return getBitmap(wi_thunderstorm, BitmapSize);
```

`getBitmap(icon_name, size)` 来自生成的 `icons/icons.h`（constexpr 两级 switch），未用到的组合不链接（见 [icons_toolchain.md](icons_toolchain.md)）。

## 3. 已知问题（记录不改码）

1. **display_utils.h:62-64 声明的 `getAvgConc` / `getAQI` / `getAQIdesc` 在全部 .cpp 中无定义**（grep 验证仅头文件出现）——死声明。因无人调用，链接器不报错。
2. `getWiFiBitmap16` 的尾注释写 `// end getWiFiBitmap24`（复制笔误，display_utils.cpp:457）。
3. `filterAlerts` 的"保最新 2 条"依赖 OWM 返回按 start 降序的假设（函数内原注释说明了这一点）；若上游排序变化，会变成"保任意 2 条"。
4. `getMoonPhaseBitmap48` 的映射 `moon_phase*28+0.5` 在 moon_phase=1.0 时 n=28（越界 1 位），恰被数组末尾的"新月"冗余项兜住（数组 29 项，首尾都是新月图标）。

## 4. 数据流角色

见 [Docs/00](../../../references/firmware/esp32-weather-epd/Docs/00-总览与硬件架构.md) 第 7 节与 [Docs/04](../../../references/firmware/esp32-weather-epd/Docs/04-UI布局与绘制管线.md) 第 4 节：结构体/数值 → 位图指针或文案 → renderer 消费。
