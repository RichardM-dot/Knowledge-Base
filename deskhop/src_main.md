# DeskHop 源码笔记：main.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/main.c`（共 120 行）

> 程序入口与双核任务表。DeskHop 没有 RTOS，两个核各跑一个 `while(true)`，
> 用 `task_scheduler()` 做协作式周期调度。

相关文档：[00-总览与双板架构](../../../references/firmware/DeskHop/Docs/00-总览与双板架构.md)、
[01-启动流程与双核调度](../../../references/firmware/DeskHop/Docs/01-启动流程与双核调度.md)

## 1. 全局对象

| 对象 | 行号 | 类型 | 说明 |
|------|------|------|------|
| `global_state` | L36 | `device_t` | 全局唯一状态单例，两核共享 |
| `device` | L37 | `device_t *` | 指向 `global_state` 的便捷指针 |
| `_firmware_metadata` | L40-L42 | `firmware_metadata_t` | 放在 `.section_metadata` 段，版本 0x0001 |

```c
device_t global_state     = {0};     /* 全局唯一状态单例：两核共享的所有状态都在这里 */
device_t *device          = &global_state;

/* 固件元数据放在链接脚本指定的独立段里，升级固件时按段定位、按版本兼容 */
firmware_metadata_t _firmware_metadata __attribute__((section(".section_metadata"))) = {
    .version = 0x0001,
};
```

（L36-L42）

`.section_metadata` 由链接脚本 `misc/memory_map.ld` 安排；构建后 `objcopy --update-section`
把 `crc32.py` 生成的版本/CRC 写进去（`CMakeLists.txt` L146-L148）。这样固件自身携带
“我是哪个版本、我的 CRC 是多少”，对端升级时可按版本比较。

## 2. 函数表

| 函数 | 行号 | 职责 |
|------|------|------|
| `main` | L56-L89 | core0 入口：初始化 → 启动 core1 → 开看门狗 → 永续调度 |
| `core1_main` | L100-L119 | core1 入口：刷新心跳 + 永续调度 |

## 3. main() 逐段

### 3.1 core0 任务表

| 序号 | 任务 | 频率 | 周期 |
|------|------|------|------|
| 0 | `usb_device_task` | `_TOP` | 每圈 |
| 1 | `kick_watchdog_task` | `_HZ(30)` | 33.3ms |
| 2 | `process_kbd_queue_task` | `_HZ(2000)` | 500µs |
| 3 | `process_mouse_queue_task` | `_HZ(2000)` | 500µs |
| 4 | `process_hid_queue_task` | `_HZ(1000)` | 1ms |
| 5 | `process_uart_tx_task` | `_TOP` | 每圈 |

```c
    static task_t tasks_core0[] = {
        [0] = {.exec = &usb_device_task,          .frequency = _TOP()},      // .-> USB device task, needs to run as often as possible   /* USB设备端（TinyUSB）：维护与电脑A/B的键鼠枚举与上报 */
        [1] = {.exec = &kick_watchdog_task,       .frequency = _HZ(30)},     // | Verify core1 is still running and if so, reset watchdog timer  /* 喂狗前先检查 core1 心跳时间戳 */
        [2] = {.exec = &process_kbd_queue_task,   .frequency = _HZ(2000)},   // | Check if there were any keypresses and send them  /* 从键盘队列取按键，经当前活跃 USB 端口上报 */
        [3] = {.exec = &process_mouse_queue_task, .frequency = _HZ(2000)},   // | Check if there were any mouse movements and send them  /* 同上，鼠标移动 */
        [4] = {.exec = &process_hid_queue_task,   .frequency = _HZ(1000)},   // | Check if there are any packets to send over vendor link  /* vendor 自定义通道：给 Web 配置器回数据 */
        [5] = {.exec = &process_uart_tx_task,     .frequency = _TOP()},      // | Check if there are any packets to send over UART  /* 把要发给另一块板子的包排队发出 */
    };                                                                       // `----- then go back and repeat forever
```

（L59-L66）

设计意图：USB 设备端时序最敏感，排第一；喂狗第二，防止输入洪峰饿死看门狗；
键鼠队列 2kHz 保证 1ms 内必有一次发送机会；UART 发送最后，它在本板非活跃时才有大流量。

### 3.2 启动顺序

```c
    sleep_ms(10);
    initial_setup(device);
    set_active_output(device, OUTPUT_A);
    multicore_launch_core1(core1_main);
    watchdog_enable(WATCHDOG_TIMEOUT, WATCHDOG_PAUSE_ON_DEBUG);
```

（L70-L83，节选）

四步不可交换：

1. `initial_setup()` 建队列、定角色、起 UART/USB/DMA；
2. `set_active_output()` 给两板一个确定的初始输出（A）；
3. 共享状态就绪后才启动 core1；
4. 最后开看门狗，避免初始化耗时被误判。

### 3.3 core1 任务表

| 序号 | 任务 | 频率 | 周期 |
|------|------|------|------|
| 0 | `usb_host_task` | `_TOP` | 每圈 |
| 1 | `packet_receiver_task` | `_TOP` | 每圈 |
| 2 | `led_blinking_task` | `_HZ(30)` | 33.3ms |
| 3 | `led_sync_task` | `_HZ(30)` | 33.3ms |
| 4 | `screensaver_task` | `_HZ(120)` | 8.33ms |
| 5 | `firmware_upgrade_task` | `_HZ(4000)` | 250µs |
| 6 | `heartbeat_output_task` | `_HZ(1)` | 1s |

```c
        [0] = {.exec = &usb_host_task,           .frequency = _TOP()},       // .-> USB host task, needs to run as often as possible  /* PIO-USB 主机轮询：读键鼠 HID 报告的入口 */
        [1] = {.exec = &packet_receiver_task,    .frequency = _TOP()},       // | Receive data over serial from the other board  /* 解析另一块板子发来的 UART 包 */
        [2] = {.exec = &led_blinking_task,       .frequency = _HZ(30)},      // | Check if LED needs blinking  /* 状态灯闪烁逻辑 */
        [3] = {.exec = &led_sync_task,           .frequency = _HZ(30)},      // | Sync LED state if needed  /* 两板 LED 状态同步 */
        [4] = {.exec = &screensaver_task,        .frequency = _HZ(120)},     // | Handle "screensaver" movements  /* 屏保模式下模拟鼠标移动 */
        [5] = {.exec = &firmware_upgrade_task,   .frequency = _HZ(4000)},    // | Send firmware to the other board if needed  /* 固件升级：把收到的固件块转发/写入 */
        [6] = {.exec = &heartbeat_output_task,   .frequency = _HZ(1)},       // | Output periodic heartbeats  /* 每秒心跳包，供对端判断本板存活 */
```

（L102-L108）

每圈第一件事：

```c
        device->core1_last_loop_pass = time_us_32();   /* 心跳时间戳：core0 喂狗前会检查它是否足够新 */
```

（L114）

心跳在任务之前刷新：只要循环还在转就算活着。看门狗关心“核是否卡死”，
不关心“某任务是否超时”。

## 4. 与其他模块

| 方向 | 模块 | 交互 |
|------|------|------|
| 调用 | `setup.c` | `initial_setup()`、`core1_main` 由 `multicore_launch_core1` 启动 |
| 调用 | `tasks.c` | `task_scheduler()` 与全部任务 |
| 调用 | `handlers.c` | `set_active_output()` |
| 共享 | 全工程 | `global_state` / `device` 指针 |
| 被引用 | `pinout.h` | `BOARD_ROLE` 宏展开为 `global_state.board_role` |

## 5. 坑点

- **`main()` 里 `sleep_ms(10)` 是阻塞的**：此时 core1 还没启动，无影响；但在任务里
  绝不能再有这种阻塞调用。
- **`multicore_launch_core1()` 只能调一次**：重复调用会 hard fault；`initial_setup()`
  里先 `multicore_reset_core1()` 是为了异常复位后能重新启动。
- **`_TOP()` 不是“最高优先级”**：它只是 period=0；真正的顺序由数组下标决定。
- **`_firmware_metadata.version` 当前是 0x0001**，而构建系统把版本写进另一处
  `.section_metadata`（`crc32.py`）；两处语义要对齐，否则心跳版本比较可能失真。
- **core1 任务表没有 `process_hid_queue_task`**：vendor/consumer/system 的发送
  全部集中在 core0，因为 TinyUSB device 只能在一个核上跑。
