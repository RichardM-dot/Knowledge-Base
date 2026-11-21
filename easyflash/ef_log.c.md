# KB: ef_log.c —— Flash 日志存储（B级导读；前轮已注，本轮未改）

对应源文件：`easyflash/src/ef_log.c`（共 771 行；B级文件头导读块：职责 + 6 个对外 API 清单 + 在初始化/日志数据流中的位置，strip_equiv PASS）

## 0. 定位与读法

无文件系统产品的崩溃日志黑洞记录仪：LOG 区被组织成**Flash 上的环形缓冲**——写满自动从头覆盖最旧扇区，重启不丢，可按 index 回读。启用条件 `EF_USING_LOG && LOG_AREA_SIZE`（后者需 ≥2 扇区）。

## 1. 关键常量与状态机（原始行号）

| 项 | 行号 | 值/含义 |
| --- | --- | --- |
| `LOG_SECTOR_MAGIC` | L54 | 0xEF30EF30（ASCII "EF0"...标识） |
| `LOG_SECTOR_HEADER_SIZE` | L56 | 12B = magic 4B + using 状态 4B + full 状态 4B |
| 扇区状态魔数 | L74-76 | 0xFFFFFFFF 空 / 0xFEFEFEFE 在用 / 0xFCFCFCFC 满 |
| 状态机 | L71 原注释 | empty→using→full；clean 后回 empty |
| `log_start_addr/log_end_addr` | L92 | 环形读写指针（init 时扫描恢复） |

扇区头三态组合（L63-69 ASCII 图）：

```text
| 0xEF30EF30 0xFFFFFFFF 0xFFFFFFFF |  empty |
| 0xEF30EF30 0xFEFEFEFE 0xFFFFFFFF |  using |
| 0xEF30EF30 0xFEFEFEFE 0xFCFCFCFC |  full  |
```

## 2. 逐函数表（原始行号 = 注后行号）

| 函数 | 行号 | 职责 | 要点 |
| --- | --- | --- | --- |
| `ef_log_init` | L106 | 挂载 | 区大小断言（≥2 扇区）；地址=EF_START_ADDR+ENV 区（按宏）；find_start_and_end_addr 恢复指针 |
| `get_sector_status` | L137 | 读扇区头判态 | `addr & ~(EF_ERASE_MIN_SIZE-1)` 求扇区首；三态+HEAD_ERROR |
| `write_sector_status` | L179 | 推进扇区状态 | 按状态写不同偏移的字（empty@+0/using@+4/full@+8） |
| `find_sec_using_end_addr` | L211 | 找扇区内已用末端 | 32B 缓冲数连续 0xFF；全空/部分用/全满三分支 |
| `find_start_and_end_addr` | L280 | **恢复环形指针** | 状态机化扫全扇区（见下） |
| `ef_log_get_used_size` | L411 | 已用统计 | 环形回卷时取补；扣扇区头 |
| `ef_log_get_total_size` | L434 | 总容量 | 扣每扇区 12B 头 |
| `log_seq_read`（static） | L455 | 跨扇区连续读 | 自动跳过每扇区头 12B |
| `log_index2addr`（static） | L487 | 逻辑 index→物理地址 | 回卷换算（环形核心算术） |
| `ef_log_read` | L517 | 对外读 | size 4 对齐；越界截断；回卷点三分支（带 ASCII 图 L549-609） |
| `ef_log_write` | L626 | 对外写 | 先填当前扇区剩余→循环"擦下一扇区+写"；写满自动滚动 |
| `get_next_flash_sec_addr`（static） | L719 | 环形下一扇区 | `(cur-区首)/扇区` 取模回卷 |
| `ef_log_clean` | L736 | 整区格式化 | 擦全区+重写各扇区 empty/using 头 |

## 3. 关键摘录（逐字，原始行号）

### 3.1 环形指针恢复的状态机（find_start_and_end_addr 核心判据，L395-402）

```c
    if (using_sec_counts != 1) {
        /* this state is almost impossible */
        EF_DEBUG("Error: There must be only one sector status is USING! Now will clean all log area.\n");
        ef_log_clean();
    } else {
        /* find the end address */
        log_end_addr = find_sec_using_end_addr(cur_using_sec_addr);
    }
```

合法性判据：全扇区扫描后 **USING 状态必须恰好 1 个**（L317-334 的相邻状态对判定 start/end 在哪个状态组合），否则视为布局损坏整区 clean——牺牲日志保健壮。

### 3.2 环形覆盖写（ef_log_write 滚动段，L663-679，含扇区切换）

```c
    while (true) {
        /* calculate next available sector address */
        erase_addr = write_addr = get_next_flash_sec_addr(write_addr - 4);
        /* move the flash log start address to next available sector address */
        if (log_start_addr == erase_addr) {
            log_start_addr = get_next_flash_sec_addr(log_start_addr);
        }
        /* erase sector */
        result = ef_port_erase(erase_addr, EF_ERASE_MIN_SIZE);
```

写到"读指针所在扇区"时读指针同步前滚——**最旧日志被无提示覆盖**，这是环形语义不是 bug。

### 3.3 逻辑地址换算（log_index2addr 回卷分支，L497-502）

```c
        if (log_start_addr + index + header_total_offset < log_area_start_addr + LOG_AREA_SIZE) {
            return virtual_addr;
        } else {
            // the address will restart from the first sector address.
            return virtual_addr - (log_area_start_addr + LOG_AREA_SIZE) + log_area_start_addr;
        }
```

## 4. 已知问题记录（不改码）

1. `SECTOR_STATUS_MAGIC_EMPUT` 拼写（应为 EMPTY，L74）——已随版本固定；
2. `ef_log_write` 中 `write_sector_status(write_addr, SECTOR_STATUS_EMPUT)` 的返回值未检查就覆盖 result（L676-677），失败被 using 写的返回值掩盖；
3. `ef_log_read` 的 `log_seq_read` 返回值在正常路径（L546）未传递（直接调 `log_seq_read(log_index2addr(index), log, size);` 未赋 result）——读失败可能被吞，回读崩溃日志时建议自行 CRC 核对内容；
4. 扇区头 12B 中的 using/full 状态写入依赖 NOR"1→0"叠加写（0xFF→0xFE...→0xFC...），移植层 write 必须支持同扇区头多次追加写（NOR 天然支持，stm32 片内需整页编程注意）。

## 5. 回读三分支（ef_log_read，环形回卷的读法，原始行号）

log_start_addr > log_end_addr（日志绕环一圈）时按 index 落点分三种（配 L549-609 的 ASCII 图）：

| 分支 | 条件 | 动作 |
| --- | --- | --- |
| 单段 | `log_index2addr(index)+size ≤ 区尾` | 一次 log_seq_read（L566） |
| 跨回卷点 | `log_index2addr(index) < 区尾` | 两段：先读到区尾，再从区首续读（L586-593），跨段时把**区尾那几个扇区头**的字节数扣除 |
| 越过回卷点 | 其他 | 地址减整区长后单段读（L611） |

三分支的本质：**逻辑地址连续、物理地址回卷**，`log_index2addr`（L487）负责把"第 N 字节日志"换算成物理地址并跳过 12B 扇区头。

## 6. 与 EasyLogger 的配合

官方推荐组合：EasyLogger 的 Flash 插件把 `ef_log_write` 作为后端，`elog_output` 每条日志即写；崩溃现场由 hardfault handler 里直接 `ef_log_write` 寄存器快照（注意 ISR 里调用要 port 层擦除安全——擦扇区耗时，建议只写不滚动的"预擦除应急扇区"或接受阻塞）。

## 7. 互链

- API 表：Docs/[06-API 与实测指南](../../../references/filesystems/EasyFlash/Docs/06-API与实测指南.md) 1.5 节
- 布局依赖：[ef_cfg_h.md](ef_cfg_h.md)；擦除契约：[ef_port.c.md](ef_port.c.md)
