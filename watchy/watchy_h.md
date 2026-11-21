# Watchy.h —— 主类声明（用户 API 面）

> 对应源文件：`src/Watchy.h`（共 173 行，含中文深注；注释前 129 行）
> 级别：A级（逐声明深注）
> 关联 Docs：[01-库框架与生命周期](../../../references/firmware/Watchy/Docs/01-库框架与生命周期.md)
> 关联 KB：[watchy_cpp.md](watchy_cpp.md)、[config_h.md](config_h.md)

---

## 1. 文件定位

用户的全部 API 面都在这个头文件：继承 `Watchy`、看懂两个结构体、
（可选）覆写两个虚函数。文件头保留原【中文导读】块；本次为每个结构体/类/
方法组添加了 doc 块与行内注。

## 2. 包含与板代选择（L7-42）

`#ifdef ARDUINO_ESP32S3_DEV` 决定 RTC 实现与头文件集：V3 引入
Watchy32KRTC.h 与大量 soc/esp_sleep 头（内部 RTC + 32K 晶振 +
`ADC_VOLTAGE_DIVIDER ((360+100)/360)` 电池分压宏 L39）；V1/V2 走 WatchyRTC.h。

## 3. 两个结构体

### weatherData（L50-62，含行内注释）

| 字段 | 类型 | 含义 |
|---|---|---|
| `temperature` | int8_t | 温度（metric ℃ / imperial ℉） |
| `weatherConditionCode` | int16_t | OpenWeatherMap 天气码（800 晴 / 5xx 雨…） |
| `isMetric` | bool | 单位制（决定画 ℃ 还是 ℉ 图标） |
| `weatherDescription` | String | 天气短语（JSONVar::stringify） |
| `external` | bool | true=API 真天气；false=本地温度退化值 |
| `sunrise`/`sunset` | tmElements_t | API 的 sys.sunrise/sunset 拆分 |

### watchySettings（L67-87，含行内注释）

用户在 examples 各表盘的 `settings.h` 用指定初始化器填好，构造函数传入：
cityID / lat / lon（二选一）、weatherAPIKey、weatherURL（带占位符模板）、
weatherUnit/weatherLang、weatherUpdateInterval（分钟）、ntpServer、
gmtOffset（秒，会被天气 API timezone 覆盖）、vibrateOClock（整点震动）。

## 4. 类声明速览（L94 起）

| 分组 | 成员 | 注 |
|---|---|---|
| static 单例 | `RTC`（L99/101 按板代二选一）、`display`（L103） | 全工程共享 |
| 数据 | `currentTime`（L104）、`settings`（L105） | 表盘直接读 |
| 生命周期 | `init(datetime="")`、`deepSleep()` | .ino 只调 init |
| 硬件服务 | `getBatteryVoltage()`、`getBoardRevision()`、`vibMotor(100,20)` | 默认参数 |
| 交互（虚） | `handleButtonPress()` **virtual** | 用户可整盘接管 |
| 菜单/页面 | showMenu/showFastMenu/showAbout/showBuzz/showAccelerometer/showUpdateFW/showSyncNTP/setTime/setupWifi/updateFWBegin | 内置 UI 全集 |
| 网络 | `syncNTP()`×3、`connectWiFi()`、`getWeatherData()` | 三重载分工见 Docs/05 |
| 表盘（虚） | `showWatchFace(partialRefresh)`、`drawWatchFace()` **virtual** | 用户 override 点 |
| private | `_bmaConfig()`、`_configModeCallback(static)`、`_readRegister/_writeRegister(static)`、`_getWeatherData(...)` | I2C 回调 static 的原因见 [watchy_cpp.md](watchy_cpp.md) |

继承模式（摘自 doc 块，L89 附近）：

```
class WatchyBasic : public Watchy { void drawWatchFace() override; };
```

## 5. extern RTC 慢存声明（L152-173 附近）

文件尾的 `extern RTC_DATA_ATTR ...` 块声明了 6 个跨睡眠全局
（定义在 Watchy.cpp）：guiState/menuIndex/sensor/WIFI_CONFIGURED/
BLE_CONFIGURED/USB_PLUGGED_IN。本次深注在声明上方加了逐项说明块——
表盘画状态图标时直接读 `WIFI_CONFIGURED/BLE_CONFIGURED`（7_SEG 样例即如此）。

## 6. 设计意图：为什么"库即固件"成立

1. **static 单例**：一块手表一块屏一颗 RTC，static 天然防多实例；
2. **virtual 只开两个口**：drawWatchFace（画什么）与 handleButtonPress
   （怎么交互），其余全封死——表盘作者不需要理解深睡/唤醒；
3. **配置进结构体不进宏**：settings.h 可为每个表盘定制（天气 key、NTP、
   震动偏好），库本体零修改；
4. **extern RTC 慢存变量当"全局黑板"**：表盘、菜单、联网三个模块
   通过它们通信，是单线程固件的简化消息总线。

## 7. 陷阱

- `getBoardRevision()` 声明返回 uint8_t，实现却 `return -1`（溢出 255），
  头文件看不出、实现在 [watchy_cpp.md](watchy_cpp.md) §4.5；
- `showUpdateFW/updateFWBegin` 在头文件仍声明，但实现整段注释停用——
  调用会链接失败，勿用；
- `weatherData` 里的 String 成员活在 RTC 慢存：冷启动后未联网前
  读 `weatherDescription` 是随机堆内容（靠 weatherIntervalCounter=-1 的
  冷启动初始化保证先联网填充，见 Docs/05 §7 第 4 条）。
