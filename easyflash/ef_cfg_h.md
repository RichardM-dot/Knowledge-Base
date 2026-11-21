# KB: ef_cfg.h —— 裁剪与布局配置（B级 API 契约注释）

对应源文件：`easyflash/inc/ef_cfg.h`（原始 98 行，注释深注后 115 行；B级导读块 + 配置契约注释，strip_equiv PASS）

## 0. 定位

移植的"必改清单"：三大功能开关 + 四个布局宏 + 一个读缓冲配置。所有值最终被 src 各文件的 `#if !defined(...)` #error 闸门兜底校验。

## 1. 配置项总表（注后行号）

| 宏 | 行号 | 作用 | 合法值/约束 | 必填 |
| --- | --- | --- | --- | --- |
| `EF_USING_ENV` | L50 | 启用 ENV | 定义即启用（默认开） | - |
| `EF_ENV_AUTO_UPDATE` | L54 | 增量升级开关 | 需配合 EF_ENV_VER_NUM | 可选 |
| `EF_ENV_VER_NUM` | L59 | 默认参数集版本号 | 固件新增默认 KV 时必须 +1 | 启用自动更新时必填 |
| `EF_BIG_ENDIAN` | L62 | 大端 MCU | 默认小端 | 可选 |
| `EF_USING_IAP` | L67 | IAP 功能 | - | 可选 |
| `EF_USING_LOG` | L70 | 日志功能 | 需 LOG_AREA_SIZE | 可选 |
| `EF_ERASE_MIN_SIZE` | L73 | 最小擦除粒度 | = 物理擦除块（1KB/4KB…） | **必填** |
| `EF_WRITE_GRAN` | L77 | 写粒度 bit | 1(NOR)/8(F4)/32(F1) | **必填** |
| `EF_READ_BUF_SIZE` | L81 | 扫描缓冲 | 默认 32B；加大加速首次扫描但费栈 | 可选 |
| `EF_START_ADDR` | L104 | 存储区物理首址 | 须在可擦写区间 | **必填** |
| `ENV_AREA_SIZE` | L107 | ENV 区大小 | ≥ 2 扇区（GC 预备队） | 用 ENV 必填 |
| `LOG_AREA_SIZE` | L110 | 日志区大小 | ≥ 2 扇区 | 用 LOG 必填 |
| `PRINT_DEBUG` | L113 | 调试打印开关 | 定义后 EF_DEBUG 生效 | 可选 |

## 2. 布局总图（原文件 L84-101 注释块）

```text
|----------------------------|   Storage Size
| Environment variables area |   ENV_AREA_SIZE
|----------------------------|
|      Saved log area        |   LOG_AREA_SIZE
|----------------------------|
|(IAP)Downloaded application |   IAP 备份（不固定）
|----------------------------|
```

`@note all area sizes must be aligned with EF_ERASE_MIN_SIZE`——所有区大小对齐擦除粒度，这是布局第一定律。

## 3. 关键摘录（逐字，原始行号）

### 3.1 写粒度注释（L75-77）

```c
/* the flash write granularity, unit: bit
 * only support 1(nor flash)/ 8(stm32f4)/ 32(stm32f1) */
#define EF_WRITE_GRAN             /* @note you must define it for a value */
```

### 3.2 GC 预备队约束（L106-107）

```c
/* ENV area size. It's at least one empty sector for GC. So it's definition must more then or equal 2 flash sector size. */
#define ENV_AREA_SIZE             /* @note you must define it for a value if you used ENV */
```

### 3.3 模式选择说明（L95-100 原文）

```c
 * The EasyFlash add the NG (Next Generation) mode start from V4.0. All old mode before V4.0, called LEGACY mode.
 *
 * - NG (Next Generation) mode is default mode from V4.0. It's easy to settings, only defined the ENV_AREA_SIZE.
 * - The LEGACY mode has been DEPRECATED. ...
```

> 深注勘误：用 legacy 模式还需额外定义 `EF_ENV_USING_LEGACY_MODE`（及可选 `EF_ENV_USING_WL_MODE` / `EF_ENV_USING_PFS_MODE`）——这些宏在 ef_cfg.h 里没有出现，是历史上通过编译器命令行 -D 传入的（demo 工程可见），深注导读已提示。

## 4. 陷阱

1. **占位宏是"故意空着"的**：`#define EF_START_ADDR`（无值）能过预编译但过不了使用处的 #error——按报错提示逐个补值即可；
2. NG 与 legacy 的配置面完全不同：NG 只要 `ENV_AREA_SIZE`；legacy 还要 `ENV_USER_SETTING_SIZE`（在 demo 层或编译器传入，源码 L42-44 的 #error 会提醒）；
3. `EF_READ_BUF_SIZE` 须 4 的倍数（ef_log.c/ef_env.c 的断言）。

## 5. 三套 ENV 模式的配置面差异

| 配置项 | NG（默认） | legacy normal | legacy WL |
| --- | --- | --- | --- |
| `ENV_AREA_SIZE` | 必须 ≥2 扇区 | 必填 | 必填 |
| `ENV_USER_SETTING_SIZE` | 不需要 | 必填（=RAM 缓存大小） | 必填 |
| `EF_ENV_USING_LEGACY_MODE` | 不定义 | 编译器 -D 定义 | 同左 |
| `EF_ENV_USING_WL_MODE` | - | 不定义 | 定义 |
| `EF_ENV_USING_PFS_MODE` | -（NG 原生掉电安全） | 可选（区×2） | 可选（区×2） |
| GC 相关阈值 | `EF_GC_EMPTY_SEC_THRESHOLD` 等（ef_env.c 内） | 无 | 无 |

> 注意：`EF_ENV_USING_LEGACY_MODE` 三个宏**不在本文件中**，历史上由 demo 工程的编译器命令行传入（如 RT-Thread 的 SConscript），这也是本文件导读块之外需要知道的"暗配置"。

## 6. demo 参考取值

| demo 工程 | EF_ERASE_MIN_SIZE | EF_WRITE_GRAN | ENV_AREA_SIZE |
| --- | --- | --- | --- |
| stm32f10x 片内 | 1024 | 32 | 4×1024 |
| stm32f4xx 片内 | 4096(选小扇区) | 8 | 视区而定 |
| SPI Flash (SFUD) | 4096 | 1 | 16×4096 起 |

## 7. 配置变更检查清单

改本文件前逐项过一遍：

- [ ] `EF_ERASE_MIN_SIZE` = 物理 erase block（查 Flash 手册，勿拍脑袋）？
- [ ] `EF_WRITE_GRAN` 与 MCU 写机制匹配（F1→32 / F4→8 / NOR→1）？
- [ ] `EF_START_ADDR` 不撞 App/Bootloader/保留区，且自身扇区对齐？
- [ ] `ENV_AREA_SIZE ≥ 2×EF_ERASE_MIN_SIZE`（NG 硬性）？
- [ ] 开了 `EF_ENV_AUTO_UPDATE` 就同步更新 `EF_ENV_VER_NUM`？
- [ ] `LOG_AREA_SIZE ≥ 2×EF_ERASE_MIN_SIZE` 且为整扇倍数？
- [ ] 所有 SIZE 是 `EF_ERASE_MIN_SIZE` 整数倍（IAP 布局偏移才不歪）？

## 8. 互链

- 各宏的运行时后果：Docs/[01-初始化与存储布局](../../../references/filesystems/EasyFlash/Docs/01-初始化与存储布局.md)、Docs/[05-移植层](../../../references/filesystems/EasyFlash/Docs/05-移植层.md)
- 使用这些宏的闸门：[easyflash.c.md](easyflash.c.md)、[ef_env.c.md](ef_env.c.md)
