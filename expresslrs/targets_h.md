# targets.h —— 编译期总闸门

对应源文件：`src/include/targets.h`（共 101 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)

---

## 1. 文件职责

在进入业务代码前确定“这台设备是什么”：协议版本、哨兵值、IRAM 属性、
监管域合法性、是否有串口、最后拉入 `hardware.h`。

---

## 2. 关键定义

| 名称 | 行号 | 值 | 说明 |
|---|---|---|---|
| `OTA_VERSION_ID` | 25 | 4 | 空口协议版本，参与 CRC 初值异或 |
| `UNDEF_PIN` | 27 | -1 | “引脚不存在”哨兵 |
| `LED_MAX_BRIGHTNESS` | 30 | 50 | LED 最大亮度（0..255） |
| `WORD_ALIGNED_ATTR` | 34 | `aligned(4)` | 4 字节对齐（SPI DMA 要求） |
| `WORD_PADDED(size)` | 35 | `(size+3)&~3` | 向上取整到 4 的倍数 |
| `ICACHE_RAM_ATTR` | 38 | `IRAM_ATTR` | 函数常驻 IRAM |

`OTA_VERSION_ID` 的注释很关键：

```c
// Used to XOR with OtaCrcInitializer and macSeed to reduce compatibility with previous versions.
// It should be incremented when the OTA packet structure is modified.
```

改包结构必须 +1，否则新旧固件会“CRC 通过但字段错位”，产生极难排查的错误。

---

## 3. 串口能力判定

```c
#if defined(DEBUG_CRSF_NO_OUTPUT)
#define OPT_CRSF_RCVR_NO_SERIAL true
#elif defined(TARGET_RX)
extern bool pwmSerialDefined;

#define OPT_CRSF_RCVR_NO_SERIAL (GPIO_PIN_RCSIGNAL_RX == UNDEF_PIN && GPIO_PIN_RCSIGNAL_TX == UNDEF_PIN && !pwmSerialDefined)
#else
#define OPT_CRSF_RCVR_NO_SERIAL false
#endif
```

三个条件同时成立才算“无串口”：

1. 没有 RX 引脚；
2. 没有 TX 引脚；
3. 用户也没把某个 PWM 焊盘配成 `somSerial`。

`pwmSerialDefined` 是运行时变量（在 `rx_main.cpp` 的 setup 里扫描 PWM 表得到），
所以这个宏在 RX 上**必须在使用前调用 setup**，不能用于全局初始化。

---

## 4. 监管域校验

```c
#if defined(RADIO_SX128X)
#define Regulatory_Domain_ISM_2400 1
// ISM 2400 band is in use => undefine other requlatory domain defines
#undef Regulatory_Domain_AU_915
...
#elif defined(RADIO_SX127X) || defined(RADIO_LR1121) || defined(RADIO_LR2021)
#if !(defined(Regulatory_Domain_AU_915) || defined(Regulatory_Domain_FCC_915) || \
        defined(Regulatory_Domain_EU_868) || defined(Regulatory_Domain_IN_866) || \
        defined(Regulatory_Domain_AU_433) || defined(Regulatory_Domain_EU_433) || \
        defined(Regulatory_Domain_US_433) || defined(Regulatory_Domain_US_433_WIDE) || \
        defined(Regulatory_Domain_TH_920) || \
        defined(UNIT_TEST))
#error "Regulatory_Domain is not defined for 900MHz device. Check user_defines.txt!"
#endif
#else
#error "Either RADIO_SX127X, RADIO_SX128X, RADIO_LR1121 or RADIO_LR2021 must be defined!"
#endif
```

设计意图：**编译期失败优先**。漏定义监管域会导致 `domains[]` 越界，
运行时表现为“上电乱跳频”，极难定位；`#error` 把问题提前到构建。

2.4G 芯片会**强制清掉**其它域定义，防止“同时定义 FCC915 与 ISM2400”造成
`domains[]` 选错。

---

## 5. 平台默认引脚

```c
#if defined(PLATFORM_ESP32)
#include <soc/uart_pins.h>
#endif
#if !defined(U0RXD_GPIO_NUM)
#define U0RXD_GPIO_NUM (3)
#endif
#if !defined(U0TXD_GPIO_NUM)
#define U0TXD_GPIO_NUM (1)
#endif
```

ESP32 的 UART0 默认是 GPIO3(RX)/GPIO1(TX)。`setupSerial()` 里大量用这两个宏
判断“UART0 是否被别的功能占用”。

---

## 6. 设计要点与观察

1. **头文件即配置校验器**：所有 `#error` 集中在这里，便于维护。
2. **哨兵统一**：`UNDEF_PIN=-1` 全工程一致，`hardware_pin()` 也返回 -1。
3. **IRAM 属性可移植**：`#undef` 后重定义，兼容 ESP32/ESP8266。
4. **最后才 include hardware.h**：因为 `hardware.h` 需要 `UNDEF_PIN` 等宏。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 43 行 | `pwmSerialDefined` 在 RX 上需先 setup，宏不能用于静态初始化 |
| 66 行 | 2.4G 强制 `#undef` 其它域，若用户手改会静默失效 |

---

## 7. 互链

- 硬件枚举：[hardware_h.md](hardware_h.md)
- 平台构建：[05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)
- native 测试：[native_h.md](native_h.md)
