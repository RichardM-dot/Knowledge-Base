# CRSF 协议头 —— crsf_protocol.h

对应源文件：`src/include/crsf_protocol.h`（共 579 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读 + 本条目逐结构表）
> 相关文档：[04-CRSF 协议对接](../../../references/firmware/ExpressLRS/Docs/04-CRSF 协议对接.md)

---

## 1. 文件职责

完整描述 CRSF 串口协议：帧头、地址、帧类型、传感器负载、通道换算、字节序工具。
ELRS 只用其中一个子集（通道、LinkStats、设备信息、参数、MSP、命令）。

---

## 2. 常量

| 宏 | 行号 | 值 | 说明 |
|---|---|---|---|
| `PACKED` | 33 | `__attribute__((packed))` | 取消结构体填充 |
| `CRSF_CRC_POLY` | 35 | `0xd5` | CRC8 多项式 |
| `US_CHANNEL_VALUE_MIN` | 37 | 476 | 拉伸下限（us） |
| `US_CHANNEL_VALUE_STD_MIN` | 39 | 988 | 标准 -100% |
| `US_CHANNEL_VALUE_CENTER` | 40 | 1500 | 中点 |
| `US_CHANNEL_VALUE_STD_MAX` | 41 | 2012 | 标准 +100% |
| `US_CHANNEL_VALUE_MAX` | 43 | 2523 | 拉伸上限 |
| `CRSF_CHANNEL_VALUE_STD_MIN` | 46 | 172 | 对应 988us |
| `CRSF_CHANNEL_VALUE_MID` | 48 | 992 | 对应 1500us |
| `CRSF_CHANNEL_VALUE_STD_MAX` | 50 | 1811 | 对应 2012us |
| `CRSF_CHANNEL_VALUE_UNSET` | 52 | 0xFFFF | 内部哨兵 |
| `CRSF_MIN_PACKET_LEN` | 54 | 4 | 最短帧 |
| `CRSF_MAX_PACKET_LEN` | 55 | 64 | 最长帧 |
| `CRSF_SYNC_BYTE` | 57 | 0xC8 | 标准帧首字节 |
| `CRSF_FRAME_NOT_COUNTED_BYTES` | 59 | 2 | sync + size |
| `CRSF_FRAME_CRC_SIZE` | 64 | 1 | CRC8 |

---

## 3. 帧结构

### 3.1 标准帧（182 行）

```c
typedef struct crsf_header_s
{
    uint8_t sync_byte;
    uint8_t frame_size;  // counts size after this byte, so it must be the payload size + 2 (type and crc)
    crsf_frame_type_e type;
    uint8_t payload[0];
} PACKED crsf_header_t;
```

`frame_size` 的语义是“本字节之后”的长度，整帧 = `frame_size + 2`。
`frame_size` 最大 62（64 - 2）。

### 3.2 扩展帧（194 行）

```c
typedef struct crsf_ext_header_s
{
    // Common header fields, see crsf_header_t
    uint8_t device_addr;
    uint8_t frame_size;
    crsf_frame_type_e type;
    // Extended fields
    crsf_addr_e dest_addr;
    crsf_addr_e orig_addr;
    uint8_t payload[0];
} PACKED crsf_ext_header_t;
```

首字节是**目标地址**而非 0xC8。这是解析器必须靠首字节区分帧型的原因。

### 3.3 通道帧（209 行）

```c
typedef struct crsf_channels_s
{
    unsigned ch0 : 11;
    ...
    unsigned ch15 : 11;
} PACKED crsf_channels_t;
```

16 × 11bit = 22 字节。

### 3.4 LinkStats（420 行）

```c
typedef struct crsfPayloadLinkstatistics_s
{
    uint8_t uplink_RSSI_1;
    uint8_t uplink_RSSI_2;
    uint8_t uplink_Link_quality;
    int8_t uplink_SNR;
    uint8_t active_antenna;
    uint8_t rf_Mode;
    uint8_t uplink_TX_Power;
    uint8_t downlink_RSSI_1;
    uint8_t downlink_Link_quality;
    int8_t downlink_SNR;
} PACKED crsfLinkStatistics_t;
```

ELRS 扩展出 `elrsLinkStatistics_t`（多一个 `downlink_RSSI_2`）。

---

## 4. 枚举

| 枚举 | 行号 | 内容 |
|---|---|---|
| `crsf_frame_type_e` | 80 | 0x02 GPS … 0x80 ArduPilot |
| `crsf_command_e` | 118 | `CRSF_COMMAND_SUBCMD_RX = 0x10` |
| `crsf_subcommand_e` | 122 | BIND / MODEL_SELECT / TIMING |
| `crsf_addr_e` | 134 | 0x00 广播 … 0xEF ELRS_LUA |
| `crsf_value_type_e` | 155 | 参数类型 0..15、127 |

关键地址：

| 地址 | 值 | 用途 |
|---|---|---|
| `CRSF_ADDRESS_BROADCAST` | 0x00 | 广播 |
| `CRSF_ADDRESS_USB` | 0x10 | USB |
| `CRSF_ADDRESS_FLIGHT_CONTROLLER` | 0xC8 | 飞控 |
| `CRSF_ADDRESS_RADIO_TRANSMITTER` | 0xEA | 遥控器 |
| `CRSF_ADDRESS_CRSF_RECEIVER` | 0xEC | 接收机 |
| `CRSF_ADDRESS_CRSF_TRANSMITTER` | 0xEE | 发射模块 |
| `CRSF_ADDRESS_ELRS_LUA` | 0xEF | ELRS 脚本 |

---

## 5. 换算函数

| 函数 | 行号 | 输入 → 输出 |
|---|---|---|
| `fmap()` | 441 | 任意区间映射（带四舍五入与钳位） |
| `CRSF_to_US()` | 448 | 172..1811 → 988..2012 |
| `UINT10_to_CRSF()` | 454 | 0..1023 → 172..1811 |
| `CRSF_to_UINT10()` | 460 | 172..1811 → 0..1023 |
| `N_to_CRSF(val,max)` | 466 | 0..max → 1000..2000 |
| `CRSF_to_N(val,cnt)` | 472 | 通道值 → 0..cnt-1 |
| `CRSF_to_SWITCH3b()` | 482 | 通道值 → 0..5 或 7（中位） |
| `SWITCH3b_to_CRSF()` | 500 | 反向 |
| `CRSF_to_BIT()` | 514 | 通道值 → 0/1 |
| `BIT_to_CRSF()` | 520 | 0/1 → 1000/2000 |

`fmap()` 的实现细节：

```c
static uint16_t ICACHE_RAM_ATTR fmap(uint16_t x, uint16_t in_min, uint16_t in_max, uint16_t out_min, uint16_t out_max)
{
    int32_t result = ((int32_t)(x - in_min) * (out_max - out_min) * 2 / (in_max - in_min) + out_min * 2 + 1)/2;
    return result < 0 ? 0 : (result > 65535 ? 65535 : result);
}
```

先乘 2 再除 2 实现四舍五入；负值钳 0、超界钳 65535。

---

## 6. 其他工具

| 函数/宏 | 行号 | 说明 |
|---|---|---|
| `CRSF_MK_FRAME_T(payload)` | 190 | 生成“头+负载+CRC”的结构体类型 |
| `CRSF_MK_EXT_FRAME_T(payload)` | 191 | 扩展帧版本 |
| `CalcCRCMsp()` | 525 | MSP 用的异或校验（不是 CRSF 的 CRC8） |
| `htobe16/htobe32/htobe24` | 535+ | 主机序 → 大端 |
| `be16toh/be32toh` | 544+ | 大端 → 主机序 |

---

## 7. 设计要点与观察

1. **全大端**：所有多字节字段都要转换，ESP32 是小端，最容易忘。
2. **首字节二义性**：标准帧 0xC8，扩展帧 dest_addr，解析器必须先判。
3. **通道值三套坐标**：CRSF（172..1811）、us（988..2012）、OTA（0..1023），
   每层转换都要过对应函数。
4. **`frame_size` 不含自身**：这是所有 CRSF 解析 bug 的头号来源。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 35 行 | `CRSF_CRC_POLY` 在此定义但 CRC 实现在 `lib/CRC/crc.h` |
| 209 行 | 位域布局依赖编译器，不同平台可能不同（ELRS 只跑 GCC，安全） |
| 500 行 | `SWITCH3b_to_CRSF()` 的 `default` 分支公式对 val=6/7 有专门处理 |

---

## 8. 互链

- 使用方：[tx_main.md](tx_main.md)、[rx_main.md](rx_main.md)
- 协议详解：[04-CRSF 协议对接](../../../references/firmware/ExpressLRS/Docs/04-CRSF 协议对接.md)
- 分片常量：[telemetry_protocol_h.md](telemetry_protocol_h.md)
