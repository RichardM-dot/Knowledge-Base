# src_usart —— USART2 + DMA 串口（VOFA 出口）

> 对应源文件：`src/usart.c`（共 169 行）、`src/usart.h`（共 64 行）
> 注释级别：B 级（文件头导读块），strip_equiv 校验 PASS
> 工程路径：`/Users/theo-richard/workspace/references/firmware/Power-Meter`

**互链 Docs：** [04-VOFA协议与串口DMA](../../../references/firmware/Power-Meter/Docs/04-VOFA协议与串口DMA.md)（协议与带宽分析）、[01-启动流程与外设初始化](../../../references/firmware/Power-Meter/Docs/01-启动流程与外设初始化.md)（3.6 节）

---

## 1. 文件职责与位置

配置 USART2：115200 8N1、收发模式；TX 方向通过 DMA1_Channel7 发送（VOFA+ justfloat 20 字节帧）。定义两个全局句柄：`huart2`（:37）与 `hdma_usart2_tx`（:38）。RX 方向只配引脚不启用业务。

数据流位置：VOFA 通道的物理出口，`VOFA.c:62` 的 `HAL_UART_Transmit_DMA(&huart2, ...)` 是唯一消费者。

---

## 2. 逐函数表

| 函数 | 行号 | 职责 |
|---|---|---|
| `MX_USART2_UART_Init` | 46-72 | 115200 8N1、16 倍过采样、调 `HAL_UART_Init` |
| `HAL_UART_MspInit` | 79-134 | 时钟、PA2/PA3 引脚、DMA1_Channel7 全参数配置、`__HAL_LINKDMA`、USART2 NVIC 优先级 1 |
| `HAL_UART_MspDeInit` | 141-165 | 关时钟、释放引脚、`HAL_DMA_DeInit`、关中断（运行期不调用） |

`usart.h` 对外暴露 `huart2`（:45）与 `MX_USART2_UART_Init`（:54），文件头注明 TX 走 DMA1_Channel7。

---

## 3. 关键代码摘录

### 3.1 串口参数（usart.c:56-63）

```c
  huart2.Instance = USART2;                          /* 使用 USART2 外设 */
  huart2.Init.BaudRate = 115200;                     /* 波特率 115200 */
  huart2.Init.WordLength = UART_WORDLENGTH_8B;       /* 8 位数据 */
  huart2.Init.StopBits = UART_STOPBITS_1;            /* 1 位停止位 */
  huart2.Init.Parity = UART_PARITY_NONE;             /* 无校验 */
  huart2.Init.Mode = UART_MODE_TX_RX;                /* 收发模式 */
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;       /* 无硬件流控 */
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;   /* 16 倍过采样 */
```

每字节线上 10 bit（1 起 + 8 数 + 1 停）→ 吞吐上限 11520 B/s，VOFA 负载率 86.8%（见 [04](../../../references/firmware/Power-Meter/Docs/04-VOFA协议与串口DMA.md) 第 3 节）。

### 3.2 DMA 通道全参数（usart.c:110-124）

```c
    /* USART2_TX DMA 配置：DMA1_Channel7，内存 → 外设 */
    hdma_usart2_tx.Instance = DMA1_Channel7;
    hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH; /* 内存搬到串口数据寄存器 */
    hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE;     /* 外设地址不递增 */
    hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE;         /* 内存地址递增 */
    hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE; /* 外设按字节 */
    hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;    /* 内存按字节 */
    hdma_usart2_tx.Init.Mode = DMA_NORMAL;                /* 普通模式（非循环） */
    hdma_usart2_tx.Init.Priority = DMA_PRIORITY_LOW;      /* 低优先级 */
    if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK)
    {
      Error_Handler();
    }

    /* 将 DMA 句柄链接到 UART 句柄的 hdmatx 字段 */
    __HAL_LINKDMA(uartHandle, hdmatx, hdma_usart2_tx);
```

`Mode = DMA_NORMAL` 是 HAL_BUSY 语义的来源：发完即停，每帧由 HAL 重新启动；上一帧未发完时新请求被拒。`__HAL_LINKDMA` 把通道挂进 `huart2.hdmatx`，串口层的 DMA 操作才找得到通道。

### 3.3 NVIC（usart.c:127-128）

```c
    /* USART2 全局中断优先级 1 */
    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
```

低于采样链（0 级）高于 CAN（2 级）：串口错误/状态事件不能干扰采样，但优先于 CAN 事件处理。

---

## 4. 引脚表

| 引脚 | 模式 | 说明 |
|---|---|---|
| PA2 | `GPIO_MODE_AF_PP` 高速 | USART2_TX（DMA 源） |
| PA3 | `GPIO_MODE_INPUT` 浮空 | USART2_RX（无业务） |

---

## 5. 已知问题（记录在案，不改码）

| # | 问题 | 位置 |
|---|---|---|
| 1 | RX 配置了输入但无接收业务 | :104-107 |
| 2 | 通道参数分散在 MspInit，dma.c 看不到全貌（CubeMX 惯例） | :110-124 |
| 3 | 波特率与 VOFA 负载率强耦合，提速需重新核算丢帧行为 | :57 |

---

## 6. 修改指引

| 想改 | 位置 |
|---|---|
| 波特率 | usart.c:57（同步核对 VOFA 负载率 <100%） |
| DMA 循环模式 | :116（需改 VOFA 组帧逻辑，不推荐） |
| 关闭 RX 引脚 | :104-107（省一个输入脚配置） |
