# fdb_utils.c —— 工具层：CRC32 / 状态表 / 存储抽象

对应源文件：`references/filesystems/FlashDB/src/fdb_utils.c`（共 463 行，A级逐函数深注）

全库的"物理规律守门员"：CRC32 算法、状态表编码（掉电安全的地基）、blob 读写、以及 KVDB/TSDB 共用的存储抽象三函数。
互链：[[fdb_kvdb]]、[[fdb_tsdb]]、[[fdb_low_lvl]]、[[fdb_file]]、Docs《05-掉电安全设计》《06-FAL与移植层》。

## 一、逐函数表（行号 = 函数定义行，注释后真实行号）

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `fdb_calc_crc32` | 98 | CRC32 查表计算 | 首尾 `crc ^ ~0U`；26-71 行是 256 项 crc32_table（多项式 0x04C11DB7）；支持分段续算 |
| `_fdb_set_status` | 122 | 内存中生成第 k 状态位图 | 115-128 行大注释表列出各写粒度字节图样；gran=1 时 `status_table[byte_index] &= (0x00ff >> (status_index % 8))`；gran>1 时整粒度写 0；返回 SIZE_MAX 表示状态 0 无需写 |
| `_fdb_get_status` | 165 | 从状态表解码当前状态 | 从高位向低位找第一个 0；断电只可能停在中间态 |
| `_fdb_write_status` | 198 | 状态推进并写 Flash | 先内存生成再只写目标字节/粒度单位（206 行 SIZE_MAX 短路）；sync 透传给底层 |
| `_fdb_read_status` | 232 | 读回状态表并解码 | read + get 的组合，del_kv/update_sec_status 用 |
| `_fdb_continue_ff_addr` | 253 | 找最后一段连续 0xFF 起点 | 32 字节分块读；末尾非 0xFF 返回 end；挂载时校正 empty_kv 用 |
| `fdb_blob_make` | 296 | 包装用户缓冲为 blob | 零拷贝，只填 buf/size |
| `fdb_blob_read` | 318 | 按 blob.saved.addr 回读 | read_len = min(size, saved.len)；失败返回 0 |
| `_fdb_flash_read` | 346 | 存储抽象读 | file_mode -> _fdb_file_read；否则 fal_partition_read |
| `_fdb_flash_erase` | 374 | 存储抽象擦 | file_mode -> _fdb_file_erase；否则 fal_partition_erase |
| `_fdb_flash_write` | 404 | 存储抽象写 | file_mode -> _fdb_file_write；否则 fal_partition_write；sync 仅文件模式有意义 |
| `_fdb_flash_write_align` | 437 | 按写粒度对齐写 | ALIGN_DOWN 整块直写 + 尾部 0xFF 填充成整粒度（446 行 C89 兼容分支）；KV name/value、TSL 数据都走它 |

## 二、关键摘录

1. **状态表核心公式**（`_fdb_set_status` 内，140-148 行附近）：

```c
#if (FDB_WRITE_GRAN == 1)
    byte_index = (status_index - 1) / 8;
#if (FDB_BYTE_ERASED == 0xFF)
    status_table[byte_index] &= (0x00ff >> (status_index % 8));
#else
    status_table[byte_index] |= (0x00ff >> (status_index % 8));
#endif
```

gran=1、NOR（擦除态 0xFF）时：第 k 个状态 = 把字节中第 k 个 bit 清零。整张状态表 6 个状态只需 1 字节。

2. **存储抽象分派**（`_fdb_flash_write` 内，418-429 行附近）：

```c
if (db->file_mode) {
#ifdef FDB_USING_FILE_MODE
    return _fdb_file_write(db, addr, buf, size, sync);
#else
    return FDB_WRITE_ERR;
#endif
} else {
#ifdef FDB_USING_FAL_MODE
    if (fal_partition_write(db->storage.part, addr, (uint8_t *)buf, size) < 0)
    {
        result = FDB_WRITE_ERR;
    }
#endif
}
```

3. **尾部填充写**（`_fdb_flash_write_align` 内，456-460 行）：

```c
align_remain = size - FDB_WG_ALIGN_DOWN(size);
if (result == FDB_NO_ERR && align_remain) {
    memcpy(align_data, (uint8_t *) buf + FDB_WG_ALIGN_DOWN(size), align_remain);
    result = _fdb_flash_write(db, addr + FDB_WG_ALIGN_DOWN(size), (uint32_t *) align_data,
            align_data_size, false);
}
```

align_data 预 memset 0xFF（451 行），剩余字节 memcpy 后整体写一个粒度单位——写总长永远是粒度整数倍。

## 三、状态表推演示例

设 6 状态（KV 节点）、gran=1、NOR：

| 调用 | 内存位图 | 写 Flash 的字节 |
|---|---|---|
| set_status(0) | 0xFF | 无（SIZE_MAX 短路） |
| set_status(1) | 0x7F | 写偏移 0 的 1 字节 0x7F |
| set_status(2) | 0x3F | 再写 1 字节 0x3F |
| set_status(4) | 0x0F | 再写 1 字节 0x0F |

读回时 `_fdb_get_status` 数第一个 0 的 bit 位即状态序号；写入中途中断，重放同一调用即可（幂等）。

## 四、互链与读码顺序

- 状态表编解码是 `fdb_kvdb.c`/`fdb_tsdb.c` 所有状态机的底层：读 Docs《05-掉电安全设计》第 1 节对照本文件 115-128 行的字节图样表。
- `_fdb_continue_ff_addr` 的消费方：`fdb_kvdb.c:588`（挂载时校正扇区 empty_kv）。
- `fdb_calc_crc32` 的两大消费方：`fdb_kvdb.c:1527`（写端算 CRC）、`fdb_kvdb.c:483`（读端验 CRC）。
- blob 三件套（make/read + kv/tsdb 的 to_blob）是用户读写数据的唯一通道，见 [[flashdb_h]]。
- 文件模式后端见 [[fdb_file]]。

## 五、fdb_calc_crc32 分段续算示例

```c
uint32_t crc = fdb_calc_crc32(0, part1, 10);   /* 首段传 0 */
crc = fdb_calc_crc32(crc, part2, 20);          /* 续段传上次返回值 */
/* 等价于一次性 fdb_calc_crc32(0, whole, 30) */
```

库内三处用到续算：read_kv 按 32B 块流式算 KV（fdb_kvdb.c:475-484）、create_kv_blob 按"长度字段+名+填充+值"逐段算（1527-1538）、update_kv_cache 算名字 CRC16（取高 16 位）。**首段传 0 不是可选约定，是正确性要求**（函数内部 `crc ^ ~0U` 两次异或的初值链）。

## 六、_fdb_flash_* 的调用纪律

| 函数 | 允许的调用方 | 不变量 |
|---|---|---|
| `_fdb_flash_read` | 任意上下文 | 无对齐要求 |
| `_fdb_flash_write` | 仅经 `_fdb_flash_write_align` 或状态表（本身就是粒度对齐） | 区域必须为 0xFF，size 粒度对齐 |
| `_fdb_flash_erase` | format_sector / tsl 滚动 | addr 扇区对齐，size 整扇区 |

库外代码（移植层钩子除外）不应直接调这三个函数；绕过对齐层写非对齐长度，在 gran>1 的 Flash 上是未定义行为。
