# DeskHop 源码笔记：usb.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/usb.c`（共 445 行）

> TinyUSB 设备端回调 + 主机端回调的集中实现。设备端负责“对电脑装键鼠”，
> 主机端负责“收真实键鼠”。

相关文档：[03-USB HID 设备侧](../../../references/firmware/DeskHop/Docs/03-USB HID 设备侧.md)、
[04-USB主机侧与设备枚举](../../../references/firmware/DeskHop/Docs/04-USB主机侧与设备枚举.md)

## 1. 编译期约束

```c
_Static_assert(MAX_DEVICES <= CFG_TUH_DEVICE_MAX,
               "MAX_DEVICES must not exceed CFG_TUH_DEVICE_MAX");
```

（L22-L23）

`MAX_DEVICES=4`、`CFG_TUH_DEVICE_MAX=4`；改大前者而不同时改后者会编译失败，
避免 `iface[4][12]` 与 TinyUSB 设备表不一致。

## 2. 函数总表

| 函数 | 行号 | 侧 | 触发时机 |
|------|------|----|----------|
| `get_led_report` | L38-L57 | device | GET_REPORT(OUTPUT) |
| `get_keyboard_report` | L59-L80 | device | GET_REPORT(INPUT) |
| `tud_hid_get_report_cb` | L85-L120 | device | 主机读报表 |
| `tud_hid_set_report_cb` | L131-L183 | device | 主机写 LED/vendor |
| `tud_mount_cb` | L193-L195 | device | 枚举完成 |
| `tud_umount_cb` | L204-L206 | device | 拔出/复位 |
| `tud_cdc_rx_cb` | L216-L232 | device | CDC 收到数据（仅 DH_DEBUG_CDC_FLASH） |
| `tuh_hid_umount_cb` | L246-L270 | host | 设备拔出 |
| `tuh_hid_mount_cb` | L286-L350 | host | HID 接口挂载 |
| `tuh_hid_report_received_cb` | L369-L427 | host | 收到一帧报告 |
| `tuh_hid_set_protocol_complete_cb` | L439-L445 | host | SET_PROTOCOL 完成 |

## 3. 设备端 GET_REPORT

### 3.1 LED

`get_led_report()` 返回 `keyboard_leds_desired[BOARD_ROLE]` 的 1 字节；
请求长度 <1 返回 0。读取“电脑希望点亮的灯”，而不是实际写给键盘的灯，
避免控制传输期间再打扰真实键盘。

### 3.2 键盘状态与安全

```c
static uint16_t get_keyboard_report(uint8_t *buffer, uint16_t request_len) {
    hid_keyboard_report_t report = {0};

    /* If the request buffer is too small, we cannot provide the report */
    if (request_len < sizeof(report))
        return 0;

    /* Inactive output should not be able to read the keyboard state */
    if (CURRENT_BOARD_IS_ACTIVE_OUTPUT)
        combine_kbd_states(&global_state, &report);

    memcpy(buffer, &report, sizeof(report));
    return sizeof(report);
}
```

（L59-L75 附近）

非活跃输出返回全零：防止一台电脑通过 GET_REPORT 窥探另一台电脑的输入。
这是 README “no information sharing” 的代码落点之一。

### 3.3 接口/ID 过滤

`tud_hid_get_report_cb()`：

- 只处理 `instance == ITF_NUM_HID`（键盘接口）；
- `report_id` 只接受 `REPORT_ID_NONE`（boot）或 `REPORT_ID_KEYBOARD`；
- OUTPUT → LED，INPUT → 键盘状态，其他返回 0。

## 4. 设备端 SET_REPORT

### 4.1 vendor 配置通道的三层门闩

```c
    if (instance == ITF_NUM_HID_VENDOR && report_id == REPORT_ID_VENDOR) {
        /* Security - only if config mode is enabled are we allowed to do anything. While the report_id
           isn't even advertised when not in config mode, security must always be explicit and never assume */
        if (!global_state.config_mode_active)
            return;

        /* We insist on a fixed size packet. No overflows. */
        if (bufsize != RAW_PACKET_LENGTH)
            return;

        uart_packet_t *packet = (uart_packet_t *) (buffer + START_LENGTH);

        /* Only a certain packet types are accepted */
        if (!validate_packet(packet))
            return;

        process_packet(packet, &global_state);
    }
```

（L138-L155）

1. 接口 + report id 匹配；
2. 必须处于配置模式；
3. 长度恰好 12 字节 + 类型白名单。

`buffer + START_LENGTH` 跳过 0xAA55，直接复用 UART 包的 10 字节结构。

### 4.2 LED

- 只接受键盘 report id（或 boot 0）、长度 1、类型 OUTPUT；
- 若 `kbd_led_as_indicator`，清 Caps 位并按 `active_output` 重设；
- 存 `keyboard_leds_desired[BOARD_ROLE]`；
- 本板直连键盘且活跃 → `restore_leds()`；
- 无条件 `send_value(leds, KBD_SET_REPORT_MSG)` 同步对端。

## 5. 主机端挂载

```c
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (dev_addr > MAX_DEVICES || instance >= MAX_INTERFACES)
        return;

    /* Get interface information */
    hid_interface_t *iface = &global_state.iface[dev_addr-1][instance];

    iface->protocol = tuh_hid_get_protocol(dev_addr, instance);

    /* Parse the report descriptor into our internal structure. */
    parse_report_descriptor(iface, desc_report, desc_len);
```

（L286-L298）

顺序：越界检查 → 记录协议 → 解析描述符 → 按接口协议登记 → 闪灯 + 通知对端 → 首次
`tuh_hid_receive_report()`。

键盘分支：`enforce_ports && BOARD_ROLE==OUTPUT_B` 直接 return；可强制 boot 协议；
记录 `kbd_dev_addr/kbd_instance`。

鼠标分支：`enforce_ports && BOARD_ROLE==OUTPUT_A` 直接 return；默认强制 report 协议
（为了滚轮），用户可配 boot。

收尾：

```c
    /* Also set mouse_connected if report descriptor contains mouse, even if interface
       protocol says keyboard. This handles composite devices like QMK. */
    if (iface->mouse.is_found) {
        global_state.mouse_connected = true;
    }
```

（L336-L340）

接口协议是键盘但描述符含鼠标集合（QMK）时，仍标记鼠标已连接。

## 6. 主机端收报

### 6.1 device_idx 映射

| idx | 含义 | 存储 |
|-----|------|------|
| 0 | 主键盘 | `local_kbd_states[0]` |
| 1 | 鼠标 | 不参与键盘合并 |
| 2 (`MAX_DEVICES-2`) | 副键盘 | 共享槽 |
| 3 (`MAX_DEVICES-1`) | 保留给远端 | `remote_kbd_state` |

```c
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        if (dev_addr == global_state.kbd_dev_addr && instance == global_state.kbd_instance) {
            /* Primary keyboard */
            device_idx = 0;
        } else {
            /* Secondary keyboard (e.g., wireless keyboard through unified dongle) */
            device_idx = (MAX_DEVICES - 2);
        }
    } else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        /* Mouse devices */
        device_idx = 1;
    } else {
        /* Other devices */
        device_idx = (dev_addr - 1) % (MAX_DEVICES - 1);
    }
```

（L389-L403）

### 6.2 派发

```c
    /* Boot protocol reports omit descriptor report IDs. */
    bool report_has_id = iface->uses_report_id && iface->protocol != HID_PROTOCOL_BOOT;

    /* If the report has an ID, use its report handler. If not, choose by interface protocol. */
    if (report_has_id || itf_protocol == HID_ITF_PROTOCOL_NONE) {
        uint8_t report_id = report_has_id ? report[0] : REPORT_ID_NONE;

        process_report_f receiver = report_receivers[iface->report_handler[report_id]];

        if (receiver != NULL)
            receiver((uint8_t *)report, len, device_idx, iface);
    }
```

（L405-L417）

`report_handler[256]` 保证任意 ID 不越界；未登记值为 0 → `REPORT_RECEIVER_NONE` → NULL。

末尾必须 `tuh_hid_receive_report(dev_addr, instance)`，否则只收一帧。

### 6.3 卸载

`tuh_hid_umount_cb()`：

- 键盘 → `keyboard_connected=false` + 清 `local_kbd_states`（防卡键）；
- 鼠标 → `mouse_connected=false`；
- 无条件 `memset(iface, 0, sizeof(hid_interface_t))`（地址复用防串设备）。

## 7. 坑点

- **`dev_addr > MAX_DEVICES` 是 `>` 不是 `>=`**：地址从 1 开始，合法 1..4。
- **`iface` 不清零会串设备**：拔键盘插鼠标，地址复用，旧的 report_handler/偏移表会污染新设备。
- **组合接收器的副键盘共享槽**：两个副键盘会互相覆盖。
- **`report_has_id` 两条件缺一不可**：描述符声明过 ID 且当前非 boot。
- **协议切换异步**：必须靠 `tuh_hid_set_protocol_complete_cb()` 回写 `iface->protocol`，
  否则后续报文错位一字节。
- **`get_keyboard_report` 用 `CURRENT_BOARD_IS_ACTIVE_OUTPUT` 做安全门**：若某天改成
  “总是返回真实状态”，会破坏跨机隔离承诺。
