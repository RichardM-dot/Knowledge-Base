# main / main.hpp：启动与任务定义（B 级）

> 对应源文件：`source/Core/Src/main.cpp`（共 100 行）、`source/Core/Inc/main.hpp`（共 55 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[01-启动流程与任务模型](../../../references/firmware/IronOS/Docs/01-启动流程与任务模型.md)

## 职责

`main.cpp` 是启动入口：初始化硬件、读设置、关输出、创建四个静态任务、启动调度器。
`main.hpp` 集中声明跨任务的全局变量与任务入口，避免各 `.cpp` 互相 `extern`。

## 启动链

```cpp
int main(void) {
  preRToSInit();

  resetWatchdog();
  // Testing for which accelerometer is mounted
  settingsWereReset = loadSettings(); // load the settings from flash

  setTipX10Watts(0); // force tip off
```

（`source/Core/Src/main.cpp:65-72`）

注意 `setTipX10Watts(0)` 必须在创建任务前调用：`preRToSInit()` 可能已经把 PWM 外设
配置成某个默认占空，不显式清零就有开机瞬间全功率的风险。

## 任务表

| 任务 | 优先级 | 栈（字/字节） | 周期 | 入口 |
| --- | --- | --- | --- | --- |
| PIDTask | Realtime(6) | 512 / 2048 | ADC 触发（~10 Hz） | `startPIDTask` |
| POWTask | AboveNormal(4) | 256 / 1024 | 事件 + 50 ms | `startPOWTask` |
| MOVTask | Normal(3) | 512 / 2048 | 100 ms | `startMOVTask` |
| GUITask | BelowNormal(2) | 512 / 2048 | 40 ms | `startGUITask` |

```cpp
  osThreadStaticDef(PIDTask, startPIDTask, osPriorityRealtime, 0, PIDTaskStackSize, PIDTaskBuffer, &PIDTaskControlBlock);
  PIDTaskHandle = osThreadCreate(osThread(PIDTask), NULL);
```

（`source/Core/Src/main.cpp:78-79`）

全部静态分配：`configSUPPORT_STATIC_ALLOCATION=1`、动态分配关闭、堆仅 1 KB。

## main.hpp 的共享变量

| 变量 | 类型 | 用途 |
| --- | --- | --- |
| `currentTempTargetDegC` | `volatile TemperatureType_t` | GUI → PID 的设定温度 |
| `pidTaskNotification` | `TaskHandle_t` | ADC ISR → PID |
| `powerSupplyWattageLimit` | `int32_t` | PD/QC → PID（W） |
| `lastMovementTime` | `TickType_t` | MOV → GUI |
| `accelInit` | `uint8_t` | MOV 内部滤波初始化标志 |
| `DetectedAccelerometerVersion` | `AccelType` | MOV 探测结果，启动告警读取 |

## 陷阱

- **`settingsWereReset` 由 `loadSettings()` 返回**，GUI 用它决定是否弹"设置已重置"。
- **任务栈单位是 `uint32_t` 元素**，`1024/2` 是 512 个元素 = 2 KB，不是 512 字节。
- **不要在 `main()` 里调用任何会阻塞的 BSP 函数**：调度器还没启动，`vTaskDelay` 会崩。

## 启动耗时估算

| 阶段 | 典型耗时 | 依据 |
| --- | --- | --- |
| `preRToSInit`（时钟/GPIO/ADC） | 5~15 ms | 各 BSP 的 `Setup.cpp` 顺序初始化 |
| `loadSettings` | <1 ms | 读 122 字节 + 范围校验 |
| `setTipX10Watts(0)` | <1 ms | 一次 ADC + PWM 写 |
| 四个任务创建 | <1 ms | 静态分配，无堆操作 |
| 调度器启动 | <1 ms | 首次切换 |

从上电到第一帧 OLED 通常 <100 ms；用户感知的"开机慢"主要来自启动 logo 的
`LOGOTime` 设置，而不是初始化。

## 优先级为什么这样排

| 任务 | 优先级 | 理由 |
| --- | --- | --- |
| PID | Realtime | 控制周期错过会直接导致温度超调 |
| POW | AboveNormal | PD 协议有 24 ms 响应窗口，不能等 |
| MOV | Normal | 100 ms 周期，晚几毫秒无影响 |
| GUI | BelowNormal | 丢一帧用户几乎察觉不到 |

如果把 GUI 提到 POW 之上，转场动画（一帧两次 I2C 传输）会挤占 PD 的响应时间，
表现为"某些充电器协商失败"。

## 静态分配的内存账

| 任务 | 栈字节 | TCB（约） |
| --- | --- | --- |
| PID | 2048 | ~100 |
| POW | 1024 | ~100 |
| MOV | 2048 | ~100 |
| GUI | 2048 | ~100 |
| Idle | `configMINIMAL_STACK_SIZE=128` 字 = 512 B | ~100 |

合计约 7.5 KB，占 GD32VF103 32 KB SRAM 的 23%，余量充足。

## 相关

- [settings_persistence.md](settings_persistence.md)：`loadSettings`
- [pid_thread.md](pid_thread.md) / [gui_thread.md](gui_thread.md)：任务主体
