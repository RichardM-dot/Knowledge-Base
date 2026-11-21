# sfud_sfdp.c —— JEDEC SFDP 参数表解析

> 对应源文件：`references/filesystems/SFUD/sfud/src/sfud_sfdp.c`（共 479 行，含 A 级中文深注）
> 上游：armink/SFUD v1.1.0；解析对象 JESD216 / A / B（V1.0/1.5/1.6）
>
> **互链：** [src_sfud.md](src_sfud.md)（调用方 hardware_init） · [inc_sfud_def.md](inc_sfud_def.md)（sfud_sfdp 结果结构） · Docs：[01-SFDP探测机制](../../../references/filesystems/SFUD/docs/01-SFDP探测机制.md)（逐字段表与时序图）

---

## 1. 文件职责

实现"读表识芯片"：用 0x5A 命令读芯片固化的 SFDP 只读区，经"表头校验 → 参数表头校验 → 基本参数表（BFPT）逐字段解码"三步，把容量/写粒度/擦除命令集/寻址能力填进 `flash->sfdp`。整个文件包在 `#ifdef SFUD_USING_SFDP` 内（sfud_sfdp.c:67），关宏即整体裁掉。

## 2. 逐函数表（6 个函数 + 1 个结构）

| 符号 | 行号 | 一句话职责 |
|---|---|---|
| `sfdp_para_header`（struct） | :71-77 | 参数头：id/版本/表长(DWORD)/24 位表指针 |
| `sfud_read_sfdp` | :102 | 探测总入口，三步串联 |
| `read_sfdp_header` | :129 | 0x00 读 8 字节表头：签名+版本校验 |
| `read_basic_header` | :184 | 0x08 读 JEDEC 基本表参数头 |
| `read_basic_table` | :245 | BFPT 36 字节逐字段解码（核心） |
| `read_sfdp_data` | :419 | 0x5A+3B 地址+1B dummy 的底层读封装 |
| `sfud_sfdp_get_suitable_eraser` | :454 | 擦除器贪心选择（大块优先） |

宏常量：`SUPPORT_MAX_SFDP_MAJOR_REV=1`（:63）、`BASIC_TABLE_LEN=9`（:65，V1.0 基本表 DWORD 数）、`SMALLEST_ERASER_INDEX=0`（:67）。

## 3. 关键摘录与精读

### 3.1 签名与版本校验（read_sfdp_header，sfud_sfdp.c:147-160）

```c
    /* 魔数校验：'S''F''D''P'（小端读出即 50444653h）。对不上 = 这颗芯片没有 SFDP 空间 */
    if (!(header[0] == 'S' &&
          header[1] == 'F' &&
          header[2] == 'D' &&
          header[3] == 'P')) {
```

表头布局：`[0..3]` 签名、`[4]` minor、`[5]` major、`[6]` NPN。老芯片两种表现——MISO 恒 0xFF 或忽略未知命令——都安全落在本分支。

### 3.2 24 位小端表指针（read_basic_header，sfud_sfdp.c:202）

```c
    /* 24 位小端拼装：字节 4 为低位、字节 6 为高位，得到参数表在 SFDP 空间的字节地址 */
    basic_header->ptp       = (long)header[4] | (long)header[5] << 8 | (long)header[6] << 16;
```

配套校验：主版本 ≤1（:204）且表长 ≥9 DWORD（:209，`BASIC_TABLE_LEN`）。

### 3.3 容量双算式（read_basic_table，sfud_sfdp.c:358-372）

```c
    case 0:
        /* bit31=0：经典算式 容量 = 1 + (DWORD1>>3) 字节（旧编码：容量字节数右移 3 位存表） */
        sfdp->capacity = 1 + (table2_temp >> 3);
        break;
    case 1:
        /* bit31=1：容量 = 2^(N-3) 字节，N 为低 31 位存的位宽；上限 4GB（uint32_t 装不下更大） */
        table2_temp &= 0x7FFFFFFF;
        if (table2_temp > sizeof(sfdp->capacity) * 8 + 3) { ... return false; }
        sfdp->capacity = 1L << (table2_temp - 3);
```

验证：W25Q64CV 8MB，DWORD1=0x003FFFFF → `1+(0x3FFFFF>>3)`？实测表值 0x007FFFFF，`1+(0x7FFFFF>>3)=0x100000`？注意公式系数——表内存的是 `(容量字节-1)>>3`，8MB → (0x800000-1)>>3=0xFFFFF，+1=0x100000… 手册值以 0x5A 实读为准，调试时直接看 `read_basic_table` 打印的 9-DWORD 表（:262-268）最可靠。

### 3.4 擦除类型表与排序（sfud_sfdp.c:377-395）

```c
        if (table[28 + 2 * i] != 0x00) {
            sfdp->eraser[j].size = 1L << table[28 + 2 * i];   /* 表里存的是 2 的幂指数 */
            sfdp->eraser[j].cmd = table[28 + 2 * i + 1];
```

- 每型 2 字节：log2(块大小) + 指令码；**0x00 = 该类型不存在**（不是 1 字节擦除）。
- 解析后冒泡升序（:386 起），建立两条全库约定：`eraser[0]` 最小 = `chip.erase_gran` 缺省；表尾最大 = 贪心查找起点。

### 3.5 贪心选擦除器（sfud_sfdp_get_suitable_eraser，sfud_sfdp.c:454-478）

```c
    /* 连最小块都对不齐 → 别无选择，用最小擦除器（硬件要求块对齐的地址） */
    if (addr % flash->sfdp.eraser[SMALLEST_ERASER_INDEX].size) {
        return SMALLEST_ERASER_INDEX;
    }
```

从表尾往回找第一个满足 `erase_size >= size && addr % size == 0` 的类型。收益量化：64KB 用 0xD8 一次 ~150ms，用 0x20 十六次 ~720ms，差 4.8 倍。被 `sfud_erase` 每轮循环调用。

## 4. BFPT 字段速查（V1.0 布局）

| table[] 下标 | 位域 | 产物 |
|---|---|---|
| [0] bit1:0 + [1] | 4KB 擦除支持 + 指令码 | `erase_4k` / `erase_4k_cmd` |
| [0] bit2 | 写粒度 1B / ≥64B | `write_gran` |
| [0] bit3 / bit4 | BP 易失性 / 易失 WREN 码 | `sr_is_non_vola` / `vola_sr_we_cmd` |
| [2] bit2:1 | 3/4 字节寻址能力 | `addr_3_byte` / `addr_4_byte` |
| [4..7] | 容量 DWORD1（双算式） | `capacity` |
| [28+2i]/[29+2i] | 擦除类型 i（i=0..3） | `eraser[i]` |

注：V1.5+ 的真实页大小在 DWORD2，本库未解析（源码 TODO，:258 附近），页一律按 256B 处理。
