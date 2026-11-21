# settingsGUI：设置菜单实现（A 级）

> 对应源文件：`source/Core/Src/settingsGUI.cpp`（共 1358 行）
> 头文件：`source/Core/Inc/settingsGUI.hpp`（共 42 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[04-用户界面与按键状态机](../../../references/firmware/IronOS/Docs/04-用户界面与按键状态机.md)、[05-设置系统与持久化](../../../references/firmware/IronOS/Docs/05-设置系统与持久化.md)

## 职责

用一张 `const menuitem[]` 表描述整个设置菜单，再配一组"绘制值/步进值/是否可见"
回调。文件里 90% 的函数只做两件事：读一个设置项，画到当前光标位置。

## 菜单模型

```cpp
typedef struct {
  // The settings description index, please use the `SETTINGS_DESC` macro with
  // the `SettingsItemIndex` enum. Use 0 for no description.
  uint8_t description;
  void (*const incrementHandler)(void);
  void (*const draw)(void); // Must not be nullptr, as that marks end of menu
  bool (*const isVisible)(void);
  // If this is set, we will automatically use the settings increment handler instead, set >= num settings to disable
  SettingsOptions   autoSettingOption;
  SettingsItemIndex shortDescriptionIndex;
  uint8_t           shortDescriptionSize;
} menuitem;
```

（`source/Core/Inc/settingsGUI.hpp:25-36`）

| 字段 | 可空 | 含义 |
| --- | --- | --- |
| `description` | 是（0） | 帮助文字索引，3 s 后显示 |
| `incrementHandler` | 是 | 自定义步进；空则用 `nextSettingValue` |
| `draw` | **否** | 绘制值；`nullptr` 是菜单结束标记 |
| `isVisible` | 是 | 条件显示 |
| `autoSettingOption` | ≥ Length 表示禁用 | 自动绑定的设置项 |
| `shortDescriptionIndex` | — | 简称索引 |
| `shortDescriptionSize` | — | 值区域字符列宽 |

## 菜单表

| 表 | 行号 | 内容 |
| --- | --- | --- |
| `rootSettingsMenu` | 273 | Power/Soldering/PowerSaving/UI/Advanced/Language/End |
| `powerMenu` | 303 | 输入电压、QC 电压、PD 超时、PD 模式 |
| `solderingMenu` | 333 | Boost、自动启动、步进、锁、Tip 类型、Profile |
| `PowerSavingMenu` | 405 | 灵敏度、睡眠温度/时间、关机时间、霍尔 |
| `UIMenu` | 434 | 温度单位、方向、滚动、动画、亮度、反色… |
| `advancedMenu` | 486 | BLE、功率上限、CJC 校准、VIN 校准、Pulse、恢复出厂 |

`subSettingsMenus[]` 的顺序必须与 `displayMenu(0..4)` 的实参一致。

## 关键函数

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `printValue` | 169 | 按 `\x01` 前缀选择大/小字体 |
| `printShortDescription` | 543 | 画简称并定位值光标 |
| `userConfirmation` | 560 | 阻塞式确认框（40 ms 滚动） |
| `setTempF(option)` | 982 | 单项温度单位换算 |
| `setTempF()` | 1005 | 全量温度单位切换（Boost/Sleep/Profile） |
| `displayBrightnessLevel` | 1112 | 显示档位并立即应用亮度 |
| `setCalibrateVIN` | 1207 | 输入电压校准子界面（嵌套循环） |
| `setResetSettings` | 1276 | 恢复出厂 → 重启 |
| `displayMenu` | 1303 | 子菜单标题 + 3 帧图标动画 |

## 温度单位切换

```cpp
static void setTempF(const enum SettingsOptions option) {
  uint16_t Temp = getSettingValue(option);
  if (getSettingValue(SettingsOptions::TemperatureInF)) {
    // Change temp to the F equiv
    // C to F == F= ( (C*9) +160)/5
    Temp = ((Temp * 9) + 160) / 5;
```

（`source/Core/Src/settingsGUI.cpp:982-987`）

这是整个文件里最危险的一段：`setTempF()` 必须把所有以温度为单位的设置项都换算，
漏掉一个就会出现"320 °F 被当成 320 °C"。当前列表：Boost、Soldering、Sleep、
Profile 的预热与 5 个阶段（`settingsGUI.cpp:1005-1020`）。

## 电压校准

```cpp
static void setCalibrateVIN(void) {
  // Jump to the voltage calibration subscreen
  OLED::clearScreen();

  for (;;) {
```

（`source/Core/Src/settingsGUI.cpp:1207-1211`）

这是一个"函数内部的模态循环"：自己读按键、自己刷屏，直到用户按退出键。
退出时 `saveSettings()` 立刻落盘，因为用户可能直接拔电。

## 菜单图标动画

```cpp
static void displayMenu(size_t index) {
  // Call into the menu
  // Draw title
  OLED::printWholeScreen(translatedString(Tr->SettingsMenuEntries[index]));
  static TickType_t menuSwitchLoopTick = 0;
  static size_t     menuCurrentIndex   = sizeof(rootSettingsMenu) + 1;
  TickType_t        step               = TICKS_100MS * 5;
```

（`source/Core/Src/settingsGUI.cpp:1303-1308`）

帧步长由 `AnimationSpeed` 决定：慢 500 ms、中 400 ms、快 300 ms。
`menuCurrentIndex` 用 static 保存，切换菜单时重置计时，保证每次进入都从第 0 帧开始。

## 陷阱

- **`draw` 不能为 nullptr**：那是结束标记，把它当成"无绘制"会让菜单提前结束。
- **`userConfirmation` 会阻塞整个 GUI 任务**（内部 40 ms 循环），
  只用于不可逆操作。
- **亮度/反色在绘制时应用**：翻到那一项才生效，不是全局即时。
- **`displayMenu` 的 index 与 `subSettingsMenus[]` 顺序强耦合**，加菜单必须两处同改。

## 相关

- [settings_menu.md](settings_menu.md)：菜单交互引擎（长按加速、帮助文字）
- [settings_persistence.md](settings_persistence.md)：`SettingsOptions` 与 flash
- [gui_thread.md](gui_thread.md)：调用者
