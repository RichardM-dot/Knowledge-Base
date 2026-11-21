# common.h —— 协议常量、核心结构与全局状态

对应源文件：`src/include/common.h`（共 448 行）

> 工程：ExpressLRS ｜ 层级：A 级（逐函数/逐结构中文深注）
> 相关文档：[00-总览与双链路架构](../../../references/firmware/ExpressLRS/Docs/00-总览与双链路架构.md)、
> [02-OTA 空中协议精读](../../../references/firmware/ExpressLRS/Docs/02-OTA 空中协议精读.md)

---

## 1. 文件职责

这是 TX/RX 共享的“字典”：所有枚举、结构、全局变量声明、查表函数原型。
按内容分五段：

| 段 | 行号 | 内容 |
|---|---|---|
| 头注释 | 1-30 | 阅读地图 |
| 包含 | 31-52 | 按芯片选驱动头文件 |
| 枚举 | 48-290 | 比例、状态、相位、速率、动作、输出模式、协议、通道 |
| 结构 | 187-226 | 性能画像、调制参数 |
| 声明 | 340-418 | RATE_MAX、查表函数、extern 变量 |

---

## 2. 枚举一览

| 枚举 | 行号 | 值的数量 | 持久化 | 备注 |
|---|---|---|---|---|
| `expresslrs_tlm_ratio_e` | 48 | 10 | 是 | 连续编码，公式依赖顺序 |
| `connectionState_e` | 65 | 12 | 否 | 用哨兵分段 |
| `TxTlmRcvPhase_e` | 99 | 3 | 否 | 仅 TX |
| `RXtimerState_e` | 108 | 3 | 否 | 仅 RX |
| `expresslrs_tlm_header_e` | 116 | 3 | 否 | 遥测信封 |
| `expresslrs_RFrates_e` | 131 | 30+ | 是（空口） | 编号即协议 |
| `tx_radio_mode_e` | 176 | 4 | 是 | 天线模式 |
| `tx_transmission_mode_e` | 186 | 2 | 是 | 普通/MAVLink |
| `action_e` | 250 | 10 | 是（4bit） | 按键动作 |
| `eServoOutputMode` | 274 | 16 | 是 | PWM 模式 |
| `eServoOutputFailsafeMode` | 295 | 3 | 是 | PWM 失控保护 |
| `eSerialProtocol` | 303 | 11 | 是 | 主串口协议 |
| `eSerial1Protocol` | 321 | 13 | 是 | 第二串口（ESP32） |
| `eFailsafeMode` | 341 | 3 | 是 | CRSF failsafe |
| `eAuxChannels` | 350 | 13 | 否 | 通道下标 |

**“持久化”一列的含义**：这些值会被写进 EEPROM 或空口，因此**只能追加、不能插队**。
`eSerialProtocol` 的注释写得很明确：

```c
    // Values are persisted in rx_config_t; append new protocols only.
```

---

## 3. 哨兵枚举：`connectionState_e`

```c
typedef enum
{
    connected,
    tentative,        // RX only
    awaitingModelId,  // TX only
    disconnected,
    MODE_STATES,
    // States below here are special mode states
    noCrossfire,
    bleJoystick,
    NO_CONFIG_SAVE_STATES,
    wifiUpdate,
    serialUpdate,
    // Failure states go below here to display immediately
    FAILURE_STATES,
    radioFailed,
    hardwareUndefined
} connectionState_e;
```

四段语义：

| 区间 | 判断式 | 含义 |
|---|---|---|
| `< MODE_STATES` | `connectionState < MODE_STATES` | 正常工作 |
| `>= MODE_STATES` | `connectionState > MODE_STATES` | 特殊模式（停射频） |
| `< NO_CONFIG_SAVE_STATES` | 同上 | 允许写 EEPROM |
| `>= FAILURE_STATES` | `>= FAILURE_STATES` | 故障 |

**坑**：`connected` 的值是 0，所以 `if (connectionState)` 恒为假。代码里一律显式比较。

---

## 4. 速率枚举：编号即协议

```c
typedef enum : uint8_t
{
    // RATE_MODULATION_BAND_RATE_MODE

    RATE_LORA_900_25HZ = 0,
    ...
    RATE_FSK_900_1000HZ_8CH,

    // 2.4GHz 段：LoRa 用于远距，FLRC 用于低延迟，FSK 用于最高 1000Hz
    RATE_LORA_2G4_25HZ = 20,
    ...
    RATE_FSK_2G4_1000HZ,

    // 双频段（LR1121/LR2021 两颗射频同时工作）：900M + 2.4G 各发一份
    RATE_LORA_DUAL_100HZ_8CH = 100,
    RATE_LORA_DUAL_150HZ,
} expresslrs_RFrates_e;
```

编号规则：

| 区段 | 范围 | 含义 |
|---|---|---|
| 900M | 0..11 | LoRa + 1 个 FSK |
| 2.4G | 20..35 | LoRa / FLRC / FSK |
| 双频 | 100..101 | 两颗射频不同频段 |

留空档（12..19、36..99）是**为未来扩展预留**，也让日志里一眼能看出频段。
这个编号会原样进 SYNC 包的 `rfRateEnum` 字段，所以是协议的一部分。

---

## 5. 两个核心结构

### 5.1 `expresslrs_rf_pref_params_s`（200 行）

```c
typedef struct expresslrs_rf_pref_params_s
{
    uint8_t index;
    int16_t RXsensitivity;                // expected min RF sensitivity
    uint16_t TOA;                         // time on air in microseconds
    uint16_t DisconnectTimeoutMs;
    uint16_t RxLockTimeoutMs;
    uint16_t SyncPktIntervalDisconnected;
    uint16_t SyncPktIntervalConnected;
    int8_t DynpowerSnrThreshUp;
    int8_t DynpowerSnrThreshDn;
} expresslrs_rf_pref_params_s;
```

字段用途：

| 字段 | 被谁读 | 用途 |
|---|---|---|
| `index` | `dynpower.cpp` | 检测速率变化时清统计 |
| `RXsensitivity` | `dynpower.cpp` | RSSI 判据的基准 |
| `TOA` | `tx/rx_main.cpp` | 算 `slack`、占空比 |
| `DisconnectTimeoutMs` | `rx_main.cpp` | 掉线判定 |
| `RxLockTimeoutMs` | `rx_main.cpp` | tentative 超时 |
| `SyncPktInterval*` | `tx_main.cpp` | SYNC 发送间隔 |
| `DynpowerSnrThreshUp/Dn` | `dynpower.cpp` | SNR 判据初值 |

`DynpowerSnrThreshUp == DYNPOWER_SNR_THRESH_NONE(-127)` 表示“该档不用 SNR，改用 RSSI”。
FLRC 档全是这个值，因为 FLRC 的 SNR 读数不可靠。

### 5.2 `expresslrs_mod_settings_t`（218 行）

```c
typedef struct expresslrs_mod_settings_s
{
    uint8_t index;
    RadioBandMod::Combined radio_type;
    expresslrs_RFrates_e enum_rate;
    uint8_t bw;
    uint8_t sf;
    uint8_t cr;
    uint8_t PreambleLen;
#if defined(RADIO_LR1121) || defined(RADIO_LR2021)
    uint8_t bw2;
    uint8_t sf2;
    uint8_t cr2;
    uint8_t PreambleLen2;
#endif
    expresslrs_tlm_ratio_e TLMinterval;
    uint8_t FHSShopInterval;
    int32_t interval;
    uint8_t PayloadLength;
    uint8_t numOfSends;
} expresslrs_mod_settings_t;
```

注意结构体写成了 `typedef struct expresslrs_mod_settings_s { ... } expresslrs_mod_settings_t;`。
在 C++ 里 **struct tag 本身就是一个类型名**，所以 `expresslrs_mod_settings_s`（tag）
与 `expresslrs_mod_settings_t`（typedef）两种写法都合法，代码里混用两者。
读代码时不要被两种写法迷惑，它们指的是同一个类型。

条件编译字段的位置很关键：LR1121/LR2021 时 `bw2..PreambleLen2` 插在中间，
所以**同一个初始化列表在不同芯片下含义不同**，复制表项时必须整行复制。

---

## 6. 常量与宏

| 宏 | 行号 | 值 | 说明 |
|---|---|---|---|
| `DYNPOWER_SNR_THRESH_NONE` | 194 | -127 | “不用 SNR”哨兵 |
| `SNR_SCALE(snr)` | 195 | `snr * RADIO_SNR_SCALE` | 浮点转定点 |
| `SNR_DESCALE(x)` | 196 | `x / RADIO_SNR_SCALE` | 定点转整数 dB |
| `RATE_MAX` | 372+ | 6/10/20/24 | 表长度 |
| `RATE_BINDING` | 373+ | 50Hz | 绑定档位 |
| `RATE_DUALBAND_BINDING` | 357+ | 2.4G 50Hz | 双频绑定 |

---

## 7. 全局声明与内联函数

```c
extern connectionState_e connectionState;
#if !defined(UNIT_TEST)
inline void setConnectionState(connectionState_e newState) {
    connectionState = newState;
    devicesTriggerEvent(EVENT_CONNECTION_CHANGED);
}
#endif
```

这是**唯一合法的状态修改方式**。直接赋值会漏掉设备事件，LED/屏幕不更新。

其余声明：

| 名称 | 行号 | 说明 |
|---|---|---|
| `get_elrs_airRateConfig` | 402 | 查调制表 |
| `get_elrs_RFperfParams` | 403 | 查性能表 |
| `get_elrs_HandsetRate_max` | 405 | 按串口降档 |
| `TLMratioEnumToValue` | 408 | 比例换算 |
| `TLMBurstMaxForRateRatio` | 410 | 连发上限 |
| `enumRatetoIndex` | 411 | 反查下标 |
| `enumRatetoIndexSafe` | 412 | 安全反查 |
| `ChannelData[16]` | 423 | 通道数据 |
| `ChannelDataReset` | 439 | 清通道 |
| `isDualRadio` | 441 | 双射频判断 |
| `isSupportedRFRate` | 444 | 档位支持判断 |

---

## 8. 设计要点与观察

1. **编号即协议**：所有会进空口/EEPROM 的枚举都显式赋值，且注释“append only”。
2. **哨兵分段**：状态机用区间判断而非位掩码，新增状态成本低。
3. **结构体按芯片变化**：`#if` 插字段导致初始化列表不可跨芯片复制。
4. **`RATE_MAX` 与表长度必须一致**：不一致会越界，且编译器不会报错。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 204 行 typedef | tag 名 `_s` 与 typedef 名 `_t` 混用，同类型两种写法 |
| 118 行 RFrates | 编号不连续（预留扩展），不能用“编号=下标”假设 |
| 65 行 connectionState | `connected == 0`，布尔判断易错 |

---

## 9. 互链

- 实现：[common_cpp.md](common_cpp.md)
- 使用：[tx_main.md](tx_main.md)、[rx_main.md](rx_main.md)
- 协议：[02-OTA 空中协议精读](../../../references/firmware/ExpressLRS/Docs/02-OTA 空中协议精读.md)
