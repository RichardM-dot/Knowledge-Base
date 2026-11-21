# DeskHop 源码笔记：uart.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/uart.c`（共 149 行）

> 板间 UART 协议的用户层：帧编码、发送任务、收包分发表。物理层/收包在
> `setup.c`（DMA）与 `utils.c`/`tasks.c`（环缓冲扫描）里。

相关文档：[02-板间UART协议精读](../../../references/firmware/DeskHop/Docs/02-板间UART协议精读.md)

## 1. 函数总表

| 函数 | 行号 | 职责 |
|------|------|------|
| `write_raw_packet` | L33-L42 | `uart_packet_t` → 12B 线格式 |
| `queue_packet` | L54-L59 | 组包入 `uart_tx_queue` |
| `send_value` | L69-L71 | 单字节消息语法糖 |
| `process_uart_tx_task` | L81-L92 | DMA 空闲时发一帧 |
| `uart_handler[]` | L98-L129 | 类型 → 处理函数表 |
| `process_packet` | L139-L149 | 验校验和 + 表驱动分发 |

## 2. 帧格式

```c
void write_raw_packet(uint8_t *dst, uart_packet_t *packet) {
    uint8_t pkt[RAW_PACKET_LENGTH] = {[0] = START1,
                                      [1] = START2,
                                      [2] = packet->type,
                                      /* [3-10] is data, defaults to 0 */
                                      [11] = calc_checksum(packet->data, PACKET_DATA_LENGTH)};

    memcpy(&pkt[START_LENGTH + TYPE_LENGTH], packet->data, PACKET_DATA_LENGTH);
    memcpy(dst, &pkt, RAW_PACKET_LENGTH);
}
```

（L33-L42）

| 偏移 | 长度 | 内容 |
|------|------|------|
| 0 | 1 | `0xAA` |
| 1 | 1 | `0x55` |
| 2 | 1 | `packet_type_e` |
| 3 | 8 | payload（小端 union） |
| 11 | 1 | 8 字节 payload 的 XOR |

常量：`START_LENGTH=2`、`TYPE_LENGTH=1`、`PACKET_DATA_LENGTH=8`、
`CHECKSUM_LENGTH=1`，`RAW_PACKET_LENGTH=12`。

### 2.1 为什么先组栈上整帧

`pkt[12]` 在栈上完成后再整体 `memcpy` 到 `uart_txbuf`，避免 DMA 启动时读到
“校验和还没写”的半帧。指定初始化器让 `data` 未用字节为 0，接收端无需长度字段。

### 2.2 校验和

`calc_checksum(packet->data, 8)` 是 XOR8，**不覆盖 type 与同步字**。
单比特错误可检，偶数个同位置翻转不可检；type 翻转可能被当成另一类消息。
对隔离短链路够用，但不是强校验。

## 3. 入队

```c
void queue_packet(const uint8_t *data, enum packet_type_e packet_type, int length) {
    uart_packet_t packet = {.type = packet_type};
    memcpy(packet.data, data, length);

    queue_try_add(&global_state.uart_tx_queue, &packet);
}
```

（L54-L59）

- `{.type=...}` 使 data/checksum 全零；
- `length` 无上限检查，当前调用点最大 8；
- 队列满（256）静默丢弃，所有调用方不检查返回值——有意为之，避免在 USB 回调里阻塞。

## 4. 发送任务

```c
void process_uart_tx_task(device_t *state) {
    uart_packet_t packet = {0};

    if (dma_channel_is_busy(state->dma_tx_channel))
        return;

    if (!queue_try_remove(&state->uart_tx_queue, &packet))
        return;

    write_raw_packet(uart_txbuf, &packet);
    dma_channel_transfer_from_buffer_now(state->dma_tx_channel, uart_txbuf, RAW_PACKET_LENGTH);
}
```

（L81-L92）

- DMA 忙则返回，防止覆盖仍在发送的 `uart_txbuf`；
- 一次只发一帧，给收包任务让路；
- 先 remove 再启动 DMA：DMA 已接管缓冲，帧必发完；启动前已检查忙。

12B @3.6864Mbaud 8N1 ≈ 32.55µs。core0 每圈都调用，吞吐远超需求。

## 5. 分发表

```c
const uart_handler_t uart_handler[] = {
    /* Core functions */
    {.type = KEYBOARD_REPORT_MSG, .handler = handle_keyboard_uart_msg},
    {.type = MOUSE_REPORT_MSG, .handler = handle_mouse_abs_uart_msg},
    {.type = OUTPUT_SELECT_MSG, .handler = handle_output_select_msg},
```

（L98-L102，节选）

共 22 项，覆盖 23 个枚举值（`GET_VAL`/`SET_VAL` 共用 `handle_api_msgs`）。
分组：核心键鼠、盒控制、配置、固件、心跳/代理。

```c
void process_packet(uart_packet_t *packet, device_t *state) {
    if (!verify_checksum(packet))
        return;

    for (int i = 0; i < ARRAY_SIZE(uart_handler); i++) {
        if (uart_handler[i].type == packet->type) {
            uart_handler[i].handler(packet, state);
            return;
        }
    }
}
```

（L139-L149）

- 校验失败静默丢弃；
- 线性查找 O(22)，非热路径（每帧一次）；
- 未知 type 无动作，不报错。

## 6. 队列与背压

| 项 | 值 |
|----|----|
| 队列深度 | 256 |
| 元素大小 | 10B（packed） |
| 约占用 | 2.5KB |
| 生产者 | core0（切换/LED/配置响应）、core1（心跳/固件请求） |
| 消费者 | core0 `process_uart_tx_task()` |

满队列策略：丢弃最新包。对键鼠是丢一帧；对控制消息可能丢一次同步，
下一次状态变化/心跳会重发；对固件请求，`request_byte()` 下一轮会重试。

## 7. 相关接收代码（不在本文件）

| 功能 | 位置 |
|------|------|
| UART 初始化 3.6864Mbaud | `setup.c:serial_init()` L46-L65 |
| DMA RX 环形缓冲 | `setup.c:configure_rx_dma()` L183-L226 |
| 写指针换算 | `tasks.c:packet_receiver_task()` L377-L396 |
| 找同步字 | `utils.c:is_start_of_packet()` L175-L177 |
| 环形差值 | `utils.c:get_ptr_delta()` L179-L191 |
| 取 10 字节 | `utils.c:fetch_packet()` L193-L203 |
| XOR 校验 | `utils.c:calc_checksum()` L37-L45 |

## 8. 坑点

1. **文件头中文导读写“921600 波特率”是错的**，实际 `serial.h` 为 3,686,400。
   以代码为准；这是导入注释的遗留。
2. **XOR8 + 不覆盖 type**：协议最薄弱处。强干扰环境建议 CRC16。
3. **误同步吞帧**：payload 里出现 `AA 55` 时，`fetch_packet()` 无条件前进 12B，
   可能把紧随其后的真实帧头吞掉。改进方向是校验失败只前进 1B。
4. **`queue_packet` 的 `length` 无检查**：新增调用点若传 >8 会写坏栈上的
   `uart_packet_t`，进而发出脏帧。
5. **`send_value` 只适合单字节**：多字节结构（如 `border_size_t`）必须用
   `queue_packet()`，否则只发第一个字节。
6. **心跳每秒一帧**：若 UART 完全断链，对端只能通过“心跳消失”间接发现；
   `handle_heartbeat_msg()` 不做超时告警，只在版本更高时触发升级。
