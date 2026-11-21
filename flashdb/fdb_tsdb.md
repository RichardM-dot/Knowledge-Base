# fdb_tsdb.c —— TSDB 时序数据库核心

对应源文件：`references/filesystems/FlashDB/src/fdb_tsdb.c`（共 1304 行，A级逐函数深注）

TSDB 是 FlashDB 的"时间流水库"：索引向下生长、数据向上生长的相向扇区 + 时间戳单调追加 + 满库滚动覆盖。无 GC、无删除、无覆盖写。
互链：[[fdb_def]]、[[fdb_utils]]、[[fdb_kvdb]]、Docs《04-TSDB时序模型》《05-掉电安全设计》。

## 一、宏与结构（注释后真实行号）

| 项 | 行号 | 说明 |
|---|---|---|
| `SECTOR_MAGIC_WORD` | 41 | 0x304C5354（"TSL0"） |
| `TSL_STATUS_TABLE_SIZE` | 43 | 状态表字节数 |
| `TSL_TIME_ALIGN_SIZE` | 47-49 | 时间戳按写粒度对齐（64BIT 时间戳开关） |
| `LOG_IDX_BASE_SIZE` | 63-66 | 索引基础长度：状态表+时间（+变长模式 8 字节） |
| `SECTOR_HDR_*_OFFSET` | 73-80 | 扇区头各字段偏移（end_info 双副本各 3 个） |
| `struct sector_hdr_data` | 113 | 扇区头：store 状态+magic+start_time+end_info[2]+reserved+padding |
| `struct log_idx_data` | 132 | TSL 索引：状态表+time（+log_len/log_addr 变长模式才有） |
| `struct check_sec_hdr_cb_args` | 152 | init 体检回调参数包 |
| `_FDB_WRITE_STATUS` / `FLASH_WRITE` 宏 | 101-111 | 带出错即 return 的写包装 |

## 二、逐函数表（行号 = 函数定义行）

### 链与读

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `read_tsl` | 170 | 读一条 TSL 索引 | 定长模式由索引序号倒推数据地址（188 行）；PRE_WRITE/UNUSED 回填无效值 |
| `get_next_sector_addr` | 206 | 扇区正向步进 | 库尾回绕 0 |
| `get_next_tsl_addr` | 227 | 扇区内索引链正向步进 | +LOG_IDX_DATA_SIZE，超 end_idx 停 |
| `get_last_tsl_addr` | 251 | 索引链反向步进 | -LOG_IDX_DATA_SIZE |
| `get_last_sector_addr` | 271 | 扇区反向步进 | 库头回绕最后扇区 |
| `read_sector_info` | 298 | 读扇区头/深读统计 | end_info[2] 双副本择优（321-330 行）；深读逐条重建 empty_idx/empty_data/remain |
| `search_start_tsl_addr` | 785 | 扇区内按时间二分 | O(log n)，步长按索引长度对齐 |

### 写

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `format_sector` | 372 | 擦除+写 EMPTY+magic | start_time/end_info 留全 FF |
| `sector_iterator` | 401 | 扇区遍历骨架 | 起点是 sector->addr（不是 oldest_addr，与 KVDB 不同） |
| `write_tsl` | 433 | 写一条 TSL（两阶段） | 448 PRE_WRITE -> 450 time/位置 -> 452 数据 -> 457 WRITE 提交 |
| `update_sec_status` | 475 | 扇区容量管理/滚动 | 482 行满判断；493-503 end_info 双副本两阶段；505 转 FULL；517-527 rollover 换扇区 |
| `tsl_append` | 557 | 追加统一入口 | 568-581 行长度/时间戳双重检查；成功后推进 cur_sec 计数（597-602 行） |

### 公有 API

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `fdb_tsl_append` | 619 | 追加（自动时间戳） | db_lock 保护 |
| `fdb_tsl_append_with_ts` | 646 | 追加（显式时间戳） | 补录历史用，仍须递增 |
| `fdb_tsl_iter` | 673 | 正序全量遍历 | 从 oldest_addr 起；USING 扇区用 db->cur_sec 副本（699 行） |
| `fdb_tsl_iter_reverse` | 727 | 倒序全量遍历 | 从 cur_sec 末条向前；遇 EMPTY/UNUSED 停 |
| `fdb_tsl_iter_by_time` | 828 | 按时间区间遍历 | from>to 自动反向（717-725 行函数指针切换）；扇区剪枝 + 二分起步 |
| `fdb_tsl_query_count` | 934 | 区间内状态计数 | 基于 iter_by_time |
| `fdb_tsl_max_blob_count` | 962 | 容量估算 | 扇区数 × (可用/单条最大) |
| `fdb_tsl_set_status` | 990 | 状态单向推进 | 只写 0，幂等 |
| `fdb_tsl_to_blob` | 1013 | TSL -> blob | addr/meta_addr/len |
| `fdb_tsl_clean` | 1096 | 清空整库 | tsl_format_all + 锁 |
| `fdb_tsdb_control` | 1114 | 参数控制 | SET_ROLLOVER 须 init 后（断言相反） |
| `fdb_tsdb_init` | 1200 | 初始化 | 1223 行 max_len<扇区 断言；体检失败默认整库格式化 |
| `fdb_tsdb_deinit` | 1297 | 反初始化 | |

### 挂载辅助

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `query_count_cb` | 911 | 计数回调 | 状态匹配 +1 |
| `check_sec_hdr_cb` | 1026 | init 体检 | 坏头记失败；USING 扇区最多 1 个；首个 EMPTY 作 cur_sec 候选 |
| `format_all_cb` | 1057 | 逐扇区 format | |
| `tsl_format_all` | 1071 | 整库格式化 | oldest/cur_sec/last_time 归零重建 |

## 三、关键摘录

1. **TSL 提交点**（write_tsl 内，457 行）：

```c
_FDB_WRITE_STATUS(db, idx_addr, idx.status_table, FDB_TSL_STATUS_NUM, FDB_TSL_WRITE, true);
```

2. **数据地址（定长模式倒推）**（read_tsl 内，177 行）：

```c
tsl->addr.log = sector_addr + db_sec_size(db) - (tsl_index_in_sector + 1) * FDB_WG_ALIGN(FDB_TSDB_FIXED_BLOB_SIZE);
```

索引序号 n 的数据块占据"扇区尾往上第 n+1 块"——索引向下、数据向上的相向布局。

3. **时间戳单调检查**（tsl_append 内，578-581 行附近）：

```c
if (cur_time <= db->last_time) {
    FDB_INFO("Warning: current timestamp ... This tsl will be dropped.\n", ...);
    return FDB_WRITE_ERR;
}
```

## 四、已知问题/TODO（仅记录）

- `read_sector_info` 中 283-286 行：`end_info` 双副本都停在 PRE_WRITE 时 `FDB_ASSERT(0)` 直接挂死（注释自述 TODO，需要用户层避免封口时频繁断电）。
- `fdb_tsl_iter`/`iter_by_time` 里 `db_init_ok` 检查失败只打印不 return（574-576、713-715 行），继续走可能读到脏数据——注意 init 失败后不要调遍历。
- `fdb_tsl_iter_by_time` 的 `search_start_tsl_addr` 假设扇区内时间严格递增；若用户混用 `append_with_ts` 塞乱时间戳，二分可能漏数据。
