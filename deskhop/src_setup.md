# DeskHop 源码笔记：setup.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/setup.c`（共 288 行）

> 上电初始化：时钟、配置、角色探测、UART、队列、TinyUSB、DMA。所有初始化
> 顺序都有依赖，不能随意调整。

相关文档：[01-启动流程与双核调度](../../../references/firmware/DeskHop/Docs/01-启动流程与双核调度.md)、
[06-PIO与底层外设](../../../references/firmware/DeskHop/Docs/06-PIO与底层外设.md)

## 1. 函数总表

| 函数 | 行号 | 职责 |
|------|------|------|
| `serial_init` | L46-L65 | UART0 3.6864Mbaud + GPIO 复用 |
| `pio_usb_host_config` | L71-L84 | PIO-USB 配置 + `tuh_init(1)` |
| `board_autoprobe` | L101-L129 | 8 次上/下拉探测判定 A/B |
| `is_config_mode_active` | L136-L148 | 读 scratch 魔术字并清除 |
| `configure_tx_dma` | L158-L181 | UART TX 单通道 |
| `configure_rx_dma` | L183-L226 | UART RX ring + 控制通道链式 |
| `initial_setup` | L234-L286 | 总初始化 |

全局缓冲：

| 对象 | 行号 | 大小 |
|------|------|------|
| `uart_buffer_pointers` | L154 | 1 个指针 |
| `uart_rxbuf` | L155 | 1024B，1024 对齐 |
| `uart_txbuf` | L156 | 32B，32 对齐 |

## 2. initial_setup

```c
void initial_setup(device_t *state) {
    /* PIO USB requires a clock multiple of 12 MHz, setting to 120 MHz */
    set_sys_clock_khz(120000, true);

    /* Search the persistent storage sector in flash for valid config or use defaults */
    load_config(state);

    /* Init and enable the on-board LED GPIO as output */
    gpio_init(GPIO_LED_PIN);
    gpio_set_dir(GPIO_LED_PIN, GPIO_OUT);

    /* Check if we should boot in configuration mode or not */
    state->config_mode_active = is_config_mode_active(state);

    /* Detect which board we're running on */
    state->board_role = board_autoprobe();

    /* Initialize and configure UART */
    serial_init();

    /* Initialize keyboard and mouse queues */
    queue_init(&state->kbd_queue, sizeof(hid_keyboard_report_t), KBD_QUEUE_LENGTH);
    queue_init(&state->mouse_queue, sizeof(mouse_report_t), MOUSE_QUEUE_LENGTH);

    /* Initialize generic HID packet queue */
    queue_init(&state->hid_queue_out, sizeof(hid_generic_pkt_t), HID_QUEUE_LENGTH);

    /* Initialize UART queue */
    queue_init(&state->uart_tx_queue, sizeof(uart_packet_t), UART_QUEUE_LENGTH);
```

（L234-L262，节选）

顺序理由：

| 步骤 | 为什么必须在此 |
|------|----------------|
| 120MHz | PIO-USB 要求 12MHz 整数倍；改时钟会使之前配置的 UART 波特率失效 |
| `load_config` | 配置里的 `led_mode`/`gaming_mode_default` 后续要用 |
| LED GPIO | 后续 `blink_led()`/`restore_leds()` |
| 配置模式判定 | 读并清 scratch，必须早于任何可能重启 |
| 角色探测 | UART 引脚选择依赖 `board_role` |
| `serial_init` | 用角色选 GP12/13 或 GP16/17 |
| 队列 | USB/任务启动前必须就绪 |
| `multicore_reset_core1` | 确保 core1 处于复位态 |
| `tud_init`/`tuh_init` | core1 启动前主机栈必须就绪 |
| DMA | 收包任务启动前必须就绪 |
| 心跳初值 | 给 core1 500ms 启动宽限 |

收尾：

```c
    /* Initialize and configure TinyUSB Device */
    tud_init(BOARD_TUD_RHPORT);

    /* Initialize and configure TinyUSB Host */
    pio_usb_host_config(state);

    /* Initialize and configure DMA */
    configure_tx_dma(state);
    configure_rx_dma(state);

    /* Load the current firmware info */
    state->_running_fw = _firmware_metadata;

    /* Update the core1 initial pass timestamp before enabling the watchdog */
    state->core1_last_loop_pass = time_us_32();
}
```

（L270-L285）

`core1_last_loop_pass = now` 很关键：若留 0，core0 喂狗任务会算出巨大差值，
认为 core1 已死，永远不喂狗，整机反复复位。

## 3. 板角色探测

```c
int board_autoprobe(void) {
    const bool probing_sequence[] = {true, false, false, true, true, false, true, false};
    const int seq_len = ARRAY_SIZE(probing_sequence);

    /* Set the pin as INPUT and initialize it */
    gpio_init(BOARD_A_RX);
    gpio_set_dir(BOARD_A_RX, GPIO_IN);

    for (int i=0; i<seq_len; i++) {
        if (probing_sequence[i])
            gpio_pull_up(BOARD_A_RX);
        else
            gpio_pull_down(BOARD_A_RX);

        /* Wait for value to settle */
        sleep_ms(3);

        /* Read the value */
        bool value = gpio_get(BOARD_A_RX);
        gpio_disable_pulls(BOARD_A_RX);

        /* If values mismatch at any point, means IC is connected and we're board A */
        if (probing_sequence[i] != value)
            return OUTPUT_A;
    }

    /* If it was just reading the pull up/down in all cases, pin is floating and we're board B */
    return OUTPUT_B;
}
```

（L101-L129）

原理：A 板的 RX 被隔离器输出驱动，不跟随内部上下拉；B 板对应引脚悬空，跟随。
8 次序列降低误判；最坏 24ms。

## 4. 配置模式判定

```c
bool is_config_mode_active(device_t *state) {
    /* Watchdog registers survive reboot (RP2040 datasheet section 2.8.1.1) */
    bool is_active = (watchdog_hw->scratch[5] == MAGIC_WORD_1 &&
                      watchdog_hw->scratch[6] == MAGIC_WORD_2);

    /* Remove, so next reboot it's no longer active */
    if (is_active)
        watchdog_hw->scratch[5] = 0;

    reset_config_timer(state);

    return is_active;
}
```

（L136-L148）

读后清 `scratch[5]`，使信标一次性。`reset_config_timer()` 设 5 分钟超时。

## 5. UART 初始化

```c
void serial_init() {
    /* Set up our UART with a default baudrate. */
    uart_init(SERIAL_UART, SERIAL_BAUDRATE);

    /* Set UART flow control CTS/RTS. We don't have these - turn them off.*/
    uart_set_hw_flow(SERIAL_UART, false, false);

    /* Set our data format */
    uart_set_format(SERIAL_UART, SERIAL_DATA_BITS, SERIAL_STOP_BITS, SERIAL_PARITY);

    /* Turn of CRLF translation */
    uart_set_translate_crlf(SERIAL_UART, false);

    /* We do want FIFO, will help us have fewer interruptions */
    uart_set_fifo_enabled(SERIAL_UART, true);

    /* Set the RX/TX pins, they differ based on the device role (A or B, check schematics) */
    gpio_set_function(SERIAL_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(SERIAL_RX_PIN, GPIO_FUNC_UART);
}
```

（L46-L65）

- 3,686,400 baud、8N1、无流控、FIFO 开；
- TX/RX 引脚按 `board_role` 选；
- `uart_set_translate_crlf(false)`：避免 0x0D/0x0A 被改写，二进制协议必须关。

## 6. DMA

### 6.1 TX

`configure_tx_dma()`（L158-L181）：8 位、读递增、写固定 UART DR、DREQ_UART0_TX、
初始 count=0 不启动。发送任务每次重新触发。

### 6.2 RX

`configure_rx_dma()`（L183-L226）：

- 数据通道：8 位、读固定 DR、写递增、ring 10 位（1024B）、DREQ_UART0_RX；
- 控制通道：32 位、写 `al2_write_addr_trig`、源为 `uart_buffer_pointers`；
- `chain_to(control)`：数据通道完成一轮触发控制通道重装写地址；
- 最后 `dma_channel_start(control)` 启动整条链。

缓冲必须 1024 字节对齐；`NEXT_RING_IDX` 的 0x3FF 掩码必须与 ring=10 一致。

## 7. 坑点

1. **初始化顺序不可交换**：先 `tud_init` 再 `set_sys_clock_khz` 会让 USB 时钟错乱。
2. **`board_autoprobe()` 依赖隔离器供电**：若隔离器未接/未供电，A 板也会被判成 B。
3. **`sleep_ms(3)`×8 = 24ms 启动延迟**：可接受，但不要在任务里复制这种阻塞。
4. **`core1_last_loop_pass` 必须在 `watchdog_enable` 前填当前时间**。
5. **`uart_rxbuf` 的 1024 对齐不能改小**：ring 模式硬要求。
6. **`dma_claim_unused_channel(true)` 失败即 panic**：RP2040 只有 12 通道。
7. **`pio_usb_host_config()` 里 `config` 是 static**：`tuh_configure` 只保存指针，
   若用栈变量会在返回后悬空。
