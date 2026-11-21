# lfs.h —— littlefs 公共 API 与 RAM 数据结构

对应源文件：`lfs.h`（共 955 行，含中文深注）

littlefs 的全部对外契约集中在此：配置结构 `lfs_config` 是移植面，
`lfs_t/lfs_file_t/lfs_dir_t` 是 RAM 状态，其余是 POSIX 风格 API。
内部实现见 → [lfs_commit.md](lfs_commit.md)、[lfs_file_ctz.md](lfs_file_ctz.md)、
[lfs_fs_ops.md](lfs_fs_ops.md)。

## 1. 关键类型逐个读

| 类型 | 行号 | 一句话 |
| --- | --- | --- |
| `enum lfs_error` | 83 | 负错误码（借 POSIX errno 数值），一个 int 双语义 |
| `enum lfs_type` | 108 | tag 类型常量（type3 布局，与盘上一一对应） |
| `enum lfs_open_flags` | 148 | 打开标志；0x00010000+ 是内部状态位 |
| `struct lfs_config` | 199 | **移植核心**：4 回调 + 几何 + 静态缓冲 |
| `struct lfs_info` | 342 | stat/dir_read 产出 |
| `struct lfs_fsinfo` | 361 | 盘上版本/几何/上限 |
| `struct lfs_attr` | 387 | 自定义属性（8bit type） |
| `struct lfs_file_config` | 404 | per-file：静态缓冲 + attrs |
| `lfs_cache_t` | 432 | 缓存四元组 {block,off,size,buffer} |
| `lfs_mdir_t` | 450 | 元数据对 RAM 镜像（9 字段，见下） |
| `lfs_dir_t` | 466 | 打开目录（mlist 节点 + 迭代状态） |
| `lfs_file_t` | 486 | 打开文件（ctz/flags/cache/pos） |
| `lfs_gstate_t` | 526 | 全局状态 {tag,pair[2]}，XOR-delta 机制 |
| `lfs_t` | 542 | 文件系统对象：RAM 预算的锚点 |

`lfs_mdir_t` 字段语义（读 lfs.c 前必须背下来）：

| 字段 | 含义 |
| --- | --- |
| `pair[2]` | 两块地址，pair[0] 恒为最新 |
| `rev` | revision count（新旧裁决+磨损计数） |
| `off` | 日志尾偏移（追加位置） |
| `etag` | 日志尾上一条明文 tag（XOR 链头） |
| `count` | 本对条目数 |
| `erased` | 尾部是否仍处擦除态（能否直接追加） |
| `split` | 有无 hardtail（目录分家过） |
| `tail[2]` | 下一对指针（soft/hard） |

## 2. API 全表（速查）

### 生命周期
| 函数 | 行号 | 语义 |
| --- | --- | --- |
| `lfs_format` | 592 | 格式化（clobber，不保持挂载） |
| `lfs_mount` | 606 | 挂载（扫描超块链+聚合 gstate） |
| `lfs_unmount` | 613 | 释放资源 |

### 通用
| 函数 | 行号 | 错误码要点 |
| --- | --- | --- |
| `lfs_remove` | 624 | 目录须空 NOTEMPTY |
| `lfs_rename` | 636 | 目标须同类型；跨对原子 |
| `lfs_stat` | 644 | NOENT/NOTDIR |
| `lfs_getattr` | 660 | 返回盘上长度；NOATTR |
| `lfs_setattr` | 672 | 隐式创建；原子生效 |
| `lfs_removeattr` | 683 | 不存在也是 0 |

### 文件
| 函数 | 行号 | 备注 |
| --- | --- | --- |
| `lfs_file_open` | 698 | 默认配置（malloc 缓冲） |
| `lfs_file_opencfg` | 717 | 静态缓冲+attrs |
| `lfs_file_close` | 728 | 隐式 sync |
| `lfs_file_sync` | 735 | 数据先落盘再提交元数据 |
| `lfs_file_read` | 742 | 返回字节数，EOF=0 |
| `lfs_file_write` | 754 | close/sync 前只在 RAM |
| `lfs_file_seek` | 763 | 越界 INVAL |
| `lfs_file_truncate` | 771 | 缩小可退回 inline |
| `lfs_file_tell/rewind/size` | 779/786/793 | size 含未提交写 |

### 目录
| 函数 | 行号 | 备注 |
| --- | --- | --- |
| `lfs_mkdir` | 803 | 链尾插入+孤儿计数 |
| `lfs_dir_open/close` | 811/818 | 登记/摘除 mlist |
| `lfs_dir_read` | 826 | 返回 1/0/负（非字节数！） |
| `lfs_dir_seek/tell/rewind` | 835/844/850 | 坐标是条目序号 |

### 文件系统级
| 函数 | 行号 | 备注 |
| --- | --- | --- |
| `lfs_fs_stat` | 860 | 版本/几何/上限 |
| `lfs_fs_size` | 869 | best effort（COW 共享重复计） |
| `lfs_fs_traverse` | 879 | 回调非 0 中断遍历 |
| `lfs_fs_mkconsistent` | 892 | 显式清中间态，幂等 |
| `lfs_fs_gc` | 912 | 保洁三件事 |
| `lfs_fs_grow` | 928 | 扩容重写超块 |
| `lfs_migrate` | 946 | v1→v2（需 LFS_MIGRATE） |

## 3. 关键摘录：RAM 预算的结构化表达

```c
// 摘录自 lfs.h:542（lfs_t）
typedef struct lfs {
    lfs_cache_t rcache;          // 全局读缓存  cache_size
    lfs_cache_t pcache;          // 全局写缓存  cache_size
    lfs_block_t root[2];         // 根目录元数据对
    struct lfs_mlist { ... } *mlist;   // 打开的 file/dir（节点在用户对象里）
    uint32_t seed;               // CRC 聚合的伪随机（分配起点）
    lfs_gstate_t gstate;         // 全局状态三副本
    lfs_gstate_t gdisk;
    lfs_gstate_t gdelta;
    struct lfs_lookahead { ... } lookahead;  // 空闲位图窗口
    const struct lfs_config *cfg;
    lfs_size_t block_count, name_max, file_max, attr_max, inline_max;
} lfs_t;
```

RAM 合计 = `2*cache_size + lookahead_size + Σ每文件cache_size`，
与文件数量、盘容量**无关**——这就是"bounded RAM"的字面意思。

## 4. 互链

- 实现细节：[lfs_commit.md](lfs_commit.md)（元数据对）、
  [lfs_file_ctz.md](lfs_file_ctz.md)（CTZ）、[lfs_fs_ops.md](lfs_fs_ops.md)（挂载/孤儿）
- 深度文章：`Docs/00-总览与设计哲学.md`、`Docs/06-API逐条精读.md`
