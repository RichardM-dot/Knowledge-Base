# DeskHop 源码笔记：utils.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/utils.c`（共 289 行）

> 全工程胶水层：UART 校验、CRC32、Flash/配置读写、重启、BOOTSEL 检测、
> 环形缓冲取包、配置端点白名单、调试打印。

相关文档：[02-板间UART协议精读](../../../references/firmware/DeskHop/Docs/02-板间UART协议精读.md)、
[06-PIO与底层外设](../../../references/firmware/DeskHop/Docs/06-PIO与底层外设.md)

## 1. 函数总表

| 函数 | 行号 | 分组 | 职责 |
|------|------|------|------|
| `calc_checksum` | L37-L45 | 校验 | XOR8 |
| `verify_checksum` | L47-L50 | 校验 | 比较 8B payload XOR |
| `crc32_iter` | L52-L55 | 校验 | 单字节 CRC32 迭代 |
| `calc_crc32` | L57-L65 | 校验 | 整块 CRC32（首尾取反） |
| `calculate_firmware_crc32` | L67-L69 | 校验 | 运行区除最后扇区 |
| `wipe_config` | L75-L79 | Flash | 擦配置扇区 |
| `write_flash_page` | L81-L91 | Flash | 扇区首页擦除 + 写 256B |
| `load_config` | L93-L115 | 配置 | 三重校验 + 默认回退 |
| `save_config` | L117-L130 | 配置 | 计算 CRC + 写 Flash |
| `reset_config_timer` | L132-L135 | 配置 | 5 分钟超时 |
| `_configure_flash_cs` | L137-L141 | BOOTSEL | 覆盖 QSPI CS 输出使能 |
| `is_bootsel_pressed` | L143-L159 | BOOTSEL | 高阻 CS 后读引脚 |
| `request_byte` | L161-L169 | 升级 | 发 REQUEST_BYTE |
| `reboot` | L171-L173 | 复位 | PPB AIRCR 系统复位 |
| `is_start_of_packet` | L175-L177 | 收包 | 比较 AA 55 |
| `get_ptr_delta` | L179-L191 | 收包 | 环形写指针差值 |
| `fetch_packet` | L193-L203 | 收包 | 取 10B 到 in_packet |
| `validate_packet` | L207-L229 | 安全 | 配置端点白名单 |
| `cdc_write_str` | L238-L268 | 调试 | 非阻塞写 CDC |
| `dh_debug_printf` | L270-L283 / L285-L287 | 调试 | printf 包装 |

## 2. 校验

```c
uint8_t calc_checksum(const uint8_t *data, int length) {
    uint8_t checksum = 0;

    for (int i = 0; i < length; i++) {
        checksum ^= data[i];
    }

    return checksum;
}
```

（L37-L45）

UART 帧用 XOR8，只覆盖 8 字节 payload。固件镜像用 CRC32：

```c
uint32_t crc32_iter(uint32_t crc, const uint8_t byte) {
    return crc32_lookup_table[(byte ^ crc) & 0xff] ^ (crc >> 8);
}
```

（L52-L55）

`calc_crc32()` 初值 0xffffffff，最后取反（标准 CRC32/ISO-HDLC）。
`calculate_firmware_crc32()` 覆盖 `STAGING_IMAGE_SIZE - FLASH_SECTOR_SIZE`，
跳过最后 4KB 元数据。

## 3. 配置三重校验

```c
void load_config(device_t *state) {
    const config_t *config   = ADDR_CONFIG;
    config_t *running_config = &state->config;

    /* Load the flash config first, including the checksum */
    memcpy(running_config, config, sizeof(config_t));

    /* Calculate and update checksum, size without checksum */
    uint32_t checksum = calc_crc32((uint8_t *)running_config, offsetof(config_t, checksum));

    /* We expect a certain byte to start the config header */
    bool magic_header_fail = (running_config->magic_header != 0xB00B1E5);

    /* We expect the checksum to match */
    bool checksum_fail = (running_config->checksum != checksum);

    /* We expect the config version to match exactly, to avoid erroneous values */
    bool version_fail = (running_config->version != CURRENT_CONFIG_VERSION);

    /* On any condition failing, we fall back to default config */
    if (magic_header_fail || checksum_fail || version_fail)
        memcpy(running_config, &default_config, sizeof(config_t));
}
```

（L93-L115）

三个条件缺一不可：

1. `magic_header == 0xB00B1E5`：区分“空白 Flash”与有效配置；
2. `checksum` 匹配：`offsetof(config_t, checksum)` 之前的字节；
3. `version == CURRENT_CONFIG_VERSION(8)`：结构体布局变化时强制回默认。

任一失败 → 整个 `config_t` 替换为 `default_config`，不尝试部分修复。

## 4. Flash 写

```c
void write_flash_page(uint32_t target_addr, uint8_t *buffer) {
    /* Start of sector == first 256-byte page in a 4096 byte block */
    bool is_sector_start = (target_addr & 0xf00) == 0;

    uint32_t ints = save_and_disable_interrupts();
    if (is_sector_start)
        flash_range_erase(target_addr, FLASH_SECTOR_SIZE);

    flash_range_program(target_addr, buffer, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}
```

（L81-L91）

- `(addr & 0xf00)==0` 判断 4KB 扇区首页（低 12 位为 0）；
- 首页才擦除，避免每页都擦；
- 擦+写全程关中断：XIP 在擦写期间不可用；
- 一次写 256B，地址必须 256 对齐。

`save_config()` 把 `config_t` 拷进 `page_buffer`，剩余补零，再调用本函数。

## 5. 重启

```c
void reboot(void) {
    *((volatile uint32_t*)(PPB_BASE + 0x0ED0C)) = 0x5FA0004;
}
```

（L171-L173）

写 ARM Cortex-M 的 AIRCR 寄存器：`0x5FA0004` = VECTKEY(0x05FA)<<16 | SYSRESETREQ(bit2)。
比 `watchdog_reboot()` 更直接，但不会走 USB 断开回调；DeskHop 多数场景用
`reboot_requested` + 看门狗超时，给 USB 栈留收尾时间。

## 6. UART 收包辅助

```c
bool is_start_of_packet(device_t *state) {
    return (uart_rxbuf[state->dma_ptr] == START1 && uart_rxbuf[NEXT_RING_IDX(state->dma_ptr)] == START2);
}
```

（L175-L177）

```c
uint32_t get_ptr_delta(uint32_t current_pointer, device_t *state) {
    uint32_t delta;

    if (current_pointer >= state->dma_ptr)
        delta = current_pointer - state->dma_ptr;
    else
        delta = DMA_RX_BUFFER_SIZE - state->dma_ptr + current_pointer;

    /* Clamp to 12 bits since it can never be bigger */
    delta = delta & 0x3FF;

    return delta;
}
```

（L179-L191）

```c
void fetch_packet(device_t *state) {
    uint8_t *dst = (uint8_t *)&state->in_packet;

    for (int i = 0; i < RAW_PACKET_LENGTH; i++) {
        /* Skip the header preamble */
        if (i >= START_LENGTH)
            dst[i - START_LENGTH] = uart_rxbuf[state->dma_ptr];

        state->dma_ptr = NEXT_RING_IDX(state->dma_ptr);
    }
}
```

（L193-L203）

- `get_ptr_delta` 处理环形回绕，`&0x3FF` 与 1024B ring 对应；
- `fetch_packet` 跳过 2B 同步字，把 10B（type+data+checksum）拷进 `in_packet`；
- 前进 12 步后 `dma_ptr` 指向下一帧起点。

## 7. 配置端点白名单

```c
bool validate_packet(uart_packet_t *packet) {
    const enum packet_type_e ALLOWED_PACKETS[] = {
        FLASH_LED_MSG,
        GET_VAL_MSG,
        GET_ALL_VALS_MSG,
        SET_VAL_MSG,
        WIPE_CONFIG_MSG,
        SAVE_CONFIG_MSG,
        REBOOT_MSG,
        PROXY_PACKET_MSG,
    };
    uint8_t packet_type = packet->type;

    /* Proxied packets are encapsulated in the data field, but same rules apply */
    if (packet->type == PROXY_PACKET_MSG)
        packet_type = packet->data[0];
```

（L207-L222）

- 只允许 8 种类型从电脑的 vendor 端点进入；
- 代理包检查内层类型；
- 不依赖“report id 没暴露”这种隐含安全，显式白名单。

## 8. 坑点

1. **XOR8 只覆盖 payload**：type 翻转可能被当成另一类消息。
2. **`load_config()` 从 XIP 直接 memcpy**：`ADDR_CONFIG` 是链接脚本符号，必须
   指向 Flash 映射地址；地址错会读到代码区。
3. **`write_flash_page()` 假设地址 256 对齐**：固件升级按页递增，满足；
   配置写入用 `ADDR_CONFIG`（4KB 对齐），也满足。
4. **`reboot()` 不等待 USB 断开**：调用后主机可能看到设备突然消失；
   对“立即复位”场景可接受。
5. **`is_bootsel_pressed()` 关中断 + 20µs 延时**：不能在中断上下文调用。
6. **`get_ptr_delta()` 的 0x3FF 掩码**：刚好写满 1024B 时返回 0，理论丢数据。
7. **`validate_packet()` 不校验长度**：长度由 `tud_hid_set_report_cb()` 的
   `bufsize==12` 保证；其他调用点需自行保证。
8. **`dh_debug_printf()` 在非 DH_DEBUG 构建里是空函数**：不会输出，但参数仍被
   求值；不要在参数里写有副作用的表达式。
