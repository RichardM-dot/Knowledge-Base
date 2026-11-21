# I2CBB2：软件模拟 I2C（B 级）

> 对应源文件：`source/Core/Drivers/I2CBB2.cpp`（共 357 行）
> 头文件：`source/Core/Drivers/I2CBB2.hpp`
> 工程：`references/firmware/IronOS` ｜ 关联文档：[04-用户界面与按键状态机](../../../references/firmware/IronOS/Docs/04-用户界面与按键状态机.md)

## 职责

Pinecil 的第二组 I2C 总线（OLED/加速度计/部分 PD 芯片）用 GPIO 位操作实现。
选择软件 I2C 的原因：硬件外设不够用，且软件实现可以在总线卡死时手动打拍解锁。

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `init` | 30 | 引脚配置为开漏输出 |
| `probe` | 64 | 地址探测（发 START + 地址，看 ACK） |
| `Mem_Read` | 75 | 带寄存器地址的读 |
| `Mem_Write` | 112 | 带寄存器地址的写 |
| `Transmit` | 128 | 原始发送 |
| `Receive` | 153 | 原始接收 |
| `TransmitReceive` | 173 | 组合传输（读前先写地址） |
| `start` / `stop` | 234 / 246 | 起始/停止条件 |
| `send` / `read` | 256 / 268 | 字节级收发（含 ACK） |
| `lock` / `unlock` | 306 / 304 | 二值信号量 |
| `wakePart` | 346 | 9 个时钟脉冲唤醒从机 |

## 位级时序

```cpp
void I2CBB2::start() {
```

（`source/Core/Drivers/I2CBB2.cpp:234`）

- START：SCL 高时 SDA 由高变低；
- STOP：SCL 高时 SDA 由低变高；
- 每位：SCL 低时改 SDA，SCL 高时采样；
- ACK：第 9 个时钟，从机拉低 SDA。

时序靠 NOP 循环实现，因此**编译器优化等级会影响速率**。工程固定 `-O2`，
不要在这个文件里加耗时逻辑。

## 互斥

```cpp
bool I2CBB2::lock() {
```

（`source/Core/Drivers/I2CBB2.cpp:306`）

用静态二值信号量保护总线：OLED（GUI 任务）、加速度计（MOV 任务）、
FUSB302（POW 任务）三方共享同一组引脚。没有互斥会出现"读到别的设备的 ACK"。

## 总线恢复

`unstick_I2C()`（在 BSP.cpp）与 `wakePart()` 配合：从机在传输中途复位时会把 SDA
一直拉低，主机需要手动发 9 个 SCL 脉冲让它释放总线。

## 陷阱

- **所有传输都是阻塞的**，一次 OLED 刷屏约 5~12 ms，期间 CPU 空转。
- **`lock()` 没有超时**，如果持锁任务崩溃，其他任务会永久等待。
- **上拉电阻不足会导致波形边沿变缓**，表现为偶发 NACK；Pinecil 用内部上拉 + 外部 4.7k。
- **不要在中断里调用**：位操作依赖任务上下文与信号量。

## 时序量化

软件 I2C 的每一位由若干 NOP + GPIO 写构成。以 96×16 OLED 的一次 refresh 为例：

| 项目 | 数值 |
| --- | --- |
| 传输字节 | 192（像素）+ 17（头）= 209 |
| 每字节 bit | 9（8 数据 + 1 ACK） |
| 总 bit | 1881 |
| 400 kHz 理想耗时 | 4.7 ms |
| 实测（含函数调用开销） | 6~8 ms |

这解释了为什么 GUI 的 40 ms 帧预算里，刷屏要占掉 15%~20%。

## 错误处理

| 情况 | 表现 | 处理 |
| --- | --- | --- |
| 从机无 ACK | `send()` 返回 false | 上层重试或放弃本次传输 |
| SDA 被拉死 | 所有传输超时 | `unstick_I2C()` 发 9 个时钟 |
| 信号量未释放 | 其他任务永久阻塞 | 无超时，属于已知风险 |
| 上拉不足 | 偶发 NACK | 硬件加 4.7k 上拉 |

## 为什么不用硬件 I2C

1. Pinecil 的 OLED 与 FUSB302 分布在不同的引脚组，硬件 I2C 外设数量不够；
2. 硬件 I2C 在从机拉死总线时无法用 GPIO 手动恢复；
3. 软件实现可以在任意引脚上复现，移植成本低。

代价是 CPU 占用：一次 209 字节传输约 6 ms 的忙等，期间 GUI 任务无法做别的事。

## 相关

- [oled.md](oled.md)：最大的总线用户
- [mov_thread.md](mov_thread.md)：加速度计读取
