# POWThread：电源任务（A 级）

> 对应源文件：`source/Core/Threads/POWThread.cpp`（共 103 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[03-电源管理与 USB PD](../../../references/firmware/IronOS/Docs/03-电源管理与 USB PD.md)

## 职责

驱动 USB-PD 策略引擎：处理 FUSB302 中断、推进协议状态机、在 PD 失败后让 BSP
尝试 QC。任务本身不解析 PD 报文（那在 `Drivers/usb-pd/` 里），只负责"什么时候
让状态机跑一步"。

## 函数

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `startPOWTask` | 38 | 任务主体（唯一的函数） |

## 启动时序

```cpp
  // Init any other misc sensors
  // postRToSInit 里做"必须在调度器启动后才能做"的板级初始化（如 BLE/外设时钟）。
  // 放在本任务而不是 main，是为了让 GUI 先亮屏，用户感知不到这段延迟。
  postRToSInit();
```

（`source/Core/Threads/POWThread.cpp:40-43`）

```cpp
  // You have to run this once we are willing to answer PD messages
  // Setting up too early can mean that we miss the ~20ms window to respond on some chargers
#ifdef POW_PD
  USBPowerDelivery::start();
```

（`source/Core/Threads/POWThread.cpp:49-55`）

顺序不能反：`postRToSInit()` 可能做 BLE/外设时钟，`USBPowerDelivery::start()`
要在 VBUS 稳定后调用；如果太早，FUSB302 读不到 ID，整个 PD 功能会被判定为不存在。

## 事件循环

```cpp
    res = pdFALSE;
    // While the interrupt is low, dont delay
    /*This is due to a possible race condition, where:
     * IRQ fires
     * We read interrupt register but dont see the Good CRC
     * Then Good CRC is set while reading it out (racing on I2C read)
     * Then we would sleep as nothing to do, but 100ms> 20ms power supply typical timeout
     */
```

（`source/Core/Threads/POWThread.cpp:68-74`）

循环结构：

| 步骤 | 代码 | 说明 |
| --- | --- | --- |
| 1 | `if (!getFUS302IRQLow())` | IRQ 低电平才等待（高电平说明还有事件） |
| 2 | `xTaskNotifyWait(..., 50 ms)` | 等 EXTI 通知，最多 50 ms |
| 3 | `IRQOccured()` | 把中断交给策略引擎 |
| 4 | `PPSTimerCallback()` | 推进 PPS/超时定时器 |
| 5 | `step()` | 跑状态机直到无事可做 |
| 6 | `power_check()` | 板级：PD 失败后尝试 QC |

50 ms 的等待上限是协议约束：USB-PD 的 `tSenderResponse` 约 24 ms，睡 100 ms
就会错过 GoodCRC 窗口，充电器会重发甚至放弃。

## 与 USBPD 的接口

```cpp
#ifdef POW_PD
    if (res != pdFALSE || getFUS302IRQLow()) {
      USBPowerDelivery::IRQOccured();
    }
    USBPowerDelivery::PPSTimerCallback();
    USBPowerDelivery::step();
```

（`source/Core/Threads/POWThread.cpp:83-88`）

三个调用的分工：

- `IRQOccured()`：告诉引擎"有硬件事件"，通常对应一次 GoodCRC 或 HardReset；
- `PPSTimerCallback()`：检查软件定时器（发送超时、PPS 周期）；
- `step()`：真正解析消息、更新状态、可能触发 Sink Capabilities 交换。

## 其它 PD 芯片分支

| 宏 | 芯片 | 调用 |
| --- | --- | --- |
| `POW_PD==1` | FUSB302 | `USBPowerDelivery::*` |
| `POW_PD_EXT==1` | HUSB238 | `hub238_check_negotiation()` |
| `POW_PD_EXT==2` | FS2711 | `FS2711::start()/negotiate()` |

Pinecil V1/V2 都是 `POW_PD==1`、`POW_PD_EXT==0`，所以只走第一条分支。

## 陷阱

- **不要在 `power_check()` 里阻塞**：它每个循环都被调用，阻塞会拖慢 PD 响应。
- **`getFUS302IRQLow()` 是低有效**，命名容易误解为"IRQ 是否低"→ 返回 true 表示
  "有未处理事件"。
- 拔掉 USB 后 FUSB302 仍可能由残余电荷供电，I2C 访问会失败；此时循环会靠
  50 ms 超时空转，不会卡死，但会占用 CPU。

## 相关

- [usbpd.md](usbpd.md)：协议胶水层
- [qc3.md](qc3.md)：PD 失败后的退路
- [main_startup.md](main_startup.md)：任务优先级与创建
