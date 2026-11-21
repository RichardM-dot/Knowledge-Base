# Core 杂项：FreeRTOS 钩子 / 系统调用 / 翻译 / 滚动文本 / 插值（B 级，合并条目）

> 对应源文件（共 6 个）：
> - `source/Core/Src/freertos.c`（67 行）
> - `source/Core/Src/FreeRTOSHooks.c`（46 行）
> - `source/Core/Src/syscalls.c`（40 行）
> - `source/Core/Src/Translation.cpp`（20 行）
> - `source/Core/Src/ScrollMessage.cpp`（103 行）
> - `source/Core/Drivers/Utils.cpp`（51 行）
>
> 工程：`references/firmware/IronOS` ｜ 关联文档：[01-启动流程与任务模型](../../../references/firmware/IronOS/Docs/01-启动流程与任务模型.md)、[04-用户界面与按键状态机](../../../references/firmware/IronOS/Docs/04-用户界面与按键状态机.md)

> 合并说明：这 6 个文件都很小（20~103 行），且没有跨文件的强逻辑链路，
> 但都属于"支撑性代码"，放在一起便于查阅。

## freertos.c

CubeMX 生成的外壳，现在只剩两个 include 和 ST 的版权头——任务创建已全部
迁移到 `main.cpp` 的 `osThreadStaticDef`。**不要以为它是任务定义文件**。

## FreeRTOSHooks.c

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `vApplicationIdleHook` | 27 | 空闲任务里喂看门狗 |
| `vApplicationGetIdleTaskMemory` | 33 | 为静态 Idle 任务提供 TCB/栈 |
| `vApplicationStackOverflowHook` | 40 | 栈溢出 → `reboot()` |

```c
void vApplicationIdleHook(void) { resetWatchdog(); }
```

（`source/Core/Src/FreeRTOSHooks.c:27`）

看门狗在 Idle 里喂，等价于"系统还有空闲时间"才喂。任何任务死循环且优先级高于 Idle
都会触发复位。这是比"在定时器里喂"更严格的策略。

```c
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  (void)pcTaskName;

  // We dont have a good way to handle a stack overflow at this point in time
  reboot();
}
```

（`source/Core/Src/FreeRTOSHooks.c:40-46`）

栈已损坏，无法安全恢复，直接复位。

## syscalls.c

newlib 的 `_read/_write/_lseek` 等桩函数。Pinecil 系列已在别处提供真实实现，
所以条件编译跳过；其它机型统一返回 -1（不支持）。

| 函数 | 行号 | 说明 |
| --- | --- | --- |
| `initialise_monitor_handles` | 28 | 空实现（semihosting 用） |
| `_getpid` | 31 | 固定返回 1 |

注意：`_write` 返回 -1 会让 `printf` 静默失败，调试输出要用
`log_system_state()`/`DEBUG_UART_OUTPUT`。

## Translation.cpp

```cpp
const char *translatedString(uint16_t offset) { return TranslationStrings + offset; }
```

（`source/Core/Src/Translation.cpp:20`）

翻译表由 `Translations/` 下的 Python 脚本在编译期生成：所有语言打包成一个大
`char` 数组，`Tr->` 结构体里存的是 `uint16` 偏移。运行时只做一次指针加法，
零解析、零分配。

代价：字符串总长不能超过 64 KB（偏移是 `uint16`）。

## ScrollMessage.cpp

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `str_display_len` | 31 | 按自定义编码数字符（>0xF0 为双字节） |
| `messageWidth` | 55 | 像素宽度；首字节 0x01 表示大字 |
| `drawScrollingText` | 68 | 跑马灯绘制（两端各停留一拍） |

```cpp
void drawScrollingText(const char *message, TickType_t currentTickOffset) {
  OLED::clearScreen();
```

（`source/Core/Src/ScrollMessage.cpp:68-69`）

函数内部会清屏，调用前画的内容会被清掉。滚动速度由
`SettingsOptions::DescriptionScrollSpeed` 决定：快 10 ms/像素、慢 20 ms/像素。

两端停留的逻辑：

```cpp
    if (messageOffset < OLED_WIDTH) {
      // Snap the message to the left edge.
      messageOffset = OLED_WIDTH;
```

（`source/Core/Src/ScrollMessage.cpp` 的 `drawScrollingText`）

## Utils.cpp

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `InterpolateLookupTable` | 26 | 在 `(x,y)` 成对表中定位并插值 |
| `LinearInterpolate` | 36 | 定点线性插值（放大 1000 倍） |
| `RequiredCurrentForTipAtVoltage` | 38 | `I = V/R`，供电协商用 |

```cpp
int32_t Utils::LinearInterpolate(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x) { return y1 + (((((x - x1) * 1000) / (x2 - x1)) * (y2 - y1))) / 1000; }
```

（`source/Core/Drivers/Utils.cpp:36`）

先乘 1000 再除，避免整数截断；代价是中间结果可能溢出 `int32`，
所以表项范围必须控制在合理区间（温度表最大约 13000 uV，安全）。

## 陷阱

- **`freertos.c` 不是任务文件**，改任务去 `main.cpp`。
- **看门狗在 Idle 里喂**：任何"忙等"都会导致复位，不要写 `while(!flag);`。
- **翻译偏移是 `uint16`**：新增语言时注意总长度。
- **`drawScrollingText` 会清屏**，不能与其他绘制组合使用。

## 相关

- [main_startup.md](main_startup.md)：任务创建与看门狗
- [tip_thermo_model.md](tip_thermo_model.md)：使用插值表
- [settings_menu.md](settings_menu.md)：使用滚动文本
