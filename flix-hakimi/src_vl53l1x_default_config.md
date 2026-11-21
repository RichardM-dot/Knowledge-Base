# src_vl53l1x_default_config.md —— ST ULD 91 字节默认配置块

> **对应源文件：`references/firmware/flix-hakimi-pio/src/vl53l1x_default_config.h`（共 41 行）**
> **机器提取，请勿手改**。数据表文件：91 字节按序写入 0x2D~0x87。
> 延伸阅读：[src_laser.md](src_laser.md)（laserInitSensor 唯一使用点）、[src_vl53l1x_regs.md](src_vl53l1x_regs.md)

## 来源（文件头 1-15 行）

- 来源：stm32duino/VL53L1X 的 `vl53l1x_class.cpp` 中 `VL51L1X_DEFAULT_CONFIGURATION[]`（ST ULD / STSW-IMG007 移植版）。
- 内容：91 字节，第 i 个写进寄存器 0x2D+i，对应 ULD 的 `VL53L1X_SensorInit()`。

## ★ 为什么必须有这一段（本工程实测）

> 文件头 8-15 行：
> 这块模块上若只按 Pololu 精简版 init 配置（不写本表），会出现"所有配置寄存器读回都正确、SYSTEM__MODE_START=0x40，但结果寄存器恒为 0x0D 01 00 FF FF…、测量序列根本不产出数据"的现象。补上本表 + ULD 的"训练测距"流程后恢复正常。
> 注意第 4 个字节（寄存器 0x30）＝ 0x01，即**中断高有效**——它决定"数据就绪"必须按 bit0==1 判断（ULD 的 CheckForDataReady 先读极性再比较）。

## 文件结构

| 行号 | 内容 |
|---|---|
| 1-24 | 文件头：来源、必要性、用法 |
| 25-27 | `#pragma once` + `#include <stdint.h>` |
| 29 | `#define LASER_DEFAULT_CONFIG_BASE 0x2D` |
| 31-41 | `static const uint8_t laserDefaultConfig[]`：8 行 ×11 字节 + 3 字节 = 91 字节，每行行尾注释标出该行首字节的寄存器地址（0x2D~0x87） |

## 使用点（唯一）

laser.ino 的 laserInitSensor() 第 3 步：

```c
// laser.ino:549-552
	// 3) ★ ST ULD 的 91 字节默认配置块（0x2D~0x87）
	for (unsigned i = 0; i < sizeof(laserDefaultConfig); i++) {
		laserWrite(LASER_DEFAULT_CONFIG_BASE + i, laserDefaultConfig[i]);
	}
```

## 陷阱

- **不要手改数值**：表来自上游，改错芯片量不出距离；改了也无法与手册/官方库对照。
- 没有逻辑可读：逐行"读懂"没有意义，它是寄存器初值的转储。
- `static const`：static 限本编译单元（拼进 laser.ino），const 只读。

## 表内关键字节与本工程实测的对应关系

虽然整表不可改，但有几个字节的值解释了 laser.ino 驱动层的行为，值得对号入座：

| 表内位置 | 寄存器 | 值 | 对驱动的影响 |
|---|---|---|---|
| 第 4 字节 | 0x30 = GPIO_HV_MUX__CTRL | 0x01 | bit4=0 ⇒ 中断极性=1 ⇒ "就绪 = bit0==1"（laserReadInterruptPolarity 读出的就是这个来源） |
| 0x2E/0x2F 对应字节 | PAD 上拉配置 | （bit0 待置 1） | laserInitSensor 第 4 步"读-改-写"把上拉切到 AVDD——配置块里这两个字节的注释就是这么要求的 |
| 尾部 0x85~0x87 | SYSTEM 组 | 0x01,0x00,0x00 | 邻近 SYSTEM__INTERRUPT_CONFIG_GPIO=0x20 的语义区（新样本就绪中断） |

其余字节覆盖距离算法、SPAD、相位校准、超时等配置区——任何一处被改坏的症状都是"初始化读回正常但测不出距离"或"距离系统性漂移"，且很难与 Pololu/ULD 应有值对照。

## 验证方法（改动了驱动流程时）

1. `laser dump` 的 [6] 段打印关键配置寄存器读回值，与 ULD 应有值对照（laser.ino:983-1003）；
2. 两次隔 150 ms 的突发读必须不同（测距在跑）；
3. `laser dss` 看实际有效 SPAD ≈ 200 个（若 ≈1.5 个，问题不在本表而在 DSS_TARGET_RATE 没写）；
4. 极端情况（怀疑表被改坏）：用 git 恢复本文件后 `laser read` 复测极差（正常 10 个样本极差 ~3 mm）。
