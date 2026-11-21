# esp8266_waveform_stubs.cpp —— ESP8266 波形函数替身

对应源文件：`src/src/esp8266_waveform_stubs.cpp`（共 23 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)

---

## 1. 文件职责

为 ESP8266 的 Arduino 核心提供两个“链接期替身”符号，避免 Servo/PWM 库
在链接时找不到 `stopWaveform` / `_stopPWM`。

---

## 2. 全部代码

```cpp
#if defined(PLATFORM_ESP8266)
#include <Arduino.h>
extern "C" IRAM_ATTR int __wrap_stopWaveform(uint8_t pin) { return true; }
extern "C" IRAM_ATTR bool __wrap__stopPWM(uint8_t pin) { return true; }
#endif
```

| 行号 | 内容 | 说明 |
|---|---|---|
| 19 | `#if defined(PLATFORM_ESP8266)` | 只在 ESP8266 编译 |
| 20 | `#include <Arduino.h>` | 拿 `IRAM_ATTR` |
| 21 | `__wrap_stopWaveform` | 恒返回 true |
| 22 | `__wrap__stopPWM` | 恒返回 true |

---

## 3. `__wrap_` 是什么

GNU ld 的 `--wrap=symbol` 特性：把所有对 `symbol` 的引用重定向到
`__wrap_symbol`，而 `__real_symbol` 指向原实现。

```
调用者 --(链接期重定向)--> __wrap_stopWaveform
```

ELRS 用它把 Arduino 核心里的波形停止函数“短路”掉，因为：

1. ELRS 的 PWM 输出走自己的 `ServoOutput` 驱动；
2. 核心的实现会操作硬件定时器，与 ELRS 的定时器冲突。

返回 `true` 表示“已成功停止”，让调用方继续往下走而不报错。

`IRAM_ATTR` 保证函数在 IRAM 里，因为原函数可能在中断上下文被调用。

---

## 4. 为什么需要 `extern "C"`

`--wrap` 是 C 链接期的符号名匹配。如果写成 C++ 函数（名字修饰），
链接器找不到 `stopWaveform` 这个符号，替身不会生效。

---

## 5. 设计要点与观察

1. **零逻辑**：两个函数永远返回 true，不做任何硬件操作。
2. **平台隔离**：整文件被 `#if PLATFORM_ESP8266` 包住，其它平台编出空文件。
3. **链接期而非编译期**：不是重写头文件，而是替换符号，对第三方库透明。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 21 行 | 返回 `int` 而不是 `bool`，与 `__wrap__stopPWM` 的返回类型不一致（历史原因） |
| 21-22 行 | 参数 `pin` 未使用，可能触发 `-Wunused-parameter`（`IRAM_ATTR` 下通常被忽略） |

---

## 6. 互链

- 平台配置：[targets_h.md](targets_h.md)
- PWM 输出：[05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)
- 同类空实现：[test_main_cpp.md](test_main_cpp.md)

---

## 7. 链接期替换的验证方法

| 目的 | 方法 | 预期 |
|---|---|---|
| 确认替身生效 | `nm -C firmware.elf \| grep stopWaveform` | 只有 `__wrap_stopWaveform` 被引用 |
| 确认没有原函数 | `nm firmware.elf \| grep __real_stopWaveform` | 不存在（核心未提供） |
| 确认段位置 | `xtensa-lx106-elf-objdump -h` | 符号落在 `.iram0.text` |

如果 `nm` 里同时出现 `stopWaveform` 与 `__wrap_stopWaveform`，说明
链接参数里漏了 `-Wl,--wrap=stopWaveform`，替身不会生效。

---

## 8. 与 ESP32 的差异

| 维度 | ESP8266 | ESP32 |
|---|---|---|
| 波形库 | 核心自带 `waveform` | LEDC 外设 |
| 是否需要替身 | 需要 | 不需要 |
| 定时器冲突 | 与 ELRS 的 hwTimer 抢 | 硬件通道独立 |
| 本文件 | 参与编译 | 整文件为空 |

这解释了为什么 `platformio.ini` 的 ESP32 环境里看不到这个文件的效果——
它被 `#if` 整段裁掉了。

---

## 9. 读码路径

1. 本文件（23 行）——看两个符号名；
2. `src/targets/esp8285-tx.ini` / `esp8285-rx.ini`——看链接参数；
3. `lib/ServoOutput/`——看 ELRS 自己的 PWM 实现，理解为什么要短路核心的。
