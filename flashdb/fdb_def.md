# fdb_def.h —— 公共类型与结构定义

对应源文件：`references/filesystems/FlashDB/inc/fdb_def.h`（共 367 行，B级文件头导读块）

全库数据结构的"总账本"：六个状态枚举、六个核心结构体、control 命令宏、可裁剪配置默认值。读任何源文件前先过一遍。
互链：[[fdb_low_lvl]]、[[fdb_kvdb]]、[[fdb_tsdb]]、[[flashdb_h]]、Docs《00-总览与双模式架构》第 3 节。

## 一、可裁剪宏默认值

| 宏 | 行号 | 默认值 | 含义 |
|---|---|---|---|
| `FDB_SW_VERSION` | 40 | "2.2.99" | 软件版本（横幅打印用，NUM 版本 41 行） |
| `FDB_KV_NAME_MAX` | 45 | 64 | KV 键名上限 |
| `FDB_KV_CACHE_TABLE_SIZE` | 50 | 64 | KV 地址缓存容量 |
| `FDB_SECTOR_CACHE_TABLE_SIZE` | 55 | 8 | 扇区信息缓存容量 |
| `FDB_KV_USING_CACHE` | 58 | （推导） | 两表都 >0 时自动定义 |
| `FDB_USING_FILE_MODE` | 63 | （推导） | 任一 FILE_*_MODE 定义时定义 |
| `FDB_FILE_CACHE_TABLE_SIZE` | 67 | 2 | 文件模式句柄缓存 |
| `FDB_WRITE_GRAN` | 72 | 1 | 写粒度 bit（FAL 模式必须在 fdb_cfg.h 显式配） |

## 二、状态机枚举（掉电安全的核心词汇）

| 枚举 | 行号 | 取值 |
|---|---|---|
| `fdb_err_t` | 142 | NO_ERR(0)/ERASE_ERR/READ_ERR/WRITE_ERR/PART_NOT_FOUND/KV_NAME_ERR/KV_NAME_EXIST/SAVED_FULL/INIT_FAILED |
| `fdb_kv_status` | 154 | UNUSED/PRE_WRITE/WRITE/PRE_DELETE/DELETED/ERR_HDR（6 态，`FDB_KV_STATUS_NUM=6` 用 #define 带出枚举外） |
| `fdb_tsl_status` | 165 | UNUSED/PRE_WRITE/WRITE/USER_STATUS1/DELETED/USER_STATUS2（USER_STATUS1 刻意排在 WRITE 与 DELETED 之间，保证可从 WRITE 单向推进） |
| `fdb_sector_store_status` | 221 | UNUSED/EMPTY/USING/FULL（扇区数据状态） |
| `fdb_sector_dirty_status` | 231 | UNUSED/FALSE/TRUE/GC（GC=回收进行中，断电可续） |

状态序号即"状态表中被清零的位号"，顺序有语义：只能从前往后走，不可跳回。

## 三、核心结构体（行号 = 定义行）

| 结构 | 行号 | 要点 |
|---|---|---|
| `fdb_default_kv_node` / `fdb_default_kv` | 130/136 | 默认 KV 表项 {key,value,value_len}；value_len=0 按 C 字符串（兼容老版本，fdb_kv_set_default 1436 行附近逻辑） |
| `struct fdb_kv` | 177 | KV 元信息：status/crc_is_ok/name_len/magic/len/value_len/name[FDB_KV_NAME_MAX]/addr{start,value} |
| `struct fdb_kv_iterator` | 192 | 用户态迭代器：curr_kv + iterated_cnt/obj_bytes/value_bytes 统计 + sector_addr/traversed_len（私有字段） |
| `struct fdb_tsl` | 203 | TSL 元信息：status/time/log_len/addr{index,log} |
| `fdb_db_type` | 215 | FDB_DB_TYPE_KV / TS（init 传入，只影响日志） |
| `struct kvdb_sec_info` | 241 | KVDB 扇区信息：check_ok/store/dirty/addr/magic/combined/remain/empty_kv |
| `struct tsdb_sec_info` | 256 | TSDB 扇区信息：+start_time/end_time/end_idx/end_info_stat[2]/empty_idx/empty_data |
| `struct kv_cache_node` | 271 | KV 缓存表项：name_crc(16bit)/active/addr，仅 8 字节 |
| `struct fdb_db` | 280 | 父类：name/type/storage(part|dir)/sec_size/max_size/oldest_addr/init_ok/file_mode/not_formatable/file 缓存/lock/unlock/user_data |
| `struct fdb_kvdb` | 313 | = parent + default_kvs/gc_request/in_recovery_check/cur_kv/cur_sector/last_is_complete_del + 两张缓存表 + ver_num |
| `struct fdb_tsdb` | 338 | = parent + cur_sec/last_time/get_time/max_len/rollover |
| `struct fdb_blob` | 351 | buf/size + saved{meta_addr,addr,len}——meta_addr 是 KV/TSL 的"身份地址"，addr 是数据地址 |

## 四、fdb_db 父类字段逐条（280 行起）

| 字段 | 谁写 | 谁读 |
|---|---|---|
| `name` | init | 日志前缀/文件模式文件名 |
| `type` | init | 日志文案 |
| `storage.part / storage.dir` | init（按模式二选一） | `_fdb_flash_*`/`_fdb_file_*` |
| `sec_size` | control 或 init 推导 | 所有扇区运算 |
| `max_size` | init（FAL）或 control（文件） | 容量判断/回绕 |
| `oldest_addr` | 挂载推导/GC 推进 | 一切遍历的起点 |
| `lock/unlock` | control 注册 | 每个 API 入口 |
| `init_ok` | `_fdb_init_finish` | 所有 API 的准入检查 |

## 五、control 命令宏

KVDB（103-109 行）：SET_SEC_SIZE(0x00)/GET_SEC_SIZE(0x01)/SET_LOCK(0x02)/SET_UNLOCK(0x03)/SET_FILE_MODE(0x09)/SET_MAX_SIZE(0x0A)/SET_NOT_FORMAT(0x0B)——SET 类全部须 init 前。

TSDB（111-120 行）：同上 + SET_ROLLOVER(0x04)/GET_ROLLOVER(0x05)/GET_LAST_TIME(0x06)——ROLLOVER 与 GET_LAST_TIME 须 init 后。

注意 KVDB 与 TSDB 的命令号复用同一数值空间（0x00~0x0B），传错对象编译期发现不了。

## 六、读码互链

- 状态枚举的流转图：Docs《02-KVDB存储模型》第 1 节、《04-TSDB时序模型》第 1 节。
- fdb_db/kvdb/tsdb 的"继承"关系：Docs《00》第 3 节与 7.5 节。
- 状态表在 Flash 上的物理编码：[[fdb_low_lvl]]。
- 每个 API 的契约：[[flashdb_h]]。

## 七、fdb_blob 的三段语义（351 行起）

```c
struct fdb_blob {
    void *buf;                /* 用户缓冲 */
    size_t size;              /* 用户缓冲大小 */
    struct {
        uint32_t meta_addr;   /* KV 节点头/TSL 索引地址（身份） */
        uint32_t addr;        /* 数据实体地址 */
        size_t len;           /* 数据实体长度 */
    } saved;
};
```

- 写路径：用户只填 buf/size（fdb_blob_make），saved 由库写完后不用管；
- 读路径：saved 必须先由 fdb_kv_to_blob/fdb_tsl_to_blob 填好，fdb_blob_read 按 saved.addr/len 回读到 buf；
- `meta_addr` 的存在意义：数据可被 GC 搬走，但"这条数据是哪条记录"由 meta_addr 表达——TSDB 的 fdb_tsl_set_status 就靠它定位索引。

## 八、常见误用与澄清

| 误用 | 实际语义 |
|---|---|
| 把 fdb_blob 当容器存数据 | blob 只是"缓冲区 + Flash 位置"的引用，不含数据 |
| 在 fdb_kv 迭代中缓存 fdb_kv 指针 | curr_kv 是迭代器内部存储，下轮迭代覆盖 |
| 给 fdb_tsl.time 赋值再 set_status | tsl 是快照，set_status 只用 addr.index |
| 用 value_len 判断键是否存在 | 用 blob.saved.len > 0（get_blob 的约定） |
