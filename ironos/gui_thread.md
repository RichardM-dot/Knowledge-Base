# GUIThread：界面主框架（A 级）

> 对应源文件：`source/Core/Threads/GUIThread.cpp`（共 323 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[04-用户界面与按键状态机](../../../references/firmware/IronOS/Docs/04-用户界面与按键状态机.md)

## 职责

GUI 任务的"外壳"：每 40 ms 画一帧，把按键与上下文交给当前模式函数，处理模式迁移、
转场动画与刷屏。具体界面内容在 `Threads/UI/logic/` 与 `Threads/UI/drawing/`。

## 全局状态

| 名称 | 说明 |
| --- | --- |
| `buttonsAtDeviceBoot` | 上电瞬间按住的键，用于进入调试菜单 |
| `currentOperatingMode` | 当前模式（本任务独占写） |
| `context` | `guiContext`：迁移时间、上一模式、scratch_state |
| `MOVEMENT_INACTIVITY_TIME` | 60 s（息屏判定） |
| `BUTTON_INACTIVITY_TIME` | 60 s（息屏判定） |

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `guiHandleDraw` | 68 | 画一帧并返回下一模式 |
| `guiRenderLoop` | 191 | 绘制 + 迁移 + 转场 + 刷屏 |
| `handle_post_init_state` | 259 | 初始化后的路由（调试/CJC/告警） |
| `startGUITask` | 286 | 任务入口与 25 fps 主循环 |

## 模式分发

```cpp
  // 模式分发：每个 case 都是"画这一帧 + 返回下一模式"。
  // StartupLogo 分支除了画 logo 还会根据 AutoStartMode 决定首次进入哪个模式，
  // 这是整个状态机的"入口岔路"。
  switch (currentOperatingMode) {
```

（`source/Core/Threads/GUIThread.cpp:104-107`）

每个 case 都是 `newMode = xxx(buttons, &context);`，模式函数不保存长期状态。
这让"画一帧"成为纯函数，任意时刻切换模式都不会留下隐式状态（除 `scratch_state`）。

## 息屏逻辑

```cpp
    // 息屏条件：温度 < 50 C（不烫手）+ 运动感应开启 + 运动和按键都静默超过 60 s。
    // 温度门限的意义：烙铁还热着就把屏幕关掉，用户会误以为已断电。
    uint32_t tipTemp = TipThermoModel::getTipInC();
    if ((tipTemp < 50) && getSettingValue(SettingsOptions::Sensitivity) &&
        (((xTaskGetTickCount() - lastMovementTime) > MOVEMENT_INACTIVITY_TIME) && ((xTaskGetTickCount() - lastButtonTime) > BUTTON_INACTIVITY_TIME))) {
```

（`source/Core/Threads/GUIThread.cpp:80-84`）

四个条件同时满足才息屏。`lastMovementTime` 由 MOVTask 写，`lastButtonTime` 由
`getButtonState()` 写。

## 模式迁移

```cpp
  if (newMode != currentOperatingMode) {
    context.viewEnterTime = xTaskGetTickCount();
    context.previousMode  = currentOperatingMode;
```

（`source/Core/Threads/GUIThread.cpp:201-203`）

随后 `memset(&context.scratch_state, 0, ...)`。不清零的后果见文档 04 的"陷阱"一节：
各模式对 `stateN` 的解释完全不同，残留值会造成"按键失灵"之类的怪现象。

## 转场动画

```cpp
  if (context.transitionMode != TransitionAnimation::None) {
    OLED::useSecondaryFramebuffer(true);
    // Now we need to fill the secondary buffer with the _next_ frame to transistion to
    guiHandleDraw();
    OLED::useSecondaryFramebuffer(false);
```

（`source/Core/Threads/GUIThread.cpp:222-226`）

代价：转场帧的绘制与 I2C 传输都翻倍（两帧），GUI 优先级最低，容易掉帧。
`animOpenState` 在菜单打开期间暂停图标动画，就是为了给转场让带宽。

## 启动

```cpp
  prepareTranslations();

  OLED::initialize(); // start up the LCD
  OLED::setBrightness(getSettingValue(SettingsOptions::OLEDBrightness));
```

（`source/Core/Threads/GUIThread.cpp:290-293`）

初始化顺序：翻译表 → OLED → 亮度/反色 → 预渲染资源 → 清滤波器 → 方向 →
读上电按键。`prepareTranslations()` 必须在第一帧之前，否则菜单会显示空串。

## 帧率

```cpp
    vTaskDelayUntil(&startRender, TICKS_100MS * 4 / 10); // Try and maintain 20-25fps ish update rate, way to fast but if we can its nice
```

（`source/Core/Threads/GUIThread.cpp:321`）

40 ms/帧。用 `DelayUntil` 而非 `Delay`，保证节拍不因绘制耗时累积漂移。

## 陷阱

- **`guiHandleDraw()` 不是可重入的**：它直接操作全局 framebuffer 与 `context`。
- **转场动画的 `Down` 分支缺 `break`**，会贯穿到 `None` 分支（行为等价于什么都不做），
  阅读时不要以为是 bug。
- **不要在其它任务里调用 OLED 绘制**：没有锁。

## 相关

- [settings_menu.md](settings_menu.md)：菜单的具体交互
- [ui_operating_modes.md](ui_operating_modes.md)：各模式实现
- [oled.md](oled.md)：framebuffer 与刷屏
