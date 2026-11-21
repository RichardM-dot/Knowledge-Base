# Buttons：按键消抖与手势（B 级）

> 对应源文件：`source/Core/Drivers/Buttons.cpp`（共 136 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[04-用户界面与按键状态机](../../../references/firmware/IronOS/Docs/04-用户界面与按键状态机.md)

## 职责

把两个 GPIO 电平变成 7 种按键事件。它是**事件式**状态机：`SHORT` 表示"按下并释放"的
完整脉冲，`LONG` 表示"按住超过 400 ms"的持续状态。

## 事件

| 事件 | 含义 |
| --- | --- |
| `BUTTON_NONE` | 无事件 |
| `BUTTON_F_SHORT` / `BUTTON_B_SHORT` | 前/后键短按（按下+释放） |
| `BUTTON_F_LONG` / `BUTTON_B_LONG` | 前/后键按住 ≥400 ms |
| `BUTTON_BOTH` | 两键同时按下并释放 |
| `BUTTON_BOTH_LONG` | 两键同时按住 ≥400 ms |

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `getButtonState` | 28 | 主状态机 |
| `waitForButtonPress` | 103 | 阻塞等待一次按键（吃掉事件） |
| `waitForButtonPressOrTimeout` | 117 | 同上，带超时 |

## 状态机的三个 static

```cpp
  static uint8_t    previousState       = 0;
  static bool       longPressed         = false;
  static TickType_t previousStateChange = 0;
  const TickType_t  timeout             = TICKS_100MS * 4;
  uint8_t           currentState;
  currentState = (getButtonA()) << 0;
  currentState |= (getButtonB()) << 1;
```

（`source/Core/Drivers/Buttons.cpp:40-46`）

bit0 = A（前键），bit1 = B（后键）。`timeout = 400 ms`（4 × 100 ms）。

## 双键的细节

```cpp
      // User has pressed a button down (nothing done on down)
      // If there is a rising edge on one of the buttons from double press we
      // want to mask that out As users are having issues with not release
      // both at once
      previousState |= currentState;
```

（`source/Core/Drivers/Buttons.cpp:73-77`）

用 `|=` 而不是 `=`：先按 A、再按 B、再依次松开，仍然识别为 `BOTH`。
如果用 `=`，第二次按下会覆盖第一次的位，双键手势永远触发不了。

## 长按只报一次

`longPressed` 在首次达到 400 ms 时置 true 并返回 `*_LONG`，之后每次调用仍返回 `*_LONG`
（因为 `currentState != 0` 且已超时），但**释放时不再返回 SHORT**。
上层因此可以安全地"每帧都收到 LONG"，不会重复触发短按。

## 共享时间戳

```cpp
  if (currentState) {
    lastButtonTime = xTaskGetTickCount();
  }
```

（`source/Core/Drivers/Buttons.cpp:48-50`）

`lastButtonTime` 被三处使用：GUI 息屏判定、帮助文字超时、休眠判定。
它在**按下的每一帧**都刷新，所以"按住不放"也会被认为用户在场。

## 陷阱

- **返回值是事件，不是电平**：同一帧内调用两次会丢掉中间事件，所有模式函数只调一次。
- **Pinecil 的按键是高有效**：`getButtonA()` 返回 1 表示按下（见 BSP.cpp）。
- **`waitForButtonPress` 会吃掉一次按键事件**，用于阻塞式提示框。
- **按压抖动没有硬件滤波**：400 ms 的长按阈值同时充当软件消抖。

## 事件时序

以 40 ms 帧为单位（数字为帧号）：

| 帧 | 物理动作 | `getButtonState()` 返回 |
| --- | --- | --- |
| 0 | 按下 A | `BUTTON_NONE`（只记录） |
| 1..9 | 保持 | `BUTTON_NONE` |
| 10 | 达到 400 ms | `BUTTON_F_LONG` |
| 11+ | 仍按住 | `BUTTON_F_LONG`（每帧） |
| 12 | 释放 | `BUTTON_NONE`（longPressed 已置位，不报 SHORT） |

短按则是：按下 → 若干帧 `NONE` → 释放帧返回 `BUTTON_F_SHORT`。

## 为什么按下时不上报

如果按下瞬间就上报，用户"按住想长按"的过程会先触发一次短按，导致模式切换。
把 SHORT 定义为"完整脉冲"后，长按与短按从根上互斥。

## 双键的两种语义

| 组合 | 事件 | 典型用途 |
| --- | --- | --- |
| 同时按下并释放 | `BUTTON_BOTH` | 退出菜单/退出调温 |
| 同时按住 ≥400 ms | `BUTTON_BOTH_LONG` | 锁定/解锁按键 |

## 相关

- [gui_thread.md](gui_thread.md)：每帧调用一次
- [ui_operating_modes.md](ui_operating_modes.md)：各模式如何解释事件
