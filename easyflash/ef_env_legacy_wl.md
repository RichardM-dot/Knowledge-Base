# KB: ef_env_legacy_wl.c —— legacy 磨损均衡模式（A级深注）

对应源文件：`easyflash/src/ef_env_legacy_wl.c`（原始 1082 行，注释深注后 1234 行；A级逐函数中文深注 + 原英文注释逐行保留，strip_equiv PASS）

## 0. 定位与读法

legacy 体系的"WL（wear leveling）模式"：RAM 全缓存 + **数据段可搬家**。比 normal 版多一层间接寻址——数据段起始地址 `cur_using_data_addr` 持久化在 ENV 区第一个"系统扇区"里；保存时擦/写失败就向后平移一个扇区重试（乐观策略），写入位置随时间游走摊薄磨损。编译条件：`EF_USING_ENV && EF_ENV_USING_LEGACY_MODE && EF_ENV_USING_WL_MODE`。

## 1. 存储布局（注后 L11-27 深注图）

```text
ENV_AREA_SIZE
+------------------+------------------------------------------+
| 系统扇区(1个)     |  数据段（可搬家，PFS 时对半各一份）          |
| 只存1个字:        |  参数区(4字) + "key=value\0" 详情区          |
| cur_using_data_  |  <---- cur_using_data_addr 指向这里         |
| addr             |                                          |
+------------------+------------------------------------------+
```

参数区 4 字：`[0]=详情区结束地址` `[1]=保存计数(PFS)` `[2]=版本号(自动更新)` `[-1]=详情区CRC32`。

## 2. 逐函数表（注后行号）

| 函数 | 行号 | 职责 | 关键细节 |
| --- | --- | --- | --- |
| `ef_env_init` | L176 | 挂载 | 数据段容量 = 区-1 扇区（PFS 再减半+偶数断言）；断言链后 load |
| `ef_env_set_default` | L229 | 出厂重建 | 详情区游标复位→重放默认表→save（PFS 两遍） |
| `get_env_start_addr` | L279 | 区首 | 即系统扇区地址 |
| `get/set_cur_using_data_addr` | L289 / L300 | 数据段游标 | 灵魂全局；持久化靠 save_cur_using_data_addr |
| `get_env_detail_addr` | L310 | 详情区首 | 游标 + 参数区 16B |
| `get/set_env_detail_end_addr` | L322 / L335 | 详情区结束游标 | 参数区第 0 字 |
| `get_env_detail_size` | L346 | 详情区长度 | CRC/扫描/搬移依据 |
| `get_env_user_used_size` | L364 | 用户占用 | 结束地址-数据段首（=搬家 write_size） |
| `ef_get_env_write_bytes` | L379 | 写入量统计 | 普通=结束地址-区首；PFS=1 扇区+区内占用 |
| `write_env` | L402 | RAM 追加 KV | 同 normal 版；`ker_len` 为原作者拼写笔误（记录不改） |
| `find_env` | L449 | RAM 查找 | 与 normal 版逐行同构 |
| `create_env` | L499 | 创建闸门 | 三检查 → write_env |
| `del_env` | L535 | RAM 删除 | memmove 前移补洞 |
| `ef_set_env` | L592 | 写入口 | NULL=删/同值 no-op/异值 del+create |
| `ef_del_env` | L639 | 删入口 | 转发 |
| `ef_get_env` | L668 | 读入口 | 返回 RAM 指针 |
| `ef_print_env` | L696 | 调试打印 | 模式名 wear leveling(+PFS)、保存计数 |
| `ef_load_env`(普通) | L750 | 两级游标校验 | 区游标→详情区游标→CRC，任一失败复位重建 |
| `ef_load_env`(PFS) | L812 | 双区 5 票仲裁 | 区游标票×2 + 详情区游标票×2 + CRC 票 + 计数票 |
| `ef_save_env` | L936 | **磨损均衡搬家循环** | 见下节精读 |
| `calc_env_crc` | L1046 | 区 CRC | 游标+详情区（PFS 计数不参与） |
| `env_crc_is_ok` | L1065 | 校验 | 现算 vs 参数字 |
| `save_cur_using_data_addr`(普通) | L1088 | 游标持久化 | 擦系统扇区写 4B——**size=4 的擦除依赖移植层兜底**（见问题 1） |
| `save_cur_using_data_addr`(PFS) | L1118 | 分区持久化 | 按目标地址选 Area0/1 的系统扇区 |
| `ef_set_and_save_env` / `ef_del_and_save_env` | L1159 / L1180 | 一步式 API | set/del + save（每次=一次全量搬家） |
| `env_auto_update` | L1201 | 增量升级 | 只补缺失 key |

## 3. 关键摘录（逐字，注后行号）

### 3.1 磨损均衡核心循环（ef_save_env，注后 L967-970）

```c
    /* wear leveling process, automatic move ENV to next available position */
    /* 【深注】循环条件即"当前落点 + 镜像大小"必须压在数据段末尾之内：本位置写不下
     * 或擦写失败都推动 cur_using_data_addr 后移，直到成功或空间耗尽退出 */
    while (get_cur_using_data_addr() + env_used_size < data_sec_end_addr) {
```

### 3.2 擦失败搬家重试（注后 L980-993）

```c
        case EF_ERASE_ERR: {
            EF_INFO("Warning: Erased ENV fault! Start address is 0x%08X, size is %ld.\n",
                    get_cur_using_data_addr(), env_used_size);
            EF_INFO("Moving ENV to next available position.\n");
            /* Calculate move offset address.
             * Current strategy is optimistic. It will offset the flash erasure minimum size.
             */
            move_offset_addr = EF_ERASE_MIN_SIZE;
            /* calculate and set next available data section address */
            set_cur_using_data_addr(get_cur_using_data_addr() + move_offset_addr);
            /* calculate and set next available ENV detail part end address */
            set_env_detail_end_addr(get_env_detail_end_addr() + move_offset_addr);
            continue;
        }
```

写失败分支（注后 L1002，`case EF_WRITE_ERR`）结构相同。**两个游标（数据段首 + 详情区结束）必须同步平移**，漏一个即布局崩坏。

### 3.3 成功后游标持久化（注后 L1023-1031）

```c
    if (get_cur_using_data_addr() + env_used_size < data_sec_end_addr) {
        /* current using data section address has changed, save it */
        if (get_cur_using_data_addr() != cur_using_addr_bak) {
            result = save_cur_using_data_addr(get_cur_using_data_addr());
        }
    } else {
        result = EF_ENV_FULL;
        EF_INFO("Error: The flash has no available space to save ENV.\n");
    }
```

## 4. 已知问题记录（不改码）

1. **擦除长度 4B 的隐式契约**：`save_cur_using_data_addr` 调 `ef_port_erase(get_env_start_addr(), 4)`——擦除粒度本应 EF_ERASE_MIN_SIZE。库把"向上取整整扇擦除"的责任交给移植层（demo 移植确实兜底），自定义移植若照抄 4B 直接下发到硬件会擦除失败/不足，深注已在函数头标注；
2. `ker_len` 拼写笔误（write_env 局部变量，L402 函数体内）；
3. 搬家循环无坏块登记：同一坏扇区若永远擦写失败，游标一路走到数据段尽头报 EF_ENV_FULL——乐观策略的代价；
4. PFS 版 ef_load_env（L812）双区全毁时要连续两次 `save_cur_using_data_addr`（L909-910），第二次失败会留下"游标已写一半"的状态（下次上电重新仲裁，无致命后果但值得知道）。

## 5. 与 normal 版的差异速查

| 维度 | ef_env_legacy.c (normal) | 本文件 (WL) |
| --- | --- | --- |
| 数据段位置 | 固定在区首后 | 可搬家，存于系统扇区 |
| 保存失败处理 | 直接报错 | 平移一个扇区重试 |
| 写入量统计口径 | 用户已用×(PFS?2:1) | 结束地址-区首 |
| 掉电恢复票数 | 3 票 | 5 票（多两层游标校验） |

## 6. 互链

- 磨损均衡机制：Docs/[03-GC 与磨损均衡](../../../references/filesystems/EasyFlash/Docs/03-GC与磨损均衡.md)；移植层擦除契约：Docs/[05](../../../references/filesystems/EasyFlash/Docs/05-移植层.md)
- 姊妹文件：[ef_env_legacy.md](ef_env_legacy.md)；NG 对照：[ef_env.c.md](ef_env.c.md)
