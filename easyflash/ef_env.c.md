# KB: ef_env.c —— ENV NG 模式核心（GBK 编码文件，KB 逐函数深读）

对应源文件：`easyflash/src/ef_env.c`（共 1858 行；**未加源码注释**——文件为 GBK 编码，按 STANDARD 红线 ENCODING_SKIP，本条目替代性提供逐函数深读。以下行号均为原始文件真实行号）

> 编码说明：文件头部已有一段前次生成的 GBK 中文导读（L1-16）；VSCode 打开请选 GBK/GB2312，Keil 默认可读。

## 0. 定位与读法

V4.0 引入的 NG（Next Generation）模式核心，FlashDB KVDB 的直系前身。三大支柱：

1. **追加写**：任何 set 不擦除、只追加新节点 + 改旧节点状态表；
2. **状态机门闩**：每节点一张 7 态状态表（见 [ef_def.h.md](ef_def_h.md)），只进不退，掉电可判读；
3. **惰性 GC**：写路径顺路回收脏扇区（见 [03-GC 与磨损均衡](../../../references/filesystems/EasyFlash/Docs/03-GC与磨损均衡.md)）。

RAM 代价：两张小缓存表（默认 16 项 KV + 4 项扇区），无全量镜像——这是它与 legacy 两兄弟的本质区别。

## 1. 全函数清单（45 个，按模块分组）

### 1.1 状态表引擎（追加写的心跳）

| 函数 | 行号 | 职责 | 要点 |
| --- | --- | --- | --- |
| `set_status` | L239 | 在 RAM 状态表里"清出"第 n 格 | gran=1 时按位清（0x80>>i），否则按整格清；返回要写的字节偏移 |
| `get_status` | L263 | 从 Flash 状态表数出当前状态 | 从末格向前找最后一个 0 所在格 |
| `write_status` | L284 | 状态推进并落盘 | 只写**新清空的那一格**（追加写，绝不重写旧格） |
| `read_status` | L310 | 读回状态表并解析 | env/扇区状态解析的统一入口 |

量化（gran=1 NOR）：7 态表仅 1 字节，每态占 1 位：0xFF→0x7F→0x3F→…；gran=32 时每态占 4 字节，7 态 24 字节。

### 1.2 双缓存表（EF_ENV_USING_CACHE）

| 函数 | 行号 | 职责 | 要点 |
| --- | --- | --- | --- |
| `update_sector_cache` | L323 | 维护"USING 扇区→empty_addr"缓存（4 项） | 扇区转 FULL 时自动失效删除 |
| `get_sector_from_cache` | L353 | 查扇区空位缓存 | 命中可免去逐节点扫描 |
| `update_env_cache` | L369 | 维护"name-CRC16→addr"缓存（16 项） | addr=FAILED_ADDR 表示删除；插入用 LRU 式活跃度淘汰 |
| `get_env_from_cache` | L413 | 查 KV 地址缓存 | **命中后必须回 Flash 复核名字**（CRC16 会碰撞），并给活跃度 +16 饱和到 0xFFFF |

缓存表是纯加速结构：删空/损坏都不影响正确性，只影响速度——上电重建（L1828-1835 全部置 FAILED_ADDR）。

### 1.3 地址扫描（无索引世界的游标）

| 函数 | 行号 | 职责 | 要点 |
| --- | --- | --- | --- |
| `continue_ff_addr` | L442 | 找数据区中第一段连续 0xFF | 32B 缓冲逐段读，定位"空区起点"；修正扫描误差（L652-663） |
| `find_next_env_addr` | L472 | 按 magic(KV40) 滑窗找下一节点 | 28B 窗口滑动匹配，返回 magic 偏移修正后的节点头地址 |
| `get_next_env_addr` | L503 | 节点迭代器核心 | 首节点=扇区头后；后续=前节点地址+前节点 len；**前节点 CRC 坏则 +1 字节重新滑窗**（L516-524）——损坏节点不致拖垮整个扇区扫描 |
| `get_next_sector_addr` | L674 | 扇区迭代器核心 | 支持 combined 跨扇区值（预留 V4.2 大数据模式），未组合时线性 +1 扇区 |

### 1.4 读路径

| 函数 | 行号 | 职责 | 要点 |
| --- | --- | --- | --- |
| `read_env` | L539 | 读节点头+校验 CRC | len 非法（~0/越界/小于最小头）→ 标 ENV_ERR_HDR 自愈（L551-560）；CRC 分段算（32B 缓冲） |
| `read_sector_meta_data` | L597 | 读扇区头+（可选）遍历节点 | traversal=true 时逐节点累加得到 empty_env/remain；USING 扇区优先查缓存 |
| `env_iterator` | L697 | 全库节点迭代器 | 扇区迭代×节点迭代双循环，回调返 true 短路 |
| `find_env_cb` | L727 | 查找回调 | 判据：`crc_is_ok && status==ENV_WRITE && 名字全等` |
| `find_env_no_cache` / `find_env` | L744 / L753 | 两级查找 | 缓存命中→read_env；未命中全扫→写回缓存 |
| `get_env` | L790 | 读值核心 | buf 可 NULL；截断读不报错 |
| `ef_get_env_blob` | L852 | 对外读 API | 锁内转发 get_env |
| `ef_get_env_obj` | L822 | 对外取对象 API | 同上 |
| `ef_read_env_value` | L910 | 按对象读值 | 只信 crc_is_ok 的对象 |
| `ef_get_env` | L882 | 旧字符串 API | 128B 静态缓冲，非可重入；值必须可打印（ef_is_str L777 判定） |

### 1.5 写路径

| 函数 | 行号 | 职责 | 要点 |
| --- | --- | --- | --- |
| `write_env_hdr` | L940 | 写节点头 | 先写状态表（PRE_WRITE 格），再写 magic 起的其余头 |
| `align_write` | L1291 | 按写粒度补齐写 | 尾部不足粒度用 0xFF 补满单元再写 |
| `update_sec_status` | L981 | 推进扇区 store 状态 | EMPTY→USING；USING 且剩余 < `EF_SEC_REMAIN_THRESHOLD` →FULL 并报 is_full |
| `alloc_env_cb` / `alloc_env` | L1044 / L1062 | 空间分配 | 三铁律：有余量/非脏/脏扇区仅 gc_request 时可用；空扇区 ≤ 阈值时置 gc_request 不动用（GC 预备队） |
| `new_env` / `new_env_by_kv` | L1207 / L1224 | 分配+GC 重试 | `__retry` 标签：GC 一次后重分配，仅重试一次 |
| `create_env_blob` | L1321 | 追加一条 KV | 头构造→CRC 四段累加→头/name/value/status 五连写（顺序即安全序）；节点长度禁超扇区 |
| `set_env` | L1443 | 写核心（同名覆盖剧本） | 预检→find 旧→旧标 PRE_DELETE→写新→旧标 DELETED→按需 GC |
| `ef_set_env_blob` | L1489 | 对外写 API | 锁内转发 set_env |
| `ef_set_env` / `ef_set_and_save_env` / `ef_save_env` | L1519 / L1535 / L1545 | 兼容 API | set_env 的字符串/别名/空壳变体（save no-op） |
| `del_env` | L1086 | 删除 | !complete_del 写 PRE_DELETE（覆盖中间态），complete_del 写 DELETED；同步把扇区 dirty 推 TRUE |
| `ef_del_env` / `ef_del_and_save_env` | L1408 / L1438 | 对外删 API | 直接 complete_del（一步到位） |

### 1.6 GC 状态机

| 函数 | 行号 | 职责 | 要点 |
| --- | --- | --- | --- |
| `sector_iterator` | L1011 | 扇区遍历 | 按 store 状态过滤，可选深度遍历节点 |
| `sector_statistics_cb` | L1031 | 空/用扇区统计 | alloc 的选路依据 |
| `gc_check_cb` | L1231 | 空扇区计数 | gc_collect 的触发判据 |
| `do_gc` | L1243 | 单扇区回收 | 先标 dirty=GC（接力棒）→搬 ENV_WRITE/PRE_DELETE 节点→format_sector |
| `move_env` | L1144 | 搬运单节点 | 旧 PRE_DELETE→新 PRE_WRITE→32B 缓冲逐段读旧写新→新 WRITE→旧 DELETED；恢复态下先查重（L1157-1167） |
| `gc_collect` | L1274 | GC 总入口 | 空扇区 ≤ `EF_GC_EMPTY_SEC_THRESHOLD`（默认 1）才动手；结束清 gc_request |

### 1.7 挂载与恢复

| 函数 | 行号 | 职责 | 要点 |
| --- | --- | --- | --- |
| `format_sector` | L953 | 扇区铸造 | 擦→全 0xFF 头→EMPTY/FALSE 状态→magic→回写 |
| `check_sec_hdr_cb` | L1710 | 体检①：扇区头 | 坏则 format；全坏→set_default（ef_load_env L1774 判定） |
| `check_and_recovery_gc_cb` | L1723 | 体检②：GC 接力 | dirty=GC → gc_request=true + gc_collect |
| `check_and_recovery_env_cb` | L1735 | 体检③：节点 | PRE_DELETE→move_env 复活；PRE_WRITE→标 ENV_ERR_HDR |
| `ef_load_env` | L1763 | 四段体检总控 | `__retry` 循环：恢复引发 GC 后重扫；in_recovery_check 贯穿始终 |
| `ef_env_init` | L1808 | 挂载入口 | 四断言（01 篇）→缓存表复位→load→auto_update |
| `env_auto_update` | L1675 | 版本增量升级 | 特殊 key `__ver_num__`（L149）存版本；只补缺失 key |
| `ef_env_set_default` | L1556 | 出厂重建 | 全扇 format→重放默认表；value_len==0 按字符串（兼容旧表） |
| `print_env_cb` / `ef_print_env` | L1597 / L1648 | 调试打印 | blob 值打地址+长度；两段式"先探后打"（__reload）省缓冲 |

## 2. 关键摘录（逐字，原始行号）

### 2.1 状态追加写的物理本质（write_status，L284-308）

```c
    /* set the status first */
    byte_index = set_status(status_table, status_num, status_index);

    /* the first status table value is all 1, so no need to write flash */
    if (byte_index == ~0UL) {
        return EF_NO_ERR;
    }
#if (EF_WRITE_GRAN == 1)
    result = ef_port_write(addr + byte_index, (uint32_t *)&status_table[byte_index], 1);
```

三个细节：状态 0（全 0xFF）**不用写**（擦除态天然成立）；每次只写新清空的那 1 字节/格；gran≠1 时按整粒度单元写（stm32 片内不支持位写）。

### 2.2 损坏节点不拖垮扫描（get_next_env_addr，L516-524）

```c
            if (pre_env->crc_is_ok) {
                addr = pre_env->addr.start + pre_env->len;
            } else {
                /* when pre_env CRC check failed, maybe the flash has error data
                 * find_next_env_addr after pre_env address */
                addr = pre_env->addr.start + EF_WG_ALIGN(1);
            }
```

前节点 CRC 坏 → 长度不可信 → 只前移 1 个对齐单元，靠 magic 滑窗（find_next_env_addr）重新对齐——单点损坏的爆炸半径被限制在一个节点。

### 2.3 分配的三铁律（alloc_env_cb，L1049-1054）

```c
    /* 1. sector has space
     * 2. the NO dirty sector
     * 3. the dirty sector only when the gc_request is false */
    if (sector->check_ok && sector->remain > *env_size
            && ((sector->status.dirty == SECTOR_DIRTY_FALSE)
                    || (sector->status.dirty == SECTOR_DIRTY_TRUE && !gc_request))) {
```

### 2.4 同名覆盖的原子性剧本（set_env，L1453-1473）

```c
        /* make sure the flash has enough space */
        if (new_env_by_kv(&sector, strlen(key), buf_len) == FAILED_ADDR) {
            return EF_ENV_FULL;
        }
        env_is_found = find_env(key, &env);
        /* prepare to delete the old ENV */
        if (env_is_found) {
            result = del_env(key, &env, false);
        }
        /* create the new ENV */
        if (result == EF_NO_ERR) {
            result = create_env_blob(&sector, key, value_buf, buf_len);
        }
        /* delete the old ENV */
        if (env_is_found && result == EF_NO_ERR) {
            result = del_env(key, &env, true);
        }
```

## 3. 已知问题/待办记录（源码内 TODO，只记录不改码）

1. L527 `//TODO 扇区连续模式`：combined 字段与"节点跨扇区"尚未实现（get_next_env_addr 遇跨扇区直接 FAILED_ADDR）；
2. L562 `//TODO 扇区连续模式，或者写入长度没有写入完整`：read_env 对"len 大于单扇区容量"直接 EF_ASSERT(0)——超大数据模式是 V4.2 规划；
3. L1750 `//TODO 绘制异常处理的状态转换图`：作者自留的状态图 TODO（本系列 Docs 04 篇已代画）。

## 4. 量化速查

| 项 | 值（gran=1 NOR 默认配置） |
| --- | --- |
| 扇区头 | 16 B（状态表 2B + magic/combined/reserved 12B） |
| KV 头 | 24 B（状态表 1B + magic/len/crc32 12B + name_len 1B + value_len 4B + 填充） |
| 一条 "temp=123" 实占 | 24 + 8(name 对齐) + 4 = 36 B |
| RAM | env_cache_table 16×8B + sector_cache_table 4×8B = 160 B |
| GC 触发 | 空扇区 ≤ 1 或 USING 扇区转 FULL |

## 5. 互链

- 写/读流程：Docs/[02-环境变量读写全流程](../../../references/filesystems/EasyFlash/Docs/02-环境变量读写全流程.md)
- GC：Docs/[03-GC 与磨损均衡](../../../references/filesystems/EasyFlash/Docs/03-GC与磨损均衡.md)；恢复：Docs/[04](../../../references/filesystems/EasyFlash/Docs/04-掉电安全与恢复.md)
- 状态机与对象定义：[ef_def_h.md](ef_def_h.md)；CRC 工具：[ef_utils.c.md](ef_utils.c.md)；legacy 对照：[ef_env_legacy.md](ef_env_legacy.md)、[ef_env_legacy_wl.md](ef_env_legacy_wl.md)
- 演进对照：Docs/[00](../../../references/filesystems/EasyFlash/Docs/00-总览与双模式架构.md) 的 FlashDB 对照表
