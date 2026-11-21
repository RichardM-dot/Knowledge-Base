# KB: ef_def.h —— 公共定义头（B级 API 契约注释）

对应源文件：`easyflash/inc/ef_def.h`（原始 124 行，注释深注后 157 行；B级导读块 + 状态机/错误码契约注释，strip_equiv PASS）

## 0. 定位

全库地基头：版本号、日志宏、断言宏、错误码、扇区三态、**KV 节点状态机与对象结构**。被 easyflash.h 与所有 src 实现包含。

## 1. 内容清单（注后行号）

| 内容 | 行号 | 说明 |
| --- | --- | --- |
| `EF_SW_VERSION` / `EF_SW_VERSION_NUM` | L50-51 | "4.1.99" / 0x40199 |
| `EF_ENV_VER_NUM` 兜底 | L57-60 | 未定义时默认 0（增量升级用） |
| `EF_ENV_NAME_MAX` | L62-64 | key 上限 32（含尾，有效 31 字符） |
| `EF_DEBUG` | L67-71 | PRINT_DEBUG 开才有效，带文件/行号 |
| `EF_INFO` | L73 | 运行信息（移植层实现） |
| `EF_ASSERT` 多行宏 | L75-80 | 断言失败打印后 `while(1)` 死循环——**带续行符，严禁宏体插注释**（深注已标注） |
| `ef_env` 结构 | L84-86 | {key, value, value_len} 默认参数表条目 |
| `EfErrCode` | L92-102 | 8 个错误码（L94 起） |
| `EfSecrorStatus` | L104-110 | legacy 扇区三态（Secror 为原拼写笔误，深注已记录） |
| `enum env_status` | L122-130 | **KV 7 态状态机**（前有 L112-121 深注块） |
| `struct env_node_obj` | L138-148 | KV 元数据对象（name 内嵌数组，前有深注块） |

## 2. 关键摘录（逐字，注后行号）

### 2.1 状态机深注（注后 L112-121，Docs 04 的 stateDiagram 数据源）

```c
/*
 * 【深注】KV 节点状态机（NG 模式，status table 按写粒度逐位/逐格落盘，
 * 只能沿"写入方向"前进，天然单调——这是追加写掉电安全的基础）：
 *   ENV_UNUSED     0xFF..：擦除态，此节点从未写过；
 *   ENV_PRE_WRITE  已写状态表但头/数据未写完（掉电在此状态 -> 上电标 ENV_ERR_HDR 作废）；
 *   ENV_WRITE      完整有效（CRC 过），set/get 只认这个状态；
 *   ENV_PRE_DELETE 已标记待删（同名覆盖时先打此标记；掉电 -> 上电 move_env 恢复旧值）；
 *   ENV_DELETED    彻底删除（GC 搬运时新副本已写好才打这个标）；
 *   ENV_ERR_HDR    头部错误（长度非法/半写），节点作废但保留元数据不撞坏相邻数据。
 */
```

### 2.2 对象结构（注后 L138-150，代码未动）

```c
struct env_node_obj {
    env_status_t status;                         /**< ENV node status, @see node_status_t */
    bool crc_is_ok;                              /**< ENV node CRC32 check is OK */
    uint8_t name_len;                            /**< name length */
    uint32_t magic;                              /**< magic word(`K`, `V`, `4`, `0`) */
    uint32_t len;                                /**< ENV node total length (header + name + value), must align by EF_WRITE_GRAN */
    uint32_t value_len;                          /**< value length */
    char name[EF_ENV_NAME_MAX];                  /**< name */
    struct {
        uint32_t start;                          /**< ENV node start address */
        uint32_t value;                          /**< value start address */
    } addr;
};
```

`name[32]` 内嵌数组是刻意的：find_env 读头时一并读入名字，避免二次 Flash 读；addr.start/value 是 Flash 绝对地址，值本体永远留在 Flash（NG 模式零拷贝哲学）。

### 2.3 错误码契约（注后 L89-91 深注 + L92-102 枚举）

```c
/*
 * 【深注】全库错误码契约：0 恒为成功；1~3 是移植层 IO 三兄弟；4~7 是 ENV 业务错。
 */
```

枚举序：`EF_NO_ERR=0`、`EF_ERASE_ERR=1`、`EF_READ_ERR=2`、`EF_WRITE_ERR=3`、`EF_ENV_NAME_ERR=4`、`EF_ENV_NAME_EXIST=5`、`EF_ENV_FULL=6`、`EF_ENV_INIT_FAILED=7`。

## 3. 陷阱

1. `EF_ASSERT` 失败即死循环——量产版本可重定义成记录后复位的弱实现，但默认行为要心里有数；
2. `EF_ENV_NAME_MAX` 改大注意：env_node_obj 内嵌 name 数组随之变大，栈上 `struct env_node_obj` 会膨胀（ef_env.c 多处局部变量）；
3. `EfSecrorStatus` 拼写（Secror）已随版本发布固定，符号层面无法修复。

## 4. env_node_obj 的 RAM 足迹

`struct env_node_obj` 含内嵌 `char name[32]`，典型 ARM 布局 sizeof ≈ 64B。它在 ef_env.c 里大量以**局部变量**出现（`set_env` 的 static、`env_iterator` 的调用方栈变量、`do_gc` 的局部 env 等），决定栈预算：

| 场景 | 并发存在的对象数 | 栈占用 |
| --- | --- | --- |
| `ef_set_env_blob` 全链 | 2~3 个（env+sector+临时） | ~200 B |
| `gc_collect`→`do_gc`→`move_env` | 3~4 个 | ~260 B |
| `ef_print_env`（env_iterator 栈上） | 1~2 个 | ~130 B |

栈预算 512B 覆盖全部 ENV API（含 port 层）；改大 `EF_ENV_NAME_MAX` 时按比例重算。

## 5. EF_DEBUG 的三级打印体系

| 宏 | 行号 | 用途 | 开关 |
| --- | --- | --- | --- |
| `EF_DEBUG` | L67-71 | 开发调试（带文件：行号） | `PRINT_DEBUG`（ef_cfg.h L113） |
| `EF_INFO` | L73 | 运行告警/提示 | 恒开，移植层实现 |
| `ef_print` | （port） | 数据输出（print_env 用） | 恒开 |

量产建议：保留 EF_INFO，关闭 PRINT_DEBUG 省下全部 EF_DEBUG 调用的格式化字符串 ROM。

## 6. 互链

- 状态机应用：Docs/[02](../../../references/filesystems/EasyFlash/Docs/02-环境变量读写全流程.md)（stateDiagram）、Docs/[04](../../../references/filesystems/EasyFlash/Docs/04-掉电安全与恢复.md)（恢复）
- 使用者：[ef_env.c.md](ef_env.c.md)（45 函数全部引用本头）
