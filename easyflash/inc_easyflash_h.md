# KB: easyflash.h —— 公共 API 头（B级 API 契约注释）

对应源文件：`easyflash/inc/easyflash.h`（原始 108 行，注释深注后 160 行；B级导读块 + API 契约注释，strip_equiv PASS）

## 0. 定位

全库**唯一**对外头文件：应用层只 include 它。内容按"源文件归属"分组声明，每组配契约注释块（注后 L58-59、L64-73、L78-85、L101-106、L122-127、L139-146 等）。

## 1. API 分组契约表（注后行号）

| 分组 | 行号 | API | 契约要点（注释原文摘述） |
| --- | --- | --- | --- |
| 入口 | L59-60 | `easyflash_init` | 必须最先调用；幂等；成功前其余 API 返回错误 |
| NG 专属 | L62-77 | `ef_get_env_blob` / `ef_get_env_obj` / `ef_read_env_value` / `ef_set_env_blob` | 任意 blob、即时落盘；get 的 buf 可 NULL 只查长度、截断读不报错；save 系列在此模式是空操作 |
| 字符串共用 | L78-96 | `ef_load_env` / `ef_print_env` / `ef_get_env` / `ef_set_env` / `ef_del_env` / `ef_save_env` / `ef_env_set_default` / `ef_get_env_write_bytes` / `ef_set_and_save_env` / `ef_del_and_save_env` | get 返回内部指针不可长期持有；legacy 的 set 只改 RAM 需 save；save 在 NG 为 no-op；set_default 整区格式化重建 |
| IAP | L99-118 | `ef_erase_bak_app` 等 9 个 | 五步曲：清备份区→分包写入→搬运到 App→校验→跳转；Spec 系列可传自定义擦写回调 |
| LOG | L121-132 | `ef_log_read` 等 5 个 | index 0 = 最旧；size 4 字节对齐；写满自动覆盖最旧扇区 |
| 工具 | L136-137 | `ef_calc_crc32` | 流式 CRC：首调 crc=0，续算传上次返回值 |
| 移植层 | L139-154（回调 147-154） | `ef_port_read/write/erase` 等 9 个 | 用户实现；read/write size 字节且写粒度对齐；erase 地址 EF_ERASE_MIN_SIZE 对齐；lock/unlock 多任务必实现 |

## 2. 关键摘录（逐字，注后行号）

### 2.1 NG 模式 blob 读的契约（注后 L64-73）

```c
/*
 * 【契约】NG 模式（ef_env.c）专属 API：任意二进制 blob、即时落盘、RAM 占用趋近 0。
 *   ef_get_env_blob:  读 KV。value_buf 可传 NULL（只查长度）；saved_value_len 出参
 *                     返回 Flash 上的真实长度；返回实际拷入 buf 的字节数（0=没找到）。
 *                     buf_len 小于真实长度时截断读，不报错。
 */
```

### 2.2 移植层契约总纲（注后 L140-146）

```c
/*
 * 【契约】移植层回调（用户在 ef_port.c 实现，见 Docs/05-移植层.md）：
 *   read/write 的 size 单位是字节，但必须是写粒度对齐（1/8/32bit 三种 Flash）；
 *   erase 的 addr 必须 EF_ERASE_MIN_SIZE 对齐；
 *   env_lock/unlock 在多任务系统中包住所有 ENV API 的临界区（单线程可为空实现）；
 *   三个打印函数接自己的串口/RTT/控制台即可。
 */
```

## 3. 结构说明

头文件用 `#ifdef EF_USING_ENV / EF_USING_IAP / EF_USING_LOG` 三面宏墙把三组 API 隔离（L62/L99/L121），裁剪任一模块时对应 API 整组消失——应用层误用会在编译期暴露。`extern "C"` 包裹（L54-56/L156-158）保证 C++ 兼容。

## 4. 陷阱

1. 本头文件不含 types 插件 API——那是 `plugins/types/ef_types.h` 的独立世界（需 cJSON）；
2. `ef_env` 结构体定义在 ef_def.h，此处只有指针用法；
3. 与 FlashDB 对照：env 组 → fdb_kvdb.h 的 kv_get/kv_set；log 组 → tsdb。API 形态一一对应便于迁移。

## 5. 全量 API 索引（注后行号）

| API | 声明行号 | 归属实现 |
| --- | --- | --- |
| `easyflash_init` | L60 | easyflash.c |
| `ef_get_env_blob` | L74 | ef_env.c（NG 专属） |
| `ef_get_env_obj` | L75 | ef_env.c |
| `ef_read_env_value` | L76 | ef_env.c |
| `ef_set_env_blob` | L77 | ef_env.c |
| `ef_load_env` | L87 | 三套 env 实现之一 |
| `ef_print_env` | L88 | 同上 |
| `ef_get_env` | L89 | 同上 |
| `ef_set_env` | L90 | 同上 |
| `ef_del_env` | L91 | 同上 |
| `ef_save_env` | L92 | 同上（NG 为空壳） |
| `ef_env_set_default` | L93 | 同上 |
| `ef_get_env_write_bytes` | L94 | 同上 |
| `ef_set_and_save_env` | L95 | 同上（DEPRECATED） |
| `ef_del_and_save_env` | L96 | 同上（DEPRECATED） |
| IAP 9 个 | L107-118 | ef_iap.c |
| LOG 5 个 | L128-132 | ef_log.c |
| `ef_calc_crc32` | L137 | ef_utils.c |
| port 回调 9 个 | L147-154 | 用户实现（ef_port.c） |

命名规律：`ef_` 前缀 + 模块域；`blob` 后缀 = 二进制安全 API；`and_save` 后缀 = legacy 时代"改完即存"的一步式 API。

## 6. 三面宏墙的裁剪矩阵

| 宏（ef_cfg.h） | 放行的声明块 | 裁掉后应用层症状 |
| --- | --- | --- |
| `EF_USING_ENV` | L62-97（blob + 字符串全部） | 参数存取 API 消失，链接报 undefined |
| `EF_USING_IAP` | L99-119 | OTA 辅助 API 消失 |
| `EF_USING_LOG` | L121-133 | 日志 API 消失 |

工具组（`ef_calc_crc32`）与移植回调组不设墙——CRC 恒可用（IAP/ENV 都依赖），port 回调是链接期契约（必须实现，哪怕空函数体）。

## 7. 迁移到 FlashDB 的 API 对照卡

| easyflash.h | FlashDB 对应 | 参数变化 |
| --- | --- | --- |
| `easyflash_init()` | `fdb_kvdb_init(&db, "env", part_name, &cfg, NULL)` | 单例 → 多实例 |
| `ef_set_env_blob(k, v, len)` | `fdb_kv_set(&db, k, v)`（value 带 len 字段） | blob 打包进 value 结构 |
| `ef_get_env_blob(k, buf, len, &saved)` | `fdb_kv_get_blob(&db, k, buf, len)` | 返回值语义一致 |
| `ef_del_env(k)` | `fdb_kv_del(&db, k)` | 一致 |
| `ef_print_env()` | `fdb_kv_print(&db)` | 一致 |
| `ef_set_and_save_env` | （无对应，已被即时写取代） | 概念消失 |

对照读法：先看本头记住"要什么"，再看 fdb_kvdb.h 看"怎么泛化"——API 演进本身就是一份设计文档。

## 8. 互链

- API 行为差异详解：Docs/[06-API 与实测指南](../../../references/filesystems/EasyFlash/Docs/06-API与实测指南.md)
- 错误码/结构：[ef_def_h.md](ef_def_h.md)；配置：[ef_cfg_h.md](ef_cfg_h.md)
