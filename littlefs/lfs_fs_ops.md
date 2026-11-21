# lfs_fs_ops —— 缓存层 / 分配器 / 文件系统级操作（lfs.c 底座区段）

对应源文件：`lfs.c`（共 7314 行）之 **块缓存层 + 分配器 + fs 级操作 +
v1 迁移 + 公共壳层**（约 67-369、382-455、681-740、803-948、4762-6060、
6069-6693 行；强耦合合并说明：这些区段共享 mlist/lookahead 状态，故合为一条）

深度解析见 `Docs/03-掉电一致性与deorphan.md`、`Docs/04-磨损均衡与块分配.md`、
`Docs/07-移植与实测指南.md`。

## 1. 块缓存层（行 67-369）——"RAM 换小 IO"

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_cache_drop` | 67 | 作废缓存（不清 buffer，惰性失效） |
| `lfs_cache_zero` | 80 | 清缓存为 0xff（擦除态，防信息泄漏） |
| `lfs_bd_read` | 96 | 双缓存读：pcache>rcache>驱动；hint 预读 |
| `lfs_bd_cmp` | 190 | 盘内容 vs RAM 逐字节比较 |
| `lfs_bd_crc` | 222 | 流式 CRC（8 字节段经缓存） |
| `lfs_bd_flush` | 250 | pcache 落盘 + 可选回读校验（坏块第一现场） |
| `lfs_bd_sync` | 292 | flush + 驱动 sync（掉电一致性物理边界） |
| `lfs_bd_prog` | 314 | 缓存写：写满即冲刷 |
| `lfs_bd_erase` | 369 | 擦除透传 |

## 2. 小工具区（行 382-455、627-677、681-740）

| 组 | 行号 | 内容 |
| --- | --- | --- |
| 路径 | 382-406 | namelen/islast/isdir（.. 消化在 dir_find） |
| pair | 407-455 | swap/isnull/cmp(弱比较)/issync(严格)/端序 |
| 端序 | 627-677 | fcrc/ctz/superblock 的 le 转换 |
| mlist | 681-712 | isopen(断言)/remove/append |
| 版本 | 717-740 | disk_version 及主次号（LFS_MULTIVERSION） |

## 3. 块分配器（行 803-948）

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_alloc_ckpoint` | 803 | "无在途分配"检查点（满盘判定依据） |
| `lfs_alloc_drop` | 814 | 清 lookahead（挂载后随机起点） |
| `lfs_alloc_lookahead` | 825 | traverse 回调：在用块标位图 |
| `lfs_alloc_scan` | 845 | 窗口绕圈推进 + 全盘重扫 |
| `lfs_alloc` | 882 | 分配主函数（位图→扫描→NOSPC） |

关键摘录：满盘判定的唯一出口

```c
// 摘录自 lfs.c:921（lfs_alloc）
if (lfs->lookahead.ckpoint <= 0) {
    LFS_ERROR("No more free space 0x%"PRIx32,
            (lfs->lookahead.start + lfs->lookahead.next)
                % lfs->block_count);
    return LFS_ERR_NOSPC;
}
```

## 4. 初始化与格式化/挂载（行 4762-5251）

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_init` | 4762 | 配置断言墙 + 三缓存分配 + 上限解析 |
| `lfs_deinit` | 4938 | 释放非静态缓存 |
| `lfs_format_` | 4964 | 根对 + 超块 + 强制 compact 防旧盘残留 |
| `lfs_tortoise_detectcycles` | 5042 | Brent 算法防 tail 链成环 |
| `lfs_mount_` | 5068 | 扫超块链 + 版本校验 + gstate 聚合 + seed 随机起点 |
| `lfs_unmount_` | 5241 | = deinit |

`lfs_init` 的断言墙值得逐条读（配置错误在挂载时全部暴露）：
对齐倍数、`block_size>=128`（CTZ 指针上限的精确公式断言）、
`block_cycles!=0`、compact_thresh/metadata_max/inline_max 约束。

## 5. 遍历与拓扑查询（行 5300-5506）

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_fs_stat_` | 5252 | fsinfo（旧版本盘时读盘上版本） |
| `lfs_fs_traverse_` | 5300 | 全盘在用块遍历（分配器/统计/收缩的地基） |
| `lfs_fs_pred` | 5409 | 目录链找前驱 |
| `lfs_fs_parent_match` | 5449 | DIRSTRUCT 指针比对回调 |
| `lfs_fs_parent` | 5475 | 全链反查父目录 |

`lfs_fs_traverse_` 覆盖三类在用块：tail 链元数据对、文件 CTZ 块、
**打开中脏文件的 RAM 分支**（未提交也算占用）。

## 6. 修复三件套（行 5508-5801）——"没有 fsck 的文件系统"

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_fs_prepsuperblock` | 5508 | 置/清 needssuperblock 位 |
| `lfs_fs_preporphans` | 5520 | 孤儿计数 ±1 + valid 位同步 |
| `lfs_fs_prepmove` | 5537 | 设置/清除 move 记录 |
| `lfs_fs_desuperblock` | 5548 | 首写前重写超块（就地升级旧版本盘） |
| `lfs_fs_demove` | 5593 | 掉电遗留 move：删源即完成移动 |
| `lfs_fs_deorphan` | 5637 | 两遍扫描：pass0 half-orphan / pass1 全孤儿 |
| `lfs_fs_forceconsistency` | 5777 | desuperblock→demove→deorphan(true) |
| `lfs_fs_mkconsistent_` | 5802 | 上述三件套 + 提交 gstate 差量 |

## 7. 统计/收缩/扩容（行 5832-6060）

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_fs_size_count/size_` | 5832/5844 | 在用块计数（best effort） |
| `lfs_fs_gc_` | 5862 | 一致性+压实超阈值元数据+预热分配器 |
| `lfs_shrink_checkblock` | 5918 | 收缩校验回调 |
| `lfs_fs_grow_` | 5932 | 扩容重写超块（不可回收） |

## 8. v1 迁移（行 6069-6693，需 LFS_MIGRATE）

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs1_crc/bd_read/bd_crc` | 6069/6074/6083 | v1 盘的 IO 包装 |
| `lfs1_*_fromle32/tole32` | 6101-6137 | v1 结构端序 |
| `lfs1_entry_size` | 6140 | 4+elen+alen+nlen |
| `lfs1_dir_fetch/next` | 6149/6214 | v1 目录读取（整块 CRC） |
| `lfs1_traverse` | 6249 | v1 在用块遍历 |
| `lfs1_moved` | 6323 | v1 move 标记全盘查重 |
| `lfs1_mount/unmount` | 6367/6431 | v1 挂载（迁移第一步） |
| `lfs_migrate_` | 6445 | 逐目录重建 v2 对 + 重写超块 |

迁移的掉电安全：v1 数据只读不动（只追加），任何一步掉电重跑即可。

## 9. 公共壳层（行 6510 起）

每个 `lfs_*` 公共函数 = `LFS_LOCK` → `LFS_TRACE(入参)` → 内部实现 →
`LFS_TRACE(返回)` → `LFS_UNLOCK`。`LFS_THREADSAFE` 时锁粒度为整个调用。

## 10. 互链

- 结构定义：[lfs_h.md](lfs_h.md)
- 心脏区段：[lfs_commit.md](lfs_commit.md)；文件区段：[lfs_file_ctz.md](lfs_file_ctz.md)
- 驱动层：[lfs_bd.md](lfs_bd.md)；工具层：[lfs_util.md](lfs_util.md)
- 深度：`Docs/03`、`Docs/04`、`Docs/07`
