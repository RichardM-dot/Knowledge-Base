# DeskHop 源码笔记：ramdisk.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/ramdisk.c`（共 139 行）

> 配置模式下的虚拟 U 盘：对外 512KB FAT12，实际镜像 64KB；写入按 UF2
> 解析并直接烧进运行区，实现“拖拽升级”。

相关文档：[08-实测与坑点指南](../../../references/firmware/DeskHop/Docs/08-实测与坑点指南.md)

## 1. 常量

```c
#define NUMBER_OF_BLOCKS 4096
#define ACTUAL_NUMBER_OF_BLOCKS 128
#define BLOCK_SIZE       512
```

（L35-L37）

| 常量 | 含义 |
|------|------|
| `NUMBER_OF_BLOCKS=4096` | 对外声明 4096×512B = 2MB |
| `ACTUAL_NUMBER_OF_BLOCKS=128` | 实际镜像 128×512B = 64KB |
| `BLOCK_SIZE=512` | MSC 块大小 |

“说谎”的原因：镜像只有 64KB（压缩后的 config.htm + FAT 表），但 U 盘容量
太小会让操作系统/用户觉得异常；超出 64KB 的读取返回全零。

## 2. MSC 回调表

| 回调 | 行号 | 作用 |
|------|------|------|
| `tud_msc_inquiry_cb` | L39-L43 | 返回 “DeskHop / Config Mode / 1.0” |
| `tud_msc_test_unit_ready_cb` | L45-L47 | 永远就绪 |
| `tud_msc_capacity_cb` | L49-L52 | 4096 块 × 512B |
| `tud_msc_start_stop_cb` | L54-L56 | 忽略启停 |
| `tud_msc_read10_cb` | L59-L74 | 读镜像/补零 |
| `tud_msc_is_writable_cb` | L76-L78 | 可写 |
| `tud_msc_write10_cb` | L81-L133 | 解析 UF2 + 写 Flash + CRC |
| `tud_msc_scsi_cb` | L136-L139 | 其余 SCSI 命令拒绝 |

## 3. 读

```c
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    const uint8_t *addr = &ADDR_DISK_IMAGE[lba * BLOCK_SIZE + offset];

    if (lba >= NUMBER_OF_BLOCKS)
        return -1;

    /* We lie about the image size - actually it's 64 kB, not 512 kB, so if we're out of bounds, return zeros */
    else if (lba >= ACTUAL_NUMBER_OF_BLOCKS)
        memset(buffer, 0x00, bufsize);

    else
        memcpy(buffer, addr, bufsize);

    return (int32_t)bufsize;
}
```

（L59-L73）

- LBA 超声明容量 → -1；
- LBA 在 64KB~512KB 之间 → 全零；
- 真实镜像 → 从 `ADDR_DISK_IMAGE` 拷贝。

注意 `addr` 在越界检查之前就计算了，但只在 else 分支使用，不会真的解引用越界地址
（指针算术本身在 C 里对越界是 UB，但 RP2040 上不会 fault）。

## 4. 写：UF2 解析

```c
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    uf2_t *uf2 = (uf2_t *)&buffer[0];
    bool is_final_block = (uf2->blockNo == STAGING_PAGES_CNT - 1);
    uint32_t flash_addr = (uint32_t)ADDR_FW_RUNNING + uf2->blockNo * FLASH_PAGE_SIZE - XIP_BASE;

    if (lba >= NUMBER_OF_BLOCKS)
        return -1;

    /* Do not parse a partial MSC write as a UF2 block. */
    if (bufsize < sizeof(uf2_t))
        return -1;

    /* If we're not detecting UF2 magic constants, we have nothing to do... */
    if (uf2->magicStart0 != UF2_MAGIC_START0 || uf2->magicStart1 != UF2_MAGIC_START1 || uf2->magicEnd != UF2_MAGIC_END)
        return (int32_t)bufsize;
```

（L81-L95）

UF2 块布局（`flash.h` L61-L72）：

| 偏移 | 字段 | 值 |
|------|------|----|
| 0 | magicStart0 | 0x0A324655 |
| 4 | magicStart1 | 0x9E5D5157 |
| 8 | flags | — |
| 12 | targetAddr | — |
| 16 | payloadSize | 必须 256 |
| 20 | blockNo | 0..1023 |
| 24 | numBlocks | — |
| 28 | fileSize | — |
| 32 | data[476] | 前 256B 是有效载荷 |
| 508 | magicEnd | 0x0AB16F30 |

`flash_addr = ADDR_FW_RUNNING + blockNo*256 - XIP_BASE`：**用 blockNo 而不是 LBA**
决定写入地址。因此文件块可以乱序到达，固件仍能写对位置。

### 4.1 校验链

1. LBA 在声明容量内；
2. 写入长度 ≥512B；
3. 三个魔术字齐全（不匹配则“吞掉”返回 bufsize，不报错）；
4. `payloadSize==256` 且 `blockNo<1024`，否则 -1。

第 3 步“不匹配返回成功”是为了兼容操作系统对 U 盘的非 UF2 写入（例如 FAT 表更新），
避免主机重试。

### 4.2 CRC 与收尾

```c
    const uint32_t last_block_with_checksum = (STAGING_IMAGE_SIZE - FLASH_SECTOR_SIZE) / FLASH_PAGE_SIZE;
    for (int i=0; i<FLASH_PAGE_SIZE && uf2->blockNo < last_block_with_checksum; i++)
        global_state.fw.checksum = crc32_iter(global_state.fw.checksum, buffer[32 + i]);

    write_flash_page(flash_addr, &buffer[32]);

    if (is_final_block) {
        global_state.fw.checksum = ~global_state.fw.checksum;

        /* If checksums don't match, overwrite first sector and rely on ROM bootloader for recovery */
        if (global_state.fw.checksum != calculate_firmware_crc32()) {
            flash_range_erase((uint32_t)ADDR_FW_RUNNING - XIP_BASE, FLASH_SECTOR_SIZE);
            reset_usb_boot(1 << PICO_DEFAULT_LED_PIN, 0);
        }
        else {
            global_state.reboot_requested = true;
        }
    }
```

（L108-L126）

- CRC 覆盖 `STAGING_IMAGE_SIZE - FLASH_SECTOR_SIZE`（256KB - 4KB）：最后 4KB 存放
  元数据/CRC 自身，不参与。
- 最后一块（blockNo==1023）到达后取反比较。
- 失败：擦首扇区 + 进 ROM BOOTSEL，用户可重烧。
- 成功：`reboot_requested=true`，core0 停止喂狗，500ms 后复位。

`toggle_led()` + `watchdog_update()` 每块都执行：给出进度反馈，同时防止长时间
写 Flash 被看门狗复位。

## 5. 为什么能“拖拽升级”

普通 U 盘只存文件；DeskHop 的 MSC 回调在收到数据时**不落盘**，而是把 UF2 载荷
直接写进 Flash 运行区。因此用户看到的是“复制文件”，实际发生的是“边接收边烧录”。

代价：

- 不支持任意文件，只认 UF2；
- 写入过程不可中断（中断会留下半镜像）；
- CRC 失败会主动变砖（擦首扇区），依赖 ROM BOOTSEL 恢复。

## 6. 坑点

1. **`NUMBER_OF_BLOCKS` 与实际镜像不一致**：读超出 64KB 返回零，写超出声明容量 -1。
2. **UF2 载荷在 `buffer[32]`**，不是 `buffer[0]`；`write_flash_page(flash_addr, &buffer[32])`。
3. **`is_final_block` 用 `blockNo == STAGING_PAGES_CNT-1`**：若 UF2 的 blockNo
   不从 0 开始或数量不足，永远不会触发收尾。
4. **CRC 跳过最后一个扇区**：`calculate_firmware_crc32()` 也按同样范围计算，
   两处必须一致。
5. **魔术字不匹配时返回成功**：这是有意“吞掉”非 UF2 写，不要误以为校验通过。
6. **写 Flash 期间关中断数毫秒**：UART 可能丢包，但配置模式下没有键鼠转发，影响小。
7. **`tud_msc_scsi_cb()` 一律拒绝**：某些主机会发 READ_CAPACITY 之外的命令，
   被拒绝后仍能工作，但日志里会有 SCSI 错误。
