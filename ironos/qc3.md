# QC3：Quick Charge 诱骗（B 级）

> 对应源文件：`source/Core/Src/QC3.cpp`（共 205 行）、`source/Core/BSP/Pinecil/QC_GPIO.cpp`（共 51 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[03-电源管理与 USB PD](../../../references/firmware/IronOS/Docs/03-电源管理与 USB PD.md)

## 职责

在 PD 不可用时，用 D+/D- 的电平组合让 QC 充电器把 VBUS 升到 9/12/20 V，
并在 QC3 连续模式下以 0.2 V 步进逼近设定电压。

## 状态机

| 状态 | 含义 |
| --- | --- |
| `NOT_STARTED` | 尚未探测 |
| `QC_3` | 支持连续模式（0.2 V 步进） |
| `QC_2` | 只支持固定档（9/12/20 V） |
| `NO_QC` | 不支持 |

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `QC_Seek9V` | 42 | D- 0.6 V + D+ 3.3 V |
| `QC_Seek12V` | 47 | D- 0.6 V + D+ 0.6 V |
| `QC_Seek20V` | 52 | D- 3.3 V + D+ 3.3 V |
| `QC_SeekContPlus` | 62 | 连续模式 +0.2 V（30 ms 脉冲） |
| `QC_SeekContNeg` | 70 | 连续模式 −0.2 V |
| `seekQC` | 80 | 按目标电压步进逼近 |
| `startQC` | 142 | 上电握手 |
| `hasQCNegotiated` | 205 | 是否已在用 QC |

## 握手

```cpp
  // Tries to negotiate QC for 9V
  // This is a multiple step process.
  // 1. Set around 0.6V on D+ for 1.25 Seconds or so
  // 2. After this It should un-short D+->D- and instead add a 20k pulldown on
  // D-
  QC_DPlusZero_Six();
```

（`source/Core/Src/QC3.cpp:157-162`）

随后轮询 `QC_DM_PulledDown()`：若充电器识别到 QC，会把 D- 拉低。
检测窗口约 1.25 s，`i==140` 时还会额外加一个下拉电阻（兼容边缘充电器）。

```cpp
  if (enteredQC) {
    // We have a QC capable charger
    QC_Seek9V();
    QC_Post_Probe_En();
    QC_Seek9V();
```

（`source/Core/Src/QC3.cpp:181-185`）

请求 9 V 后再等 10×100 ms 确认 VBUS > 8 V，成功则判为 `QC_3`。

## 连续模式步进

```cpp
    while (steps > 0) {
      QC_SeekContPlus();
      vTaskDelay(3 * TICKS_10MS);
      steps--;
    }
```

（`source/Core/Src/QC3.cpp` 的 `seekQC`）

`steps = (目标 − 当前) / 2`，每步 0.2 V、间隔 30 ms。太快充电器识别不到，
太慢用户会看到电压缓慢爬升。

## GPIO 原语（QC_GPIO.cpp）

| 函数 | 作用 |
| --- | --- |
| `QC_DPlusZero_Six` | 拉低 D+（0.6 V 档） |
| `QC_DPlusThree_Three` | 拉高 D+（3.3 V 档） |
| `QC_DNegZero_Six` / `QC_DNegThree_Three` | D- 两档 |
| `QC_DM_PullDown` / `QC_DM_No_PullDown` | 探测用 20k 下拉 |
| `QC_resync` | 周期性重跑 `seekQC` 纠偏 |

## 陷阱

- **QC 必须等 PD 有结论后再启动**（`power_check()` 的守卫），否则会打断 PD 协商。
- **`startQC` 会先测输入电压**：若已经 >8 V（外部高压输入），直接放弃 QC。
- **`QCTries > 10` 后永久 `NO_QC`**：避免反复握手拖慢启动。
- QC3 的电压是"充电器自己保持"的，负载突变后可能掉回 5 V，所以需要 `QC_resync`。

## D+/D- 电平表

| 目标 | D+ | D- |
| --- | --- | --- |
| 5 V（默认） | 0.6 V | 0 V |
| 9 V | 3.3 V | 0.6 V |
| 12 V | 0.6 V | 0.6 V |
| 20 V | 3.3 V | 3.3 V |
| 连续模式 | 0.6 V | 3.3 V |

连续模式下，D+ 上的 0.2 V 步进脉冲由 `QC_SeekContPlus/Neg` 产生：
先切到连续模式，再短促地切到 20 V/12 V 电平并立即切回，充电器把这次"毛刺"
解释为 +1/−1 档。

## 时序预算

| 阶段 | 时间 | 说明 |
| --- | --- | --- |
| D+ 0.6 V 保持 | 1.25 s | 充电器识别 QC |
| 请求 9 V 后等待 | 最多 1 s | 10 × 100 ms 轮询 VBUS |
| 每步连续调压 | 30 ms | `vTaskDelay(3 * TICKS_10MS)` |

最坏情况下 `startQC` 会阻塞约 2.3 s，这也是它放在 POW 任务而不是 GUI 的原因。

## 相关

- [usbpd.md](usbpd.md)：优先级更高的 PD 路径
- [pow_thread.md](pow_thread.md)：调用 `power_check()`
