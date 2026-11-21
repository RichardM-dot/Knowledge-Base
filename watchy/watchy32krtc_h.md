# Watchy32KRTC.h —— V3 内部 RTC 声明

> 对应源文件：`src/Watchy32KRTC.h`（共 35 行，含中文深注；注释前 22 行）
> 级别：B级（文件头导读扩展）
> 关联 Docs：[02-RTC与时间系统](../../../references/firmware/Watchy/Docs/02-RTC与时间系统.md)
> 关联 KB：[watchy32krtc_cpp.md](watchy32krtc_cpp.md)

---

## 1. 文件定位

Watchy v3.0（ESP32-S3）的 RTC 类声明：无独立 RTC 芯片，用内部 RTC +
外部 32K 晶振实现与 `WatchyRTC` 相同签名的接口，由 `Watchy.h` 的
`#ifdef ARDUINO_ESP32S3_DEV` 分支选用（替代 WatchyRTC.h）。

## 2. 类成员与方法（注释后 L17-35 附近）

```cpp
class Watchy32KRTC {
public:
  Watchy32KRTC();
  void init();
  void config(String datetime); //datetime format is YYYY:MM:DD:HH:MM:SS
  void clearAlarm();
  void read(tmElements_t &tm);
  void set(tmElements_t tm);
  uint8_t temperature();

private:
  String _getValue(String data, char separator, int index);
  void _timeval_to_tm(struct timeval *tv, struct tm *tm);
};
```

| 方法 | 语义（实现见 KB watchy32krtc_cpp.md） |
|---|---|
| init | 空（内部 RTC 免初始化） |
| config | strptime → settimeofday |
| clearAlarm | 空（timer 唤醒源接管） |
| read | time()+localtime_r → tmElements_t |
| set | mktime → settimeofday |
| temperature | 恒 0 |

私有 `_getValue`/`_timeval_to_tm` 均为**未被调用的工具函数**（死代码）。

## 3. 接口一致性对照

| 接口 | WatchyRTC.h | 本头文件 |
|---|---|---|
| init/config/clearAlarm/read/set/temperature | 有 | 签名完全一致 |
| 成员 rtc_ds/rtc_pcf/rtcType | 有 | **无**（无芯片成员） |
| 地址/年份偏移宏 | 有 | 无（不需要） |

`Watchy::RTC` 静态成员的类型由编译宏二选一（Watchy.h:99/101），
主类代码不写任何 `#ifdef` —— 抽象层的收益就在这一行。

## 4. 头文件依赖

`Arduino.h` + `TimeLib.h`（tmElements_t）+ `config.h`。比 WatchyRTC.h
轻得多：不需要 DS3232RTC/Rtc_Pcf8563 两个第三方库——V3 编译产物里
根本没有 RTC 驱动代码（省 flash）。

## 5. 陷阱

- **没有 rtcType 成员**：如果有代码读 `RTC.rtcType`（如 Watchy.cpp 的
  getBatteryVoltage 分支）在 V3 路径会编译失败——V3 用 `#ifdef` 分支
  自己的分压公式避开；自定义代码同理小心。
- clearAlarm 空实现的语义陷阱见 [watchy32krtc_cpp.md](watchy32krtc_cpp.md) §5。
