# DeskHop 源码笔记：tasks.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/tasks.c`（共 396 行）

> 协作式调度器 + 系统级任务集：喂狗、USB 轮询、屏保、心跳、固件搬运、UART 收包。

相关文档：[01-启动流程与双核调度](../../../references/firmware/DeskHop/Docs/01-启动流程与双核调度.md)、
[02-板间UART协议精读](../../../references/firmware/DeskHop/Docs/02-板间UART协议精读.md)

## 1. 函数总表

| 函数 | 行号 | 核 | 频率 | 职责 |
|------|------|----|------|------|
| `task_scheduler` | L43-L51 | 0/1 | — | 到点执行一次，未到点返回 |
| `kick_watchdog_task` | L69-L82 | 0 | 30Hz | 检查 core1 心跳后喂狗 |
| `usb_device_task` | L96-L98 | 0 | `_TOP` | `tud_task()` |
| `usb_host_task` | L109-L113 | 1 | `_TOP` | `tuh_inited() ? tuh_task()` |
| `screensaver_pong` | L122-L144 | 1 | 按需 | PONG 模式一步 |
| `screensaver_jitter` | L147-L155 | 1 | 按需 | JITTER 模式一步 |
| `screensaver_task` | L170-L225 | 1 | 120Hz | 屏保四道闸门 + 限速 |
| `heartbeat_output_task` | L239-L283 | 1 | 1Hz | 心跳 + 配置模式超时 |
| `process_hid_queue_task` | L285-L321 | 0 | 1kHz | vendor/consumer/system 发送 |
| `firmware_upgrade_task` | L323-L361 | 1 | 4kHz | 逐 4B 拉固件、写页、CRC |
| `packet_receiver_task` | L377-L396 | 1 | `_TOP` | UART 环缓冲扫描组包 |

## 2. 调度器

```c
void task_scheduler(device_t *state, task_t *task) {
    uint64_t current_time = time_us_64();

    if (current_time < task->next_run)   /* 还没到点，直接返回，不空转占用 CPU */
        return;

    task->next_run = current_time + task->frequency;   /* 用"当前时间+周期"而不是"+=周期"，避免落后时连环补跑 */
    task->exec(state);   /* 执行任务本体（任务必须快速返回，绝不能阻塞） */
}
```

（L43-L51）

- `uint64_t` 微秒：避免 32 位 71.6 分钟回绕。
- `current + period`：错过的节拍直接放弃，保证频率上限，不补跑。
- `frequency=0`（`_TOP()`）：`next_run=now`，下一圈必执行。

复杂度 O(1)，每核每圈对 N 个任务各调一次，N=6 或 7。

## 3. 看门狗互检

```c
void kick_watchdog_task(device_t *state) {
    /* Read the timer AFTER duplicating the core1 timestamp,
       so it doesn't get updated in the meantime. */
    uint32_t core1_last_loop_pass = state->core1_last_loop_pass;
    uint32_t current_time         = time_us_32();

    /* If a reboot is requested, we'll stop updating watchdog */
    if (state->reboot_requested)
        return;

    /* If core1 stops updating the timestamp, we'll stop kicking the watchog and reboot */
    if ((uint32_t)(current_time - core1_last_loop_pass) < CORE1_HANG_TIMEOUT_US)
        watchdog_update();
}
```

（L69-L82）

时序正确性：

1. 先复制 `core1_last_loop_pass`，再读当前时间 → 差值只会偏大，不会把死核看成活核。
2. `uint32_t` 无符号减法处理 71 分钟回绕，只要差值 < 2^31 就正确。
3. 阈值 500ms（`CORE1_HANG_TIMEOUT_US`），与看门狗超时相同；core1 停止后约 0.5~0.53s 复位。
4. `reboot_requested` 短路：主动重启时停止喂狗，500ms 后硬件复位。

## 4. 固件搬运状态机

```c
void firmware_upgrade_task(device_t *state) {
    if (!state->fw.upgrade_in_progress || !state->fw.byte_done)
        return;

    if (queue_is_full(&state->uart_tx_queue))
        return;

    /* If we're on the last element of the current page, page is done - write it.
       Address zero is not the end of a page: nothing has arrived yet */
    /* 地址低字节为 0 说明刚跨过一页边界（0 不是页首：还没收到任何字节），
     * 把攒满的 256 字节页缓冲写进 Flash */
    if (TU_U32_BYTE0(state->fw.address) == 0x00 && state->fw.address != 0) {

        uint32_t page_start_addr = (state->fw.address - 1) & 0xFFFFFF00;
        write_flash_page((uint32_t)ADDR_FW_RUNNING + page_start_addr - XIP_BASE, state->page_buffer);
    }
```

（L323-L338）

节拍：`request_byte()` 置 `byte_done=false` 并发送请求；对端回包后
`handle_response_byte_msg()` 写 `page_buffer[offset]`、`address += 4`、`byte_done=true`；
本任务下一轮继续。4kHz 只是上限，实际受 UART 往返限制。

页写条件：地址低字节为 0 且地址非 0。`(address-1) & 0xFFFFFF00` 得到上一页起始。
`ADDR_FW_RUNNING + page_start - XIP_BASE` 把 XIP 地址转成 Flash 偏移。

结束条件：

```c
    if (state->fw.address >= STAGING_IMAGE_SIZE) {   /* 收满整个镜像，收尾 */
        state->fw.upgrade_in_progress = 0;
        state->fw.checksum = ~state->fw.checksum;   /* CRC32 收尾取反（规范如此） */
```

（L341-L343）

CRC 不匹配时擦除运行区首扇区并 `reset_usb_boot()`，把设备交给 ROM 恢复；
匹配则更新 `_running_fw` 并请求重启。

## 5. UART 收包任务

```c
void packet_receiver_task(device_t *state) {
    /* 读 DMA 剩余计数换算出当前写入位置（环形缓冲里的下标） */
    uint32_t current_pointer
        = (uint32_t)DMA_RX_BUFFER_SIZE - dma_channel_hw_addr(state->dma_rx_channel)->transfer_count;
    uint32_t delta = get_ptr_delta(current_pointer, state);   /* 自上次处理位置到写入位置的新字节数 */

    /* If we don't have enough characters for a packet, skip loop and return immediately */
    while (delta >= RAW_PACKET_LENGTH) {   /* 至少凑够一整帧（12B）才值得扫描 */
        if (is_start_of_packet(state)) {
            fetch_packet(state);
            process_packet(&state->in_packet, state);
            return;
        }

        /* No packet found, advance to next position and decrement delta */
        state->dma_ptr = NEXT_RING_IDX(state->dma_ptr);
        delta--;
    }
}
```

（L377-L396）

- 写指针 = `1024 - transfer_count`，直接读 DMA 寄存器，零拷贝。
- 逐字节找 `0xAA 0x55`；找到即取 12B，处理一帧后 return。
- 一次一帧形成背压，剩余数据下一圈继续；粘包天然被拆分。
- 没找到则 `dma_ptr` 前进 1、`delta--`，滑动窗口继续找。

## 6. 屏保

### 6.1 模式与限速

```c
    const uint32_t delays[] = {
        0,        /* DISABLED, unused index 0 */
        5000,     /* PONG, move mouse every 5 ms for a high framerate */
        10000000, /* JITTER, once every 10 sec is more than enough */
    };
```

（L171-L175）

`_HZ(120)` 任务周期 8.33ms，所以 PONG 实际步进 8.33ms，不是 5ms。JITTER 10s 一步。

### 6.2 四道闸门

| 闸门 | 行号 | 条件 |
|------|------|------|
| 1 启用 | L181 | `mode != DISABLED` |
| 2 空闲够久 | L185 | `inactivity >= idle_time_us` 且 mode 合法 |
| 3 未超最大时长 | L189 | `max_time_us==0` 或未超 |
| 4 输出限制 | L194 | `only_if_inactive` 时本板不能是活跃输出 |
| 5 USB 就绪 | L202 | `tud_ready()` |

### 6.3 PONG/JITTER

`pong` 用 static 的 `report` 与 `dx=20/dy=25`，撞到 [0,32767] 边界反向；
`jitter` 每次把 Y 取反，产生 ±2 像素往复。

## 7. 心跳

`heartbeat_output_task()`（L239-L283）：

1. 升级中直接返回；
2. 配置模式下检查 5 分钟超时 → `reboot()`；否则 `blink_led()` 每秒提醒；
3. `DH_DEBUG` 下检测 BOOTSEL 按键；
4. 组包：

```c
    uart_packet_t packet = {
        .type = HEARTBEAT_MSG,
        .data16 = {
            [0] = state->_running_fw.version,
            [2] = state->active_output,
        },
    };

    queue_try_add(&global_state.uart_tx_queue, &packet);   /* 非阻塞入发送队列，满了就丢（下秒再发，心跳丢一拍无妨） */
```

（L260-L268）

## 8. hid 发送任务

`process_hid_queue_task()`（L285-L321）三段式：

1. `queue_try_peek()` 取队头但不移除；
2. `tud_hid_n_ready(instance)` 未就绪则返回；
3. boot 协议下丢弃（无法带 report id）；否则 `tud_hid_n_report()`，成功才 `queue_try_remove()`。

## 9. 坑点

- **任务内禁止 `sleep_ms()`**：会让同核后续任务全部停摆。
- **`screensaver_task` 的 delays 与任务频率不一致**：PONG 实际 8.33ms，注释的 5ms 是意图。
- **`firmware_upgrade_task` 每轮只发一个请求**：依赖对端回包推进，若对端丢包，
  `byte_done` 永远 false，升级卡死。没有超时重试——靠心跳重新触发只在
  `upgrade_in_progress==false` 时有效，卡死状态下不会重试。
- **`packet_receiver_task` 找到同步字就取 12B**：误同步会吞掉紧随其后的真实帧（见 UART 笔记）。
- **`usb_host_task` 的 `tuh_inited()` 检查不能删**：未初始化时 `tuh_task()` 会访问空链表。
