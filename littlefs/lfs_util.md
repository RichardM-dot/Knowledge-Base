# lfs_util —— CRC32 与工具层

对应源文件：`lfs_util.c`（共 55 行）、`lfs_util.h`（共 341 行）

littlefs 的"校验地基 + 可移植性垫片"。CRC32 是掉电安全的基石
（提交校验、坏块检测、伪随机 seed），工具层全部可被用户宏替换。

## 1. lfs_util.c：软件 CRC32（行 35）

```c
// 摘录自 lfs_util.c:35（lfs_crc）
uint32_t lfs_crc(uint32_t crc, const void *buffer, size_t size) {
    static const uint32_t rtable[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };

    const uint8_t *data = buffer;

    for (size_t i = 0; i < size; i++) {
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 0)) & 0xf];
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 4)) & 0xf];
    }

    return crc;
}
```

要点：

- 多项式 `0x04c11db7`（标准 CRC-32），初值 `0xffffffff`；
- **半字节查表**：rtable 仅 16 项 = 64 字节 ROM、零 RAM 表——
  嵌入式取向（256 项字节表更快但 ROM 翻 16 倍）；
- 每字节两步：先低 4 位、再高 4 位（`>>0`/`>>4`）；
- `-DLFS_CRC=my_crc` 可重定向到硬件 CRC 外设。

三大用途（lfs.c）：元数据 commit 的 CRC tag、`lfs_bd_cmp` 之外的
完整性回读、挂载时 `seed = crc(seed, 各对CCRC)` 造伪随机数。

## 2. lfs_util.h 逐函数表

### 配置开关（行 14-79）

| 宏/机制 | 行号 | 作用 |
| --- | --- | --- |
| `LFS_STRINGIZE` | 14 | 两级展开把宏值变字符串（引 LFS_CONFIG 头文件用） |
| `LFS_CONFIG` | 23 | 整包替换工具层（自带全部 utils） |
| `LFS_DEFINES` | 40 | 只附加宏定义 |

### 调试打印（行 81-140）

| 宏 | 行号 | 说明 |
| --- | --- | --- |
| `LFS_TRACE` | 81 | 全调用链跟踪（LFS_YES_TRACE 打开） |
| `LFS_DEBUG/WARN/ERROR` | 91/101/111 | 分级日志；`LFS_NO_*` 关闭 |
| 三元结构 `X -> X_` | 各组 | 自动补尾部 ""，兼容严格 __VA_ARGS__ |
| `LFS_ASSERT` | 127 | 默认 assert()；LFS_NO_ASSERT 关闭 |

### 位运算与数学（行 142-230）

| 函数 | 行号 | 用途 |
| --- | --- | --- |
| `lfs_max/lfs_min` | 142/147 | 参数不得有副作用 |
| `lfs_aligndown/alignup` | 157/162 | read/prog_size 对齐 |
| `lfs_npw2` | 172 | 最小 2 的幂（CTZ 跳距） |
| `lfs_ctz` | 194 | **末尾 0 计数**（跳表指针数 = ctz(n)+1） |
| `lfs_popc` | 209 | 1 计数（ctz_index 的 O(1) 公式） |
| `lfs_scmp` | 226 | 序列算术比较（rev 回绕裁决） |

### 端序（行 236-293）

| 函数 | 行号 | 说明 |
| --- | --- | --- |
| `lfs_fromle32/tole32` | 236/255 | 盘上数据（小端）⇄ 主机 |
| `lfs_frombe32/tobe32` | 265/284 | **仅 tag 用**（盘上唯一大端） |

### CRC 与内存（行 296-333）

| 项 | 行号 | 说明 |
| --- | --- | --- |
| `lfs_crc` 声明/内联 | 296/300 | LFS_CRC 重定向或外部实现 |
| `lfs_malloc/free` | 312/325 | LFS_MALLOC/Free 重定向；无堆返回 NULL |

## 3. 移植者的三个决策点

1. **CRC 后端**：默认软表（64B ROM）；有 CRC 外设就 `-DLFS_CRC`；
2. **日志后端**：`-DLFS_TRACE=my_log` 接到 RTT/UART；
3. **内存策略**：全静态缓冲（lfs_config 三 buffer）或 LFS_MALLOC 钩子。

## 4. 互链

- 消费者：[lfs_commit.md](lfs_commit.md)（CRC/端序/序列比较）、
  [lfs_file_ctz.md](lfs_file_ctz.md)（ctz/popc/npw2）、[lfs_fs_ops.md](lfs_fs_ops.md)
- 深度：`Docs/02-元数据对与提交日志.md`、`Docs/01-目录与文件模型.md`
