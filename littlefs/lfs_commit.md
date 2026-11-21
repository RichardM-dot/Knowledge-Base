# lfs_commit —— 元数据对与提交日志（lfs.c 核心区段）

对应源文件：`lfs.c`（共 7314 行）之 **tag 系统 + 元数据对读取/提交区段**
（约 476-607、949-3001 行；缓存层与分配器见 [lfs_fs_ops.md](lfs_fs_ops.md)）

本条覆盖 littlefs 的"心脏"：一切元数据（含 superblock）的读取与原子提交。
深度解析见 `Docs/02-元数据对与提交日志.md`；掉电恢复见
[lfs_fs_ops.md](lfs_fs_ops.md) 与 `Docs/03-掉电一致性与deorphan.md`。

## 1. tag 系统（行 476-552）

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_tag_isvalid` | 476 | valid 位判定（盘上首位是反相） |
| `lfs_tag_isdelete` | 485 | size 全 1 => 删除标记 |
| `lfs_tag_type1/2/3` | 490/495/500 | 3/4/11 位类型抽取 |
| `lfs_tag_chunk/splice` | 505/510 | 子类型 / id 平移量 |
| `lfs_tag_id/size` | 515/520 | 文件 id（0x3ff=无 id）/ 数据长度 |
| `lfs_tag_dsize` | 529 | 盘上占位（`tag+isdelete` 进位技巧） |

宏 `LFS_MKTAG`（行 466）：`(type<<20)|(id<<10)|size`。

```c
// 摘录自 lfs.c:529
static inline lfs_size_t lfs_tag_dsize(lfs_tag_t tag) {
    return sizeof(tag) + lfs_tag_size(tag + lfs_tag_isdelete(tag));
}
```

## 2. gstate 全局状态（行 557-612）

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_gstate_xor` | 557 | delta 的 XOR 合并（全局状态=全链 XOR 和） |
| `lfs_gstate_iszero` | 564 | 无待处理状态 |
| `lfs_gstate_hasorphans/getorphans` | 572/577 | 孤儿计数（tag 低 9 位） |
| `lfs_gstate_hasmove/hasmovehere` | 582/593 | move 中间态 |
| `lfs_gstate_needssuperblock` | 588 | 旧版本盘标记（bit9） |
| `lfs_gstate_fromle32/tole32` | 599/607 | 端序 |

## 3. 元数据对读取族

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_dir_getslice` | 949 | 反向扫 tag 链找最新匹配（读路径核心） |
| `lfs_dir_get` | 1022 | getslice 整段读封装 |
| `lfs_dir_getread` | 1034 | inline 文件当块设备读（LFS_BLOCK_INLINE 哨兵） |
| `lfs_dir_traverse_filter` | 1099 | compact 过滤旧版本回调 |
| `lfs_dir_traverse` | 1170 | 提交迭代器（显式栈，3 层深） |
| `lfs_dir_fetchmatch` | 1376 | **对读取核心**：rev 裁决+CRC 校验+tag 匹配 |
| `lfs_dir_fetch` | 1674 | 纯 fetch（mask=-1 永不匹配的技巧） |
| `lfs_dir_getgstate` | 1687 | 读 MOVESTATE delta 并 XOR 进 gstate |
| `lfs_dir_getinfo` | 1710 | 填 lfs_info |
| `lfs_dir_find_match` | 1755 | 按名字逐字节比对回调 |
| `lfs_dir_find` | 1794 | 路径解析（消化 . 与 ..） |

## 4. 提交族

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_dir_commitprog` | 1920 | 最小写单元（数据+CRC 推进） |
| `lfs_dir_commitattr` | 1943 | 写 tag+数据（盘上数据直通搬运） |
| `lfs_dir_commitcrc` | 2000 | 收尾：CCRC+FCRC+回读校验 |
| `lfs_dir_alloc` | 2159 | 新对分配（逆序分配+rev 对齐模数） |
| `lfs_dir_drop` | 2210 | 空对摘除（偷 gdelta+改前驱 tail） |
| `lfs_dir_split` | 2236 | 50% 分家 |
| `lfs_dir_commit_size/commit_commit` | 2272/2290 | traverse 回调 |
| `lfs_dir_needsrelocation` | 2297 | 磨损搬迁触发器 `(block_cycles+1)|1` |
| `lfs_dir_compact` | 2320 | 全量重写（擦 pair[1]→rev→条目→tail→gstate→CRC） |
| `lfs_dir_splittingcompact` | 2501 | 分家决策（40 字节保留量） |
| `lfs_dir_relocatingcommit` | 2620 | 追加/compact 二选一 + fixmlist |
| `lfs_dir_orphaningcommit` | 2801 | 搬迁连锁（父指针/前驱 tail） |
| `lfs_dir_commit` | 3001 | 总入口（完成后同步 deorphan） |

## 5. 用户目录 API

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_mkdir_` | 3031 | 建目录（对分配→链尾→父登记） |
| `lfs_dir_open_/close_` | 3131/3176 | 迭代状态 |
| `lfs_dir_read_` | 3189 | 合成 . 与 ..，跨块走 tail |
| `lfs_dir_seek_/tell_/rewind_` | 3239/3277/3283 | 条目序号坐标 |

## 6. 关键摘录：双块裁决

```c
// 摘录自 lfs.c:1392（lfs_dir_fetchmatch）
uint32_t revs[2] = {0, 0};
for (int i = 0; i < 2; i++) {
    int err = lfs_bd_read(lfs, NULL, &lfs->rcache, sizeof(revs[i]),
            pair[i], 0, &revs[i], sizeof(revs[i]));
    revs[i] = lfs_fromle32(revs[i]);
    ...
    if (err != LFS_ERR_CORRUPT &&
            lfs_scmp(revs[i], revs[(i+1)%2]) > 0) {
        r = i;                       // rev 新者排前
    }
}
dir->pair[0] = pair[(r+0)%2];
dir->pair[1] = pair[(r+1)%2];
```

`lfs_scmp`（lfs_util.h:226）= `(int)(unsigned)(a-b)`：序列算术，
回绕也能裁决新旧——元数据对可能被擦写 2^32 次。

## 7. 关键摘录：CRC 收尾的 self-check

```c
// 摘录自 lfs.c:2135（lfs_dir_commitcrc 末段）
// make sure to check crc in case we happen to pick
// up an unrelated crc (frozen block?)
err = lfs_bd_crc(lfs,
        NULL, &lfs->rcache, sizeof(uint32_t),
        commit->block, off1, sizeof(uint32_t), &crc);
if (err) {
    return err;
}

if (crc != 0) {
    return LFS_ERR_CORRUPT;
}
```

CRC(crc)==0 的自校验性质：防止回读时"捡到"块里无关的旧 CRC
（frozen superblock 场景），是提交完成的最终判据。

## 8. 互链

- 配套：[lfs_h.md](lfs_h.md)（`lfs_mdir_t` 字段表）、
  [lfs_fs_ops.md](lfs_fs_ops.md)（缓存层/孤儿回收）、
  [lfs_file_ctz.md](lfs_file_ctz.md)（文件数据）
- 深度：`Docs/02-元数据对与提交日志.md`、`Docs/05-存储布局总图.md`
