# src_can —— CAN1 外设初始化（1 Mbps）

> 对应源文件：`src/can.c`（共 157 行）、`src/can.h`（共 64 行）
> 注释级别：B 级（文件头导读块），strip_equiv 校验 PASS
> 工程路径：`/Users/theo-richard/workspace/references/firmware/Power-Meter`

**互链 Docs：** [03-主循环与CAN任务](../../../references/firmware/Power-Meter/Docs/03-主循环与CAN任务.md)（位定时推导与协议）、[01-启动流程与外设初始化](../../../references/firmware/Power-Meter/Docs/01-启动流程与外设初始化.md)（3.4 节）

---

## 1. 文件职责与位置

配置 bxCAN 主机（CAN1）：正常模式、1 Mbps、AutoBusOff/AutoRetransmission 使能；PA11=RX 浮空输入、PA12=TX 复用推挽；`USB_LP_CAN1_RX0_IRQn` 优先级 2。全局句柄 `hcan`（:39）被 CAN_TASK.c 与 main.c 使用。滤波器与启动不在本文件——在 CAN_TASK.c 的 `CAN_Filter_Init`（应用层二次初始化）。

文件头导读块（can.c:1-10）给出位定时计算；bit 定时参数在 :59-70。

---

## 2. 逐函数表

| 函数 | 行号 | 职责 |
|---|---|---|
| `MX_CAN_Init` | 49-79 | 位定时/模式参数 12 项，调 `HAL_CAN_Init` |
| `HAL_CAN_MspInit` | 86-125 | CAN1+GPIOA 时钟、PA11/PA12 引脚、RX0 中断优先级 2 |
| `HAL_CAN_MspDeInit` | 132-153 | 关时钟、DeInit 引脚、关中断（运行期不调用） |

`can.h` 对外暴露 `hcan`（:45）与 `MX_CAN_Init`（:54），文件头注明位速率与引脚。

---

## 3. 关键代码摘录

### 3.1 位定时与模式（can.c:59-70）

```c
  hcan.Instance = CAN1;                              /* 使用 CAN1 外设 */
  hcan.Init.Prescaler = 2;                           /* 分频后时钟 36/2 = 18 MHz */
  hcan.Init.Mode = CAN_MODE_NORMAL;                  /* 正常工作模式 */
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;             /* 重同步跳转宽度 1 TQ */
  hcan.Init.TimeSeg1 = CAN_BS1_16TQ;                 /* 段 1 = 16 TQ */
  hcan.Init.TimeSeg2 = CAN_BS2_1TQ;                  /* 段 2 = 1 TQ */
  hcan.Init.TimeTriggeredMode = DISABLE;             /* 关闭时间触发模式 */
  hcan.Init.AutoBusOff = ENABLE;                     /* 硬件自动恢复总线关闭 */
  hcan.Init.AutoWakeUp = DISABLE;                    /* 关闭自动唤醒（非睡眠模式） */
  hcan.Init.AutoRetransmission = ENABLE;             /* 发送失败自动重发 */
  hcan.Init.ReceiveFifoLocked = DISABLE;             /* FIFO 满时新报文覆盖旧报文 */
  hcan.Init.TransmitFifoPriority = DISABLE;          /* TXFP=0：按报文 ID 优先级发送 */
```

位速率完整推导（APB1 = 36 MHz）：

```text
TQ = Prescaler ÷ 36 MHz = 2 ÷ 36 MHz = 55.56 ns
位时间 = 1（同步段）+ 16（BS1）+ 1（BS2）= 18 TQ
位速率 = 1 ÷ (18 × 55.56 ns) = 1 Mbps
采样点 = (1+16) ÷ 18 ≈ 94.4%（偏后，长网络建议 BS1=15/BS2=2 得 88.9%）
```

三个行为开关的含义：`AutoBusOff` 硬件自动从 Bus-Off 恢复（与 CAN_TASK.c:113 的软件恢复构成双保险）；`AutoRetransmission` 仲裁失败/出错自动重发；`ReceiveFifoLocked=0` FIFO 满时**新报文覆盖旧报文**（防堵但可能丢旧帧）。

### 3.2 引脚与 NVIC（can.c:102-119）

```c
    /* CAN 引脚配置：
     *   PA11 → CAN_RX（输入，浮空）
     *   PA12 → CAN_TX（复用推挽输出，高速） */
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* CAN1 RX0 中断（与 USB 低优先级中断共用向量）：
     * 优先级 2（数值越大优先级越低） */
    HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
```

CAN 收发事件优先级最低（2）：允许被采样与串口事件抢占——丢的只是几 µs 延迟，换来 500 Hz 节拍的稳定。

---

## 4. 时序量化

| 项 | 数值 | 计算 |
|---|---|---|
| TQ | 55.56 ns | 2 ÷ 36 MHz |
| 位时间 | 18 TQ = 1 µs | 1+16+1 |
| 位速率 | 1 Mbps | 1 ÷ 1 µs |
| 8 字节数据帧 | ~111 bit（无填充）~135 bit（含填充） | 帧头 47 + 数据 64 + CRC 16 等 |
| 出线耗时 | ~0.13 ms | 硬件并行，不占 CPU |
| 总线负载 | ~6.5% | 500 帧/s × ~130 bit ÷ 1 Mbps |

---

## 5. 已知问题（记录在案，不改码）

| # | 问题 | 位置 |
|---|---|---|
| 1 | 采样点 94.4% 偏后，长网络/多节点兼容性差 | :63-64 |
| 2 | RX 中断使能但 RxFifo0 回调未实现（报文被丢） | CAN_TASK.c:93 侧 |
| 3 | `SlaveStartFilterBank=14` 对单 CAN 无意义（互联型才用） | CAN_TASK.c:79 侧 |

---

## 6. 修改指引

| 想改 | 位置 |
|---|---|
| 波特率 | can.c:60-64（Prescaler/BS1/BS2 三者联动，保持 18 TQ 或重算采样点） |
| 采样点调 87.5% | BS1=15、BS2=2（同为 18 TQ，速率不变） |
| 回环自测 | :61 改 `CAN_MODE_LOOPBACK`（无需收发器即可验证控制器） |
