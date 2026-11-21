# DeskHop 源码笔记：handlers.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/handlers.c`（共 716 行）

> 两类处理器的实体：键盘热键动作（`hotkeys[].action_handler`）与 UART 消息
> 处理器（`uart_handler[].handler`）。另有切换收口 `set_active_output()`。

相关文档：[00-总览与双板架构](../../../references/firmware/DeskHop/Docs/00-总览与双板架构.md)、
[08-实测与坑点指南](../../../references/firmware/DeskHop/Docs/08-实测与坑点指南.md)

## 1. 热键处理器

| 函数 | 行号 | 触发热键 | 动作 |
|------|------|----------|------|
| `output_toggle_hotkey_handler` | L42-L50 | `HOTKEY_MODIFIER + HOTKEY_TOGGLE`（默认左Ctrl+Caps） | 翻转输出 |
| `_get_border_position` | L59-L66 | — | 按 Y 记 top/bottom |
| `_screensaver_set` | L75-L81 | — | 本机/对端设屏保模式 |
| `screen_border_hotkey_handler` | L92-L103 | 右Shift+F12+Y | 记录边界并同步/保存 |
| `fw_upgrade_hotkey_handler_A` | L112-L115 | 左Shift+右Shift+A | A 板进 ROM BOOTSEL |
| `fw_upgrade_hotkey_handler_B` | L128-L131 | 左Shift+右Shift+B | 通知 A 板升级 |
| `switchlock_hotkey_handler` | L140-L143 | 右Ctrl+K | 切换锁 |
| `toggle_gaming_mode_handler` | L154-L157 | 左Ctrl+右Shift+G | 游戏模式 |
| `screenlock_hotkey_handler` | L171-L195 | 右Ctrl+L | 两台一起锁屏 |
| `wipe_config_hotkey_handler` | L208-L212 | 右Shift+F12+D | 擦配置 |
| `mouse_zoom_hotkey_handler` | L223-L226 | 右Ctrl+右Alt | 慢速鼠标 |
| `enable_screensaver_pong_hotkey_handler` | L236-L242 | 左Ctrl+右Shift+S | PONG |
| `enable_screensaver_jitter_hotkey_handler` | L253-L259 | 左Ctrl+右Shift+J | JITTER |
| `disable_screensaver_hotkey_handler` | L270-L272 | 左Ctrl+右Shift+X | 关屏保 |
| `config_enable_hotkey_handler` | L285-L294 | 左Ctrl+右Shift+C+O | 进/出配置模式 |

## 2. UART 处理器

| 函数 | 行号 | 消息类型 |
|------|------|----------|
| `handle_keyboard_uart_msg` | L310-L322 | `KEYBOARD_REPORT_MSG` |
| `handle_mouse_abs_uart_msg` | L334-L346 | `MOUSE_REPORT_MSG` |
| `handle_output_select_msg` | L354-L362 | `OUTPUT_SELECT_MSG` |
| `handle_fw_upgrade_msg` | L370-L379 | `FIRMWARE_UPGRADE_MSG` |
| `handle_mouse_zoom_msg` | L386-L388 | `MOUSE_ZOOM_MSG` |
| `handle_set_report_msg` | L399-L408 | `KBD_SET_REPORT_MSG` |
| `handle_switch_lock_msg` | L415-L417 | `SWITCH_LOCK_MSG` |
| `handle_sync_borders_msg` | L428-L441 | `SYNC_BORDERS_MSG` |
| `handle_flash_led_msg` | L449-L451 | `FLASH_LED_MSG` |
| `handle_wipe_config_msg` | L460-L463 | `WIPE_CONFIG_MSG` |
| `handle_screensaver_msg` | L472-L474 | `SCREENSAVER_MSG` |
| `handle_consumer_control_msg` | L483-L485 | `CONSUMER_CONTROL_MSG` |
| `handle_save_config_msg` | L496-L498 | `SAVE_CONFIG_MSG` |
| `handle_reboot_msg` | L507-L509 | `REBOOT_MSG` |
| `handle_proxy_msg` | L521-L526 | `PROXY_PACKET_MSG` |
| `handle_toggle_gaming_msg` | L536-L538 | `GAMING_MODE_MSG` |
| `handle_api_msgs` | L553-L587 | `GET_VAL_MSG` / `SET_VAL_MSG` |
| `handle_api_read_all_msg` | L589-L598 | `GET_ALL_VALS_MSG` |
| `handle_request_byte_msg` | L608-L621 | `REQUEST_BYTE_MSG` |
| `handle_response_byte_msg` | L633-L658 | `RESPONSE_BYTE_MSG` |
| `handle_heartbeat_msg` | L671-L692 | `HEARTBEAT_MSG` |
| `set_active_output` | L708-L716 | 内部收口 |

## 3. 切换收口

```c
void set_active_output(device_t *state, uint8_t new_output) {
    state->active_output = new_output;
    restore_leds(state);
    send_value(new_output, OUTPUT_SELECT_MSG);

    /* If we were holding a key down and drag the mouse to another screen, the key gets stuck.
       Changing outputs = no more keypresses on the previous system. */
    release_all_keys(state);
}
```

（L708-L716）

顺序不可交换：先改本地状态 → 恢复 LED → 通知对端 → 释放按键。
`release_all_keys()` 内部的 `queue_kbd_report()` 会按“新的”活跃输出决定方向，
所以必须在 `active_output` 更新之后调用。

## 4. 双屏锁屏

```c
void screenlock_hotkey_handler(device_t *state, hid_keyboard_report_t *report) {
    hid_keyboard_report_t lock_report = {0}, release_keys = {0};

    for (int out = 0; out < NUM_SCREENS; out++) {
        switch (state->config.output[out].os) {
            case WINDOWS:
            case LINUX:
                lock_report.modifier   = KEYBOARD_MODIFIER_LEFTGUI;
                lock_report.keycode[0] = HID_KEY_L;
                break;
            case MACOS:
                lock_report.modifier   = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTGUI;
                lock_report.keycode[0] = HID_KEY_Q;
                break;
            default:
                break;
        }
```

（L171-L187，节选）

- Windows/Linux：GUI+L；
- macOS：Ctrl+GUI+Q；
- 本机走 `queue_kbd_report` + `release_all_keys`；
- 对端走两包 UART：按下 + 全松开。

**已知瑕疵**：`lock_report` 在循环中复用，`default` 分支不重置。若第二个
output 的 OS 未配置，会沿用第一个的组合键。修复只需循环开头清零。
未改代码，仅记录。

## 5. 配置 API

```c
void handle_api_msgs(uart_packet_t *packet, device_t *state) {
    uint8_t value_idx = packet->data[0];
    const field_map_t *map = get_field_map_entry(value_idx);

    /* If we don't have a valid map entry, return immediately */
    if (map == NULL)
        return;

    /* Create a pointer to the offset into the structure we need to access */
    uint8_t *ptr = (((uint8_t *)&global_state) + map->offset);

    if (packet->type == SET_VAL_MSG) {
        /* Not allowing writes to objects defined as read-only */
        if (map->readonly)
            return;

        memcpy(ptr, &packet->data[1], map->len);
    }
    else if (packet->type == GET_VAL_MSG) {
        uart_packet_t response = {.type=GET_VAL_MSG, .data={[0] = value_idx}};
        memcpy(&response.data[1], ptr, map->len);
        queue_cfg_packet(&response, state);
    }

    /* With each GET/SET message, we reset the configuration mode timeout */
    reset_config_timer(state);
}
```

（L553-L587）

安全点：

- 编号必须存在；
- 只读字段拒绝写；
- 长度来自映射表，不由报文控制；
- GET 响应走 vendor HID 队列；
- 每次读写重置 5 分钟超时。

## 6. 固件搬运

### 6.1 请求端（A 板）

```c
void handle_request_byte_msg(uart_packet_t *packet, device_t *state) {
    uint32_t address = packet->data32[0];

    if (address >= STAGING_IMAGE_SIZE)
        return;

    /* Add requested data to bytes 4-7 in the packet and return it with a different type */
    uint32_t data = *(uint32_t *)&ADDR_FW_RUNNING[address];
    packet->data32[1] = data;

    queue_packet(packet->data, RESPONSE_BYTE_MSG, PACKET_DATA_LENGTH);
}
```

（L608-L621）

地址上限防越界；`ADDR_FW_RUNNING[address]` 强转 `uint32_t*` 依赖 4 字节对齐
（调用方按 4 递增）。

### 6.2 应答端（B 板）

```c
void handle_response_byte_msg(uart_packet_t *packet, device_t *state) {
    uint16_t offset = packet->data[0];
    uint32_t address = packet->data32[0];

    if (address != state->fw.address) {
        state->fw.upgrade_in_progress = false;
        state->fw.address = 0;
        return;
    }
```

（L633-L641）

地址不匹配立即中止——宁可不升级，也不写错数据。`offset` 是地址低字节，
即页内偏移 0..255。CRC 只累计到倒数第二个扇区。

### 6.3 心跳触发

`handle_heartbeat_msg()`（L671-L692）：对端版本严格大于本板才启动；
`DISABLE_FW_UPGRADE` 编译期开关直接 return；升级中不重复触发。

## 7. 代理包

```c
void handle_proxy_msg(uart_packet_t *packet, device_t *state) {
    /* Block packets from being proxied if config mode isn't active. */
    if (!state->config_mode_active)
        return;

    queue_packet(&packet->data[1], (enum packet_type_e)packet->data[0], PACKET_DATA_LENGTH - 1);
}
```

（L521-L526）

配置页面只连 A 板，要改 B 板配置时由 A 板转发。`config_mode_active` 是硬门闩，
普通运行时不充当局域网关。

## 8. 坑点

1. **`screenlock_hotkey_handler` 的 `lock_report` 复用**（见 §4）。
2. **`handle_set_report_msg` 存到 `OTHER_ROLE`**：这个 LED 状态属于“对端电脑”，
   本板只是代存；写错索引会导致切换后灯不对。
3. **`handle_request_byte_msg` 直接改传入的 `packet`** 再发回：`packet` 指向
   `state->in_packet`，复用同一块内存；`queue_packet` 会先拷贝，安全。
4. **`handle_proxy_msg` 的 `data[0]` 是内层类型**：没有再次 `validate_packet()`；
   依赖 `tud_hid_set_report_cb()` 对 PROXY 的白名单，以及接收端 handler 自身的门闩。
5. **`handle_heartbeat_msg` 无“版本更低”处理**：若对端版本低，本板不会主动推送，
   只有对端心跳发现自己低才会拉取；单向触发，避免双端同时搬运。
6. **`config_enable_hotkey_handler` 的魔术字只写不清**：退出配置模式靠
   `is_config_mode_active()` 读后清 scratch[5]，不要手动清两个。
