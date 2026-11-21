# DeskHop 源码笔记：led.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/led.c`（共 155 行）

> 板载 LED（GP25）与真实键盘 LED 的状态管理：常亮模式、非阻塞闪烁、
> 键盘灯同步、Caps Lock 作为输出指示。

相关文档：[07-屏幕与状态反馈](../../../references/firmware/DeskHop/Docs/07-屏幕与状态反馈.md)

## 1. 函数总表

| 函数 | 行号 | 职责 |
|------|------|------|
| `set_keyboard_leds` | L41-L55 | 通过 host SET_REPORT 写键盘灯 |
| `onboard_led_should_be_on` | L57-L78 | 按 led_mode/活跃输出/空闲判定 |
| `update_onboard_led` | L80-L87 | 状态变化才写 GPIO |
| `restore_leds` | L89-L98 | 板载 + 键盘灯一起恢复 |
| `toggle_led` | L100-L107 | 翻转板载灯（受 led_mode 限制） |
| `blink_led` | L109-L113 | 请求闪 5 次 |
| `led_sync_task` | L115-L125 | 30Hz 对账 desired/actual |
| `led_blinking_task` | L127-L155 | 80ms 推进闪烁 |

## 2. 常亮判定

```c
static bool onboard_led_should_be_on(device_t *state) {
    if (state->active_output != BOARD_ROLE)
        return false;

    uint64_t inactivity = time_us_64() - state->last_activity[BOARD_ROLE];

    switch (state->config.led_mode) {
        case LED_SHORT_IDLE:
            return inactivity < short_idle_timeout_us;

        case LED_LONG_IDLE:
            return inactivity < long_idle_timeout_us;

        case LED_STATUS_ONLY:
        case LED_DISABLED:
            return false;

        case LED_ACTIVE_OUTPUT:
        default:
            return true;
    }
}
```

（L57-L78）

| 模式 | 行为 |
|------|------|
| `LED_ACTIVE_OUTPUT` | 活跃板常亮 |
| `LED_SHORT_IDLE` | 最近 1s 有输入才亮 |
| `LED_LONG_IDLE` | 最近 120s 有输入才亮 |
| `LED_STATUS_ONLY` | 不常亮，只闪 |
| `LED_DISABLED` | 不亮 |

`active_output != BOARD_ROLE` 优先于所有模式：非活跃板永远不常亮。

## 3. 非阻塞闪烁

```c
void blink_led(device_t *state) {
    /* Since LEDs might be ON previously, we go OFF, ON, OFF, ON, OFF */
    state->blinks_left     = 5;
    state->last_led_change = time_us_32();
}
```

（L109-L113）

只置计数与时间戳，不做任何延时。推进在 30Hz 任务里：

```c
void led_blinking_task(device_t *state) {
    const int blink_interval_us = 80000; /* 80 ms off, 80 ms on */
    static uint8_t leds;

    /* If there is no more blinking to be done, exit immediately */
    if (state->blinks_left == 0)
        return;

    /* We have some blinks left to do, check if they are due, exit if not */
    if ((time_us_32()) - state->last_led_change < blink_interval_us)
        return;

    /* Toggle the LED state */
    uint8_t new_led_state = toggle_led();

    /* Also keyboard leds (if it's connected locally) since on-board leds are not visible */
    leds = new_led_state * 0x07; /* Numlock, capslock, scrollock */

    if (state->keyboard_connected)
        set_keyboard_leds(leds, state);

    /* Decrement the counter and update the last-changed timestamp */
    state->blinks_left--;
    state->last_led_change = time_us_32();

    /* Restore LEDs in the last pass */
    if (state->blinks_left == 0)
        restore_leds(state);
}
```

（L127-L155）

时序：5 次 ×80ms = 400ms。`new_led_state * 0x07` 把键盘三个灯与板载灯同步。
最后一次翻转后 `restore_leds()` 回到“应显示”状态，避免停在随机亮灭。

## 4. 键盘 LED

```c
void set_keyboard_leds(uint8_t requested_led_state, device_t *state) {
    static uint8_t new_led_value;

    new_led_value = requested_led_state;
    if (state->keyboard_connected) {
        if(tuh_hid_set_report(state->kbd_dev_addr,
                              state->kbd_instance,
                              0,
                              HID_REPORT_TYPE_OUTPUT,
                              &new_led_value,
                              sizeof(uint8_t)))

            state->keyboard_leds_actual[BOARD_ROLE] = requested_led_state;
    }
}
```

（L41-L55）

- `static new_led_value`：TinyUSB 可能异步读取，栈变量会悬空；
- 只有传输成功才更新 `actual`，`led_sync_task()` 据此重试；
- `kbd_dev_addr/kbd_instance` 来自 host mount 时记录的主键盘。

`restore_leds()` 用 `keyboard_leds_desired[active_output]`：每台电脑各自保存
Caps/Num/Scroll 状态，切换后恢复对应那套。

## 5. Caps Lock 指示

在 `usb.c:tud_hid_set_report_cb()` 里：

- `kbd_led_as_indicator=1` 时，清 Caps 位，按 `active_output` 重设；
- 电脑以为在控制 Caps，实际被固件覆盖。

## 6. 坑点

1. **`LED_DISABLED` 只关板载灯**：`toggle_led()` 仍返回计算值，
   `led_blinking_task()` 仍会设置键盘三灯。
2. **闪烁与 `led_sync_task` 可能打架**：同步任务 33ms 一次，可能覆盖闪烁状态；
   实际观察到的闪烁可能不均匀。
3. **`static new_led_value` 不能改局部变量**：异步 SET_REPORT 会读悬空指针。
4. **`blink_led()` 不检查 `blinks_left`**：连续调用会重置计数，闪烁延长。
5. **`led_sync_task` 只在 `keyboard_connected` 时同步键盘灯**：键盘拔掉后
   `actual` 保留旧值，重新插上会立即补一次。
6. **`onboard_led_should_be_on()` 用 `time_us_64()`**：64 位减法，无回绕问题。
