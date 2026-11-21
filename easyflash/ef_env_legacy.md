# KB: ef_env_legacy.c —— legacy normal 模式（A级深注）

对应源文件：`easyflash/src/ef_env_legacy.c`（原始 922 行，注释深注后 1114 行；A级逐函数中文深注 + 原英文注释逐行保留，strip_equiv PASS）

## 0. 定位与读法

legacy 体系的"normal 模式"：**RAM 全缓存 + 整区擦写**。所有 KV 以 `key=value\0` 文本流缓存在 `env_cache[]`，`ef_save_env` 才整块落盘。编译条件：`EF_USING_ENV && EF_ENV_USING_LEGACY_MODE && !EF_ENV_USING_WL_MODE`（文件首尾宏墙 L57/L1114）。

> **勘误记录**：文件头部前次生成的 B级导读写成了"依赖文件系统的文件模式"，与本文件实际架构不符（原英文注释 `Environment variables operating interface. (normal mode)` 为准）。深注阶段已在导读下方补充勘误块（注后 L11-27），原导读内容按红线未改动。

任务书所述 kg 用户配置模式在本版本无独立源文件（早期 ef_kg.c 已并入 env 体系），其字符串 API 语义由本文件与 wl 版共同承载，说明统一并入本条目。

## 1. 存储布局（注后 L92-110 深注图）

系统区按字索引：`[0]=结束地址游标` `[1]=保存计数(PFS)` `[2]=版本号(自动更新)` `[-1]=数据段CRC32`；数据段为紧凑 `key=value\0` 流，4 字节对齐，填充必须 `\0`。PFS 模式下 ENV 区对半分 Area0/Area1 乒乓。

## 2. 逐函数表（注后行号）

| 函数 | 行号 | 职责 | 关键细节 |
| --- | --- | --- | --- |
| `ef_env_init` | L179 | 挂载 | 断言链：4 字对齐 + 区域配比（normal 1:1 / PFS 1:2）→ ef_load_env → auto_update → init_ok |
| `ef_env_set_default` | L246 | 出厂重建 | 游标复位→重放默认表→save（PFS 连写两次清两区） |
| `get_env_system_addr` | L299 | 系统区首 | normal=env_start_addr；PFS=cur_load_area_addr |
| `get_env_data_addr` | L314 | 数据段首 | 系统区首 + ENV_PARAM_BYTE_SIZE |
| `get_env_end_addr` / `set_env_end_addr` | L327 / L340 | 游标读写 | 系统区第 0 字，RAM 维护、落盘持久化 |
| `get_env_data_size` | L351 | 数据段长度 | 游标-数据段首（CRC/扫描/搬移依据） |
| `get_env_user_used_size` | L366 | 总占用 | 游标-系统区首（含系统区，落盘 write_size） |
| `ef_get_env_write_bytes` | L381 | 写入量统计 | PFS ×2 |
| `write_env` | L408 | 追加 KV 到 RAM | len=key+val+2 上取整 4；容量 `>=` 判满；填 0 对齐；置脏 |
| `find_env` | L461 | RAM 线性查找 | strncmp 前缀 + `env[key_len]=='='` 全名确认 + strlen 对齐跳步 |
| `create_env` | L511 | 创建闸门 | 非空/无 '='/不重名 → write_env |
| `del_env` | L553 | 删除 | memmove 前移补洞 + 游标回退（RAM 内 O(n) 搬运） |
| `ef_set_env` | L616 | 写入口 | NULL=删；同值 no-op；异值 del+create；锁包裹 |
| `ef_del_env` | L664 | 删入口 | init 检查+锁+转发 |
| `ef_get_env` | L697 | 读入口 | strchr('=') 定位，返回 RAM 指针（会失效） |
| `ef_print_env` | L727 | 调试打印 | 字数组小端逐字节还原；'\0' 换行 |
| `ef_load_env`(normal) | L781 | 挂载判定 | 游标 0xFFFFFFFF/越界→set_default；CRC 失败→set_default（无副本可回退！） |
| `ef_load_env`(PFS) | L827 | 双区仲裁 | 游标票→CRC 票→saved_count 票（0 vs 0xFFFFFFFF 回绕特判） |
| `ef_save_env` | L920 | 唯一落盘动作 | !changed 直接返回；普通=整区擦写；PFS=写败者区+指针互换+计数++ |
| `calc_env_crc` | L990 | 区 CRC | 游标+[计数]+数据段（版本号随数据段机制保护） |
| `env_crc_is_ok` | L1016 | 校验 | 现算 vs 参数字比对 |
| `ef_set_and_save_env` | L1036 | 一步式写 | set+save（DEPRECATED 语义源头） |
| `ef_del_and_save_env` | L1057 | 一步式删 | 同上 |
| `env_auto_update` | L1081 | 增量升级 | 版本号不符→只补缺失 key→save |

## 3. 关键摘录（逐字，注后行号）

### 3.1 PFS 仲裁的核心一票（注后 L875-880）

```c
        /* the bigger saved count area is valid */
        if ((area0_saved_count > area1_saved_count) || ((area0_saved_count == 0) && (area1_saved_count == 0xFFFFFFFF))) {
            area1_is_valid = false; /* 票3：计数比较，区0 胜出（含回绕特判） */
        } else {
            area0_is_valid = false; /* 票3：区1 胜出 */
        }
```

### 3.2 乒乓保存（ef_save_env PFS 分支，注后 L935-943）

```c
    write_addr = next_save_area_addr;
    write_size = get_env_user_used_size();
    /* replace next_save_area_addr with cur_load_area_addr */
    next_save_area_addr = cur_load_area_addr; /* 指针互换：旧有效区沦为下一轮的写目标 */
    cur_load_area_addr = write_addr;
    /* change the ENV end address to next save area address */
    set_env_end_addr(write_addr + write_size); /* 游标平移到新区的绝对地址 */
    /* ENV area saved count +1 */
    env_cache[ENV_PARAM_INDEX_SAVED_COUNT]++; /* 每保存一次递增，上电仲裁的"新钟" */
```

### 3.3 同值 no-op（ef_set_env，注后 L639）

```c
            if (strcmp(old_value, value)) { /* 值相同则不写：Flash 擦写寿命宝贵 */
```

## 4. 已知问题记录（不改码）

1. `del_env`（注后 L553）里 `((uint32_t) del_env ...)` 把指针强转 32 位整数做偏移运算——64 位平台（库官方声明不支持的"64位除外"）会截断；
2. PFS 模式 `ef_save_env` 若**写失败**，cur/next 指针已互换且计数已加——RAM 视图与 Flash 实际错位，只能靠下次成功保存修复（写失败路径未回滚）；
3. 头部 B级导读的"文件模式"表述错误（见第 0 节勘误）。

## 5. 关键摘录补遗（逐字，注后行号）

### 5.1 记录追加与 4 字节对齐（write_env，注后 L413-417）

```c
    /* calculate ENV storage length, contain '=' and '\0'. */
    env_str_len = key_len + value_len + 2;
    if (env_str_len % 4 != 0) {
        env_str_len = (env_str_len / 4 + 1) * 4; /* 向上取整到 4 字节对齐（KV 流必须字对齐） */
    }
```

### 5.2 扫描跳步（find_env，注后 L485-494）

```c
        } else {
            /* calculate ENV length, contain '\0'. */
            env_len = strlen(env) + 1;
            /* next ENV and word alignment */
            if (env_len % 4 == 0) {
                env += env_len;
            } else {
                env += (env_len / 4 + 1) * 4;
            }
        }
```

strlen+1 后对齐跳步——这就是"填充必须 \0"的原因（write_env 注后 L439 的 memset），两处规则失配即扫描错位。

### 5.3 整区回写的两段失败语义（ef_save_env，注后 L950-976）

```c
    switch (result) {
    case EF_ERASE_ERR: {
        ...
        /* will return when erase fault */
        return result;   /* 擦失败：立即返回，不带病写 */
    }
    }
    /* write ENV to flash */
    result = ef_port_write(write_addr, env_cache, write_size);
    ...
    env_cache_changed = false;  /* 写失败也清脏标志：RAM 视图与 Flash 可能不一致，上层需重试 */
```

## 6. set 的决策树量化

以 4KB 区、50 条 KV、平均 40B/条为例：改一条值 = del（RAM memmove ~2KB）+ create（追加 40B）+ save（擦 4KB + 写 ~2KB）。**写放大 ≈ 100 倍**，每分钟改一次的设备年擦写 ~52 万次超 NOR 寿命——量化理解它为何被 NG 取代。

## 7. 互链

- 模式对比与演进：Docs/[00](../../../references/filesystems/EasyFlash/Docs/00-总览与双模式架构.md)；掉电恢复：Docs/[04](../../../references/filesystems/EasyFlash/Docs/04-掉电安全与恢复.md)
- 同构函数的 WL 版：[ef_env_legacy_wl.md](ef_env_legacy_wl.md)；NG 版对照：[ef_env.c.md](ef_env.c.md)
