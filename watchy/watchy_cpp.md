# Watchy.cpp —— 手表主类实现（逐函数精读）

> 对应源文件：`src/Watchy.cpp`（共 1401 行，含本次中文深注；注释前 1168 行）
> 级别：A级（逐函数中文深注）
> 关联 Docs：[01-库框架与生命周期](../../../references/firmware/Watchy/Docs/01-库框架与生命周期.md)、
> [04-菜单与按键状态机](../../../references/firmware/Watchy/Docs/04-菜单与按键状态机.md)、
> [05-Wi-Fi与低功耗实测指南](../../../references/firmware/Watchy/Docs/05-Wi-Fi与低功耗实测指南.md)
> 关联 KB：[watchy_h.md](watchy_h.md)、[watchyrtc_cpp.md](watchyrtc_cpp.md)、[bma_cpp.md](bma_cpp.md)

---

## 1. 文件定位

Watchy 库的核心实现：init→深睡→唤醒的固件主循环、按键分派、内置页面、
天气/NTP、电池/板卡探测、BMA423 配置全部在此。文件开头有两块导读：
原【中文导读】头（L1-13）+ 本次添加的【函数总览】块（L15-26，行号为注释前原行号）。

## 2. 静态对象与 RTC 慢存全局（L28-58）

| 行 | 定义 | 说明 |
|---|---|---|
| L30-36 | `Watchy::RTC` + `ACTIVE_LOW` 宏 | V3 用 Watchy32KRTC（低有效），否则 WatchyRTC（高有效） |
| L37-38 | `Watchy::display` | GxEPD2_BW 单例，模板参数即 WatchyDisplay |
| L40-58 | RTC_DATA_ATTR 全局区 | guiState/menuIndex/sensor/天气缓存等 11 个跨睡眠变量 |

RTC 慢存语义要点：**深睡保持、冷启动随机化**。`weatherIntervalCounter=-1`
与 `alreadyInMenu=true` 的初值只在冷启动生效一次。

## 3. 逐函数表

| # | 函数 | 注释后行 | 职责一句话 | 关键点 |
|---|---|---|---|---|
| 1 | `init()` | L70 | 总入口：按唤醒原因分派，末尾必深睡 | 无循环；"主循环"=深睡↔唤醒 |
| 2 | `deepSleep()` | L155 | 屏休眠→清闹钟→配唤醒源→深睡 | V1/V2 与 V3 唤醒源不同 |
| 3 | `handleButtonPress()` | L204 | 按键分派 + 快菜单 5s 轮询 | 两段复制粘贴逻辑 |
| 4 | `showMenu()` | L369 | 主菜单 6 项反白高亮 | guiState←0，alreadyInMenu←false |
| 5 | `showFastMenu()` | L409 | 菜单滚动的恒部分刷版本 | 与 showMenu 唯一差 display(true) |
| 6 | `showAbout()` | L448 | 版本/电压/uptime/WiFi 信息页 | uptime=RTC 时间差 |
| 7 | `showBuzz()` | L502 | 马达演示页 | 全刷→vibMotor→回菜单 |
| 8 | `vibMotor()` | L521 | 方波驱动马达 | 阻塞 delay；默认 100ms×20 |
| 9 | `setTime()` | L539 | 手动对时：字段游标+闪烁 | MENU 前进/BACK 后退/UD 加减 |
| 10 | `showAccelerometer()` | L716 | 加速度实时页 200ms 刷新 | 唯一持续清醒循环 |
| 11 | `showWatchFace()` | L796 | 表盘外壳：asyncPowerOn→用户画→送显 | guiState←-1 |
| 12 | `drawWatchFace()` | L811 | 基类默认表盘（虚） | 用户 override 点 |
| 13 | `getWeatherData()` | L830 | 天气入口（透传 settings） | — |
| 14 | `_getWeatherData()` | L846 | 节流+HTTP+JSON+顺路NTP | 30min 计数器在 RTC 慢存 |
| 15 | `getBatteryVoltage()` | L921 | ADC 毫伏×分压比 | V1/V2 ×2；V3 ×1.278 |
| 16 | `getBoardRevision()` | L941 | I2C 探测+GPIO 判代际 | 10/15/20/30 |
| 17 | `_readRegister()` | L976 | BMA I2C 读回调（static） | 注册进 bma4_dev.bus_read |
| 18 | `_writeRegister()` | L995 | BMA I2C 写回调（static） | 返回 0=ACK |
| 19 | `_bmaConfig()` | L1012 | BMA 一站式配置（仅冷启动） | 详见 Docs/03 |
| 20 | `setupWifi()` | L1123 | WiFiManager AP 配网 | 关 busyCallback light sleep |
| 21 | `_configModeCallback()` | L1160 | AP 模式屏显热点信息（static） | — |
| 22 | `connectWiFi()` | L1182 | NVS 凭据直连（~10s 阻塞） | 失败路径关射频 |
| 23 | `showUpdateFW()`/`updateFWBegin()` | L1203/1223 | BLE OTA 页面与流程 | **整段注释停用** |
| 24 | `showSyncNTP()` | L1315 | 手动对时页 | 回显新时间 3s |
| 25-27 | `syncNTP()` ×3 重载 | L1366/1376/1388 | UDP 对时→写 RTC | RTC 存本地时间 |

## 4. 关键摘录与解读

### 4.1 init() 的三分支骨架（L70-146）

```cpp
void Watchy::init(String datetime) {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause(); // get wake up reason
  #ifdef ARDUINO_ESP32S3_DEV
    Wire.begin(WATCHY_V3_SDA, WATCHY_V3_SCL);     // init i2c
  #else
    Wire.begin(SDA, SCL);                         // init i2c
  #endif
  RTC.init();
  // Init the display since is almost sure we will use it
  display.epd2.initWatchy();
```

- L72：唤醒原因是硬件锁存值，读一次就"知道这次为什么活"；
- L74/76：I2C 引脚按板代宏切换（V3: 12/11，V1/V2: 默认 21/22）；
- L78：`RTC.init()` 每次唤醒都做 ACK 探测（开销 1-2 次 I2C 事务）；
- L80：屏幕 init（不刷新，只建控制器状态）。

三个分支（L83-144）：RTC tick → 按 guiState 刷表盘或菜单超时回表盘；
EXT1 → handleButtonPress；default → 冷启动全量初始化。末尾 L145 `deepSleep()`
——**init 永不返回**（esp_deep_sleep_start 不返回），.ino 的 loop() 是死代码。

### 4.2 整点震动（L94-99）

```cpp
      if (settings.vibrateOClock) {
        if (currentTime.Minute == 0) {
          // The RTC wakes us up once per minute
          vibMotor(75, 4);
        }
      }
```

整点检测就是"分钟==0"：每分钟 tick 时碰巧是整点就震 4 个半周期×75ms。
无闹钟队列、无定时器——一切复用每分钟心跳。

### 4.3 深睡的 V1/V2 防漏电循环（L176-185）

```cpp
  // Set GPIOs 0-39 to input to avoid power leaking out
  /* 悬空引脚全部切输入，切断经外部上下拉/外设的漏电通路；
     ignore 位掩码 = 0xF10309C2，跳过不能动的系统引脚：
     GPIO6/7/8/11 = flash SPI，GPIO1 = UART0 TX，另含 16/17/24/28-31 */
  const uint64_t ignore = 0b11110001000000110000100111000010; // Ignore some GPIOs due to resets
  for (int i = 0; i < GPIO_NUM_MAX; i++) {
    if ((ignore >> i) & 0b1)
      continue;
    pinMode(i, INPUT);
  }
```

把除掩码外的全部 GPIO 切输入。手算掩码：`0b11110001000000110000100111000010`
= 0xF10309C2，置位的是 1,6,7,8,11,16,17,24,28,29,30,31 号脚。

### 4.4 天气节流与无网退化（L849-853、L896-905）

节流：`weatherIntervalCounter`（RTC 慢存）每 tick +1，到 `updateInterval`
（默认 30）才联网；联网成功清 0。退化：`sensor.readTemperature()` + 天气码 800
+ `external=false`。顺路对时：`gmtOffset = int(responseObject["timezone"])`
后调 `syncNTP(gmtOffset)`（L884-886）——旅行时一次天气即校准时区。

### 4.5 板卡探测判定树（L941-963）

```cpp
  if(chip_info.model == CHIP_ESP32){ //Revision 1.0 - 2.0
    Wire.beginTransmission(0x68); //v1.0 has DS3231
    if (Wire.endTransmission() == 0){
      return 10;
    }
    delay(1);
    Wire.beginTransmission(0x51); //v1.5 and v2.0 have PCF8563
    if (Wire.endTransmission() == 0){
        pinMode(35, INPUT);
        if(digitalRead(35) == 0){
          return 20; //in rev 2.0, pin 35 is BTN 3 and has a pulldown
        }else{
          return 15; //in rev 1.5, pin 35 is the battery ADC
        }
    }
  }
```

0x68 有 ACK→v1.0；0x51 有 ACK→GPIO35 区分 v2.0（按键下拉读 0）与
v1.5（ADC 悬空读 1）；S3→v3.0；否则 return -1（uint8_t 溢出为 255）。

## 5. 陷阱与记录（不改代码）

- **快菜单与唤醒分派重复**：6 项 switch 在 L214-238 与 L299-323 两份，
  改菜单要同步；`showMenu/showFastMenu` 的 menuItems 也两份。
- **`getBoardRevision()` 失败返回 -1**：uint8_t 返回类型溢出为 255，
  调用方需按 255 处理"未知板"。
- **`showAbout` uptime 的 days 换算**：`(totalSeconds % (86400*30))/86400`
  按月取模——超过 30 天的天数会回卷，属显示取舍而非 bug。
- **`_readRegister` 固定返回 0**：不校验 NACK，I2C 故障被吞掉（BMA 读失败
  表现为全 0 数据），KB [bma_cpp.md](bma_cpp.md) 有对应记录。
- **`gmtOffset` 类型 long 但 JSON 只给 int**：时区都是整小时，够用。
- 深读入口：Docs/01（生命周期）、Docs/04（状态机）、Docs/05（联网与功耗）。
