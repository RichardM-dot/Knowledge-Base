# src_main —— 程序入口、时钟配置与采样回调

> 对应源文件：`src/main.c`（共 264 行）、`src/main.h`（共 88 行）
> 注释级别：A 级（逐函数中文深注），strip_equiv 校验 PASS
> 工程路径：`/Users/theo-richard/workspace/references/firmware/Power-Meter`

**互链 Docs：** [00-总览与硬件架构](../../../references/firmware/Power-Meter/Docs/00-总览与硬件架构.md)、[01-启动流程与外设初始化](../../../references/firmware/Power-Meter/Docs/01-启动流程与外设初始化.md)、[05-中断系统与数据流总图](../../../references/firmware/Power-Meter/Docs/05-中断系统与数据流总图.md)

---

## 1. 文件职责与位置

`main.c` 是全工程唯一"串起一切"的文件：入口 `main()` 完成 10 步初始化，业务核心在 TIM1 更新中断回调里（读 INA226 → 缩放 int16 → CAN 发送 + VOFA DMA 发送），主循环空转。`main.h` 提供引脚分配总表与 `Error_Handler()` 声明，被所有外设文件包含（HAL 头的公共入口）。

数据流位置：**采集与调度中枢**。上游是 INA226（I2C），下游是 CAN_TASK（CAN 出口）与 VOFA（串口出口）。

---

## 2. 全局变量表

| 变量 | 类型 | 单位 | 行号 | 用途 |
|---|---|---|---|---|
| `shunt_volt` | float | V | main.c:52 | 分流电压测量值 |
| `bus_volt` | float | V | main.c:52 | 总线电压测量值 |
| `current` | float | A | main.c:52 | 电流测量值 |
| `power` | float | W | main.c:52 | 功率测量值 |
| `send_shunt` | int16_t | mV | main.c:60 | CAN 发送用（×1000） |
| `send_bus` | int16_t | mV | main.c:60 | CAN 发送用（×1000） |
| `send_curr` | int16_t | mA | main.c:60 | CAN 发送用（×1000） |
| `send_power` | int16_t | 0.01 W | main.c:60 | CAN 发送用（×100） |

两套变量并存的原因：VOFA 通道需要原始 float（精度无损），CAN 通道用 int16 省 4 倍带宽。float → int16 转换只在中断上下文一处发生，无并发问题。

---

## 3. 逐函数表

| 函数 | 行号 | 职责 | 调用时机 |
|---|---|---|---|
| `HAL_TIM_PeriodElapsedCallback` | 77-103 | 2 ms 采样节拍：读 INA226、缩放、CAN+VOFA 发送 | TIM1 更新中断（HAL 弱函数重写） |
| `main` | 131-185 | 10 步初始化后空转 | 复位入口 |
| `SystemClock_Config` | 196-227 | HSE 8 MHz ×9 → 72 MHz，总线分频，Flash 2WS | main 第 2 步 |
| `Error_Handler` | 239-248 | 关中断 + 死循环 | 各 HAL_xxx_Init 失败 |
| `assert_failed` | 257-263 | 断言失败回调（空实现） | 仅 USE_FULL_ASSERT 编译 |

头文件 `main.h` 对外仅暴露 `Error_Handler()`（main.h:70-74），引脚分配写在文件头注释块（main.h:8-14）：CAN1 PA11/PA12 1 Mbps、I2C1 PB6/PB7 400 kHz、USART2 PA2/PA3 115200 + DMA1_Channel7、TIM1 500 Hz、SWD PA13/PA14。

---

## 4. 关键代码摘录

### 4.1 采样回调（业务核心，main.c:77-103）

```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* 只处理 TIM1 的更新事件，其它定时器事件不响应 */
  if (htim == &htim1)
  {
    /* ① 读取 INA226 四个测量值（分流电压、总线电压、电流、功率） */
    shunt_volt = INA226_Read_ShuntVoltage(&hi2c1);
    bus_volt   = INA226_Read_BusVoltage(&hi2c1);
    current    = INA226_Read_Current(&hi2c1);
    power      = INA226_Read_Power(&hi2c1);

    /* ② 转换为整型，减少 CAN 每帧的字节数
     *   电压/电流放大 1000 倍（mV / mA），功率放大 100 倍（0.01 W）
     *   注意：总线电压超过 32.767 V、功率超过 327.67 W 时 int16_t 会溢出，
     *         此为原工程既有行为，移植时保持原样（VOFA 通道发送原始 float，不受影响） */
    send_shunt = (int16_t)(shunt_volt * 1000.0f);
    send_bus   = (int16_t)(bus_volt   * 1000.0f);
    send_curr  = (int16_t)(current    * 1000.0f);
    send_power = (int16_t)(power      * 100.0f);

    /* ③ 通过 CAN1 发送：标准帧 ID = 0x605，数据为 4 个 int16（大端） */
    CAN_send(&hcan, 0x605, send_shunt, send_bus, send_curr, send_power);

    /* ④ 同时通过 USART2 + DMA 发送 VOFA+ justfloat 波形数据 */
    VOFA_justfloat(shunt_volt, bus_volt, current, power);
  }
}
```

深读要点：

- `htim == &htim1` 判断是 HAL 回调惯例：同一回调被所有定时器共享，必须按句柄过滤；
- 4 次 I2C 读合计约 0.6 ms（400 kHz），是本中断唯一的阻塞点，详见 [02-INA226](../../../references/firmware/Power-Meter/Docs/02-INA226驱动精读.md) 第 4 节；
- `×1000/×100` 缩放的溢出线：32.767 V / 32.767 A / 327.67 W，CAN 通道越线回绕，VOFA 通道无此问题；
- 两个出口都在中断里"写入即走"：CAN 只进邮箱（硬件异步出线），VOFA 只启动 DMA（1.74 ms 后台搬运）。

### 4.2 初始化顺序（main.c:156-172）

```c
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_CAN_Init();
  MX_TIM1_Init();
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */
  /* 初始化 INA226（写配置寄存器与校准寄存器）。
   * 原工程未检查返回值，此处保持相同行为。 */
  INA226_Init(&hi2c1);

  /* 启动 TIM1 更新中断：此后每 2 ms 进入一次 HAL_TIM_PeriodElapsedCallback */
  HAL_TIM_Base_Start_IT(&htim1);

  /* 配置 CAN 验收滤波器并启动 CAN（同时打开 RX FIFO0 与发送邮箱空中断） */
  CAN_Filter_Init(&hcan);
```

硬约束：**MX_DMA_Init 必须在 MX_USART2_UART_Init 之前**——USART2 的 MspInit 里要 `HAL_DMA_Init` + `__HAL_LINKDMA`（usart.c:110-124），DMA 时钟未开则配置无效。

### 4.3 时钟配置（main.c:202-223 关键行）

```c
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;          /* 8 MHz × 9 = 72 MHz */
  ...
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;        /* HCLK = 72 MHz */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;         /* PCLK1 = 36 MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;         /* PCLK2 = 72 MHz */
  ...
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)  /* 2 等待周期 */
```

由此派生的三个外设频率基数：CAN = PCLK1 36 MHz（位定时 1 Mbps）、I2C1 = 400 kHz、TIM1 = PCLK2 72 MHz（2 ms 节拍）。72 MHz 下 Flash 必须 2 WS，否则取指出错。

---

## 5. 时序与量化

| 项 | 数值 | 计算 |
|---|---|---|
| 采样周期 | 2 ms（500 Hz） | 72 MHz ÷ 72 ÷ 2000 |
| 回调 CPU 占用 | 约 620 µs（31%） | I2C 600 + 转发 2 + 打包/DMA 启动 ~15 |
| VOFA 出口带宽 | 10000 B/s / 11520 B/s = 86.8% | 500 × 20 B |
| CAN 出口负载 | 约 6.5% | 500 帧 × ~130 bit @1 Mbps |
| 最坏回调时长 | 400 ms | I2C 超时 100 ms × 4 |

---

## 6. 已知问题（记录在案，不改码）

| # | 问题 | 位置 |
|---|---|---|
| 1 | `INA226_Init` 返回值未检查，I2C 故障静默失败 | main.c:166 |
| 2 | int16 缩放在 32.767 V / 32.767 A / 327.67 W 处回绕 | main.c:92-95 |
| 3 | 回调内无超时预算保护，I2C 卡死放大为 400 ms | main.c:83-86 |
| 4 | `Error_Handler` 无外部指示（仅关中断死循环） | main.c:239-248 |

---

## 7. 修改指引（改参数去哪里）

| 想改 | 位置 |
|---|---|
| CAN 发送 ID / 内容 | main.c:98（`CAN_send(&hcan, 0x605, ...)`） |
| 采样后的缩放比 | main.c:92-95 |
| 主频 | main.c:196-227（同步改 FLASH_LATENCY） |
| 出口开关（暂停 CAN/VOFA） | main.c:98 / :101 注释对应行 |
