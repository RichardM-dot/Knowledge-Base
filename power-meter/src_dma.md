# src_dma —— DMA1 控制器与 Channel7

> 对应源文件：`src/dma.c`（共 61 行）、`src/dma.h`（共 60 行）
> 注释级别：B 级（文件头导读块），strip_equiv 校验 PASS
> 工程路径：`/Users/theo-richard/workspace/references/firmware/Power-Meter`

**互链 Docs：** [01-启动流程与外设初始化](../../../references/firmware/Power-Meter/Docs/01-启动流程与外设初始化.md)（3.2 节）、[04-VOFA协议与串口DMA](../../../references/firmware/Power-Meter/Docs/04-VOFA协议与串口DMA.md)（3 节 DMA 配置全貌）、[05-中断系统与数据流总图](../../../references/firmware/Power-Meter/Docs/05-中断系统与数据流总图.md)

---

## 1. 文件职责与位置

F103 的 DMA 是固定请求映射：**USART2_TX 硬连 DMA1_Channel7**，不可改选。本文件只做"控制器层"两件事：开 DMA1 时钟、配置并使能 Channel7 的 NVIC 中断。通道的传输参数（方向/宽度/模式）在 `usart.c` 的 `HAL_UART_MspInit` 里配置（usart.c:110-124）——CubeMX 的分工习惯：控制器归 MX_DMA_Init，通道细节跟着使用者走。

**初始化顺序红线**：`MX_DMA_Init()` 必须先于 `MX_USART2_UART_Init()`（main.c:157 vs :161），否则 MspInit 配置通道时 DMA1 时钟未开。

---

## 2. 逐函数表

| 函数 | 行号 | 职责 |
|---|---|---|
| `MX_DMA_Init` | 49-57 | `__HAL_RCC_DMA1_CLK_ENABLE()` + Channel7 NVIC 优先级 0 并使能 |

`dma.h` 仅声明 `MX_DMA_Init`（:50），并注明"必须在 USART2 之前调用"（:48）。

---

## 3. 关键代码摘录（dma.c:49-57 全文）

```c
void MX_DMA_Init(void)
{
  /* 使能 DMA1 控制器时钟 */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* 配置 DMA1_Channel7 中断优先级并使能中断（USART2_TX 使用） */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);
}
```

优先级 0 与 TIM1 同级的取舍：DMA 收尾中断（清标志、复位 UART gState）希望尽快完成，让下一帧 `HAL_UART_Transmit_DMA` 尽早可用；又不能比 TIM1 高（否则采样被 DMA 收尾延后）。同级"不互相抢占"是两者兼容的最优解。

---

## 4. 与其它文件的关系图

```text
dma.c:49  MX_DMA_Init（时钟+NVIC）
   │
   ├─ usart.c:110-124  HAL_UART_MspInit 配置通道参数并 __HAL_LINKDMA
   ├─ VOFA.c:62        HAL_UART_Transmit_DMA 发起传输
   ├─ stm32f1xx_it.c:215  DMA1_Channel7_IRQHandler 转发
   └─ HAL_DMA_IRQHandler  清标志 → gState=READY → 允许下一帧
```

---

## 5. 已知问题（记录在案，不改码）

| # | 问题 | 位置 |
|---|---|---|
| 1 | 通道参数分散在 usart.c，读 dma.c 看不到全貌（CubeMX 惯例，非 bug） | usart.c:110-124 |
| 2 | Channel7 优先级 0（NVIC），通道间 DMA 仲裁优先级却配 LOW——本工程 DMA1 仅一条通道，无竞争 | usart.c:117 |

---

## 6. 修改指引

| 想改 | 位置 |
|---|---|
| 新增 DMA 外设 | 在其 MspInit 配通道 + 此处/相应处使能 NVIC |
| Channel7 中断优先级 | dma.c:55（改前看 [05](../../../references/firmware/Power-Meter/Docs/05-中断系统与数据流总图.md) 嵌套分析） |
