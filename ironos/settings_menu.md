# SettingsMenu：二级菜单交互引擎（B 级）

> 对应源文件：`source/Core/Threads/UI/logic/SettingsMenu.cpp`（共 316 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[04-用户界面与按键状态机](../../../references/firmware/IronOS/Docs/04-用户界面与按键状态机.md)

## 职责

把 `settingsGUI.cpp` 里的静态菜单表变成可交互的界面：上下移动、进入子菜单、
长按加速、3 秒后显示帮助文字、绘制滚动条。

## 状态（全部在 `guiContext.scratch_state`）

| 字段 | 含义 |
| --- | --- |
| `state1` mainEntry | 一级菜单下标 |
| `state2` subEntry | 二级下标（0 = 停留在一级） |
| `state3` autoRepeatAcceleration | 长按加速量 |
| `state4` autoRepeatTimer | 下次允许触发的时刻 |
| `state5` currentMenuLength | 当前菜单可见项数（缓存） |
| `state6` isRenderingHelp | 是否正在显示帮助 |

模式切换时 `guiRenderLoop` 会 `memset` 这块内存，所以所有字段在进入菜单时都是 0。

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `printShortDescription` | 39 | 画简称并定位值光标 |
| `render_menu` | 56 | 画菜单项或帮助文字 |
| `getMenuLength` | 74 | 数可见项 |
| `moveToNextEntry` | 90 | 下移（跳过不可见项） |
| `gui_SettingsMenu` | 137 | 主入口：绘制 + 按键处理 |

## 帮助文字

```cpp
  if ((xTaskGetTickCount() - lastButtonTime < HELP_TEXT_TIMEOUT_TICKS) || item->description == 0) {
```

（`source/Core/Threads/UI/logic/SettingsMenu.cpp:58`）

`HELP_TEXT_TIMEOUT_TICKS = 3 s`。3 秒内显示"简称 + 值"，之后切换成滚动的完整描述。
这样既不打断操作，又能在用户犹豫时给出解释。

## 菜单长度必须运行时数

```cpp
uint16_t getMenuLength(const menuitem *menu, const uint16_t stop) {
  // walk this menu to find the length
  uint16_t counter = 0;
  for (uint16_t pos = 0; pos < stop; pos++) {
    // End of list
    if (menu[pos].draw == nullptr) {
      return counter;
    }
```

（`source/Core/Threads/UI/logic/SettingsMenu.cpp:74-80`）

`#ifdef` 会改变表内容，长度只能运行时数。`stop=128` 是防御性上限。

## 长按加速

```cpp
  case BUTTON_F_LONG:
    if (xTaskGetTickCount() + (*autoRepeatAcceleration) > (*autoRepeatTimer) + PRESS_ACCEL_INTERVAL_MAX) {
      callIncrementHandler();
```

（`source/Core/Threads/UI/logic/SettingsMenu.cpp:256-258`）

参数（`settingsGUI.hpp:16-18`）：

| 宏 | 值 | 作用 |
| --- | --- | --- |
| `PRESS_ACCEL_STEP` | 33 ms | 每次重复后加速量 |
| `PRESS_ACCEL_INTERVAL_MIN` | 100 ms | 最快重复间隔 |
| `PRESS_ACCEL_INTERVAL_MAX` | 300 ms | 初始重复间隔 |

间隔从 300 ms 逐步降到 100 ms，形成"越按越快"的手感。

## 按键映射

```cpp
  bool    swapButtonSettings = getSettingValue(SettingsOptions::ReverseButtonSettings);
  uint8_t buttonPress;
  switch (buttons) {
  case BUTTON_F_LONG:
    buttonPress = swapButtonSettings ? BUTTON_B_LONG : BUTTON_F_LONG;
```

（`source/Core/Threads/UI/logic/SettingsMenu.cpp:219-224`）

`ReverseButtonSettings` 在菜单层交换 A/B 语义，方便左撇子；注意它只影响菜单，
不影响焊接模式的调温键（那是 `ReverseButtonTempChangeEnabled`）。

## 陷阱

- **`isVisible` 跳过逻辑是递归的**：连续多个不可见项会递归 `moveToNextEntry`，
  菜单项非常多时注意栈深。
- **修改设置后要清 `currentMenuLength`**：可见项数量可能变化（例如关掉 Pulse）。
- **`BUTTON_BOTH` 在子菜单里是"返回上一级"，在一级菜单里是"退出并保存"**。
- **帮助文字渲染时 `isRenderingHelp=1`**，此时按任意键先清标志而不是改设置。

## 相关

- [settings_gui.md](settings_gui.md)：菜单表与绘制回调
- [settings_persistence.md](settings_persistence.md)：`nextSettingValue`
- [buttons.md](buttons.md)：事件来源
