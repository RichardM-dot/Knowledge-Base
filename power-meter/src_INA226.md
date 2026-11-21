# src_INA226 —— INA226 驱动与校准

> 对应源文件：`src/INA226.c`（共 187 行）、`src/INA226.h`（共 104 行）
> 注释级别：A 级（逐函数中文深注），strip_equiv 校验 PASS
> 工程路径：`/Users/theo-richard/workspace/references/firmware/Power-Meter`

**互链 Docs：** [02-INA226驱动精读](../../../references/firmware/Power-Meter/Docs/02-INA226驱动精读.md)（公式推导与 I2C 时序全文）、[00-总览与硬件架构](../../../references/firmware/Power-Meter/Docs/00-总览与硬件架构.md)

---

## 1. 文件职责与位置

INA226 是 TI 的电流/电压/功率监测芯片，I2C 接口，本工程 7 位地址 0x40（HAL 8 位形式 `0x80`，A0=A1=GND）。`INA226.h` 定义寄存器地址、量程宏与协议常量；`INA226.c` 实现"UID→采样电阻映射 → Current_LSB/CAL 计算 → 写 CONFIG/CAL → 四个换算读取函数"。

数据流位置：**数据源头**。被 `main.c:83-86` 的 TIM1 回调每 2 ms 调用一次（4 次 I2C 阻塞读）。

---

## 2. 头文件关键宏（INA226.h）

| 宏 | 值 | 行号 | 含义 |
|---|---|---|---|
| `INA226_I2C_ADDR` | 0x80 | :25 | 7 位 0x40 左移 1 位的 HAL 形式 |
| `INA226_R_SHUNT` | 0.001f | :29 | 默认采样电阻 1 mΩ（可被板级表覆盖） |
| `INA226_MAX_CURRENT` | 32.7f | :33 | 设计最大电流，决定 Current_LSB |
| `INA226_REG_*` | 0x00~0x05 | :37-42 | CONFIG/SHUNT/BUS/POWER/CURRENT/CALIB |
| `INA226_CURRENT_LSB` | 32.7/32767 | :49 | 编译期参考值（运行时另有 static 计算） |
| `INA226_CALIB_VAL` | 5130（默认参数） | :54 | 编译期参考（实际用板级电阻重算） |
| `INA226_CONFIG_VAL` | 0x0417 | :62 | 见下表 |

CONFIG 0x0417 逐位（INA226.h:56-61 注释 + datasheet 验算）：

| 字段 | 位 | 值 | 含义 |
|---|---|---|---|
| AVG | 11 | 000 | 平均 1 次 |
| VBUSCT | 10 | 100 | 总线转换 1.1 ms |
| VSHCT | 7 | 000 | 分流转换 140 µs |
| bit4 | 4 | 1 | 保留位原工程写 1（移植保留） |
| MODE | 2 | 111 | 分流+总线连续转换 |

单轮转换 1.24 ms < 2 ms 采样周期，但驱动不检查 ConversionReady，偶发读到旧值。

---

## 3. 逐函数表（INA226.c）

| 函数 | 行号 | 职责 | 返回 |
|---|---|---|---|
| `INA226_Init` | 67-109 | 查 UID 表、算 LSB/CAL、写 CONFIG 与 CAL | HAL 状态 |
| `INA226_Read_ShuntVoltage` | 117-129 | 读 0x01，×2.5 µV | V，失败 0.0f |
| `INA226_Read_BusVoltage` | 137-148 | 读 0x02，×1.25 mV | V，失败 0.0f |
| `INA226_Read_Current` | 159-168 | 读 0x04，×g_CurrentLSB | A，失败 0.0f |
| `INA226_Read_Power` | 176-187 | 读 0x03，×25×g_CurrentLSB | W，失败 0.0f |

文件级变量：`board_conf[]`（:34-38，3 项 UID→电阻表）、`g_CurrentLSB`（:45，运行时 LSB）、`raw_v1/raw_v/raw_i/raw_p`（:48-51，原始值供调试观察）、`mcu_id`（:54）、`rx_data1[2]`（:151，电流读缓冲，原工程全局保留）。

---

## 4. 关键代码摘录

### 4.1 校准计算（INA226.c:76-94）

```c
    /* ① 读取 STM32 唯一 ID 的低 32 位 */
    mcu_id = HAL_GetUIDw0();

    /* ② 在板级配置表中查找匹配的采样电阻 */
    for (i = 0; i < BOARD_CONF_COUNT; i++) {
        if (board_conf[i].uid == mcu_id) {
            current_r_shunt = board_conf[i].r_shunt;
            break;
        }
    }

    /* ③ 计算电流 LSB：把最大电流均匀映射到 15 位有符号满量程 */
    g_CurrentLSB = INA226_MAX_CURRENT / 32767.0f;

    /* ④ 计算校准寄存器值：CAL = 0.00512 / (Current_LSB × R_SHUNT) */
    if (g_CurrentLSB > 0 && current_r_shunt > 0) {
        calib_val = (uint16_t)(0.00512f / (g_CurrentLSB * current_r_shunt));
    } else {
        calib_val = 0;  /* 参数异常时写入 0，避免出现非法值 */
    }
```

数值代入：Current_LSB = 32.7/32767 = 0.997955 mA；CAL（默认 1 mΩ）= 0.00512/(0.997955e-3 × 1e-3) = 5130.49 → **5130**；板 1（0.9897 mΩ）= 5183.88 → **5183**。`(uint16_t)` 向零截断是 datasheet 推荐做法。

### 4.2 板级映射表（INA226.c:34-38）

```c
static const BoardConfig_t board_conf[] = {
    {0x0664FF54, 0.0009897f},   /* 板子 1：约 0.99 mΩ */
    {0x87654321, 0.005f},       /* 板子 2：5 mΩ（示例） */
    {0x11223344, 0.010f},       /* 板子 3：10 mΩ（示例） */
};
```

设计意图：不同功率板焊不同阻值采样电阻，用芯片 96 位 UID 低 32 位当"板号"，一份固件自动适配。未命中则用默认 1 mΩ——换板后若电流系数异常，先在调试器里看 `mcu_id` 是否在表内。

### 4.3 寄存器写入（INA226.c:96-108）

```c
    /* ⑤ 写配置寄存器（高字节在前） */
    tx_data[0] = (INA226_CONFIG_VAL >> 8) & 0xFF;
    tx_data[1] = INA226_CONFIG_VAL & 0xFF;
    status = HAL_I2C_Mem_Write(hi2c, INA226_I2C_ADDR, INA226_REG_CONFIG,
                               1, tx_data, 2, 100);
    if (status != HAL_OK) return status;
```

注意：CONFIG 写失败**提前返回**，CAL 保持上电默认 0 → 电流/功率寄存器无效。而 main.c:166 不检查返回值，故障表现为"读数全 0"而非报错。

### 4.4 典型读取函数（INA226.c:117-129）

```c
float INA226_Read_ShuntVoltage(I2C_HandleTypeDef *hi2c)
{
    uint8_t rx_data[2];

    if (HAL_I2C_Mem_Read(hi2c, INA226_I2C_ADDR, INA226_REG_SHUNT,
                         1, rx_data, 2, 100) != HAL_OK) {
        return 0.0f;
    }

    /* 高字节在前，组合成 16 位有符号数 */
    raw_v1 = (int16_t)((rx_data[0] << 8) | rx_data[1]);
    return raw_v1 * 2.5e-6f;  /* LSB = 2.5 µV */
}
```

四个读取函数同构：Mem_Read 2 字节（超时 100 ms）→ 大端拼接 → 乘固定/运行时 LSB。失败返回 0.0f 在"无电流"语义上恰好合理，但与真实断流无法区分（配合总线电压同判）。

---

## 5. 换算公式汇总（速查）

| 物理量 | 公式 | LSB | 满量程 |
|---|---|---|---|
| 分流电压 | raw × 2.5 µV | 2.5 µV 固定 | ±81.92 mV |
| 总线电压 | raw × 1.25 mV | 1.25 mV 固定 | 40.96 V（芯片上限 36 V） |
| 电流 | raw × Current_LSB | 0.998 mA | 32.7 A（超程回绕） |
| 功率 | raw × 25 × Current_LSB | ≈ 24.95 mW | ≈ 817 W（寄存器 int16 读出上限 32767） |

---

## 6. 时序账

| 操作 | 耗时 | 场景 |
|---|---|---|
| Mem_Write 2B | ~0.17 ms | 初始化 ×2 次 |
| Mem_Read 2B | ~0.16 ms | 运行期每 2 ms ×4 → 约 0.6 ms |
| I2C 超时上限 | 100 ms/次 | 总线卡死最坏 400 ms（在 0 级中断里） |

---

## 7. 已知问题（记录在案，不改码）

| # | 问题 | 位置 |
|---|---|---|
| 1 | 不检查 ConversionReady，偶发读旧值 | 各 Read 函数 |
| 2 | 失败返回 0.0f 与真实断流不可区分 | 各 Read 函数 |
| 3 | 原始值存全局（raw_*）非线程安全风格 | :48-51 |
| 4 | 电流读缓冲 rx_data1 为全局（其余为栈） | :151 |
| 5 | 头文件 `INA226_CURRENT_LSB`/`CALIB_VAL` 宏与运行时计算并存，易误用 | INA226.h:49-54 |
| 6 | 电流寄存器按 32.7 A 设计，超程回绕 | INA226.h:33 |

---

## 8. 修改指引

| 想改 | 位置 |
|---|---|
| 采样电阻默认值 | INA226.h:29 |
| 电流量程（重算 CAL） | INA226.h:33（Current_LSB 随之变） |
| 转换时间/平均次数 | INA226.h:62（CONFIG_VAL） |
| 增加新板 | INA226.c:34-38 表加一行（UID 用调试器读 mcu_id） |
| I2C 超时 | 各函数第 5 参数 `100` |
