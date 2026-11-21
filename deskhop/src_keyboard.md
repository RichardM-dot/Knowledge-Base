# DeskHop 源码笔记：keyboard.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/keyboard.c`（共 480 行）

> 键盘链路的上层：热键表、键码匹配、多键盘状态合并、本地/UART 分流、
> 多媒体/系统键转发。

相关文档：[05-HID报告解析与转发](../../../references/firmware/DeskHop/Docs/05-HID报告解析与转发.md)

## 1. 函数总表

| 函数 | 行号 | 职责 |
|------|------|------|
| `hotkeys[]` | L55-L151 | 13 条热键定义（数据驱动） |
| `key_in_report` | L154-L162 | 6 个键码槽里找某键 |
| `check_specific_hotkey` | L166-L179 | 修饰键 + 键序列匹配 |
| `check_all_hotkeys` | L182-L190 | 遍历热键表 |
| `update_kbd_state` | L198-L209 | 存某接口的 6KRO 报文 |
| `update_remote_kbd_state` | L212-L214 | 存对端报文 |
| `add_keys` | L218-L231 | 去重合并键码 |
| `release_all_keys` | L234-L242 | 清状态 + 发全零报文 |
| `combine_kbd_states` | L245-L261 | 多键盘合并 |
| `send_keyboard_report` | L264-L271 | 按 boot/report 发送 |
| `process_kbd_queue_task` | L273-L296 | core0 出队上报 |
| `queue_kbd_report` | L299-L306 | 入队（未连接丢弃） |
| `send_key` | L309-L322 | 本地/UART 分流 |
| `send_consumer_control` | L325-L332 | 多媒体分流 |
| `send_system_control` | L335-L342 | 系统键分流 |
| `process_keyboard_report` | L352-L388 | 键盘主入口 |
| `process_consumer_report` | L390-L423 | 多媒体报文处理 |
| `process_system_report` | L426-L443 | 系统键报文处理 |
| `get_keyboard` | L446-L459 | 按 report id 找键盘 |
| `get_or_add_keyboard` | L462-L479 | 解析期分配槽位 |

## 2. 热键表

```c
hotkey_combo_t hotkeys[] = {
    /* Main keyboard switching hotkey */
    {.modifier       = HOTKEY_MODIFIER,
     .keys           = {HOTKEY_TOGGLE},
     .key_count      = 1,
     .pass_to_os     = false,
     .action_handler = &output_toggle_hotkey_handler},

    /* Pressing right ALT + right CTRL toggles the slow mouse mode */
    {.modifier       = KEYBOARD_MODIFIER_RIGHTALT | KEYBOARD_MODIFIER_RIGHTCTRL,
     .keys           = {},
     .key_count      = 0,
     .pass_to_os     = true,
     .acknowledge    = true,
     .action_handler = &mouse_zoom_hotkey_handler},
```

（L55-L68）

字段含义：

| 字段 | 作用 |
|------|------|
| `modifier` | 要求按下的修饰键位掩码 |
| `keys[6]` | 要求同时按下的键码序列 |
| `key_count` | 键序列长度（0 表示纯修饰键组合） |
| `pass_to_os` | 命中后是否仍把按键发给系统 |
| `acknowledge` | 是否闪灯确认 |
| `action_handler` | 动作函数 |

新增功能 = 加一行表项 + 实现 handler，不改匹配逻辑。

### 2.1 匹配语义

```c
bool check_specific_hotkey(hotkey_combo_t keypress, const hid_keyboard_report_t *report) {
    /* We expect all modifiers specified to be detected in the report */
    if (keypress.modifier != (report->modifier & keypress.modifier))
        return false;

    for (int n = 0; n < keypress.key_count; n++) {
        if (!key_in_report(keypress.keys[n], report)) {
            return false;
        }
    }

    /* Getting here means all of the keys were found. */
    return true;
}
```

（L166-L179）

- 修饰键是**子集匹配**：热键要求 Ctrl，实际按了 Ctrl+Shift 仍命中；
- 键序列必须全部出现，但不要求顺序；
- 每个热键的键码可以出现在 6 个槽的任意位置。

子集匹配的好处是“组合键容错”，坏处是可能与用户的其他快捷键冲突
（例如 Ctrl+Caps 的切机键在按住 Shift 时也会触发）。

## 3. 键盘主入口

```c
void process_keyboard_report(uint8_t *raw_report, int length, uint8_t itf, hid_interface_t *iface) {
    hid_keyboard_report_t new_report = {0};
    device_t *state                  = &global_state;
    hotkey_combo_t *hotkey           = NULL;

    if (length < KBD_REPORT_LENGTH)
        return;

    /* No more keys accepted if we're about to reboot */
    if (global_state.reboot_requested)
        return;

    extract_kbd_data(raw_report, length, itf, iface, &new_report);

    /* Update the keyboard state for this device */
    update_kbd_state(state, &new_report, itf);

    /* Check if any hotkey was pressed */
    hotkey = check_all_hotkeys(&new_report, state);

    /* ... and take appropriate action */
    if (hotkey != NULL) {
        /* Provide visual feedback we received the action */
        if (hotkey->acknowledge)
            blink_led(state);   /* 闪灯确认：告诉用户热键已被识别 */

        /* Execute the corresponding handler */
        hotkey->action_handler(state, &new_report);   /* 执行热键动作（切机/锁/升级…） */

        /* And pass the key to the output PC if configured to do so. */
        if (!hotkey->pass_to_os)
            return;   /* 该热键不透传（如切机键），到此为止 */
    }

    /* This method will decide if the key gets queued locally or sent through UART */
    send_key(&new_report, state);
}
```

（L352-L388）

顺序：长度检查 → 重启门闩 → 提取 6KRO → 记状态 → 查热键 → 执行 → 可选透传 →
合并分流。

`reboot_requested` 门闩防止升级/重启期间误触发动作。

## 4. 多键盘合并

```c
void combine_kbd_states(device_t *state, hid_keyboard_report_t *combined_report) {
    memset(combined_report, 0, sizeof(hid_keyboard_report_t));

    /* Combine all local keyboards up to max_kbd_idx */
    for (uint8_t i = 0; i <= state->max_kbd_idx; i++) {
        combined_report->modifier |= state->local_kbd_states[i].modifier;
        add_keys(combined_report, &state->local_kbd_states[i]);
    }

    /* Add remote keyboard */
    combined_report->modifier |= state->remote_kbd_state.modifier;
    add_keys(combined_report, &state->remote_kbd_state);
}
```

（L245-L261）

- 修饰键按位或；
- 键码去重后填入 6 个槽；
- 本机所有键盘 + 对端一个键盘合并成一份报文。

`add_keys()` 用 `memchr` 找空槽，`key_in_report` 去重，O(6×6) 常数级。

### 4.1 6KRO 限制

合并结果最多 6 个键码。若同时按下的键超过 6 个：
- NKRO 键盘在提取阶段已被截断到 6；
- 多键盘合并时，后来的键找不到空槽会被丢弃。

这是 USB boot 协议兼容格式的硬限制。要支持全键无冲，需要改用 NKRO 报表
（对电脑声明位图），但会牺牲 BIOS 兼容性。

## 5. 分流

```c
void send_key(hid_keyboard_report_t *report, device_t *state) {
    /* Create a combined report from all device states */
    hid_keyboard_report_t combined_report;
    combine_kbd_states(state, &combined_report);

    if (CURRENT_BOARD_IS_ACTIVE_OUTPUT) {
        /* Queue the combined report */
        queue_kbd_report(&combined_report, state);
        state->last_activity[BOARD_ROLE] = time_us_64();
    } else {
        /* Send the combined report to ensure all keys are included */
        queue_packet((uint8_t *)&combined_report, KEYBOARD_REPORT_MSG, KBD_REPORT_LENGTH);
    }
}
```

（L309-L322）

- 本板活跃 → 本地 `kbd_queue` → core0 → 电脑；
- 非活跃 → UART → 对端板 → 对端电脑；
- 无论如何都先合并，保证对端收到的是“全部按键状态”，不是单个键盘的片段。

`send_consumer_control()`/`send_system_control()` 同理，只是报文类型不同。

## 6. 队列发送

```c
void process_kbd_queue_task(device_t *state) {
    hid_keyboard_report_t report;

    /* If we're not connected, we have nowhere to send reports to. */
    if (!state->tud_connected)
        return;

    /* Peek first, if there is anything there... */
    if (!queue_try_peek(&state->kbd_queue, &report))
        return;

    /* If we are suspended, let's wake the host up */
    if (tud_suspended())
        tud_remote_wakeup();

    /* If it's not ok to send yet, we'll try on the next pass */
    if (!tud_hid_n_ready(ITF_NUM_HID))
        return;

    bool succeeded = send_keyboard_report(&report);

    /* ... then we can remove it from the queue. Race conditions shouldn't happen [tm] */
    if (succeeded)
        queue_try_remove(&state->kbd_queue, &report);
}
```

（L273-L296）

peek→send→成功后 remove：发送失败留队头重试，不丢键。
`send_keyboard_report()` 按 `tud_hid_n_get_protocol()` 决定 boot（无 ID）或
report（`REPORT_ID_KEYBOARD`）。

## 7. 多媒体/系统键

`process_consumer_report()`：

- 若 `consumer.is_variable`，按位扫描 data，置位则取 `cc_array[i]`；
- 否则直接把原始字节拷进 4 字节报告；
- 活跃 → `send_consumer_control()`，否则 UART。

`process_system_report()`：要求 `data_len >= 1`，取 data[0]，活跃 → 本地，否则 UART。

## 8. 坑点

- **热键修饰键是子集匹配**：Ctrl+Caps 在 Ctrl+Shift+Caps 时也触发。
- **键序列不要求顺序**：`keys={F12,D}` 与 `{D,F12}` 等价。
- **`HOTKEY_TOGGLE` 不能设为 `HID_KEY_NONE`**：`user_config.h` 明确警告，
  NONE(0) 会出现在大量报文中，导致键盘失灵。
- **6KRO 截断**：超过 6 键同时按下会丢键；NKRO 键盘已在提取阶段降维。
- **`release_all_keys()` 发全零报文**：切换输出时必须做，否则旧电脑卡键。
- **`update_kbd_state()` 的 `device_idx` 来自 usb.c 映射**：副键盘共享槽，
  两个副键盘会互相覆盖。
- **`get_or_add_keyboard()` 只分配不提交**：`num_keyboards++` 在
  `handle_keyboard_descriptor_values()` 里；漏掉会导致同一槽被反复初始化。
