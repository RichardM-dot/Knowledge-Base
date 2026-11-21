# sfud.h —— 对外 API 声明

> 对应源文件：`references/filesystems/SFUD/sfud/inc/sfud.h`（共 205 行，含 B 级导读注释块）
> 上游：armink/SFUD v1.1.0
>
> **互链：** [inc_sfud_def.md](inc_sfud_def.md)（sfud_flash 结构契约） · [src_sfud.md](src_sfud.md)（全部实现） · Docs：[00-总览与架构](../../../references/filesystems/SFUD/docs/00-总览与架构.md)

---

## 1. 文件职责

用户唯一需要 include 的头：13 个函数声明，分三组。实现全部在 `../src/sfud.c`；本文件只做声明（B 级注释 = 文件头两块导读：原作者式简介 + API 契约补充块 sfud.h:9-27）。

## 2. API 逐条表

| 函数 | 行号 | 组 | 语义要点 |
|---|---|---|---|
| `sfud_init` | :71 | 生命周期 | 初始化设备表全部设备 |
| `sfud_device_init` | :80 | 生命周期 | 初始化单设备（可单独用） |
| `sfud_get_device` | :89 | 生命周期 | 按下标取设备，越界 NULL |
| `sfud_get_device_num` | :96 | 生命周期 | 设备总数 |
| `sfud_get_device_table` | :103 | 生命周期 | 注册表 const 视图 |
| `sfud_qspi_fast_read_enable` | :119 | 生命周期（QSPI） | 选快读指令；**必须在 device_init 之后** |
| `sfud_read` | :132 | 数据操作 | 任意长读，不限页 |
| `sfud_erase` | :145 | 数据操作 | 按擦除粒度对齐擦除 |
| `sfud_write` | :157 | 数据操作 | 纯写（只 1→0），不擦除 |
| `sfud_erase_write` | :169 | 数据操作 | 先擦后写组合（非原子） |
| `sfud_chip_erase` | :178 | 数据操作 | 整片擦 0xC7 |
| `sfud_read_status` | :188 | 状态寄存器 | 0x05 读 1 字节 |
| `sfud_write_status` | :199 | 状态寄存器 | 0x50/0x06 前导 + 0x01 写 |

## 3. 契约要点（对应文件头 API 契约块）

1. **顺序约束**：数据操作 API 前置条件 `init_ok == true`，未初始化调用被 `SFUD_ASSERT(flash->init_ok)` 拦截——注意断言失败是 `while(1)` 死循环而非返回错误（仅 `SFUD_DEBUG_MODE` 时启用）。
2. **写语义**：`sfud_write` 不擦除；目标区非 0xFF 会得到"新旧位与"。覆盖写语义用 `sfud_erase_write`。
3. **擦除对齐**：按 `chip.erase_gran` 对齐；起始扇区整块被擦（不对齐的代价见 Docs 03/05）。
4. **线程安全**：单设备内靠 `spi.lock` 串行；多设备并发写还需全局串行（static 命令缓冲约束，见 [src_sfud.md](src_sfud.md) 第 4 节）。
5. **错误码**：`sfud_err` 六种（0~5），定义在 sfud_def.h:225-232。

## 4. 摘录：QSPI 快读声明的条件编译（sfud.h:105-119）

```c
#ifdef SFUD_USING_QSPI
/**
 * Enbale the fast read mode in QSPI flash mode. Default read mode is normal SPI mode.
 *
 * it will find the appropriate fast-read instruction to replace the read instruction(0x03)
 * fast-read instruction @see SFUD_FLASH_EXT_INFO_TABLE
 *
 * @note When Flash is in QSPI mode, the method must be called after sfud_device_init().
 *
 * @param flash flash device
 * @param data_line_width the data lines max width which QSPI bus supported, such as 1, 2, 4
 *
 * @return result
 */
sfud_err sfud_qspi_fast_read_enable(sfud_flash *flash, uint8_t data_line_width);
#endif /* SFUD_USING_QSPI */
```

`@note` 里的"必须在 device_init 之后"是硬依赖：选指令要查 QSPI 扩展信息表，表按 JEDEC ID 三元组匹配，而 ID 要识别后才有。

## 5. 使用模板

```c
#include <sfud.h>

sfud_init();                                              /* 1. 库初始化（全表） */
const sfud_flash *table = sfud_get_device_table();        /* 2. 拿设备 */
sfud_erase_write(&table[0], 0, 4096, buf);                /* 3. 擦写 */
sfud_read(&table[0], 0, 4096, rbuf);                      /* 4. 读回 */
```

单设备可跳过 `sfud_init` 直接 `sfud_device_init(&my_flash)`（设备对象自行定义，chip 手填，见 README 2.3.5）。
