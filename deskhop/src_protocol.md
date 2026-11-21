# DeskHop 源码笔记：protocol.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/protocol.c`（共 187 行）

> 配置 API 的字段映射表 + 把内部报文封装成 HID 队列元素。UART 消息类型
> 定义在 `protocol.h`，处理函数在 `handlers.c`。

相关文档：[02-板间UART协议精读](../../../references/firmware/DeskHop/Docs/02-板间UART协议精读.md)、
[03-USB HID 设备侧](../../../references/firmware/DeskHop/Docs/03-USB HID 设备侧.md)

## 1. 函数总表

| 函数 | 行号 | 职责 |
|------|------|------|
| `api_field_map[]` | L26-L92 | 字段编号 → 结构体偏移/类型/只读 |
| `get_field_map_entry` | L94-L105 | 按编号线性查找 |
| `get_field_map_index` | L112-L122 | 按数组下标取（越界钳位） |
| `get_field_map_length` | L124-L126 | 表项数 |
| `_queue_packet` | L140-L154 | 通用 HID 队列封装 |
| `queue_cfg_packet` | L160-L166 | 配置帧 → vendor 接口 |
| `queue_cc_packet` | L173-L178 | 多媒体 → consumer ID |
| `queue_system_packet` | L185-L187 | 系统键 → system ID |

## 2. 字段映射表

```c
const field_map_t api_field_map[] = {
/* Index, Rdonly, Type, Len, Offset in struct */
    { 0,  true,  UINT8,  1, offsetof(device_t, active_output) },
    { 1,  true,  INT16,  2, offsetof(device_t, pointer_x) },
    { 2,  true,  INT16,  2, offsetof(device_t, pointer_y) },
    { 3,  true,  INT16,  2, offsetof(device_t, mouse_buttons) },
```

（L26-L31）

编号分段：

| 段 | 编号 | 内容 |
|----|------|------|
| 运行状态（只读） | 0-3 | 活跃输出、光标 X/Y、按键 |
| 输出 A | 10-22 | 屏幕数、速度、边界、OS、方位、屏保 |
| 输出 B | 40-52 | 同上 |
| 全局配置 | 70-77 | 版本、boot 模式、LED、热键、加速、端口约束 |
| 固件/状态 | 78-84 | 版本、CRC、连接、锁、相对鼠标、游戏模式、LED 模式 |

用 `offsetof()` 而不是手写偏移，结构体布局变化时表自动跟随（但仍需人工确认语义）。

### 2.1 64 位字段截断

```c
    /* Until we increase the payload size from 8 bytes, clamp to avoid exceeding the field size */
    /* 64 位时间字段被截成 7 字节：payload 只有 8 字节，首字节已用于字段编号，
     * 剩余 7 字节装不下完整 uint64，协议层按 7 字节小端传输（高位丢失）。 */
    { 21, false, UINT64, 7, offsetof(device_t, config.output[0].screensaver.idle_time_us) },
```

（L46-L50）

`data[0]` 是字段编号，只剩 7 字节；`UINT64` 语义上不完整。屏保空闲时间
最大 2^56µs ≈ 2284 年，实际够用，但读代码时要意识到“7 字节不是笔误”。

## 3. 查找

```c
const field_map_t* get_field_map_entry(uint32_t index) {
    for (unsigned int i = 0; i < ARRAY_SIZE(api_field_map); i++) {
        if (api_field_map[i].idx == index) {
            return &api_field_map[i];
        }
    }

    return NULL;
}
```

（L94-L105）

- 参数是协议编号，不是数组下标；
- 线性 O(41)，配置模式非热路径；
- 未找到返回 NULL，`handle_api_msgs()` 直接丢弃。

`get_field_map_index()` 参数是数组下标，越界钳到末项，供“读取全部”遍历。

## 4. HID 队列封装

```c
void _queue_packet(uint8_t *payload, device_t *state, uint8_t type, uint8_t len, uint8_t id, uint8_t inst) {
    hid_generic_pkt_t generic_packet = {
        .instance = inst,
        .report_id = id,
        .type = type,
        .len = len,
    };

    memcpy(generic_packet.data, payload, len);
    queue_try_add(&state->hid_queue_out, &generic_packet);
}
```

（L140-L154）

队列元素自带 `instance`/`report_id`，发送任务 `process_hid_queue_task()` 无需
判断来源，直接把整包交给 `tud_hid_n_report()`。

### 4.1 配置帧

```c
void queue_cfg_packet(uart_packet_t *packet, device_t *state) {
    uint8_t raw_packet[RAW_PACKET_LENGTH];
    write_raw_packet(raw_packet, packet);
    _queue_packet(raw_packet, state, 0, RAW_PACKET_LENGTH, REPORT_ID_VENDOR, ITF_NUM_HID_VENDOR);
}
```

（L160-L166）

先编码成 12B 原始帧（含 0xAA55/类型/CRC），再入 vendor 队列。与
`tud_hid_set_report_cb()` 的“长度必须恰为 12”对称。

### 4.2 多媒体/系统键

```c
void queue_cc_packet(uint8_t *payload, device_t *state) {
    _queue_packet(payload, state, 1, CONSUMER_CONTROL_LENGTH, REPORT_ID_CONSUMER, ITF_NUM_HID);
}
```

（L173-L178）

`type=1`/`type=2` 只是 `hid_generic_pkt_t` 的内部子类型标记，用于调试区分，
与 `enum packet_type_e` 无关。

## 5. 与 handlers 的配合

| 场景 | 生产者 | 队列 | 消费者 |
|------|--------|------|--------|
| Web 配置读字段 | `handle_api_msgs()` → `queue_cfg_packet()` | `hid_queue_out` | core0 `process_hid_queue_task()` |
| 多媒体键转发 | `handle_consumer_control_msg()` → `queue_cc_packet()` | 同上 | 同上 |
| 系统键转发 | `handle_consumer_control_msg` 同类 | 同上 | 同上 |
| 本机多媒体键 | `send_consumer_control()` → `queue_cc_packet()` | 同上 | 同上 |

## 6. 坑点

1. **`_queue_packet` 的 `len` 无上限**：`data[12]`，调用方必须保证 ≤12；
   `queue_cfg_packet` 用 12，cc 用 4，system 用 1。
2. **`queue_cfg_packet` 返回的是“已编码帧”**：配置页面收到的是 0xAA55 开头的
   12 字节，不是裸 `uart_packet_t`；解析时要跳过 2 字节。
3. **`get_field_map_index()` 与 `get_field_map_entry()` 参数语义不同**：
   前者是数组下标，后者是协议编号；混用会读到错误字段。
4. **`offsetof` 指向 `device_t` 而非 `config_t`**：`handle_api_msgs()` 里
   `((uint8_t *)&global_state) + map->offset`，所以表项必须相对 `device_t`。
5. **只读字段靠 `map->readonly` 拦截**：`SET_VAL_MSG` 命中只读项直接 return，
   不会回错误；配置页面表现为“保存无效”，需要看代码才知道原因。
