# DeskHop 源码笔记：constants.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/constants.c`（共 61 行）

> 只存放一张 CRC32 查找表（256 项 × 4B = 1KB），供 `utils.c:crc32_iter()` 使用。
> 没有函数、没有状态，是本工程最简单的编译单元。

相关文档：[06-PIO与底层外设](../../../references/firmware/DeskHop/Docs/06-PIO与底层外设.md)

## 1. 文件结构

| 区段 | 行号 | 内容 |
|------|------|------|
| 许可证 | L1-L10 | GPLv3 头 |
| 中文导读（新增） | L12-L15 | B 级文件导读 |
| include | L16 | `#include "main.h"` |
| 表注释 | L18 | `/* CRC32 Lookup Table, Polynomial = 0xEDB88320 */` |
| 表定义 | L28-L61 | `const uint32_t crc32_lookup_table[]` |

```c
/* CRC32 Lookup Table, Polynomial = 0xEDB88320 */
const uint32_t crc32_lookup_table[] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
```

（L18-L20）

最后一项：

```c
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};
```

（L60-L61）

## 2. 多项式与生成方式

- 多项式：`0xEDB88320`，即标准 CRC-32/ISO-HDLC 的**反射**形式
  （原多项式 `0x04C11DB7` 按位反转）。
- 表项 256 个，每项覆盖一个字节的 8 次移位。
- 生成逻辑（等价伪代码）：

```text
for i in 0..255:
    crc = i
    for bit in 0..7:
        if crc & 1: crc = (crc >> 1) ^ 0xEDB88320
        else:       crc >>= 1
    table[i] = crc
```

`crc32_iter(crc, byte)` 的更新式：

```c
return crc32_lookup_table[(byte ^ crc) & 0xff] ^ (crc >> 8);
```

（`src/utils.c` L52-L55）

初值 `0xffffffff`，最终取反，得到标准 CRC32 结果。

## 3. 内存与性能

| 项 | 数值 |
|----|------|
| 表项数 | 256 |
| 单表项 | 4B |
| 总大小 | 1KB（Flash 常量区） |
| 单字节计算 | 1 次查表 + 1 次异或 + 1 次移位 |
| 256KB 固件 CRC | 262144 次迭代 |

`calculate_firmware_crc32()` 对 256KB 减 4KB 做 CRC，约 25.8 万次迭代。
在 120MHz Cortex-M0+ 上，每次约 6~8 周期，合计约 1.5~2ms。升级收尾时执行一次，
可接受；若频繁校验会成为瓶颈。

## 4. 坑点

1. **表在 RAM 还是 Flash**：`const` 且未加 `__not_in_flash`，默认留在 Flash，
   通过 XIP 读取。Flash 擦写期间不可访问——但 CRC 只在正常运行时算，安全。
2. **不要改成“运行时生成”**：省 1KB Flash，但启动多花 256×8 次循环，且增加代码。
3. **多项式方向**：`0xEDB88320` 是反射式；若误用 `0x04C11DB7`，结果与
   `crc32.py`/标准工具不一致，固件校验必失败。
4. **表项数量必须是 256**：少一项会在 `(byte ^ crc) & 0xff == 255` 时越界读。
5. **本文件没有函数**，A/B 级注释只需文件头；逐函数表为空是正常的。

## 6. 自检方法

### 6.1 表项自洽性

用任意 CRC32 工具对字符串 `"123456789"` 计算，标准结果应为 `0xCBF43926`。
若把 `crc32_iter()` 从初值 `0xffffffff` 开始逐字节喂入，最后取反，应得到同一值。
这是验证多项式/字节序是否写错的最快方法。

### 6.2 与构建脚本对齐

`CMakeLists.txt` L146 调用 `misc/crc32.py` 生成 `.section_metadata`。该脚本必须与
固件使用同一多项式、初值、取反规则；否则：

- `calculate_firmware_crc32()` 与 UF2 携带的 CRC 不一致；
- 升级收尾会误判失败，擦首扇区进 BOOTSEL。

验证：对同一 `deskhop.bin` 分别跑 `crc32.py` 和固件里的 CRC 函数（可临时在
`setup.c` 打印），结果应一致。

### 6.3 性能估算

每次迭代：1 次数组索引（XIP 读，可能命中缓存）+ 1 次 XOR + 1 次右移。
RP2040 无数据缓存，XIP 读有等待周期；1KB 表若频繁访问，建议放到 RAM
（`__not_in_flash`）以稳定时延。当前 CRC 只在升级收尾跑一次，留在 Flash 足够。
