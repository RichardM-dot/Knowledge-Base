# fdb_kvdb.c —— KVDB 键值数据库核心

对应源文件：`references/filesystems/FlashDB/src/fdb_kvdb.c`（共 2344 行，A级逐函数深注）

KVDB 是 FlashDB 的"设置参数库"：追加式日志 + 6 态 KV 状态机 + GC。本文件是全库最大也最核心的文件。
互链：[[fdb_def]]（结构定义）、[[fdb_utils]]（状态表/CRC）、[[fdb_tsdb]]（姊妹模式）、Docs《02-KVDB存储模型》《03-KVDB缓存与校验》《05-掉电安全设计》。

## 一、宏与常量（含魔法数字出处）

| 宏 | 值 | 行号 | 含义 |
|---|---|---|---|
| `SECTOR_MAGIC_WORD` | 0x30424446（"FDB1"） | 57 | 扇区头 magic |
| `KV_MAGIC_WORD` | 0x3030564B（"KV00"） | 59 | KV 节点头 magic，断链重同步锚点 |
| `GC_MIN_EMPTY_SEC_NUM` | 1 | 61 | GC 至少要用 1 个空扇区 |
| `FDB_SEC_REMAIN_THRESHOLD` | KV头+FDB_KV_NAME_MAX | 65 | 扇区转 FULL 的剩余空间阈值 |
| `FDB_GC_EMPTY_SEC_THRESHOLD` | 1 | 70 | 触发 GC 的空扇区阈值 |
| `FDB_STR_KV_VALUE_MAX_SIZE` | 128 | 75 | 旧字符串接口 static 缓冲大小 |
| `SECTOR_NOT_COMBINED` | 0xFFFFFFFF（NOR） | 84 | 扇区未合并标记 |
| `FAILED_ADDR` | 0xFFFFFFFF | 91 | 分配失败哨兵 |
| `VER_NUM_KV_NAME` | "__ver_num__" | 122 | KV 自动升级的版本号键名 |

结构体：`sector_hdr_data`（124 行，扇区头）、`kv_hdr_data`（140 行，节点头 18B@gran1）。几何宏：`SECTOR_HDR_DATA_SIZE`/`KV_HDR_DATA_SIZE`（97/101 行）按写粒度对齐后的实际头长。

## 二、逐函数表（行号 = 函数定义行，注释后真实行号）

### 缓存层（#ifdef FDB_KV_USING_CACHE）

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `update_sector_cache` | 181 | 扇区信息增改缓存 | check_ok=false 等价删除条目 |
| `get_sector_from_cache` | 213 | 按地址查扇区缓存 | 命中返回表内指针 |
| `update_sector_empty_addr_cache` | 232 | 追加后刷新 empty_kv/remain | 写路径每次调 |
| `update_sector_status_store_cache` | 247 | 同步扇区 store 状态 | |
| `update_kv_cache` | 265 | KV 缓存增/改/删三合一 | 键=name CRC16；类 LRU：active-- 老化，淘汰最小 |
| `get_kv_from_cache` | 319 | 查缓存并回读名字复核 | CRC16 碰撞靠 strncmp 兜底；命中 active+=64 |

### 链与查找层

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `find_next_kv_addr` | 359 | 32B 滑窗扫 magic 定位下节点 | 步进 28 重叠 4，防跨窗漏检（buf[32] 在 361 行） |
| `get_next_kv_addr` | 401 | KV 链步进 | CRC 坏节点从 +1 对齐处重新同步 magic |
| `read_kv` | 446 | 读节点+CRC 校验 | 458 行 len 非法检查 -> 写 ERR_HDR 拉黑；CRC 坏仍读名字告警 |
| `read_sector_info` | 525 | 读扇区头/深读统计 | 深读尾部 0xFF 校正 empty_kv（588-594 行） |
| `get_next_sector_addr` | 621 | 环形扇区步进 | combined 决定步长（TODO 未实现） |
| `kv_iterator` | 655 | 全库遍历骨架 | 回调返回 true 提前终止 |
| `find_kv_cb` | 693 | 查找过滤 | 等长+CRC OK+状态 WRITE |
| `find_kv_no_cache` | 717 | 全盘扫描查找 | 慢路径 |
| `find_kv` | 735 | 查找统一入口 | 缓存快路径 -> 失败回扫并回填 |
| `fdb_is_str` | 765 | 可打印字符判断 | 旧接口判串 |
| `get_kv` | 787 | 查找+读值 | min(buf_len, value_len) |

### 公有读 API

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `fdb_kv_get_obj` | 825 | 取 KV 元信息 | 持锁调 find_kv |
| `fdb_kv_to_blob` | 857 | KV -> blob | meta_addr/addr/len 三字段 |
| `fdb_kv_get_blob` | 879 | blob 读（推荐） | |
| `fdb_kv_get` | 915 | 字符串读（废弃） | static 缓冲不可重入 |

### 写路径

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `write_kv_hdr` | 947 | 两阶段写头① | PRE_WRITE 先行，再写 magic 后字段 |
| `format_sector` | 970 | 擦除+重建扇区头 | gran>1 拆多次写防重复编程（989 行注释） |
| `update_sec_status` | 1040 | 推进 EMPTY->USING->FULL | FULL 由 remain<阈值触发，出参 is_full |
| `sector_iterator` | 1084 | 扇区遍历骨架 | 可按状态过滤+可选深读 |
| `sector_statistics_cb` | 1110 | 统计 EMPTY/USING 数 | |
| `alloc_kv_cb` | 1129 | 选址过滤 | 有空间+非 dirty（或无 GC 请求） |
| `alloc_kv` | 1156 | 分配写入地址 | USING 优先；空扇区=阈值时不消耗留 GC |
| `del_kv` | 1192 | 两阶段删除 | 1216 PRE_DELETE / 1219 DELETED；1237 行顺带标脏扇区 |
| `move_kv` | 1268 | 搬移 KV（GC/恢复用） | 预删->PRE_WRITE->分块 32B 复制（1297 行 buf）->WRITE->删旧 |
| `new_kv` | 1338 | 分配+GC 重试 | 最多 GC 一轮（already_gc） |
| `new_kv_ex` | 1363 | 长度换算层 | 头+对齐名+对齐值 |
| `gc_check_cb` | 1373 | 探测空扇区 | |
| `do_gc` | 1397 | 单扇区 GC | 先置 DIRTY_GC（可恢复，1408 行）；PRE_DELETE 也搬 |
| `gc_collect_by_free_size` | 1446 | GC 入口 | 空扇区<=阈值才真正 GC；结束清 gc_request |
| `gc_collect` | 1474 | 全量 GC（恢复用） | |
| `create_kv_blob` | 1493 | 写一条完整 KV | CRC 1526-1538 含对齐 0xFF；提交点 1561 |
| `fdb_kv_del` | 1587 | 删除 API | 一步 DELETED |
| `set_kv` | 1618 | set 语义 | 预删->写新->彻底删->按需 GC |
| `fdb_kv_set_blob` | 1666 | blob 写 API（推荐） | |
| `fdb_kv_set` | 1699 | 字符串写 API | |
| `fdb_kv_set_default` | 1720 | 恢复出厂 | 清缓存+全扇区 format+重建默认表 |

### 打印/升级/恢复/init

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `print_kv_cb` | 1770 | 打印回调 | 两遍扫描判串（buf[32] 在 1784 行） |
| `fdb_kv_print` | 1824 | 打印全部 KV | 尾部打印容量统计 |
| `kv_auto_update` | 1856 | KV 增量升级 | 版本号变则补写缺失键 |
| `check_oldest_addr_cb` | 1893 | 定位最老扇区 | EMPTY->FULL/USING 跳变点 |
| `check_sec_hdr_cb` | 1914 | 扇区头体检 | 默认就地 format；not_formatable 则终止 |
| `check_and_recovery_gc_cb` | 1936 | 续跑中断 GC | 见 DIRTY_GC -> gc_collect |
| `check_and_recovery_kv_cb` | 1957 | 逐 KV 恢复 | PRE_DELETE 收敛 / 1972 行 PRE_WRITE 拉黑 / WRITE 预热缓存 |
| `_fdb_kv_load` | 2001 | 挂载主流程 | 全坏 -> set_default；GC 后 goto 重扫 |
| `fdb_kvdb_control` | 2048 | 参数控制 | SET 类断言 init_ok==false |
| `fdb_kvdb_init` | 2122 | 初始化 | 挂载在解锁状态做（2179 行 `result = _fdb_kv_load(db);`） |
| `fdb_kvdb_deinit` | 2208 | 反初始化 | |
| `fdb_kv_iterator_init` | 2227 | 迭代器初始化 | 从 oldest_addr 起 |
| `fdb_kv_iterate` | 2256 | 迭代器步进 | 只吐 WRITE+CRC OK 节点 |
| `fdb_kvdb_check` | 2305 | 全库体检 | 强制逐节点 read_kv |

## 三、关键摘录

1. **写入提交点**（create_kv_blob 内，1561-1562 行）：

```c
result = _fdb_write_status((fdb_db_t) db, kv_addr, kv_hdr.status_table, FDB_KV_STATUS_NUM, FDB_KV_WRITE,
        true);
```

sync=true 的 WRITE 是事务提交点；此前断电节点停在 PRE_WRITE，恢复时拉黑，旧值有效。

2. **两阶段删除**（del_kv 内，1216 与 1219 行）：

```c
result = _fdb_write_status((fdb_db_t)db, old_kv->addr.start, status_table, FDB_KV_STATUS_NUM, FDB_KV_PRE_DELETE, false);
...
result = _fdb_write_status((fdb_db_t)db, old_kv->addr.start, status_table, FDB_KV_STATUS_NUM, FDB_KV_DELETED, true);
```

3. **GC 恢复标记**（do_gc 内，1408 行）：

```c
_fdb_write_status((fdb_db_t)db, sector->addr + SECTOR_DIRTY_OFFSET, status_table, FDB_SECTOR_DIRTY_STATUS_NUM, FDB_SECTOR_DIRTY_GC, true);
```

DIRTY_GC 让"GC 做到一半断电"在挂载时被 `check_and_recovery_gc_cb` 识别并续跑。

4. **长度非法即拉黑**（read_kv 内，458 行）：

```c
if (kv->len == UINT32_MAX || kv->len > db_max_size(db) || kv->len < KV_HDR_DATA_SIZE) {
```

全 FF（半写）或越界的 len 直接把节点写为 ERR_HDR，之后遍历永久跳过。

## 四、已知问题/TODO（不改代码，仅记录）

- `Sector continuous mode`（超大 KV 跨扇区）：`get_next_kv_addr`/`read_kv` 等多处 `//TODO`，`combined` 字段已预留但未实现；单条 KV 上限 = 扇区大小 - 扇区头。
- `fdb_kv_get`（915 行）static 缓冲非重入，头注释自述 DEPRECATED。
- `del_kv` 内 `#else` 分支的 `DIRTY_STATUS_TABLE_SIZE` 少了 `FDB_` 前缀（1291 行附近），仅在 `KV_STATUS_TABLE_SIZE < FDB_DIRTY_STATUS_TABLE_SIZE` 配置下编译，默认配置不可达——记为潜在坑。
- GC 与写入在锁内串行执行，最坏延迟 = 整扇区搬移；无后台 GC 线程。
