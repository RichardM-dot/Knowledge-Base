# src_stm32f1xx_it —— 中断向量与 HAL 转发

> 对应源文件：`src/stm32f1xx_it.c`（共 275 行；配套声明 `src/stm32f1xx_it.h` 共 80 行，不在注释清单内、已有注释）
> 注释级别：A 级（逐函数中文深注），strip_equiv 校验 PASS
> 工程路径：`/Users/theo-richard/workspace/references/firmware/Power-Meter`

**互链 Docs：** [05-中断系统与数据流总图](../../../references/firmware/Power-Meter/Docs/05-中断系统与数据流总图.md)（优先级地图与时间预算全文）、[01-启动流程与外设初始化](../../../references/firmware/Power-Meter/Docs/01-启动流程与外设初始化.md)

---

## 1. 文件职责与位置

中断向量的"第一跳"。结构分两段：9 个 Cortex-M3 内核异常（多数死循环兜底）+ 4 个外设中断（统一模式：只调 HAL 对应 `HAL_xxx_IRQHandler`，把标志位处理与用户回调分发交给 HAL）。用户业务永远写在 HAL 回调里（如 main.c:77），不写在这里。

外部句柄引用（:61-65）：`hcan`、`htim1`、`hdma_usart2_tx`、`huart2`——分别定义在 can.c/tim.c/usart.c。

---

## 2. 逐函数表

### 内核异常（:78-205）

| 函数 | 行号 | 行为 |
|---|---|---|
| `NMI_Handler` | 78-89 | 死循环（原工程行为，:84-87） |
| `HardFault_Handler` | 95-105 | 死循环（非法访问兜底） |
| `MemManage_Handler` | 110-120 | 死循环（M3 无 MPU 一般不触发） |
| `BusFault_Handler` | 125-135 | 死循环 |
| `UsageFault_Handler` | 140-150 | 死循环 |
| `SVC_Handler` | 155-163 | 空（无 RTOS） |
| `DebugMon_Handler` | 168-176 | 空 |
| `PendSV_Handler` | 181-189 | 空（无 RTOS） |
| `SysTick_Handler` | 196-205 | `HAL_IncTick()` 累加 1 ms 时基 |

### 外设中断（:215-271）

| 函数 | 行号 | 转发目标 | 抢占优先级 | 配置处 |
|---|---|---|---|---|
| `DMA1_Channel7_IRQHandler` | 215-224 | `HAL_DMA_IRQHandler(&hdma_usart2_tx)` | 0 | dma.c:55 |
| `USB_LP_CAN1_RX0_IRQHandler` | 231-240 | `HAL_CAN_IRQHandler(&hcan)` | 2 | can.c:118 |
| `TIM1_UP_IRQHandler` | 247-256 | `HAL_TIM_IRQHandler(&htim1)` | 0 | tim.c:107 |
| `USART2_IRQHandler` | 262-271 | `HAL_UART_IRQHandler(&huart2)` | 1 | usart.c:127 |

---

## 3. 关键代码摘录

### 3.1 采样节拍入口（stm32f1xx_it.c:247-256）

```c
void TIM1_UP_IRQHandler(void)
{
  /* USER CODE BEGIN TIM1_UP_IRQn 0 */

  /* USER CODE END TIM1_UP_IRQn 0 */
  HAL_TIM_IRQHandler(&htim1);
  /* USER CODE BEGIN TIM1_UP_IRQn 1 */

  /* USER CODE END TIM1_UP_IRQn 1 */
}
```

HAL 内部完成"查 UIE 标志 → 清标志 → 调 `HAL_TIM_PeriodElapsedCallback`"，落到 main.c:77。**不要把业务写进 USER CODE 区**：业务写在回调里可以与向量解耦，且重生成代码时 USER CODE 区外的修改会被覆盖。

### 3.2 DMA 完成收尾（stm32f1xx_it.c:215-224）

```c
void DMA1_Channel7_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_usart2_tx);
}
```

每 2 ms 触发一次（20 字节发完）：HAL 清传输完成标志并把 UART `gState` 复位为 READY——下一次 `HAL_UART_Transmit_DMA()` 是否返回 HAL_BUSY 由它决定。

### 3.3 CAN/USB 共享向量（stm32f1xx_it.c:231-240）

```c
void USB_LP_CAN1_RX0_IRQHandler(void)
{
  HAL_CAN_IRQHandler(&hcan);
}
```

一个向量三件事：RX FIFO0 消息挂起（已使能）、TX 邮箱空（已使能、无业务）、CAN 错误（→ `HAL_CAN_ErrorCallback`，CAN_TASK.c:113 做 Bus-Off 软件恢复）。F103 上 USB 与 CAN1_RX0 共享向量号，本工程不用 USB。

### 3.4 SysTick 心脏（stm32f1xx_it.c:196-205）

```c
void SysTick_Handler(void)
{
  HAL_IncTick();
}
```

1 ms 累加 uwTick。HAL 的所有超时（含 I2C 的 100 ms 参数）都建立在它之上——SysTick 停摆则 I2C 超时永不触发，I2C 卡死会演变为永久阻塞。

---

## 4. 中断链路速查

```text
TIM1 更新（2 ms）
└─ TIM1_UP_IRQHandler          :247
   └─ HAL_TIM_IRQHandler       HAL
      └─ HAL_TIM_PeriodElapsedCallback   main.c:77
         ├─ INA226 四读          I2C1 阻塞 ~0.6 ms
         ├─ CAN_send             入邮箱，硬件出线
         └─ VOFA_justfloat       启动 DMA1_Channel7
DMA1_Channel7 传输完成（每 2 ms）
└─ DMA1_Channel7_IRQHandler    :215 → HAL_DMA_IRQHandler 收尾
CAN 收发事件
└─ USB_LP_CAN1_RX0_IRQHandler  :231 → HAL_CAN_IRQHandler
USART2 状态事件
└─ USART2_IRQHandler           :262 → HAL_UART_IRQHandler
SysTick（1 ms）
└─ SysTick_Handler             :196 → HAL_IncTick
```

---

## 5. 已知问题（记录在案，不改码）

| # | 问题 | 位置 |
|---|---|---|
| 1 | NMI 里死循环（NMI 不可屏蔽，通常应记日志/复位） | :84-87 |
| 2 | HardFault 无现场保存输出，调试只能靠断点 | :100-104 |
| 3 | TX 邮箱空中断已使能但无业务（每秒 ~500 次空进） | CAN_TASK.c:99 使能侧 |
| 4 | CAN RX 中断已使能但无 RxFifo0 回调实现，报文被丢 | CAN_TASK.c:93 使能侧 |

---

## 6. 修改指引

| 想改 | 位置 |
|---|---|
| 中断里加轻量逻辑 | 对应函数的 USER CODE 区（:217/:233/:249/:264 等） |
| 采样/收发业务 | 不要放这里 → main.c 回调 / CAN_TASK 回调 |
| 优先级 | 各外设 MspInit（tim.c:107 / dma.c:55 / usart.c:127 / can.c:118） |
