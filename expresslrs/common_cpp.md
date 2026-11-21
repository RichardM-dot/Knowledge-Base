# common.cpp —— 空口速率表与全局状态

对应源文件：`src/src/common.cpp`（共 434 行）

> 工程：ExpressLRS ｜ 层级：A 级（逐函数中文深注）
> 相关文档：[00-总览与双链路架构](../../../references/firmware/ExpressLRS/Docs/00-总览与双链路架构.md)、
> [02-OTA 空中协议精读](../../../references/firmware/ExpressLRS/Docs/02-OTA 空中协议精读.md)

---

## 1. 文件职责

这是 TX/RX 共用的“参数数据库”+“全局状态定义”。内容分四块：

| 块 | 行号 | 内容 |
|---|---|---|
| 头注释 | 1-25 | 阅读地图与不变量 |
| SX127x 表 | 27-60 | 900M LoRa 六档 |
| SX1280 表 | 62-94 | 2.4G FLRC/LoRa 十档 |
| LR1121 表 | 96-146 | 20 档（含双频） |
| LR2021 表 | 148-206 | 24 档（含 FLRC） |
| 查表函数 | 232-300 | 越界钳位、按串口能力降档 |
| 查枚举 | 290-323 | 档位编号 ↔ 下标 |
| 全局变量 | 325-345 | connectionState、ChannelData 等 |
| 工具函数 | 342-434 | ChannelDataReset、TLM 换算、isDualRadio |

---

## 2. 两张表的结构

每个射频芯片定义两张**等长平行表**：

```c
expresslrs_mod_settings_s ExpressLRS_AirRateConfig[RATE_MAX] = { ... };
expresslrs_rf_pref_params_s ExpressLRS_AirRateRFperf[RATE_MAX] = { ... };
```

`RATE_MAX` 由 `common.h` 按芯片给出：SX127x=6、SX1280=10、LR1121=20、LR2021=24。
**表长度必须与 RATE_MAX 严格一致**，否则 `get_elrs_airRateConfig()` 会读到越界内存。

### 2.1 调制表字段（位置初始化）

| 位置 | 字段 | 示例（SX1280 index 0） |
|---|---|---|
| 1 | `index` | 0 |
| 2 | `radio_type` | `FLRC_2G4` |
| 3 | `enum_rate` | `RATE_FLRC_2G4_1000HZ` |
| 4 | `bw` | `SX1280_FLRC_BR_0_650_BW_0_6` |
| 5 | `sf` | `SX1280_FLRC_BT_1` |
| 6 | `cr` | `SX1280_FLRC_CR_1_2` |
| 7 | `PreambleLen` | 32 |
| 8 | `TLMinterval` | `TLM_RATIO_1_128` |
| 9 | `FHSShopInterval` | 2 |
| 10 | `interval` | 1000（us） |
| 11 | `PayloadLength` | `OTA4_PACKET_SIZE` |
| 12 | `numOfSends` | 1 |

LR1121/LR2021 的 `expresslrs_mod_settings_s` 多了 `bw2/sf2/cr2/PreambleLen2` 四个字段，
插在第 7 与第 8 之间。**这就是为什么直接复制 SX1280 的表项到 LR1121 会错位**。

### 2.2 性能表字段

| 字段 | 单位 | 用途 |
|---|---|---|
| `RXsensitivity` | -dBm | 动态功率的基准点 |
| `TOA` | us | 时序计算、占空比 |
| `DisconnectTimeoutMs` | ms | RX 掉线判定 |
| `RxLockTimeoutMs` | ms | tentative 超时 |
| `SyncPktIntervalDisconnected` | ms | 未连接时的 SYNC 间隔 |
| `SyncPktIntervalConnected` | ms | 已连接时的 SYNC 间隔 |
| `DynpowerSnrThreshUp/Dn` | 定点 | SNR 判据阈值，`-127` 表示改用 RSSI |

---

## 3. 函数清单

| 函数 | 行号 | 返回值 | 说明 |
|---|---|---|---|
| `get_elrs_airRateConfig(index)` | 232 | `expresslrs_mod_settings_s*` | 越界钳到 `RATE_MAX-1` |
| `get_elrs_RFperfParams(index)` | 248 | `expresslrs_rf_pref_params_s*` | 同上，必须与上者成对调用 |
| `get_elrs_HandsetRate_max(rateIndex, minInterval)` | 267 | `uint8_t` | 找满足串口能力的最低档 |
| `enumRatetoIndex(eRate, idx)` | 290 | `bool` | 线性查找，O(RATE_MAX) |
| `enumRatetoIndexSafe(eRate)` | 312 | `uint8_t` | 找不到时给最慢/最快档 |
| `ChannelDataReset()` | 342 | void | 全通道置 0xFFFF |
| `TLMratioEnumToValue(enumval)` | 355 | `uint8_t` | 比例枚举 → 分母 |
| `TLMBurstMaxForRateRatio(rateHz, ratioDiv)` | 374 | `uint8_t` | 遥测连发上限 |
| `isDualRadio()` | 400 | bool | 是否有第二颗射频 |
| `isSupportedRFRate(index)` | 413 | bool | LR1121/LR2021 专用三重判据 |

---

## 4. 关键代码

### 4.1 越界钳位（232 行）

```c
expresslrs_mod_settings_s *get_elrs_airRateConfig(uint8_t index)
{
    if (RATE_MAX <= index)
    {
        // Set to last usable entry in the array
        index = RATE_MAX - 1;
    }
    return &ExpressLRS_AirRateConfig[index];
}
```

为什么需要：`index` 来自 EEPROM 或空口，可能是脏值（例如从 24 档的 LR2021
换到 6 档的 SX127x，配置里还存着 index=15）。钳位保证不越界，代价是“静默降级”
——用户会看到“配置的档位不是实际档位”，这也是排查时要先看 Web UI 实际档位的原因。

### 4.2 按串口能力降档（267 行）

```c
uint8_t get_elrs_HandsetRate_max(uint8_t rateIndex, uint32_t minInterval)
{
    while (rateIndex < RATE_MAX)
    {
        expresslrs_mod_settings_s const * const ModParams = &ExpressLRS_AirRateConfig[rateIndex];
        // Handset interval = time between packets from handset, which is expected to be air rate * number of times it is sent
        uint32_t handsetInterval = ModParams->interval * ModParams->numOfSends;
        if (handsetInterval >= minInterval && isSupportedRFRate(rateIndex))
            break;
        ++rateIndex;
    }

    return rateIndex;
}
```

判据是 `interval * numOfSends >= minInterval`：连发 N 次时占用 N 个包周期，
必须不短于遥控器出帧间隔，否则会重复发同一帧。`minInterval` 由
`handset->getMinPacketInterval()` 给出（420k 波特率下约 2000us）。

返回值可能等于 `RATE_MAX`（全都不满足），调用者要做边界处理。

### 4.3 安全查表（312 行）

```c
uint8_t enumRatetoIndexSafe(expresslrs_RFrates_e const eRate)
{
    uint8_t idx;
    if (enumRatetoIndex(eRate, idx))
        return idx;

    // If 25Hz selected and not available, return the slowest rate available
    // else return the fastest rate available (500Hz selected but not available)
    return (eRate == RATE_LORA_900_25HZ) ? RATE_MAX - 1 : 0;
}
```

语义：想要最慢档但硬件没有 → 给最慢的可用档；其它情况 → 给最快的。
绑定流程用它，保证“无论配置多离谱，都能找到一个能用的档位”。

### 4.4 回传比例换算（355 行）

```c
uint8_t ICACHE_RAM_ATTR TLMratioEnumToValue(expresslrs_tlm_ratio_e const enumval)
{
    // !! TLM_RATIO_STD/TLM_RATIO_DISARMED should be converted by the caller !!
    if (enumval == TLM_RATIO_NO_TLM)
        return 1;

    // 1 << (8 - (enumval - TLM_RATIO_NO_TLM))
    // 1_128 = 128, 1_64 = 64, 1_32 = 32, etc
    return 1 << (8 + TLM_RATIO_NO_TLM - enumval);
}
```

验证：`TLM_RATIO_1_128 = 2`，`TLM_RATIO_NO_TLM = 1`，所以
`1 << (8 + 1 - 2) = 1 << 7 = 128`。公式依赖枚举**连续且按 2 的幂排列**，
新增比例必须插在正确位置。

### 4.5 遥测连发上限（374 行）

```c
    constexpr uint32_t TELEM_MIN_LINK_INTERVAL_MS = 512U;

    unsigned retVal = TELEM_MIN_LINK_INTERVAL_MS * rateHz / ratioDiv / 1000U;

    // Reserve one slot for LINK telemetry. 256 becomes 255 here, safe for return in uint8_t
    if (retVal > 1)
        --retVal;
    else
        retVal = 1;
```

以 1000Hz、1:2 为例：`512 * 1000 / 2 / 1000 = 256`，减 1 得 255。
这是 uint8_t 能表示的最大安全值（256 会溢出成 0）。
注释“Reserve one slot for LINK telemetry”说明减 1 是为了给 LinkStats 留位置。

---

## 5. 全局变量

| 变量 | 行号 | 初值 | 说明 |
|---|---|---|---|
| `connectionHasModelMatch` | 324 | false | ModelMatch 是否通过 |
| `teamraceHasModelMatch` | 325 | true | 团队赛模式是否匹配 |
| `InBindingMode` | 326 | false | 是否绑定中 |
| `ExpressLRS_currTlmDenom` | 327 | 1 | 当前 TLM 分母 |
| `connectionState` | 328 | disconnected | 全局状态 |
| `ExpressLRS_currAirRate_Modparams` | 329 | nullptr | 当前调制参数指针 |
| `ExpressLRS_currAirRate_RFperfParams` | 330 | nullptr | 当前性能参数指针 |
| `ChannelData[16]` | 333 | 未初始化 | 16 通道 |

注意两个指针初值是 `nullptr`：在 `SetRFLinkRate()` 之前访问它们会崩溃。
所以 `setup()` 的顺序要求“先 ChangeRadioParams 再启动定时器”。

---

## 6. 设计要点与观察

1. **表驱动**：加档位只加一行，不改流程。这是 ELRS 支持四种芯片的核心手法。
2. **平行表**：调制与性能分离，便于独立调整（例如只改 TOA 不改 BW）。
3. **越界静默降级**：不崩溃但会“配置与实效不符”，排查时先对齐两边。
4. **`ICACHE_RAM_ATTR`**：`enumRatetoIndex`/`TLMratioEnumToValue`/`isDualRadio`
   都在 ISR 里被调用，必须常驻 IRAM。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| `get_elrs_HandsetRate_max()` 267 行 | 可能返回 `RATE_MAX`，调用者需自行处理 |
| `isSupportedRFRate()` 413 行 | 仅 LR1121/LR2021 编译；其它芯片内联返回 true |
| SX127x 表 27-60 行 | 最后一档 numOfSends=4，LQ 统计需用 LQCalcDVDA |

---

## 7. 互链

- 头文件定义：[common_h.md](common_h.md)
- 使用方：[tx_main.md](tx_main.md)、[rx_main.md](rx_main.md)
- 协议细节：[02-OTA 空中协议精读](../../../references/firmware/ExpressLRS/Docs/02-OTA 空中协议精读.md)
