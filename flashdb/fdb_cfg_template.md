# fdb_cfg_template.h —— 用户配置模板

对应源文件：`references/filesystems/FlashDB/inc/fdb_cfg_template.h`（共 73 行，B级文件头导读块）

使用方法：把本文件改名为 `fdb_cfg.h` 放进头文件搜索路径再裁剪。flashdb.h 第 20 行 `#include <fdb_cfg.h>` 硬依赖它，全库的编译裁剪从这里开始。
互链：[[fdb_def]]（未在此列出的宏默认值）、[[fdb_low_lvl]]（FDB_WRITE_GRAN 的连锁反应）、Docs《06-FAL与移植层》第 3 节、Docs《01-初始化与分区管理》第 7 节。

## 一、配置项逐条表（行号 = 注释后真实行号）

| 配置项 | 行号 | 类型 | 说明 |
|---|---|---|---|
| `FDB_USING_KVDB` | 33 | 开关 | 启用键值数据库（fdb_kvdb.c 才编译） |
| `FDB_KV_AUTO_UPDATE` | 37 | 开关（默认关） | 版本号驱动的 KV 增量升级（依赖 `__ver_num__` 键，fdb_kvdb.c 的 kv_auto_update） |
| `FDB_USING_TSDB` | 41 | 开关 | 启用时序数据库（fdb_tsdb.c 才编译） |
| `FDB_TSDB_FIXED_BLOB_SIZE` | 47 | 开关+值（默认关） | TSDB 定长日志：每条索引省 8 字节；开启后所有 blob 必须等长且不可与变长混存 |
| `FDB_USING_FAL_MODE` | 50 | 模式三选一 | FAL 存储模式（嵌入式），要求定义 FDB_WRITE_GRAN |
| `FDB_WRITE_GRAN` | 55 | 必填（FAL 模式） | 写粒度 bit：NOR=1 / stm32f2/f4=8 / stm32f1=32 / stm32f7=64 / stm32h5=128 / stm32h7=256 |
| `FDB_USING_FILE_LIBC_MODE` | 59 | 模式三选一 | LIBC 文件模式（fopen/fread，可移植含 Windows） |
| `FDB_USING_FILE_POSIX_MODE` | 62 | 模式三选一 | POSIX 文件模式（open/read，类 Unix） |
| `FDB_BIG_ENDIAN` | 65 | 开关（默认小端） | magic 扫描（find_next_kv_addr）与状态表编码的字节序分支 |
| `FDB_PRINT` | 68 | 重定向 | 默认 printf；嵌入式重定向到自己的日志宏 |
| `FDB_DEBUG_ENABLE` | 70 | 开关 | 打开 FDB_DEBUG 调试日志（量大，量产建议关） |

## 二、编译期连锁反应

| 开关 | 直接效果 | 连锁效果 |
|---|---|---|
| FDB_USING_KVDB | fdb_kvdb.c 参与编译 | flashdb.h 暴露 KV API；RAM 增加 `sizeof(fdb_kvdb)`（含两张缓存表约 1KB） |
| FDB_USING_TSDB | fdb_tsdb.c 参与编译 | 暴露 TSL API；RAM 增加 `sizeof(fdb_tsdb)` |
| FDB_WRITE_GRAN=N | 状态表尺寸公式分支（fdb_low_lvl.h:34/36） | KV 头/扇区头大小、FDB_WG_ALIGN 单位、format_sector 写法全部变化 |
| FDB_USING_FILE_*_MODE | fdb_file.c 参与编译 | fdb.c 的 init 分支要求先 SET_SEC_SIZE/MAX_SIZE |
| FDB_KV_AUTO_UPDATE | kv_auto_update 参与编译 | init 流程多一步版本比对与补写 |

## 三、典型配置组合

| 场景 | 组合 |
|---|---|
| STM32 + SPI NOR（最常见） | KVDB + TSDB + FAL_MODE + WRITE_GRAN=1 |
| PC/Linux 开发调试 | KVDB + FILE_POSIX_MODE（linux demo 用法） |
| 仅存参数的小 MCU | 只开 KVDB + FAL_MODE（TSDB 代码不编译省 flash） |
| 固定长度传感器日志 | TSDB + FIXED_BLOB_SIZE=sizeof(结构体) |
| 量产（无调试口） | 去掉 FDB_DEBUG_ENABLE，自定义 FDB_PRINT |

## 四、坑点

- `FDB_WRITE_GRAN` 在 FAL 模式下未定义/未赋值：模板里它故意留空（55 行注释 `you must define it for a value`），忘记赋值会以 0 参与编译，状态表公式/对齐全部失真。
- 文件模式与 FAL 模式互斥：同时定义时不会编译报错，但 `db->file_mode` 标志决定走哪条路，混配后果自负。
- `FDB_DEBUG_ENABLE` 打开后每次挂载/写入都有日志，串口带宽紧张的板子先量再开。
- 修改本文件后所有用到 FlashDB 的 .c 都要重编（宏贯穿头文件链）。

## 五、配置示例（三种典型 fdb_cfg.h）

### 5.1 STM32F103 + 外挂 SPI NOR（W25Q64）

```c
#define FDB_USING_KVDB
#define FDB_USING_TSDB
#define FDB_USING_FAL_MODE
#define FDB_WRITE_GRAN                1
/* #define FDB_DEBUG_ENABLE          量产关闭 */
```

### 5.2 Linux PC 调试（demos/linux 用法）

```c
#define FDB_USING_KVDB
#define FDB_USING_TSDB
#define FDB_USING_FILE_POSIX_MODE
#define FDB_DEBUG_ENABLE
```

### 5.3 小容量片内 Flash 只用 KVDB

```c
#define FDB_USING_KVDB
#define FDB_USING_FAL_MODE
#define FDB_WRITE_GRAN                32   /* stm32f1 半页编程 */
```

## 六、裁剪核对清单（改完配置后过一遍）

- [ ] FAL 模式下 FDB_WRITE_GRAN 已赋值且与芯片编程单位一致；
- [ ] KVDB/TSDB 至少开一个（全关则 flashdb.h 只有 blob/CRC 可用）；
- [ ] FIXED_BLOB_SIZE 一旦量产不再修改（改了旧数据不兼容）；
- [ ] FDB_PRINT 已重定向到板级日志（否则 printf 可能硬Fault）；
- [ ] 相应分区已在 FAL 分区表规划好（KVDB 与 TSDB 各一个，勿共用）。

## 七、与代码宏的三处交叉校验点

1. `FDB_WRITE_GRAN` 的合法值由 fdb_kvdb.c:52 编译期校验（1/8/32/64/128/256 之外直接 #error）；
2. `FDB_KV_AUTO_UPDATE` 打开后必须在 fdb_kvdb_init 前设置 `db->ver_num`（fdb_def.h 的 fdb_kvdb 结构），否则升级逻辑以 0 为版本号；
3. 文件模式三选一与 demos 的对应：linux demo 用 POSIX，其他 Windows 工程用 LIBC。

## 八、反模式

| 反模式 | 后果 |
|---|---|
| 直接改 fdb_def.h 的默认宏代替 fdb_cfg.h | 升级库版本时被覆盖，配置丢失 |
| 用 #undef 改库内宏 | 破坏 ODR，行为未定义 |
| TSDB 定长与变长在同一分区混用 | 第二种写入直接 FDB_WRITE_ERR |
