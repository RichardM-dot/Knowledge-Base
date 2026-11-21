# UI 工作模式（B 级，合并条目）

> 对应源文件（共 6 个模式文件 + 7 个工具文件）：
> - `source/Core/Threads/UI/logic/Soldering.cpp`（189 行）
> - `source/Core/Threads/UI/logic/Sleep.cpp`（63 行）
> - `source/Core/Threads/UI/logic/HomeScreen.cpp`（88 行）
> - `source/Core/Threads/UI/logic/TemperatureAdjust.cpp`（105 行）
> - `source/Core/Threads/UI/logic/CJC.cpp`（54 行）
> - `source/Core/Threads/UI/logic/ShowStartupWarnings.cpp`（131 行）
> - `source/Core/Threads/UI/logic/utils/*.cpp`（欠压/休眠/关机/超时/延时）
>
> 工程：`references/firmware/IronOS` ｜ 关联文档：[04-用户界面与按键状态机](../../../references/firmware/IronOS/Docs/04-用户界面与按键状态机.md)

> 本条目合并 13 个强耦合的小文件，因为它们共同构成"一个模式函数 = 画一帧 + 返回下一模式"
> 的同一套约定，分开记录会重复同样的上下文。

## 统一约定

每个模式函数签名都是：

```cpp
OperatingMode xxx(const ButtonState buttons, guiContext *cxt);
```

- 只读 `buttons` 与 `cxt`，返回值是下一帧的模式；
- 长期状态放 `cxt->scratch_state`，模式切换时被清空；
- 自己画界面（调用 `ui_draw_*`），不返回像素。

## 函数表

| 文件 | 函数 | 行号 | 职责 |
| --- | --- | --- | --- |
| Soldering.cpp | `handleSolderingButtons` | 23 | 焊接模式按键（锁/Boost/调温/退出） |
| Soldering.cpp | `gui_solderingMode` | 107 | 每帧写目标温度、画状态、检查退出 |
| Sleep.cpp | `gui_SolderingSleepingMode` | 15 | 睡眠/休眠共用 |
| HomeScreen.cpp | `handleHomeButtons` | 23 | 待机页按键分发 |
| HomeScreen.cpp | `drawHomeScreen` | 70 | 待机页绘制（强制目标 0） |
| TemperatureAdjust.cpp | `gui_solderingTempAdjust` | 17 | 调温（长按加速、3 s 超时保存） |
| CJC.cpp | `performCJCC` | 16 | 冷端校准（16 次采样） |
| ShowStartupWarnings.cpp | `showWarnings` | 18 | 四道启动告警 |
| utils/checkUndervoltage.cpp | `checkForUnderVoltage` | 8 | DC 欠压检测 |
| utils/shouldDeviceSleep.cpp | `shouldBeSleeping` | 7 | 运动/霍尔超时判定 |
| utils/shouldDeviceShutdown.cpp | `shouldShutdown` | 7 | 关机超时判定 |
| utils/getSleepTimeout.cpp | `getSleepTimeout` | 5 | 设置值 → 毫秒 |
| utils/GUIDelay.cpp | `GUIDelay` | 4 | 统一 50 ms 让出总线 |

## 焊接模式

```cpp
  // Update the setpoints for the temperature
  if (cxt->scratch_state.state2) {
    if (getSettingValue(SettingsOptions::TemperatureInF)) {
      currentTempTargetDegC = TipThermoModel::convertFtoC(getSettingValue(SettingsOptions::BoostTemp));
    } else {
      currentTempTargetDegC = (getSettingValue(SettingsOptions::BoostTemp));
    }
```

（`source/Core/Threads/UI/logic/Soldering.cpp:123-129`）

`state2` = Boost 标志。每次进入本函数都重写 `currentTempTargetDegC`，所以 PID 永远
拿到最新目标；这也是"离开焊接模式后目标必须由别的模式改写"的原因
（HomeScreen 会把它置 0）。

退出优先级：

```
欠压 → 关机 → 睡眠 → 热失控 → 用户按键
```

## 按键锁的四态编码

`state1`：

| 值 | 含义 |
| --- | --- |
| 0 | 未锁 + 已释放 |
| 1 | 未锁 |
| 2 | 已锁 |
| 3 | 已锁 + 已释放 |

`>=2` 表示锁定；必须从 3 才能解锁，保证"锁定后先松手再按"。

## 睡眠/休眠

```cpp
  if (cxt->scratch_state.state4) {
    // Hibernating mode
    currentTempTargetDegC = 0;
  } else {
    if (getSettingValue(SettingsOptions::TemperatureInF)) {
      currentTempTargetDegC = TipThermoModel::convertFtoC(min(getSettingValue(SettingsOptions::SleepTemp), getSettingValue(SettingsOptions::SolderingTemp)));
```

（`source/Core/Threads/UI/logic/Sleep.cpp:30-35`）

`state4` = 休眠标志。睡眠温度取 `min(SleepTemp, SolderingTemp)`，防止睡眠温度
高于工作温度。

## 调温的自动重复

```cpp
    if (xTaskGetTickCount() - (*autoRepeatTimer) + (*autoRepeatAcceleration) > PRESS_ACCEL_INTERVAL_MAX) {
      delta              = -getSettingValue(SettingsOptions::TempChangeLongStep);
```

（`source/Core/Threads/UI/logic/TemperatureAdjust.cpp:45-46`）

与设置菜单同一套 `PRESS_ACCEL_*` 参数；温度按步长对齐取整后再夹到 MIN/MAX。
3 秒无操作自动 `saveSettings()` 并返回上一模式。

## 冷端校准

```cpp
  if (!isTipDisconnected() && abs(int(TipThermoModel::getTipInC() - getHandleTemperature(0) / 10)) < 10) {
    // Take 16 samples, only sample
    if (cxt->scratch_state.state1 < 16) {
```

（`source/Core/Threads/UI/logic/CJC.cpp:26-28`）

条件：烙铁头已接、且与手柄温差 <10 °C。采样 16 次求平均写入 `CalibrationOffset`。
`preStartChecks()` 未完成时不能校准（MHP30 会占用输出级）。

## 启动告警

```cpp
  switch (cxt->scratch_state.state1) {
  case 0: // Settings reset warning
    if (settingsWereReset) {
      if (warnUser(translatedString(Tr->SettingsResetMessage), buttons)) {
```

（`source/Core/Threads/UI/logic/ShowStartupWarnings.cpp:21-24`）

四道：设置重置 → 器件校验 → 加速度计缺失 → PD 芯片缺失。后两道有"最多提示 2 次"
的计数器（存 flash），避免老设备每次上电都弹窗。

## 工具函数

| 函数 | 关键逻辑 |
| --- | --- |
| `checkForUnderVoltage` | 仅 DC 输入；上电 2 s 后才检查 |
| `shouldBeSleeping` | 运动灵敏度 + 睡眠时间都非 0；霍尔超时 |
| `shouldShutdown` | 关机时间（分钟）或长按后键 |
| `getSleepTimeout` | 设置值 1..5 → 10..50 s；≥6 → (值−5) 分钟 |
| `GUIDelay` | `vTaskDelay(50 ms)`，给加速度计让 I2C 带宽 |

## 陷阱

- **模式函数每帧都会被调用**，不能在里面做一次性初始化（用 `scratch_state` 标志位）。
- **`currentTempTargetDegC` 是全局的**：离开焊接模式必须显式改写，否则 PID 会继续加热。
- **`TemperatureAdjust` 进入时先吞掉一帧按键**（`waitForRelease`），否则长按会被当成连续加温。
- **`ShowStartupWarnings` 的计数器用 `nextSettingValue` 递增**，不是直接赋值。

## 相关

- [gui_thread.md](gui_thread.md)：模式分发
- [settings_menu.md](settings_menu.md)：设置模式的实现
- [buttons.md](buttons.md)：按键事件
