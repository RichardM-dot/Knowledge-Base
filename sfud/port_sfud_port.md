# sfud_port.c —— SPI 移植层样例

> 对应源文件：`references/filesystems/SFUD/sfud/port/sfud_port.c`（共 178 行，含 A 级中文深注）
> 上游：armink/SFUD v1.1.0 官方移植骨架（空实现，供填空）
>
> **互链：** [inc_sfud_def.md](inc_sfud_def.md)（sfud_spi 结构与 RETRY 宏） · [src_sfud.md](src_sfud.md)（调用方） · Docs：[02-SPI移植层](../../../references/filesystems/SFUD/docs/02-SPI移植层.md) · [05-实测与移植指南](../../../references/filesystems/SFUD/docs/05-实测与移植指南.md)

---

## 1. 文件职责

SFUD 全部平台相关性的落点：SPI 收发、QSPI 快读、总线锁、日志输出、移植初始化。本文件是"填空骨架"——四个函数体是空的或只有 printf，真实实现参考 demo 四平台。移植 = 回答五个问题（见文件头导读块，sfud_port.c:1-16）。

## 2. 逐函数表（5 个函数 + 2 个全局）

| 符号 | 行号 | 必填? | 一句话职责 |
|---|---|---|---|
| `log_buf[256]` | :46 | — | 日志格式化共享静态缓冲 |
| `spi_write_read` | :61 | **必填** | 一帧事务：先写 write_size 字节紧接读 read_size 字节 |
| `qspi_read` | :86 | QSPI 必填 | 按 read_cmd_format 发多线快读 |
| `sfud_spi_port_init` | :106 | **必填** | 挂函数指针 + retry 配置（hardware_init 第一站） |
| `sfud_log_debug` | :144 | **必填** | SFUD_DEBUG 出口（带文件行号） |
| `sfud_log_info` | :168 | **必填** | SFUD_INFO 出口（始终编译） |

另有可选的 `spi_lock/spi_unlock`（样例未定义，真机 demo 提供）。

## 3. 关键摘录与精读

### 3.1 spi_write_read 的契约（sfud_port.c:61-77）

```c
static sfud_err spi_write_read(const sfud_spi *spi, const uint8_t *write_buf, size_t write_size, uint8_t *read_buf,
        size_t read_size) {
    sfud_err result = SFUD_SUCCESS;
    uint8_t send_data, read_data;

    /**
     * add your spi write and read code
     */

    return result;
}
```

三条硬契约：

1. **CS 全程不释放**：SFUD 典型调用是"发 0x9F + 接 3 字节"，命令上下文靠 CS 维持；
2. **支持三组合**：只写（擦/写命令）、只读（少）、先写后读（读 ID/状态/数据）；
3. **阻塞返回**：返回时数据必须到位，错误用非 0 错误码上抛。

真机模板（语义，摘自 stm32l475 demo）：`CS 低 → 发 → 收 → CS 高`，`user_data` 携带 `{QSPI_HandleTypeDef*, CS_GPIOx, CS_PIN}`。

### 3.2 qspi_read 的参数映射（sfud_port.c:86-93）

`instruction_lines / address_lines / data_lines` 的取值 0/1/2/4 映射 HAL 的 `QSPI_xxx_NONE / 1_LINE / 2_LINES / 4_LINES`；`dummy_cycles` 原样传入；`address_size` 要从 cmd_format 取（**不要硬编码 24 位**，>16MB 芯片是 32 位——l475 demo 硬编码的坑记录在 Docs 05）。

### 3.3 port_init 的最小必做项（sfud_port.c:106-125）

```c
    /**
     * add your port spi bus and device object initialize code like this:
     * 1. rcc initialize
     * 2. gpio initialize
     * 3. spi device initialize
     * 4. flash->spi and flash->retry item initialize
     *    flash->spi.wr = spi_write_read; //Required
     *    flash->spi.qspi_read = qspi_read; //Required when QSPI mode enable
     *    flash->spi.lock = spi_lock;
     *    flash->spi.unlock = spi_unlock;
     *    flash->spi.user_data = &spix;
     *    flash->retry.delay = null;
     *    flash->retry.times = 10000; //Required
     */
```

官方注释即清单：`wr` 与 `retry.times` 是 Required。`retry.times` 建议给 60*10000（覆盖整片擦，量化见 [src_sfud.md](src_sfud.md) 3.5）。多设备时按 `flash->spi.name` 分发挂接。

### 3.4 日志函数的两个细节（sfud_port.c:144-178）

```c
    /* args point to the first variable parameter */
    va_start(args, format);
    printf("[SFUD](%s:%ld) ", file, line);
    /* must use vprintf to print */
    vsnprintf(log_buf, sizeof(log_buf), format, args);
```

- 必须用 `vsnprintf`（不是 vsprintf）——256B 上限防溢出；
- `log_buf` 是全库共享静态缓冲，多任务并发打印会互相覆盖，必要时移植层自加锁；
- `sfud_log_debug` 仅 `SFUD_DEBUG_MODE` 打开时被引用，关调试宏后可整体裁掉。

## 4. 移植核对清单

| 项 | 缺失后果 |
|---|---|
| `flash->spi.wr` | hardware_init 断言死循环（sfud.c:367） |
| `flash->retry.times` | wait_busy 0 次即超时，任何写擦必失败 |
| `flash->spi.lock/unlock` | 多任务/中断竞争下总线时序被打断 |
| `sfud_log_*` | 链接错误（SFUD_INFO 始终引用） |
| QSPI 的 `qspi_read` + `sfud_qspi_fast_read_enable` | 快读静默回退 0x03，无提速 |
