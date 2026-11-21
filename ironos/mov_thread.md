# MOVThread：加速度计与运动检测（B 级）

> 对应源文件：`source/Core/Threads/MOVThread.cpp`（共 257 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[01-启动流程与任务模型](../../../references/firmware/IronOS/Docs/01-启动流程与任务模型.md)

## 职责

每 100 ms 读一次加速度计，判断"是否被移动/拿起"，刷新 `lastMovementTime`。
GUI 用它决定休眠与息屏；自动转屏也在这里直接改 OLED 方向。

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `detectAccelerometerVersion` | 53 | 按固定顺序探测 5 种芯片 |
| `readAccelerometer` | 114 | 多态分发到具体芯片 |
| `startMOVTask` | 162 | 任务主体：探测 → 8 点滑动平均 → 阈值判定 |

## 探测顺序

```cpp
  // Order matters for probe order, some Acceleromters do NOT like bad reads; and we have a bunch of overlap of addresses
  void detectAccelerometerVersion() {
```

（`source/Core/Threads/MOVThread.cpp:52-53`）

顺序：MMA8652 → LIS2DH12（含 clone 判定）→ BMA223 → SC7A20 → MSA301 → GPIO 振动开关。
地址重叠，错误探测可能把总线锁死，所以顺序不能随意调整。

## 运动判定

```cpp
    int32_t threshold = 1500 + (9 * 200);
    threshold -= getSettingValue(SettingsOptions::Sensitivity) * 200; // 200 is the step size
```

（`source/Core/Threads/MOVThread.cpp:191-192`）

灵敏度 0..9 映射到阈值 3300..1500：

| Sensitivity | 阈值 | 手感 |
| --- | --- | --- |
| 0 | 3300 | 关闭（但设置项为 0 也会关休眠） |
| 5 | 2300 | 中等 |
| 9 | 1500 | 最灵敏 |

LIS 克隆片另除以 6（`threshold /= 6`），因为其原始差值偏小。

## 滤波

8 点环形缓冲求平均，与当前值比较：

```cpp
    int32_t error = (abs(avgx - tx) + abs(avgy - ty) + abs(avgz - tz));
```

（`source/Core/Threads/MOVThread.cpp:233`）

三轴绝对差之和超过阈值 → 判定为移动 → `lastMovementTime = xTaskGetTickCount()`。
用"和"而不是"欧氏距离"是为了避免开方。

## 初始化细节

```cpp
  osDelay(TICKS_100MS / 5); // This is here as the BMA doesnt start up instantly and can wedge the I2C bus if probed too fast after boot
  detectAccelerometerVersion();
  osDelay(TICKS_100MS / 2); // wait ~50ms for setup of accel to finalise
```

（`source/Core/Threads/MOVThread.cpp:170-172`）

上电 20 ms 后才能探测，否则 BMA 可能锁死 I2C；探测后再等 50 ms 让芯片稳定。

## 自动转屏

```cpp
    if (getSettingValue(SettingsOptions::OrientationMode) == 2) {
      if (rotation != ORIENTATION_FLAT) {
        OLED::setRotation(rotation == ORIENTATION_LEFT_HAND); // link the data through
      }
    }
```

（`source/Core/Threads/MOVThread.cpp:204-208`）

只在 `AUTO` 模式且不是平放时改方向；平放时保持上一次方向，避免桌面放置时来回翻转。

## 陷阱

- **探测失败会关闭 Sensitivity**（设为 0），从此休眠只能靠按键超时。
- **`lastMovementTime = 0` 有特殊含义**：表示"从未移动"，自动启动模式下直接判睡。
- **I2C 与 OLED 共用总线**，MOV 的高频读取会与 GUI 抢带宽，`GUIDelay()` 的 50 ms
  延迟就是为此设计的。

## CPU 与总线开销

| 项目 | 数值 |
| --- | --- |
| 采样周期 | 100 ms |
| 每次 I2C 读 | 6 字节（三轴 + 状态） |
| 每次耗时 | 约 2~3 ms（软件 I2C） |
| 总线占用率 | 2%~3% |

看起来很低，但 MOV 与 OLED 共用总线；GUI 刷屏一次要 6~8 ms，两者叠加时
`GUIDelay()` 的 50 ms 让出就显得必要。

## 姿态判定的边界

`Orientation` 由芯片内部的姿态引擎给出（不是固件算的），只有三种：
`LEFT_HAND`、`RIGHT_HAND`、`FLAT`。固件只做一件事：AUTO 模式下非平放时
调用 `OLED::setRotation()`。平放时保持上一次方向，避免桌面放置时来回翻转。

## 相关

- [buttons.md](buttons.md)：另一个"用户在场"信号
- [ui_operating_modes.md](ui_operating_modes.md)：`shouldBeSleeping` 使用本文件的时间戳
