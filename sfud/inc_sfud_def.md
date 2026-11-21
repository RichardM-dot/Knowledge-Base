# sfud_def.h —— 内部公共定义（结构/宏/命令表）

> 对应源文件：`references/filesystems/SFUD/sfud/inc/sfud_def.h`（共 328 行，含 B 级导读注释块）
> 上游：armink/SFUD v1.1.0
>
> **互链：** [inc_sfud_h.md](inc_sfud_h.md)（API） · [inc_sfud_cfg.md](inc_sfud_cfg.md)（配置） · [src_sfud.md](src_sfud.md) / [src_sfud_sfdp.md](src_sfud_sfdp.md) / [port_sfud_port.md](port_sfud_port.md)（三个使用方） · Docs：[00-总览与架构](../../../references/filesystems/SFUD/docs/00-总览与架构.md)
> 关联文件：`sfud/inc/sfud_flash_def.h`（205 行，未加源码注释）——`sfud_flash_chip`/写模式枚举/厂商表/芯片表/QSPI 扩展表，被本文件 :37 include，强耦合一并在此解读。

---

## 1. 文件职责

被所有 .c 包含的"语言层"四块内容：日志/断言/重试宏、SPI 命令宏表、错误码与状态位、三个核心结构体（sfud_spi / sfud_sfdp / sfud_flash）。B 级注释只有文件头一块导读（sfud_def.h:1-27），宏体内部严禁插注释（多行宏反斜杠续行会断）。

## 2. 内容地图（行号为注释后实号）

| 内容 | 行号 | 备注 |
|---|---|---|
| `SFUD_DEBUG` / `SFUD_INFO` 宏 | :60-73 | DEBUG 版带文件行号，闭版空宏 |
| `SFUD_ASSERT` | :77 / :84 | 开启=打印+`while(1)`；关闭=空 |
| `SFUD_RETRY_PROCESS` | :94 | **体内含 break**，只能用在循环内 |
| `SFUD_SW_VERSION "1.1.0"` | :100 | |
| SPI 命令宏表 | :105-201 | 每个 `#ifndef` 可覆写 |
| `SFUD_SFDP_ERASE_TYPE_MAX_NUM 4` | :211 | JESD216 V1.0 擦除类型上限 |
| 状态寄存器位 | :216-220 | WIP=bit0 / WEL=bit1 / SRP=bit7 |
| `sfud_err` 枚举 | :225-232 | 六种错误码 |
| `sfud_qspi_read_cmd_format` | :238-247 | QSPI 快读格式（7 字段） |
| `sfud_sfdp` 结构 | :256-273 | SFDP 解析产物 |
| `sfud_spi` 结构 | :279-296 | 移植层函数指针表 |
| `sfud_flash` 结构 | :301-322 | 设备对象 |



## 3. 命令宏表（sfud_def.h:105-201，全部可用 #ifndef 覆写）

| 宏 | 值 | 用途 | 主要调用点 |
|---|---|---|---|
| `SFUD_CMD_WRITE_ENABLE` | 0x06 | WREN | set_write_enabled |
| `SFUD_CMD_WRITE_DISABLE` | 0x04 | WRDI | 各引擎收尾 |
| `SFUD_CMD_READ_STATUS_REGISTER` | 0x05 | RDSR | wait_busy |
| `SFUD_VOLATILE_SR_WRITE_ENABLE` | 0x50 | 易失 WREN | sfud_write_status |
| `SFUD_CMD_WRITE_STATUS_REGISTER` | 0x01 | WRSR | sfud_write_status |
| `SFUD_CMD_PAGE_PROGRAM` | 0x02 | 页编程 | page256_or_1_byte_write |
| `SFUD_CMD_AAI_WORD_PROGRAM` | 0xAD | AAI 写 | aai_write |
| `SFUD_CMD_ERASE_CHIP` | 0xC7 | 整片擦 | sfud_chip_erase |
| `SFUD_CMD_READ_DATA` | 0x03 | 普通读 | sfud_read / qspi 格式 |
| `SFUD_CMD_FAST_READ_DATA` | 0x0B | 快读 | sfud_read（FAST_READ 宏） |
| `SFUD_CMD_DUAL/QUAD_*` | 0x3B/0xBB/0x6B/0xEB | 多线快读 | QSPI 格式选择 |
| `SFUD_CMD_JEDEC_ID` | 0x9F | 读 ID | read_jedec_id |
| `SFUD_CMD_READ_SFDP_REGISTER` | 0x5A | 读 SFDP | read_sfdp_data |
| `SFUD_CMD_ENABLE_RESET` / `SFUD_CMD_RESET` | 0x66 / 0x99 | 软复位两步 | reset |
| `SFUD_CMD_ENTER/EXIT_4B_ADDRESS_MODE` | 0xB7 / 0xE9 | 4 字节地址 | set_4_byte_address_mode |

## 4. 三个核心结构

### 4.1 sfud_spi（:279-296）——移植层函数指针表

```c
typedef struct __sfud_spi {
    char *name;
    /* SPI bus write read data function */
    sfud_err (*wr)(const struct __sfud_spi *spi, const uint8_t *write_buf, size_t write_size, uint8_t *read_buf,
                   size_t read_size);
```

`wr` 是全库唯一强依赖；`qspi_read`（QSPI 时）、`lock/unlock`（可空，调用点判空）、`user_data`（透传硬件句柄）见 [port_sfud_port.md](port_sfud_port.md)。

### 4.2 sfud_sfdp（:256-273）——SFDP 解析产物

`available` 置位后其余字段才可信；`eraser[4]` 升序（eraser[0] 最小）；`write_gran` 单位字节；容量算式与字段映射见 [src_sfud_sfdp.md](src_sfud_sfdp.md)。

### 4.3 sfud_flash（:301-322）——设备对象

身份（name/index）+ 芯片参数（chip）+ 移植通道（spi/retry）+ 运行状态（init_ok/addr_in_4_byte/sfdp/read_cmd_format）。字段分组与 RAM 估算见 [src_sfud.md](src_sfud.md) 第 1 节。

## 5. sfud_flash_def.h 速读（关联文件）

| 符号 | 行号 | 说明 |
|---|---|---|
| `enum sfud_write_mode` | :42-47 | PAGE_256B/BYTE/AAI/DUAL_BUFFER 四种位掩码 |
| `sfud_flash_chip` | :56-65 | name+三字节 ID+容量+写模式+擦除粒度+擦除命令 |
| `SFUD_MF_TABLE` | :98-118 | 18 家厂商 ID→名称（日志用） |
| `SFUD_FLASH_CHIP_TABLE` | :126-158 | 31 款芯片兜底参数（含 GD25Q64B、SST25VF016B…） |
| `SFUD_FLASH_EXT_INFO_TABLE` | :166-198 | QSPI 快读模式位掩码表（15 款） |

添加新芯片的方法（README 2.5）：在 `SFUD_FLASH_CHIP_TABLE` 末尾加一行 `{名称, mf_id, type_id, capacity_id, 容量, 写模式, 擦除粒度, 擦除命令}`。

## 6. 陷阱标注

1. `SFUD_ASSERT` 失败是**死循环**不是可恢复错误——量产必须关 `SFUD_DEBUG_MODE`；
2. `SFUD_RETRY_PROCESS` 宏体内 `break`——放在非循环上下文直接编译错误或逻辑错误；
3. 多行宏（:77、:94）的续行符之间不能插任何注释；
4. `sfud_err` 与返回 bool 的 `sfud_read_sfdp`（src_sfud_sfdp.c:102）约定不同，混用注意。
