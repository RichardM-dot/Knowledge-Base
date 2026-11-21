# Watchy32KRTC.cpp —— V3 内部 RTC 实现

> 对应源文件：`src/Watchy32KRTC.cpp`（共 118 行，含中文深注；注释前 102 行）
> 级别：B级（文件头导读扩展）
> 关联 Docs：[02-RTC与时间系统](../../../references/firmware/Watchy/Docs/02-RTC与时间系统.md)、
> [05-Wi-Fi与低功耗实测指南](../../../references/firmware/Watchy/Docs/05-Wi-Fi与低功耗实测指南.md)
> 关联 KB：[watchy32krtc_h.md](watchy32krtc_h.md)、[watchyrtc_cpp.md](watchyrtc_cpp.md)

---

## 1. 文件定位

仅 `ARDUINO_ESP32S3_DEV`（Watchy v3.0）编译路径使用的 RTC 实现：
v3.0 取消了独立 RTC 芯片，改用 ESP32-S3 内部 RTC + 板载 32.768kHz 晶振，
本类用 newlib 的 time API（time/localtime_r/mktime/settimeofday/strptime）
实现与 WatchyRTC 完全一致的五接口——主类零改动切换。

文件头（L1-15 附近的导读块）列出了职责与坑；L23-29 还保留原作者的
被注释代码块（`setenv("TZ",...); tzset();` 时区设置示例）。

## 2. 函数表（注释后行）

| 函数 | 行 | 实现 | 注意 |
|---|---|---|---|
| 构造 / `init()` | L19 / L21 | 空实现 | 内部 RTC 无需初始化 |
| `config(datetime)` | L32 | strptime 解析 → mktime → settimeofday | TZ 未设时 mktime 按 UTC |
| `clearAlarm()` | L52 | **空实现** | 唤醒由 esp_sleep_enable_timer_wakeup 负责 |
| `read(tm)` | L56 | time()+localtime_r → 手动搬字段 | `tm_year - 70` 年代换算 |
| `set(tm)` | L73 | mktime → settimeofday | `tm.Year + 70` 反向换算 |
| `temperature()` | L93 | 恒 0 | 无温度传感 |
| `_getValue()` | L97 | ':' 切串（与 WatchyRTC 同款） | config 未用它（用 strptime） |
| `_timeval_to_tm()` | L113 | timeval→tm 工具 | 无人调用（死函数） |

## 3. 关键摘录

### 3.1 config：strptime 一次成型（L32-50）

```cpp
void Watchy32KRTC::config(String datetime) { // String datetime format is YYYY:MM:DD:HH:MM:SS
    struct tm timeInfo;
    memset(&timeInfo, 0, sizeof(timeInfo));

    // Parse the time string
    if (strptime(datetime.c_str(), "%Y:%m:%d:%H:%M:%S", &timeInfo) == NULL) {
        // Failed to parse the time string
    }

    // Convert tm to timeval
    struct timeval tv;
    tv.tv_sec = mktime(&timeInfo);
    tv.tv_usec = 0;

    // Set the time using settimeofday
    if (settimeofday(&tv, NULL) != 0) {
        // Error setting the time
    }
}
```

与 WatchyRTC 的六段 `_getValue` 解析相比，strptime 一行完成（S3 有完整
newlib）。注意三个错误分支都是空注释体——失败静默。

### 3.2 read 的年代换算（L56-71）

```cpp
  time_t now;
  struct tm timeInfo;
  time(&now);
  // Set timezone to China Standard Time
  //setenv("TZ", "CST-8", 1);
  //tzset();
  localtime_r(&now, &timeInfo);
  tm.Year   = timeInfo.tm_year - 70;
  tm.Month  = timeInfo.tm_mon + 1;
  tm.Day    = timeInfo.tm_mday;
  tm.Wday   = timeInfo.tm_wday + 1;
  tm.Hour   = timeInfo.tm_hour;
  tm.Minute = timeInfo.tm_min;
  tm.Second = timeInfo.tm_sec;
```

- `tm_year - 70`：tm_year 是"自 1900"，tmElements_t 是"自 1970"；
- `tm_wday + 1`：0-6 → TimeLib 1-7 域；
- 被注释的 TZ 设置保留着：想要本地时间需自己打开（默认 UTC）。

## 4. 与 WatchyRTC 的行为差异速查

| 行为 | WatchyRTC（外置芯片） | 本类（V3） |
|---|---|---|
| 时间保持 | 芯片独立供电，深睡绝对不丢 | 系统时钟跨深睡由 RTC 计时器维持 |
| 每分钟唤醒 | 芯片闹钟 ext0 | `esp_sleep_enable_timer_wakeup`（deepSleep 里对齐整分） |
| clearAlarm | 清标志+预约 | 空操作 |
| 温度 | DS3231 有 | 无（恒 0） |
| 时区 | 芯片存"本地时间"（写入时已偏移） | 系统时钟 UTC + localtime 换算（TZ 未设=UTC） |

## 5. 陷阱与记录

- **TZ 双语义交汇**：NTP 对时写入的是"已加偏移的本地 epoch"（syncNTP 的
  breakTime 语义），而 read() 又经 localtime_r 按 TZ（默认 UTC）解释一次——
  偏移可能被叠加或抵消，V3 对时后显示偏差是首查点（Docs/02 §7）。
- **`_getValue`/`_timeval_to_tm` 是死代码**：config 用 strptime，read 手搬
  字段，两个工具函数无人调用（保留不删）。
- **clearAlarm 空实现**：若自定义代码对 V3 调用 `RTC.clearAlarm()` 期望
  "重新预约"，会静默无效——预约逻辑只在 deepSleep 里。
- `init()` 空体：每次唤醒的探测开销为零（对比 WatchyRTC 的 I2C 探测），
  这是 V3 省电的小改进。
- 头文件声明见 [watchy32krtc_h.md](watchy32krtc_h.md)。
