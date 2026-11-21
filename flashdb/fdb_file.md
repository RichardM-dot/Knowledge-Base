# fdb_file.c —— 文件模式后端（PC 调试用）

对应源文件：`references/filesystems/FlashDB/src/fdb_file.c`（共 330 行，B级文件头导读 + 函数注）

用文件系统模拟 Flash 分区，让 FlashDB 能在 PC/Linux 上开发调试。整个文件包在 `#ifdef FDB_USING_FILE_MODE` 里，嵌入式 FAL 模式下完全不编译。
互链：[[fdb_utils]]（调用它的三函数）、[[fdb_cfg_template]]、Docs《06-FAL与移植层》第 6 节。

## 一、模拟模型

- 一个数据库目录下，**每个扇区一个文件**：`<目录>/<库名>.fdb.<扇区号>`（库名截 8 字符）。
- 数据库内相对地址 addr 映射：扇区号 = `FDB_ALIGN_DOWN(addr, sec_size) / sec_size`，文件内偏移 = `addr % sec_size`。
- **erase = 把整个文件重写为 0xFF**（完整模拟"擦除态"语义，上层逻辑零改动）。
- 句柄缓存 `FDB_FILE_CACHE_TABLE_SIZE=2`（默认）：命中扇区直接用缓存 fd，未命中按 LRU 换入换出。

## 二、逐函数表（行号 = 函数定义行，注释后真实行号）

POSIX 版（FDB_USING_FILE_POSIX_MODE，open/read/lseek/close）：

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `get_db_file_path` | 32 | 地址 -> 文件路径 | `"%.*s.fdb.%d"`；路径超长 FDB_ASSERT(0) |
| `get_file_from_cache` | 58 | 按扇区号查句柄缓存 | 未命中返回 -1 |
| `update_file_cache` | 68 | 句柄缓存更新 | 表满时整体后移一位、新句柄放表头（LRU） |
| `open_db_file` | 99 | 打开（或清空）扇区文件 | clean=true 时 O_TRUNC 清空；失败返回 -1 |
| `_fdb_file_read` | 135 | 读 | lseek + read，短读返回 FDB_READ_ERR |
| `_fdb_file_write` | 151 | 写 | sync=true 时 fsync（162 行）——掉电安全的关键 |
| `_fdb_file_erase` | 170 | 擦 | 32 字节 0xFF 缓冲循环重写 + fsync |

LIBC 版（FDB_USING_FILE_LIBC_MODE，fopen/fseek/fread/fwrite，`#elif` 分支）：

| 函数 | 行号 | 职责 | 与 POSIX 版差异 |
|---|---|---|---|
| `get_file_from_cache` | 194 | 查缓存 | FILE* 判 NULL |
| `update_file_cache` | 204 | 更新缓存 | fclose 替代 close |
| `open_db_file` | 237 | 打开文件 | "wb+" 清空 / "rb+" 读写 |
| `_fdb_file_read` | 273 | 读 | fread(buf, size, 1, fp) |
| `_fdb_file_write` | 287 | 写 | fflush 替代 fsync（296 行） |
| `_fdb_file_erase` | 304 | 擦 | fwrite 0xFF + fflush |

两套实现共用 `get_db_file_path` 与同样的映射规则，行为一致。

## 三、关键摘录

1. **路径生成**（`get_db_file_path` 内，40-47 行附近）：

```c
char file_name[DB_NAME_MAX + 4 + 10];
uint32_t sec_addr = FDB_ALIGN_DOWN(addr, db->sec_size);
int index = sec_addr / db->sec_size;

snprintf(file_name, sizeof(file_name), "%.*s.fdb.%d", DB_NAME_MAX, db->name, index);
```

2. **擦除模拟**（POSIX `_fdb_file_erase` 内，176-186 行附近）：

```c
lseek(fd, 0, SEEK_SET);
for (i = 0; i * BUF_SIZE < size; i++)
{
    memset(buf, 0xFF, BUF_SIZE);
    write(fd, buf, BUF_SIZE);
}
memset(buf, 0xFF, BUF_SIZE);
write(fd, buf, size - i * BUF_SIZE);
fsync(fd);
```

3. **LRU 换入换出**（`update_file_cache` POSIX 版，84-95 行附近）：

```c
} else {
    /* cache is full, move to end */
    for (int i = FDB_FILE_CACHE_TABLE_SIZE - 1; i > 0; i--) {
        close(db->cur_file[i]);
        memcpy(&db->cur_file[i], &db->cur_file[i - 1], sizeof(db->cur_file[0]));
        ...
    }
    /* add to head */
    db->cur_file[0] = fd;
```

表满时从尾部关句柄、整体后移、新句柄插表头——数组版 LRU，O(表长) 但表长只有 2。

## 四、陷阱与互链

- 库名经 `%.*s` 截 8 字符，同名前缀的两个库（如 "env_kvdb1"/"env_kvdb2"）在文件模式下文件名可能只差序号，注意隔离目录。
- 首次运行目录里没有扇区文件，`open(O_RDWR)` 失败 -> 读返回 FDB_READ_ERR -> 上层挂载体检判为坏扇区 -> 自动 format 创建文件，这是正常流程。
- 掉电测试必须针对"fsync 后断电"，进程被杀不算数。
- FAL 模式下本文件不编译；抽象入口在 [[fdb_utils]] 的 `_fdb_flash_read/erase/write`。
- 初始化时的差异见 Docs《01-初始化与分区管理》5.1 节。

## 五、一次跨扇区写操作的句柄周转

设 FDB_FILE_CACHE_TABLE_SIZE=2，顺序写扇区 3 -> 4 -> 5 -> 3：

| 步骤 | 缓存表内容（扇区:fd） | 动作 |
|---|---|---|
| 初始 | {} | — |
| 写扇区 3 | {3:fd1} | open(3) 缓存 |
| 写扇区 4 | {3:fd1, 4:fd2} | open(4) 缓存 |
| 写扇区 5 | {4:fd2, 5:fd3} | 3 被 LRU 挤出（close fd1） |
| 回写扇区 3 | {5:fd3, 3:fd1} | 4 被挤出（close fd2） |

每换一个扇区一次 close+open；`db->cur_sec` 记录"最近操作扇区"（fdb_def.h:288 附近字段），部分路径用它免查找。

## 六、与 FAL 后端的行为差异对照

| 行为 | FAL 后端 | 文件后端 |
|---|---|---|
| 写后持久性 | 立即（硬件） | sync=true 才 fsync/fflush |
| 擦除实现 | 真实擦除 | 重写 0xFF（快但占盘） |
| 地址空间 | 分区内相对地址 | 目录内"文件号+偏移" |
| 坏块 | 可能真实存在 | 不存在（文件系统兜底） |

PC 上调通的逻辑搬到嵌入式，行为差异集中在"擦除耗时"与"掉电窗口"，其余语义一致。
