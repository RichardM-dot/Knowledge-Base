# IRQ（Pinecil）：ADC/PWM 中断（A 级）

> 对应源文件：`source/Core/BSP/Pinecil/IRQ.cpp`（共 175 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[02-温度控制环精读](../../../references/firmware/IronOS/Docs/02-温度控制环精读.md)、[06-安全保护机制](../../../references/firmware/IronOS/Docs/06-安全保护机制.md)

## 职责

控温环的"硬件半边"：ADC 转换完成唤醒 PID、PWM 周期中断装载占空并硬件关断、
FUSB302 中断唤醒 POW。所有 ISR 都保持最短（几十条指令），重活留给任务。

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `ADC0_1_IRQHandler` | 29 | 注入组完成 → 唤醒 PID |
| `TIMER1_IRQHandler` | 62 | UP：装载占空/切模式；CH1：强制关断 |
| `switchToFastPWM` | 101 | ~10 Hz，周期 279 |
| `switchToSlowPWM` | 117 | ~5 Hz，周期 267 |
| `setTipPWM` | 133 | 写 `pendingPWM` + 重置安全计数 |
| `EXTI5_9_IRQHandler` | 149 | FUSB302 IRQ → 通知 POW |
| `getFUS302IRQLow` | 168 | IRQ 线电平查询 |

## PWM 硬件模型

```cpp
volatile uint16_t PWMSafetyTimer = 0;
volatile uint8_t  pendingPWM     = 0;
```

（`source/Core/BSP/Pinecil/IRQ.cpp:53-54`）

一个周期：

| 时刻（tick） | 事件 |
| --- | --- |
| 0 | UP 中断：安全计数递减、装载 `pendingPWM` 到 CH1 |
| 0..pulse | 加热 |
| pulse | CH1 匹配：TIMER2 输出强制 0 |
| 265 | CH0 匹配：TRGO 触发 ADC 注入组 |
| 265..279 | 采样窗口 14 tick |
| 279 | 周期结束，ADC EOIC 唤醒 PID |

`powerPWM=255`、`holdoffTicks=10`、`tempMeasureTicks=14`（`BSP.cpp:16-18`）。

## ADC 中断

```cpp
void              ADC0_1_IRQHandler(void) {

  adc_interrupt_flag_clear(ADC0, ADC_INT_FLAG_EOIC);
  // unblock the PID controller thread
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (pidTaskNotification) {
      vTaskNotifyGiveFromISR(pidTaskNotification, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
}
```

（`source/Core/BSP/Pinecil/IRQ.cpp:29-40`）

两个防御：调度器未启动时不发通知（启动早期 ADC 已经在跑），
`pidTaskNotification` 为空时不发（任务尚未创建）。

## PWM 中断与安全计数

```cpp
    if (PWMSafetyTimer) {
      PWMSafetyTimer--;
```

（`source/Core/BSP/Pinecil/IRQ.cpp:70-71`）

`setTipPWM` 每次把它置 10。PID 停转 → 计数归零 → 不再写 CH1 → 输出保持关闭。
这是**唯一不依赖 RTOS 的保护**。

## 快/慢模式

```cpp
void switchToFastPWM(void) {
  fastPWM           = true;
  totalPWM          = powerPWM + tempMeasureTicks + holdoffTicks;
  TIMER_CAR(TIMER1) = (uint32_t)totalPWM;

  // ~10Hz
  TIMER_CH0CV(TIMER1) = powerPWM + holdoffTicks;
  // 1 kHz tick rate
  TIMER_PSC(TIMER1) = 18000;
}
```

（`source/Core/BSP/Pinecil/IRQ.cpp:101-110`）

慢模式把周期改成 `255+7+5=267`、`PSC=36000`。注释里的 `~10Hz` 指控制/采样率，
`1 kHz tick rate` 指计数器节拍——两者不是一回事。

## FUSB302 中断

```cpp
    if (POWTaskHandle != nullptr) {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xTaskNotifyFromISR(POWTaskHandle, 1, eSetBits, &xHigherPriorityTaskWoken);
```

（`source/Core/BSP/Pinecil/IRQ.cpp:154-156`）

用 `eSetBits` 只传"有事件"这一位信息，不需要队列；POWTask 醒来后统一走
`IRQOccured()/step()`。

## 陷阱

- **不要在 ISR 里做 I2C/除法/浮点**：这里所有代码都是寄存器级。
- **`PWMSafetyTimer` 是 volatile**：编译器不能把它优化进寄存器。
- **改 `totalPWM` 会同时改变控制频率与可测温度上限**：采样窗口太短会让读数偏低。
- **`getFUS302IRQLow` 返回 true 表示"还有事件"**（低有效），命名易误解。

## 相关

- [pid_thread.md](pid_thread.md)：被唤醒的任务
- [power_cpp.md](power_cpp.md)：占空换算
- [pow_thread.md](pow_thread.md)：PD 事件消费者
