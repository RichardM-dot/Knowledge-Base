# src_vl53l1x_regs.md —— VL53L1X 寄存器地址表

> **对应源文件：`references/firmware/flix-hakimi-pio/src/vl53l1x_regs.h`（共 1215 行）**
> **机器提取，请勿手改**。数据表文件：1186 条"名字 = 16 位地址"，无逻辑。
> 延伸阅读：[src_laser.md](src_laser.md)（唯一消费者）、[src_vl53l1x_default_config.md](src_vl53l1x_default_config.md)

## 来源与提取方式（文件头 1-23 行）

- 来源：Pololu vl53l1x-arduino（master）`VL53L1X.h` 的 `enum regAddr : uint16_t`，该库基于 ST 官方 API（STSW-IMG007）与数据手册/UM2356。
- 提取：按 `名字 = 0xXXXX` 逐行解析，共 **1186 条**，未人工增删；本固件驱动的 55 个寄存器名全部命中。
- **寄存器地址固定 16 位**：传输先高字节后低字节（laser.ino 的 laserI2cRead16Reg/Write16Reg）。

## 文件结构

| 行号 | 内容 |
|---|---|
| 1-23 | 文件头导读（来源/用法/命名规则） |
| 24-26 | `#pragma once` + `#include <stdint.h>` |
| 28-1215 | `enum VL53L1XReg : uint16_t { ... }`，1186 条 |

## 本固件实际用到的寄存器（按用途分组）

| 用途 | 寄存器 | 值 |
|---|---|---|
| 身份 | IDENTIFICATION__MODEL_ID / REVISION_ID | 期望 0xEACC |
| 复位/启动 | SOFT_RESET / FIRMWARE__SYSTEM_STATUS / SYSTEM__MODE_START | 0x00/0x01；bit0 启动完成；0x40 连续 |
| 配置块 | LASER_DEFAULT_CONFIG_BASE=0x2D（在 default_config.h） | 0x2D~0x87 |
| 上拉 | PAD_I2C_HV__EXTSUP_CONFIG / GPIO_HV_PAD__CTRL | bit0=1 |
| **DSS（最大坑）** | DSS_CONFIG__TARGET_TOTAL_RATE_MCPS | **必须写 0x0A00**（0x0024） |
| 中断 | GPIO_HV_MUX__CTRL / GPIO__TIO_HV_STATUS / SYSTEM__INTERRUPT_CLEAR / SYSTEM__INTERRUPT_CONFIG_GPIO | 极性 bit4；就绪 bit0；清 0x01 |
| 模式 | PHASECAL_CONFIG__TIMEOUT_MACROP / RANGE_CONFIG__VCSEL_PERIOD_A/B / RANGE_CONFIG__VALID_PHASE_HIGH / SD_CONFIG__WOI_SD0 / SD_CONFIG__INITIAL_PHASE_SD0 | Short=0x14/0x07/0x05/0x38/0x0705/0x0606；Long=0x0A/0x0F/0x0D/0xB8/0x0F0D/0x0E0E |
| 预算 | RANGE_CONFIG__TIMEOUT_MACROP_A/B | ULD 离散数值表（laser.ino:420-427） |
| VHV | VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND / VHV_CONFIG__INIT | 训练后 0x09 / 0x00 |
| ROI | ROI_CONFIG__USER_ROI_CENTRE_SPAD / REQUESTED_GLOBAL_XY_SIZE | 0xC7 / 0xFF |
| 结果 | RESULT__RANGE_STATUS(0x0089) 起 17 字节突发读；RESULT__OSC_CALIBRATE_VAL(&0x3FF)；OSC_MEASURED__FAST_OSC__FREQUENCY | 距离在偏移 [13][14] |
| 周期 | SYSTEM__INTERMEASUREMENT_PERIOD | (OSC_CALIBRATE_VAL&0x3FF)×ms×1.075 |
| 其他 | ALGO__RANGE_MIN_CLIP（dump 写读试验）、ALGO__PART_TO_PART_RANGE_OFFSET_MM | — |

## 用法要点

- `enum : uint16_t`：每条常量是 16 位无符号数 = 寄存器地址；laser.ino 里直接 `laserRead16(RESULT__RANGE_STATUS)`。
- 基名/_HI/_LO 三行同地址模式：基址 = 高字节位置，_LO = 基址+1（表内 149 组别名全部如此）。
- 查寄存器直接编辑器搜索名字；想知道固件真正用了哪些，看 laser.ino 的 laserDumpRegisters 打印清单。
- 旧函数名 laserI2cReadRegs/WriteReg 已改 laserI2cRead16Reg/Write16Reg（文件头 20-21 行说明）。

## 结果寄存器区（0x0089 起）的字节含义——驱动解析的依据

`RESULT__RANGE_STATUS = 0x0089` 起突发读 17 字节（laser.ino:797），每个偏移对应的寄存器名都能在本表查到：

| 偏移 | 寄存器 | 用途 |
|---|---|---|
| [0] | RESULT__RANGE_STATUS（0x89） | 状态码（&0x1F 后按 ULD 映射） |
| [1] | RESULT__REPORT_STATUS（0x8A） | 不用 |
| [2] | RESULT__STREAM_COUNT（0x8B） | 不用 |
| [3][4] | RESULT__DSS_ACTUAL_EFFECTIVE_SPADS_SD0（0x8C） | 实际有效 SPAD 数（8.8 定点，`laser dss` 诊断用） |
| [5][6] | RESULT__PEAK_SIGNAL_COUNT_RATE_MCPS_SD0（0x8E） | 不用 |
| [7][8] | RESULT__AMBIENT_COUNT_RATE_MCPS_SD0（0x90） | 环境光（9.7 定点 /128 = MCPS） |
| [9][10] | RESULT__SIGMA_SD0（0x92） | 不用 |
| [11][12] | RESULT__PHASE_SD0（0x94） | 不用 |
| [13][14] | RESULT__FINAL_CROSSTALK_CORRECTED_RANGE_MM_SD0（0x96） | **距离 mm** |
| [15][16] | RESULT__PEAK_SIGNAL_COUNT_RATE_CROSSTALK_CORRECTED_MCPS_SD0（0x98） | 信号强度 |

## 本表与 Pololu 库/ST ULD 的关系

- Pololu 的 `enum regAddr` 是 ST 官方 API 寄存器名的超集搬运——所以表内名字与 ULD 源码（如 vl53l1x_class.cpp）一致，交叉查手册/ULD 源码都不用改名。
- 唯一要小心的差异：Pololu 的精简 init 流程（不写 91 字节配置块、写死中断极性、关 VHV 自校准）在本模块上**三处都是错的**——正确流程见 laser.ino 的 laserInitSensor（本表只提供地址，不提供流程）。
