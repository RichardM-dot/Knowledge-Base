# main.cpp —— 主循环与深睡调度（A级）

对应源文件：`platformio/src/main.cpp`（共 438 行）
相关文档：[Docs/00-总览与硬件架构](../../../references/firmware/esp32-weather-epd/Docs/00-总览与硬件架构.md) · [Docs/01-启动流程与Wi-Fi连接](../../../references/firmware/esp32-weather-epd/Docs/01-启动流程与Wi-Fi连接.md) · [Docs/05-低功耗与实测指南](../../../references/firmware/esp32-weather-epd/Docs/05-低功耗与实测指南.md)
互链：[client_utils.md](client_utils.md) · [api_response.md](api_response.md) · [renderer.md](renderer.md)

## 0. 文件定位

Arduino 入口（setup/loop）。一轮显示周期的总调度：电池检查 → Wi-Fi → NTP → 两个 API → BME280 → 整屏渲染 → 深睡。**loop() 是空的**：深睡唤醒等效复位，程序永远从 setup() 重新执行。已按 A 级逐函数深注（函数头 `@brief/@param/@note` 块 + 行内注释）。

## 1. 文件级全局

| 符号 | 行号 | 说明 |
|---|---|---|
| `owm_onecall` | 59 | `static owm_resp_onecall_t`，One Call 解析结果，~6.3KB |
| `owm_air_pollution` | 60 | `static owm_resp_air_pollution_t`，空气质量，~1.1KB |
| `prefs` | 62 | NVS 句柄（Preferences，命名空间 `weather_epd`） |

两个结构体必须 `static`（文件级）而非栈上：合计约 7.4KB，超出 loopTask 默认 8KB 栈的安全余量。

## 2. 逐函数表

| 函数 | 行号 | 签名要点 | 职责 |
|---|---|---|---|
| `beginDeepSleep` | 76 | `(unsigned long startTime, tm *timeInfo)`，不返回 | 计算对齐的深睡秒数并进入深睡 |
| `setup` | 170 | 无参 | 一轮完整业务流程的调度核心 |
| `loop` | 435 | 无参 | 空实现，永不运行 |

### 2.1 beginDeepSleep（76-155）

- **坐标系平移**（89-107）：把 BED_TIME/当前时刻都折算到"以 WAKE_TIME 为 0 点"的 0~23 小时坐标系，SLEEP_DURATION 非 60 倍数时也能正确对齐；未启用睡前省电时 `bedtimeHour = INT_MAX` 哨兵。
- **对齐**（111-119）：`sleepMinutes = SLEEP_DURATION - offsetMinutes`；距下个对齐点 <2 分钟或相位已过 95% 时多跳一档（main.cpp:113-119）。
- **就寝**（124-135）：预测唤醒小时 ≥ bedtimeHour 时直接睡到次日 WAKE_TIME。
- **RTC 校准**（146-149）：`+= 3ULL` 后 `*= 1.0015f`，补偿深睡 RTC 振荡器偏快。
- **收尾**（150-155）：`esp_sleep_enable_timer_wakeup(秒×1e6)` → `esp_deep_sleep_start()`。

关键摘录（main.cpp:89-95，坐标系平移）：

```cpp
  // 就寝小时换算到"以 WAKE_TIME 为 0 点"的坐标系；未启用睡前省电时用
  // INT_MAX 作哨兵值——任何预测唤醒小时都小于它，即"永不就寝"
  int bedtimeHour = INT_MAX;
  if (BED_TIME != WAKE_TIME)
  {
    bedtimeHour = (BED_TIME - WAKE_TIME + 24) % 24;
  }
```

关键摘录（main.cpp:111-119，对齐与跳档）：

```cpp
  // align wake time to nearest multiple of SLEEP_DURATION
  int sleepMinutes = SLEEP_DURATION - offsetMinutes;
  // 距下个对齐点不足 2 分钟、或本周期已走过 95% 以上时，直接跳到再下一个
  // 对齐点——避免"刚睡着几分钟就被唤醒"，白付一次 Wi-Fi+刷屏的能量峰值
  if (desiredSleepSeconds - offsetSeconds < 120
   || offsetSeconds / (float)desiredSleepSeconds > 0.95f)
  { // if we have a sleep time less than 2 minutes OR less 5% SLEEP_DURATION,
    // skip to next alignment
    sleepMinutes += SLEEP_DURATION;
  }
```

### 2.2 setup（170-433）分区行号

| 分区 | 行号 | 失败出路 |
|---|---|---|
| 串口/LED/NVS | 176-186 | - |
| 电池低电检查 | 186-243 | 画低电屏；危急档**不设唤醒源**（218-223）→ 无限休眠 |
| Wi-Fi | 258-283 | 区分 `WL_NO_SSID_AVAIL` 与其他失败两种错误屏 |
| NTP | 286-301 | 时间错误屏 |
| One Call API | 303-319 | API 错误屏（错误码+短语） |
| Air Pollution API | 320-343 | 同上 |
| killWiFi | 345 | 数据到手立刻断射频 |
| BME280 | 346-383 | 失败置 NAN → 界面画 "--" 并记入状态栏 |
| 渲染 | 405-422 | `initDisplay` + `do{...}while(display.nextPage())` + `powerOffDisplay` |
| 深睡 | 425 | `beginDeepSleep(startTime, &timeInfo)` |

关键摘录（main.cpp:409-422，渲染分页结构）：

```cpp
  initDisplay();
  do
  {
    drawCurrentConditions(owm_onecall.current, owm_onecall.daily[0],
                          owm_air_pollution, inTemp, inHumidity);
    drawOutlookGraph(owm_onecall.hourly, owm_onecall.daily, timeInfo);
    drawForecast(owm_onecall.daily, timeInfo);
    drawLocationDate(CITY_STRING, dateStr);
#if DISPLAY_ALERTS
    drawAlerts(owm_onecall.alerts, CITY_STRING, dateStr);
#endif
    drawStatusBar(statusStr, refreshTimeStr, wifiRSSI, batteryVoltage);
  } while (display.nextPage());
  powerOffDisplay();
```

所有 draw* 必须包在同一 do/while 里（GxEPD2 分页模式约定），见 [renderer.md](renderer.md)。

## 3. 低电三档状态机

关键摘录（main.cpp:216-226，危急/极低两档的深睡设置）：

```cpp
    if (batteryVoltage <= CRIT_LOW_BATTERY_VOLTAGE)
    { // critically low battery
      // don't set esp_sleep_enable_timer_wakeup();
      // We won't wake up again until someone manually presses the RST button.
      Serial.println(TXT_CRIT_LOW_BATTERY_VOLTAGE);
      Serial.println(TXT_HIBERNATING_INDEFINITELY_NOTICE);
    }
    else if (batteryVoltage <= VERY_LOW_BATTERY_VOLTAGE)
    { // very low battery
      esp_sleep_enable_timer_wakeup(VERY_LOW_BATTERY_SLEEP_INTERVAL
                                    * 60ULL * 1000000ULL);
```

"无限休眠"的实现 = **不调用** `esp_sleep_enable_timer_wakeup`（无定时唤醒源）直接 `esp_deep_sleep_start()`。

| 阈值（config.cpp:177-180） | 档位 | 行为（main.cpp:203-243） |
|---|---|---|
| ≤3535mV | WARN | 正常刷新，状态栏图标变强调色 |
| ≤3462mV | LOW | 首次画低电屏（NVS `lowBat` 标记，191-197），此后睡 30min 只复测 |
| ≤3442mV | VERY_LOW | 睡 120min 复测 |
| ≤3404mV | CRIT | 无限休眠，仅 RST 键唤醒 |

## 4. 已知问题（记录不改码）

1. **main.cpp:51**（include 段）：`#if defined(USE_HTTPS_WITH_CERT_VERIF) || defined(USE_HTTPS_WITH_CERT_VERIF)` —— 同一宏判了两遍，疑似本意是 `USE_HTTPS_NO_CERT_VERIF`。当前无实际影响（两种模式都含 WiFiClientSecure.h）。
2. **月落时间为 0** 时（当天无月落）drawCurrentMoonset 会显示 1969/1970 之类的时间（renderer 侧未特判）。

## 5. 数据流角色

setup() 是数据总线：`startWiFi → configTzTime → getOWM* → bme.read*` 的结果全部汇入 `draw*` 调用；`wifiRSSI/batteryVoltage/statusStr` 三个旁路信号只进状态栏。见 [Docs/00](../../../references/firmware/esp32-weather-epd/Docs/00-总览与硬件架构.md) 第 7 节数据流图。
