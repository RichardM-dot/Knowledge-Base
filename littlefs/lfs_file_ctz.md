# lfs_file_ctz —— CTZ 跳表与文件操作（lfs.c 文件区段）

对应源文件：`lfs.c`（共 7314 行）之 **CTZ 跳表 + 文件操作区段**
（约 3305-4388 行；attr 在 4659-4740 行）

文件的"inode"只有 8 字节（`{head,size}`），全部魔力在 CTZ 跳表。
深度解析见 `Docs/01-目录与文件模型.md`；元数据提交见
[lfs_commit.md](lfs_commit.md)。

## 1. CTZ 跳表四函数

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_ctz_index` | 3305 | **O(1) 数学核心**：偏移→(块号,块内偏移) |
| `lfs_ctz_find` | 3324 | 跳表查询（贪心跳跃 O(log n) 读） |
| `lfs_ctz_extend` | 3369 | 追加一块（COW 尾块 + 写 ctz(n)+1 个指针） |
| `lfs_ctz_traverse` | 3475 | 从尾到头遍历全部块（统计/扫描用） |

### 核心摘录：popcount 修正的 O(1) 索引

```c
// 摘录自 lfs.c:3305（lfs_ctz_index）
static int lfs_ctz_index(lfs_t *lfs, lfs_off_t *off) {
    lfs_off_t size = *off;
    lfs_off_t b = lfs->cfg->block_size - 2*4;
    lfs_off_t i = size / b;
    if (i == 0) {
        return 0;
    }

    i = (size - 4*(lfs_popc(i-1)+2)) / b;
    *off = size - b*i - 4*lfs_popc(i);
    return i;
}
```

推导依据（DESIGN.md）：第 n 块有 `ctz(n)+1` 个指针，
`Σ(ctz(i)+1) = 2n - popcount(n)`（OEIS A001511/A005187），
故文件大小与块号满足 `N = B*n - 4*(2n - popcount(n))`，
先按净容量 `b = B-8` 估 n'，再用 popcount 修正一次即精确。

## 2. 文件操作族

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_file_opencfg_` | 3529 | 打开实现（建条目/读元数据/attrs/缓存） |
| `lfs_file_open_` | 3703 | 默认配置版 |
| `lfs_file_close_` | 3716 | sync→摘 mlist→释放；ERRED 不提交 |
| `lfs_file_relocate` | 3743 | 写中数据整体搬块（坏块/outline） |
| `lfs_file_outline` | 3815 | inline 转 CTZ |
| `lfs_file_flush` | 3835 | RAM 状态对齐盘：丢读缓存/落写缓存 |
| `lfs_file_sync_` | 3923 | 提交：flush→bd_sync→元数据 commit |
| `lfs_file_flushedread` | 3994 | 读执行体（ctz_find 定位+缓存拷出） |
| `lfs_file_read_` | 4056 | 读入口（先 flush 写） |
| `lfs_file_flushedwrite` | 4081 | 写执行体（追加只碰尾块） |
| `lfs_file_write_` | 4172 | 写入口（APPEND/补洞/FBIG） |
| `lfs_file_seek_` | 4220 | 同块缓存命中免 flush |
| `lfs_file_truncate_` | 4279 | 缩退 inline/定位新尾；扩补 0 |
| `lfs_file_tell_/rewind_/size_` | 4362/4368/4381 | 纯 RAM 查询 |

## 3. flags 位语义（读代码必需）

| 位 | 含义 |
| --- | --- |
| `LFS_F_DIRTY` | 元数据与盘不一致（sync 时提交） |
| `LFS_F_DUSTY` | 其它句柄 sync 过本文件（需重新同步） |
| `LFS_F_WRITING` | 有未落盘的写（RAM 分支是最新真相） |
| `LFS_F_READING` | 读缓存有效 |
| `LFS_F_ERRED` | 写过程中出错（close/sync 拒绝提交） |
| `LFS_F_INLINE` | 数据在元数据对里（head=LFS_BLOCK_INLINE=-2） |

## 4. 关键摘录：提交顺序即掉电一致性

```c
// 摘录自 lfs.c:3932（lfs_file_sync_）
// before we commit metadata, we need sync the disk to make sure
// data writes don't complete after metadata writes
if (!(file->flags & LFS_F_INLINE)) {
    err = lfs_bd_sync(lfs, &lfs->pcache, &lfs->rcache, false);
    if (err) {
        return err;
    }
}
```

顺序固定为 **数据 sync → 元数据 commit**：
元数据（CTZSTRUCT 的 head/size）是文件数据的"指针"，必须最后提交；
反过来就会指向半写块。掉电一致性窗口由此被压到"一条元数据 commit"。

## 5. 关键摘录：追加零写放大

```c
// 摘录自 lfs.c:3430（lfs_ctz_extend）
index += 1;
lfs_size_t skips = lfs_ctz(index) + 1;
lfs_block_t nhead = head;
for (lfs_off_t i = 0; i < skips; i++) {
    nhead = lfs_tole32(nhead);
    err = lfs_bd_prog(lfs, pcache, rcache, true,
            nblock, 4*i, &nhead, 4);
    ...
```

新块只写自己的 `ctz(index)+1` 个指针（平均 2 个），
旧链一字节不动——追加写放大为 0，这是对 backwards 链表
+多层指针设计的直接回报。

## 6. 用户 attr API（行 4659-4740）

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_getattr_` | 4659 | USERATTR+type 切片读（root 特判） |
| `lfs_commitattr` | 4694 | 写/删公共体（size=0x3ff 删） |
| `lfs_setattr_` | 4719 | 上限校验后提交 |
| `lfs_removeattr_` | 4731 | 删除标记 |

## 7. 复杂度速查

| 操作 | 复杂度 | 说明 |
| --- | --- | --- |
| 顺序读 | O(n) 块读 + O(n log n) 跳跃被块粒度稀释 | 实测近线性 |
| 追加写 | O(1) 块分配 + 平均 2 指针 | 零写放大 |
| seek 后写 | 尾块 COW + 后续数据 flush 拷贝 | 随机写有代价 |
| inline 读 | 元数据对切片（无独立块 IO） | 小文件最快 |

## 8. 互链

- 上层：[lfs_h.md](lfs_h.md) `lfs_file_t` 字段表
- 底座：[lfs_commit.md](lfs_commit.md)（元数据提交）、[lfs_fs_ops.md](lfs_fs_ops.md)（分配器）
- 深度：`Docs/01-目录与文件模型.md`、`Docs/03-掉电一致性与deorphan.md`
