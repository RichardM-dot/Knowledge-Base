# flashdb.h —— 公有 API 契约

对应源文件：`references/filesystems/FlashDB/inc/flashdb.h`（共 186 行，A级 API 契约中文注）

整个库的对外门面：7 个生命周期函数 + 2 个 blob 函数 + 11 个 KV 函数 + 10 个 TSDB 函数 + 1 个工具函数。
互链：[[fdb_def]]（结构）、[[fdb_kvdb]]/[[fdb_tsdb]]（实现）、[[fdb_utils]]（blob/CRC 实现）、Docs《07-API与实测指南》。

## 一、头文件结构摘录（15-26 行）

```c
#include <stdint.h>
...
#include <fdb_cfg.h>          /* 用户配置（由 fdb_cfg_template.h 改名而来） */

#ifdef FDB_USING_FAL_MODE
#include <fal.h>              /* FAL 模式才拉入 FAL 头 */
#endif

#include <fdb_def.h>          /* 结构与枚举定义 */
```

`fdb_cfg.h` 是全库编译裁剪的总开关，缺它直接编译失败；`fal.h` 只在 FAL 模式引入，文件模式不依赖 FAL。

## 二、API 全表（声明行 = 注释后真实行号）

### 生命周期（40-70 行）

| 声明行 | API | 契约要点 |
|---|---|---|
| 40 | `fdb_kvdb_init(db, name, path, default_kv, user_data)` | path=FAL 分区名/目录；default_kv 可 NULL；几何 control 须先行 |
| 46 | `fdb_kvdb_control(db, cmd, arg)` | cmd 见 fdb_def.h 的 FDB_KVDB_CTRL_* |
| 51 | `fdb_kvdb_check(db)` | 全库 CRC 体检 |
| 55 | `fdb_kvdb_deinit(db)` | 文件模式关句柄 |
| 61 | `fdb_tsdb_init(db, name, path, get_time, max_len, user_data)` | get_time 须单调；max_len<扇区 |
| 66 | `fdb_tsdb_control(db, cmd, arg)` | SET_ROLLOVER 须 init 后 |
| 70 | `fdb_tsdb_deinit(db)` | |

### blob 通用（76-81 行）

| 声明行 | API | 契约要点 |
|---|---|---|
| 76 | `fdb_blob_make(blob, buf, len)` | 零拷贝包装；返回 blob 支持链式 |
| 81 | `fdb_blob_read(db, blob)` | 读 min(size, saved.len)；失败 0 |

### KV（87-131 行）

| 声明行 | API | 契约要点 |
|---|---|---|
| 87 | `fdb_kv_set(db, key, value)` | 字符串封装；value=NULL 删除 |
| 92 | `fdb_kv_get(db, key)` | 废弃：static 缓冲不可重入 |
| 96 | `fdb_kv_set_blob(db, key, blob)` | blob.buf=NULL 即删除 |
| 101 | `fdb_kv_get_blob(db, key, blob)` | 返回实际字节数；saved.len 回填真实值长 |
| 105 | `fdb_kv_del(db, key)` | 日志式删除 |
| 110 | `fdb_kv_get_obj(db, key, kv)` | 只取元信息 |
| 114 | `fdb_kv_to_blob(kv, blob)` | 填 saved 三字段 |
| 118 | `fdb_kv_set_default(db)` | 恢复出厂 |
| 122 | `fdb_kv_print(db)` | 调试打印 |
| 126 | `fdb_kv_iterator_init(db, itr)` | 用户态迭代器 |
| 131 | `fdb_kv_iterate(db, itr)` | true=取到一条 false=遍历完 |

### TSDB（137-180 行）

| 声明行 | API | 契约要点 |
|---|---|---|
| 137 | `fdb_tsl_append(db, blob)` | 时间戳自动取 get_time()，须严格递增 |
| 141 | `fdb_tsl_append_with_ts(db, blob, ts)` | 显式时间戳，补录用 |
| 145 | `fdb_tsl_iter(db, cb, arg)` | cb 返回 true 中断；回调内勿调写 API |
| 149 | `fdb_tsl_iter_reverse(db, cb, arg)` | 从最新向最老遍历 |
| 154 | `fdb_tsl_iter_by_time(db, from, to, cb, arg)` | from>to 自动反向；扇区剪枝+二分 |
| 158 | `fdb_tsl_query_count(db, from, to, status)` | 区间内指定状态条数 |
| 162 | `fdb_tsl_max_blob_count(db)` | 容量估算 |
| 166 | `fdb_tsl_set_status(db, tsl, status)` | 状态只能单向推进 |
| 170 | `fdb_tsl_clean(db)` | 清空整库，不可逆 |
| 174 | `fdb_tsl_to_blob(tsl, blob)` | TSL -> blob |
| 180 | `fdb_calc_crc32(crc, buf, size)` | 首调传 0，续算传上次返回值 |

## 三、错误码总表（fdb_err_t，实现见各文件）

| 码 | 语义 | 常见来源 |
|---|---|---|
| FDB_NO_ERR | 成功 | — |
| FDB_ERASE_ERR | 擦除失败 | 底层驱动 |
| FDB_READ_ERR | 读失败 / not_formatable 挂载失败 | 底层驱动 / 挂载体检 |
| FDB_WRITE_ERR | 写失败 / TSL 超限或时间不递增 | 底层驱动 / tsl_append |
| FDB_PART_NOT_FOUND | 分区不存在 | _fdb_init_ex |
| FDB_KV_NAME_ERR | 键不存在或超 64B | del_kv / create_kv_blob |
| FDB_KV_NAME_EXIST | 键已存在 | （保留码，当前主流程未用） |
| FDB_SAVED_FULL | 空间不足 | alloc_kv / create_kv_blob / TSDB 非 rollover 满 |
| FDB_INIT_FAILED | 初始化失败 | 几何校验 / 挂载 |

## 四、使用守则

1. 所有句柄（fdb_kvdb/fdb_tsdb/fdb_blob/fdb_kv/fdb_tsl）放静态区或调用栈现场，**blob 不跨函数长期持有**。
2. 多线程：init 前注册 lock/unlock；iter 回调里禁止再调本库写 API。
3. 错误处理：init 返回非 FDB_NO_ERR 后所有 API 拒绝服务；set/append 的返回值必须检查。
4. 示例与坑点：Docs《07-API与实测指南》第 3/4/6 节。

## 五、API 使用范式三则

### 5.1 KV 读-改-写闭环

```c
struct fdb_blob blob;
int cfg = 0;
fdb_kv_get_blob(db, "cfg", fdb_blob_make(&blob, &cfg, sizeof(cfg)));
if (blob.saved.len == sizeof(cfg)) { /* 命中 */ } else { cfg = 默认值; }
cfg++;
fdb_kv_set_blob(db, "cfg", fdb_blob_make(&blob, &cfg, sizeof(cfg)));
```

### 5.2 TSDB 追加-查询-回读闭环

```c
fdb_tsl_append(db, fdb_blob_make(&blob, &sample, sizeof(sample)));
fdb_tsl_iter_by_time(db, t1, t2, cb, db);   /* cb 里 tsl_to_blob + blob_read */
```

### 5.3 状态标记

```c
/* 上传线程: 查未上传的条数 */
size_t n = fdb_tsl_query_count(db, 0, now, FDB_TSL_WRITE);
/* 上传后逐条标记（状态只能向前推） */
fdb_tsl_set_status(db, tsl, FDB_TSL_USER_STATUS1);
```

## 六、API 与内部函数的对应关系

| 公有 API | 实现主体 | 内部依赖 |
|---|---|---|
| fdb_kv_set_blob | set_kv | find_kv/del_kv/create_kv_blob/gc |
| fdb_kv_get_blob | get_kv | find_kv（缓存或全扫） |
| fdb_kv_iterate | （独立实现） | read_sector_info/get_next_kv_addr |
| fdb_tsl_append | tsl_append | update_sec_status/write_tsl |
| fdb_tsl_iter_by_time | （独立实现） | search_start_tsl_addr |
| fdb_blob_read | fdb_utils.c:318 | _fdb_flash_read |
