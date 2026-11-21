# config.h —— 引脚表与硬件常量

> 对应源文件：`src/config.h`（共 141 行，含中文深注；注释前 124 行）
> 级别：B级（文件头导读扩展）
> 关联 Docs：[00-总览与硬件架构](../../../references/firmware/Watchy/Docs/00-总览与硬件架构.md)、
> [04-菜单与按键状态机](../../../references/firmware/Watchy/Docs/04-菜单与按键状态机.md)
> 关联 KB：[watchy_h.md](watchy_h.md)

---

## 1. 文件定位

全工程唯一的"接线图"：板级宏选择展开哪套引脚，其余文件只认宏名。
四段内容：版本号 → 板级引脚 → 屏/WiFi/菜单常量 → BLE 版本号。
被所有 src 文件 include。

## 2. 宏全景表（按注释后行号）

### 2.1 版本（L18）

| 宏 | 值 |
|---|---|
| `WATCHY_LIB_VER` | "1.4.14"（About 页显示；注意 library.properties 是 1.4.15，两处不同步——记录不改） |

### 2.2 V3（S3）引脚（L22-54 附近，`#ifdef ARDUINO_ESP32S3_DEV`）

| 宏 | 值 | | 宏 | 值 |
|---|---|---|---|---|
| SDA/SCL | 12/11 | | DISPLAY_CS/DC | 33/34 |
| SPI SS/MOSI/MISO/SCK | 33/48/46/47 | | DISPLAY_RES/BUSY | 35/36 |
| MENU/BACK/UP/DOWN 键 | 7/6/0/8 | | ACC_INT_1/2 | 14/13 |
| VIB_MOTOR_PIN | 17 | | BATT_ADC/CHRG_STATUS | 9/10 |
| USB_DET_PIN | 21 | | RTC_INT_PIN | -1（不用） |

按键掩码：`MENU_BTN_MASK BIT64(7)` 等；`BTN_PIN_MASK` 四键或。

### 2.3 V1/V1.5/V2 引脚（L56-100 附近）

| 宏 | v1.0 | v1.5 | v2.0 |
|---|---|---|---|
| MENU/BACK/DOWN 键 | 26/25/4 | 26/25/4 | 26/25/4 |
| UP 键 | 32 | 32 | 35 |
| DISPLAY_CS/DC/RES/BUSY | 5/10/9/19 | 同 | 同 |
| ACC_INT_1/2 | 14/12 | 同 | 同 |
| VIB_MOTOR_PIN | 13 | 同 | 同 |
| RTC_INT_PIN | 27 | 同 | 同 |
| BATT_ADC_PIN | **33** | **35** | **34** |
| RTC_TYPE | 1（DS3231） | 2（PCF8563） | 2（PCF8563） |

未定义任何板级宏时 `#pragma message` 提示并**默认 ARDUINO_WATCHY_V20**
（L58-61 附近的两条 message + 默认宏）。

### 2.4 屏/WiFi/菜单/对时/BLE 常量（L104-141 附近）

| 宏 | 值 | 用途 |
|---|---|---|
| DISPLAY_WIDTH/HEIGHT | 200/200 | 屏分辨率 |
| WIFI_AP_TIMEOUT / WIFI_AP_SSID | 60 / "Watchy AP" | 配网 |
| WATCHFACE_STATE / MAIN_MENU_STATE / APP_STATE / FW_UPDATE_STATE | -1/0/1/2 | guiState 取值 |
| MENU_HEIGHT / MENU_LENGTH | 25/6 | 菜单布局 |
| SET_HOUR…SET_DAY | 0-4 | 对时字段游标 |
| HOUR_12_24 | 24 | 12/24 小时制（7_SEG 表盘读） |
| BLE_DEVICE_NAME / WATCHFACE_NAME | "Watchy BLE OTA" / "Watchy 7 Segment" | BLE 广播 |
| SOFTWARE/HARDWARE_VERSION_* | 1.0.0 / 1.0 | BLE 版本特征 |

## 3. 板级宏树（折叠逻辑）

```
ARDUINO_ESP32S3_DEV ──→ V3 引脚组（低有效按键、USB 检测、RTC_INT=-1）
否则：
  未定义 V10/V15/V20 ──→ #pragma message + 默认 V20
  ARDUINO_WATCHY_V10 ──→ UP=32, ADC=33, RTC=DS3231
  ARDUINO_WATCHY_V15 ──→ UP=32, ADC=35, RTC=PCF8563
  ARDUINO_WATCHY_V20 ──→ UP=35, ADC=34, RTC=PCF8563
```

`ACTIVE_LOW` 的对称宏在 Watchy.cpp:32/35 定义（V3=0 低有效，V1/V2=1 高有效），
不在本文件——查按键极性要跨文件看。

## 4. 使用陷阱

- **改菜单项数**：`MENU_LENGTH` 必须与 showMenu/showFastMenu 的 menuItems[]
  数组长度一致（Watchy.cpp 两处复制粘贴）。
- **UP_BTN_MASK 与 UP_BTN_PIN 成对改**：ext1 位图是 BIT64(引脚号)，
  换引脚要同时改两者，否则按键唤醒后 wakeupBit 判错键。
- **RTC_TYPE 宏现在只被 library 使用方间接用**：运行期实际靠
  WatchyRTC::init 的 I2C 探测（WatchyRTC.cpp:18），编译期宏是历史遗留。
- **单行掩码宏勿改多行续行**：`BTN_PIN_MASK  MENU_BTN_MASK|BACK_BTN_MASK|...`
  若换成 `\` 续行，中间插注释会改变宏展开（红线规则）。
- **版本号两处不同步**：`WATCHY_LIB_VER "1.4.14"` vs library.properties
  `version=1.4.15`——About 页显示的是前者。
- **V3 的 SPI SS 与 DISPLAY_CS 同为 33**：屏与（未来）SD 卡同片选脚的
  设计提示，当前固件未用 SD。
