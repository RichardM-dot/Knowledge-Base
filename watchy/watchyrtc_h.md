# WatchyRTC.h —— RTC 抽象层声明

> 对应源文件：`src/WatchyRTC.h`（共 57 行，含中文深注；注释前 41 行）
> 级别：A级（逐声明深注）
> 关联 Docs：[02-RTC与时间系统](../../../references/firmware/Watchy/Docs/02-RTC与时间系统.md)
> 关联 KB：[watchyrtc_cpp.md](watchyrtc_cpp.md)

---

## 1. 常量宏（注释后 L13-18 附近）

| 宏 | 值 | 含义 |
|---|---|---|
| `DS3231` | 1 | rtcType：Maxim DS3231（v1.0） |
| `PCF8563` | 2 | rtcType：NXP PCF8563（v1.5/v2.0） |
| `RTC_DS_ADDR` | 0x68 | DS3231 I2C 地址 |
| `RTC_PCF_ADDR` | 0x51 | PCF8563 I2C 地址 |
| `YEAR_OFFSET_DS` | 1970 | DS3232RTC 时间基准 |
| `YEAR_OFFSET_PCF` | 2000 | PCF8563 年份寄存器基准（BCD 00-99） |

## 2. 类成员

| 成员 | 类型 | 说明 |
|---|---|---|
| `rtc_ds` | DS3232RTC | JChristensen/DS3232RTC 库对象 |
| `rtc_pcf` | Rtc_Pcf8563 | Rtc_Pcf8563 库对象 |
| `rtcType` | uint8_t | DS3231(1)/PCF8563(2)；0=探测失败（陷阱） |

两个驱动对象**同时**作为成员存在（不省那几十字节，换来了简单）。

## 3. 方法签名与注释要点

| 方法 | 签名 | 深注要点 |
|---|---|---|
| `init()` | void | I2C 探测两颗候选地址；每次唤醒都调 |
| `config()` | void config(String datetime) | datetime 格式 "YYYY:MM:DD:HH:MM:SS"；仅冷启动 |
| `clearAlarm()` | void | 清标志+预约下一分钟；深睡前必调 |
| `read()` | void read(tmElements_t &tm) | 输出参数；Wday/年域换算 |
| `set()` | void set(tmElements_t tm) | PCF 分支 makeTime/breakTime 算 Wday |
| `temperature()` | uint8_t | DS3231 整度数；PCF 返 255 无效 |
| `_DSConfig()` / `_PCFConfig()` | private | 各芯片专属配置 |
| `_getDayOfWeek()` | private int | **死声明：无定义无调用**（保留不删） |
| `_getValue()` | private String | ':' 切串取段 |

## 4. 抽象设计评价

- **接口按"手表需要"裁剪**：没有暴露闹钟任意时刻设置（只有每分钟）——
  简化正确，手表的心跳本就只要每分钟；
- **rtcType 裸奔**：public 成员被 Watchy::getBatteryVoltage 直接读
  （Watchy.cpp:925），封装有缺口但无害；
- **datetime 字符串协议**："YYYY:MM:DD:HH:MM:SS" 从 .ino 传到芯片寄存器，
  中间经 _getValue 六段解析——朴素但可行；strptime（V3 实现）是它的替代。

## 5. 与其他文件的关系

- `Watchy.cpp` 的 `RTC.init()/config()/clearAlarm()/read()/set()` 全部经由
  本类（实现见 [watchyrtc_cpp.md](watchyrtc_cpp.md)）；
- `Watchy.h` 的 `#else #include "WatchyRTC.h"`（V1/V2 路径）；
- V3 对应物 [watchy32krtc_h.md](watchy32krtc_h.md) 接口签名完全一致。
