# WatchyRTC.cpp —— RTC 双芯片抽象实现（逐函数精读）

> 对应源文件：`src/WatchyRTC.cpp`（共 215 行，含中文深注；注释前 157 行）
> 级别：A级（逐函数中文深注）
> 关联 Docs：[02-RTC与时间系统](../../../references/firmware/Watchy/Docs/02-RTC与时间系统.md)
> 关联 KB：[watchyrtc_h.md](watchyrtc_h.md)、[watchy32krtc_cpp.md](watchy32krtc_cpp.md)

---

## 1. 文件定位

把 DS3231（v1.0）与 PCF8563（v1.5/v2.0）两颗 RTC 统一成 5 个接口
（init/config/clearAlarm/read/set + temperature），主类完全无感。
第三方库对象（DS3232RTC 的 `rtc_ds`、Rtc_Pcf8563 的 `rtc_pcf`）直接作成员。

## 2. 逐函数表

| # | 函数 | 注释后行 | 职责 | 关键点 |
|---|---|---|---|---|
| 1 | 构造 | L9 | 空 | rtcType 初值 0（未知） |
| 2 | `init()` | L18 | I2C 探测定 rtcType | 0x68→DS3231；0x51→PCF8563；都无→静默失败 |
| 3 | `config()` | L42 | 冷启动配置分派 | 只在 init 的 default 分支调用 |
| 4 | `clearAlarm()` | L58 | 清标志+预约下一分钟 | 深睡前必调；INT 不清=唤醒风暴 |
| 5 | `read()` | L82 | 读时间进 tmElements_t | PCF 需 7 次 I2C；Wday 域 +1 |
| 6 | `set()` | L105 | 写时间 | makeTime/breakTime 往返算 Wday |
| 7 | `temperature()` | L127 | DS3231 有真温度；PCF 返 255 | 255=无效哨兵 |
| 8 | `_DSConfig()` | L142 | DS3231 初始时间+ALM2 每分钟闹钟 | squareWave 关掉 |
| 9 | `_PCFConfig()` | L169 | PCF8563 初始时间+借 clearAlarm 配闹钟 | POR 后月份寄存器=0 须修复 |
| 10 | `_getValue()` | L201 | 按':'切串取段 | O(n) 单趟，解析 YYYY:MM:DD:... |

## 3. 关键摘录

### 3.1 探测（L18-40）

```cpp
void WatchyRTC::init() {
  byte error;
  Wire.beginTransmission(RTC_DS_ADDR);
  error = Wire.endTransmission();
  if (error == 0) {
    rtcType = DS3231;
    rtc_ds.begin();
  } else {
    Wire.beginTransmission(RTC_PCF_ADDR);
    error = Wire.endTransmission();
    if (error == 0) {
      rtcType = PCF8563;
    } else {
      // RTC Error
    }
  }
}
```

`endTransmission()==0` 即从机 ACK。两颗都不在线时 rtcType=0，之后所有
`if (rtcType == DS3231)` 都走 else（按 PCF 操作）——查硬件故障先怀疑这里。

### 3.2 每分钟闹钟（L58-72）

DS3231 分支一句 `rtc_ds.alarm(DS3232RTC::ALARM_2)` 读状态即清
（闹钟已在 _DSConfig 配成 ALM2_EVERY_MINUTE 永久模式）；
PCF8563 分支要手动"清标志 + 下一分钟 +99 哨兵"：

```cpp
    rtc_pcf.clearAlarm(); // resets the alarm flag in the RTC
    nextAlarmMinute = rtc_pcf.getMinute();
    nextAlarmMinute =
        (nextAlarmMinute == 59)
            ? 0
            : (nextAlarmMinute + 1); // set alarm to trigger 1 minute from now
    rtc_pcf.setAlarm(nextAlarmMinute, 99, 99, 99);
```

99=不比较该字段 → 只按分钟匹配的每分钟闹钟；59→0 回绕已处理。

### 3.3 读与写的域换算（L82-121）

read：PCF 分支逐字段 getter 拼 tmElements_t，`Wday = getWeekday() + 1`
（TimeLib 1-7 vs 芯片 0-6），`y2kYearToTm()` 把 2000 基换到 1970 基。
set：`makeTime`→`breakTime` 往返是为**借 TimeLib 算星期**（NTP 对时
不填 Wday），`setDate(day, Wday-1, month, 0, tmYearToY2k(Year))`
的 century=0 表示 2000 系；写完 `clearAlarm()` 重约闹钟。

### 3.4 DS3231 配置（L142-161）

```cpp
  rtc_ds.squareWave(DS3232RTC::SQWAVE_NONE); // disable square wave output
  rtc_ds.setAlarm(DS3232RTC::ALM2_EVERY_MINUTE, 0, 0, 0,
                  0); // alarm wakes up Watchy every minute
  rtc_ds.alarmInterrupt(DS3232RTC::ALARM_2, true); // enable alarm interrupt
```

三步：关方波（省电且不与 INT 冲突）、配每分钟闹钟、开中断输出。
datetime 非空时先 `_getValue` 六段解析写初始时间。

## 4. 与 V3 实现的接口对照

| 接口 | 本文件（外置芯片） | Watchy32KRTC（V3） |
|---|---|---|
| clearAlarm | 清标志+预约 | **空实现**（timer 唤醒源接管） |
| read | I2C 读芯片 | time()+localtime_r |
| temperature | DS3231 真温度 | 恒 0 |

## 5. 陷阱与记录

- **`_getDayOfWeek()` 是死声明**：头文件声明（[watchyrtc_h.md](watchyrtc_h.md) §3）
  但本文件从未定义/调用——不删不改，仅记录。
- **两颗都探测失败无报错**：rtcType=0 走 PCF 分支，I2C 全 NACK，
  read 全 0——表现为"时间停在 2000-01-01 00:00:00"一类怪象。
- **`set()` 无返回值**：写失败（I2C NACK）无法感知；对时成功与否靠
  showSyncNTP 读回验证（Watchy.cpp:1315 起）。
- **PCF8563 闹钟只到分钟**：秒级唤醒需换 timer 或改用 AIE+定时器中断，
  库未提供。
- **`temperature()` PCF 返 255**：调用方要判断 255 才不显示"255℃"。
