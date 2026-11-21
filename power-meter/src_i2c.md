# src_i2c —— I2C1 外设（INA226 通道）

> 对应源文件：`src/i2c.c`（共 138 行；配套 `src/i2c.h` 共 64 行，不在注释清单内、已有注释）
> 注释级别：B 级（文件头导读块），strip_equiv 校验 PASS
> 工程路径：`/Users/theo-richard/workspace/references/firmware/Power-Meter`

**互链 Docs：** [02-INA226驱动精读](../../../references/firmware/Power-Meter/Docs/02-INA226驱动精读.md)（I2C 时序逐字节）、[01-启动流程与外设初始化](../../../references/firmware/Power-Meter/Docs/01-启动流程与外设初始化.md)（3.3 节）

---

## 1. 文件职责与位置

配置 I2C1 主机：400 kHz 快速模式、7 位地址、PB6=SCL / PB7=SDA 复用开漏。全局句柄 `hi2c1`（:36）被 INA226 驱动的全部读写使用（`INA226.c` 6 处 `HAL_I2C_Mem_Write/Read`）。I2C 无中断/DMA 参与——**同步阻塞模式**，调用方在 TIM1 中断里，这是采样链唯一的阻塞点。

---

## 2. 逐函数表

| 函数 | 行号 | 职责 |
|---|---|---|
| `MX_I2C1_Init` | 45-72 | 配置 ClockSpeed/DutyCycle/地址模式，调 `HAL_I2C_Init` |
| `HAL_I2C_MspInit` | 79-108 | GPIOB 时钟、PB6/PB7 复用开漏高速、I2C1 外设时钟 |
| `HAL_I2C_MspDeInit` | 115-134 | 关时钟、释放 PB6/PB7（运行期不调用） |

`i2c.h` 对外暴露 `hi2c1`（:45）与 `MX_I2C1_Init`（:54）。

---

## 3. 关键代码摘录

### 3.1 外设参数（i2c.c:55-63）

```c
  hi2c1.Instance = I2C1;                          /* 使用 I2C1 外设 */
  hi2c1.Init.ClockSpeed = 400000;                 /* SCL = 400 kHz */
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;         /* 快速模式占空比 2:1 */
  hi2c1.Init.OwnAddress1 = 0;                     /* 本机地址（主机模式不用） */
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT; /* 7 位地址模式 */
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;/* 关闭双地址模式 */
  hi2c1.Init.OwnAddress2 = 0;                     /* 第二地址不使用 */
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;/* 关闭广播呼叫 */
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;    /* 允许从机时钟延展 */
```

要点：

- `DutyCycle = 2`：快速模式下 Tlow/Thigh = 2（即高:低 = 1:2），符合 I2C 规范的 Fast-mode 占空比；
- `NoStretch = DISABLE`：允许从机拉低 SCL 拖慢主机——INA226 转换期间可能使用，保持标准行为；
- `ClockSpeed = 400000`：HAL 内部据此计算 CCR 寄存器（APB1 36 MHz 基准）。

### 3.2 引脚（i2c.c:92-99）

```c
    /* I2C1 引脚配置：
     *   PB6 → I2C1_SCL
     *   PB7 → I2C1_SDA
     * 复用开漏模式，高速输出 */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
```

开漏是 I2C 电气规范的硬要求（线与仲裁、双向），**必须有外部上拉**——INA226 模块一般自带；裸芯片要外接 4.7 kΩ 左右。

---

## 4. 时序量化

| 操作 | 字节流 | 耗时 @400 kHz |
|---|---|---|
| Mem_Write 2B | S+addr+W+reg+2data+P | ~0.17 ms |
| Mem_Read 2B | S+addr+W+reg+Sr+addr+R+2data+P（重复起始） | ~0.16 ms |
| 采样期 4 连读 | INA226 寄存器 0x01/02/04/03 | ~0.6 ms（占 2 ms 周期 30%） |
| 超时上限 | 每次调用 100 ms | 总线卡死最坏 400 ms（0 级中断里） |

---

## 5. 已知问题（记录在案，不改码）

| # | 问题 | 位置 |
|---|---|---|
| 1 | 阻塞模式 + 中断上下文调用，无 DMA 化 | 架构层面（INA226.c 调用侧） |
| 2 | 无总线恢复逻辑（SDA 卡低需 9 个时钟解锁，HAL 不做） | — |
| 3 | MspDeInit 未关 I2C 中断（本工程 I2C 不用中断，无影响） | :115-134 |

---

## 6. 修改指引

| 想改 | 位置 |
|---|---|
| I2C 速率 | i2c.c:56（100k/400k；INA226 支持 400 kHz、2.94 MHz 高速模式需另配） |
| 改 DMA 化 | INA226.c 调用侧改 `HAL_I2C_Mem_Read_DMA` + 此处使能事件/错误中断 |
| 换引脚 | i2c.c:96-98（I2C1 只能映射到 PB6/PB7 或重映射 PB8/PB9） |
