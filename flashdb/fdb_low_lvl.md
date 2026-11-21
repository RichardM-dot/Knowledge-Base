# fdb_low_lvl.h —— 底层接口与 Flash 特性宏

对应源文件：`references/filesystems/FlashDB/inc/fdb_low_lvl.h`（共 86 行，B级文件头导读块）

KVDB/TSDB 共用的"物理层词汇表"：写粒度换算、状态表尺寸公式、对齐宏、哨兵值、底层函数原型声明（实现在 fdb_utils.c）。
互链：[[fdb_utils]]（实现）、[[fdb_def]]、[[fdb_cfg_template]]、Docs《05-掉电安全设计》《06-FAL与移植层》。

## 一、宏逐条表（行号 = 注释后真实行号）

| 宏 | 行号 | 定义 | 说明 |
|---|---|---|---|
| `FDB_STATUS_TABLE_SIZE(n)` | 34/36 | gran=1: (n*gran+7)/8；gran>1: ((n-1)*gran+7)/8 | N 个状态的状态表字节数。gran=1 时 6 状态=1 字节；gran=8 时 6 状态=5 字节 |
| `FDB_BYTE_ERASED` | 40 | 0xFF | 擦除态字节 |
| `FDB_BYTE_WRITTEN` | 42 | 0x00 | 已写态字节 |
| `FDB_ALIGN(size, align)` | 47 | 向上取整对齐 | RT-Thread 风格，13 对齐 4 得 16 |
| `FDB_WG_ALIGN(size)` | 49 | 按写粒度对齐 | 粒度单位 = (FDB_WRITE_GRAN+7)/8 字节 |
| `FDB_ALIGN_DOWN` / `FDB_WG_ALIGN_DOWN` | 54/56 | 向下对齐 | 扇区地址换算/整块写 |
| `FDB_STORE_STATUS_TABLE_SIZE` | 58 | STATUS_TABLE_SIZE(4) | 扇区 store 状态表字节数 |
| `FDB_DIRTY_STATUS_TABLE_SIZE` | 59 | STATUS_TABLE_SIZE(4) | 扇区 dirty 状态表字节数 |
| `FDB_DATA_UNUSED` | 63 | 0xFFFFFFFF（NOR） | "无数据"哨兵（缓存空位/未初始化地址） |
| `FDB_FAILED_ADDR` | 69 | 0xFFFFFFFF | 分配失败/非法地址哨兵 |

## 二、底层函数原型（71-84 行）

| 声明行 | 函数 | 实现位置 |
|---|---|---|
| 71 | `_fdb_set_status` | fdb_utils.c:122 |
| 72 | `_fdb_get_status` | fdb_utils.c:165 |
| 73 | `_fdb_continue_ff_addr` | fdb_utils.c:253 |
| 74-77 | `_fdb_init_ex/_fdb_init_finish/_fdb_deinit/_fdb_db_path` | fdb.c（见 [[fdb]]） |
| 78 | `_fdb_write_status` | fdb_utils.c:198 |
| 79 | `_fdb_read_status` | fdb_utils.c:232 |
| 80-82 | `_fdb_flash_read/_fdb_flash_erase/_fdb_flash_write` | fdb_utils.c:346/374/404 |
| 84 | `_fdb_flash_write_align` | fdb_utils.c:437 |

下划线前缀 = 库内部 API，用户代码不要直接调用（跨版本无兼容承诺）。

## 三、为什么状态表公式有两种

```c
#if (FDB_WRITE_GRAN == 1)
#define FDB_STATUS_TABLE_SIZE(status_number)       ((status_number * FDB_WRITE_GRAN + 7)/8)
#else
#define FDB_STATUS_TABLE_SIZE(status_number)       (((status_number - 1) * FDB_WRITE_GRAN + 7)/8)
#endif
```

gran=1（NOR 逐位写）：一个状态占 1 bit，N 个状态占 N bit（向上取整字节）——状态 0 全 FF 不占位。gran>=8（按字节/字编程）：每个状态独占一个粒度单位，N 个状态要 N-1 个单位（状态 0 = 全擦除态，不需要真实存储单元）。这直接决定了 KV 头 18B（gran1）vs 22B+（gran8）的差别。

## 四、对齐宏计算示例

以 FDB_WRITE_GRAN=1（粒度单位 1 字节）与 8（粒度单位 1 字节）、32（粒度单位 4 字节）为例：

| 表达式 | gran=1 | gran=8 | gran=32 |
|---|---|---|---|
| `FDB_WG_ALIGN(13)` | 13 | 13 | 16 |
| `FDB_WG_ALIGN(1)` | 1 | 1 | 4 |
| `FDB_WG_ALIGN_DOWN(13)` | 13 | 13 | 12 |
| 状态表 6 状态 | 1 B | 5 B | 5 B（占 2 个 4B 单位中的 5B，仍按 5） |

KV 名/值、TSL 数据的落盘长度一律过 `FDB_WG_ALIGN`，保证对齐填充部分可预测、可复算（CRC 对称性）。

## 五、互链

- 状态表位图图样大表：[[fdb_utils]] `_fdb_set_status`（fdb_utils.c:115-128 注释）。
- `FDB_WG_ALIGN` 的消费现场：[[fdb_kvdb]] `create_kv_blob`（fdb_kvdb.c:1509）。
- 移植时如何选 FDB_WRITE_GRAN：Docs《06-FAL与移植层》第 4 节及其量化表。

## 六、FDB_BYTE_ERASED 非 0xFF 的分支

第 40 行 `FDB_BYTE_ERASED` 与 42 行 `FDB_BYTE_WRITTEN` 是一对反相常量；某些"擦除态为 0"的存储（或反向模拟）下，63-66 行的 `FDB_DATA_UNUSED` 会切到 0x00000000 分支，状态表公式在 `_fdb_set_status`（fdb_utils.c）里也随之切换 `&=`/`|=` 方向。默认全库按 NOR（0xFF 擦除态）设计，改这对宏属于"非常规移植"，需要同步验证状态机语义。

## 七、哨兵值的使用现场

| 哨兵 | 生产者 | 消费者 |
|---|---|---|
| FDB_DATA_UNUSED | kv_cache_table 初始化/update_kv_cache 删除 | get_kv_from_cache 跳过空位 |
| FDB_DATA_UNUSED | tsdb init 时 cur_sec.addr/oldest_addr | check_sec_hdr_cb 判断"还没找到" |
| FDB_FAILED_ADDR | alloc_kv/new_kv 失败 | create_kv_blob/move_kv 判 FDB_SAVED_FULL |
| FDB_FAILED_ADDR | sector_cache_table 初始化 | read_sector_info 判断缓存不可信 |

两个哨兵值同为 0xFFFFFFFF（NOR 全擦除态）——"没有数据"与"没有地址"在 Flash 语义上天然同一，这是利用擦除态做标记的经典手法。

## 八、读码小抄

1. 看 KV 头多大：`KV_HDR_DATA_SIZE = FDB_WG_ALIGN(sizeof(struct kv_hdr_data))`（fdb_kvdb.c:101）；
2. 看状态表几字节：`KV_STATUS_TABLE_SIZE = FDB_STATUS_TABLE_SIZE(6)`（fdb_kvdb.c:93）；
3. 换算任意地址所在扇区：`FDB_ALIGN_DOWN(addr, db_sec_size(db))`；
4. 换算任意长度实占：`FDB_WG_ALIGN(len)`。

## 九、宏与两个数据库头结构的关系速查

| 数据库头 | 组成 | 由哪些宏决定大小 |
|---|---|---|
| KV 扇区头 | store 表 + dirty 表 + magic + combined + reserved | FDB_STORE_STATUS_TABLE_SIZE + FDB_DIRTY_STATUS_TABLE_SIZE |
| KV 节点头 | KV 状态表 + magic + len + crc32 + name_len + value_len (+padding) | KV_STATUS_TABLE_SIZE（fdb_kvdb.c 内再封装） |
| TSDB 扇区头 | store 表 + magic + start_time + end_info[2] + reserved | FDB_STORE_STATUS_TABLE_SIZE + TSL_TIME_ALIGN_SIZE |
| TSL 索引 | 状态表 + time (+log_len+log_addr) | TSL_STATUS_TABLE_SIZE + FDB_WG_ALIGN(sizeof(fdb_time_t)) |

全部头部大小都随 FDB_WRITE_GRAN 放大——这就是"写粒度决定小记录空间效率"的机制根源。
