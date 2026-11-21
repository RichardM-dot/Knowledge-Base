# KB: easyflash.c —— 整库初始化入口（A级深注）

对应源文件：`easyflash/src/easyflash.c`（原始 109 行，注释深注后 144 行；A级逐函数中文深注，strip_equiv PASS）

## 0. 定位与读法

全库唯一的"总开关"：定义 `easyflash_init()`，按 `ef_cfg.h` 的功能宏串行拉起 ENV/IAP/LOG 三个子模块，并用静态 `init_ok` 保证幂等。文件另附两段原文注释：Backup Area 的线性布局图（ENV 区→LOG 区→IAP 区）与 NG/legacy 模式说明。

- 数据流中的位置：一切 API 的前置（见 [../references/filesystems/EasyFlash/Docs/01-初始化与存储布局.md]）。
- 互链：[ef_env.c.md](ef_env.c.md)（第 2 步的实现）、[ef_port.c.md](ef_port.c.md)（第 1 步的实现）。

## 1. 逐函数表

| 函数 | 行号(注后) | 签名要点 | 职责 | 关键细节 |
| --- | --- | --- | --- | --- |
| `easyflash_init` | L96 | `EfErrCode easyflash_init(void)` | 四步串行初始化流水线 | 详见下节 |

全文件仅此一个对外函数；子模块函数用 **extern 就地声明**（L97-100）而非包含模块头——EasyFlash 刻意只暴露 `easyflash.h` 一个公共头，模块间以"约定原型"解耦。

## 2. 关键摘录（逐字，注后行号）

### 2.1 幂等开关（注后 L105-109）

```c
    static bool init_ok = false; /* 幂等开关：仅本文件可见，成功后置位，重启前不再重复初始化 */

    if (init_ok) {
        return EF_NO_ERR; /* 已初始化过则短路返回，避免重复扫描 Flash（扫一遍日志区较耗时） */
    }
```

要点：
- `static` 局部变量存放在 .bss，上电即 false；
- **失败不置位**：任一步失败，下次调用会重试完整初始化（注后 L136 `init_ok = true; /* 四步全部通过才置位 */`）；
- 多任务首次并发调用 init 无锁保护，约定 init 只在单线程启动阶段调用。

### 2.2 四步流水线（注后 L111-137 核心）

```c
    /* 第 1 步：移植层入口，取出用户默认 ENV 表（ef_port.c 的 default_env_set[]） */
    result = ef_port_init(&default_env_set, &default_env_set_size);
    ...
        /* 第 2 步：ENV 挂载 + 掉电恢复。这一步是初始化中最重的一步（多次读 Flash、可能格式化） */
        result = ef_env_init(default_env_set, default_env_set_size);
    ...
        /* 第 3 步：IAP 只算备份区首地址（ENV 区、LOG 区之后），纯内存运算 */
        result = ef_iap_init();
    ...
        /* 第 4 步：日志区扇区头扫描，恢复环形缓冲区读写指针 */
        result = ef_log_init();
```

| 步骤 | Flash 开销 | 失败表现 |
| --- | --- | --- |
| 1 port_init | 0（纯指针） | 几乎不可能 |
| 2 env_init | 全扇区头扫描 + 可能格式化重建 | 配置宏非法时 EF_ASSERT 死循环 |
| 3 iap_init | 0（地址算术） | 几乎不可能 |
| 4 log_init | 逐扇区读头（12B/扇区） | 扇区头错会触发整区 clean |

任一步失败：后续步骤跳过，返回首个错误码，`init_ok` 保持 false。

## 3. 编译期闸门（注后 L62-64）

```c
#if !defined(EF_START_ADDR)
#error "Please configure backup area start address (in ef_cfg.h)"
#endif
```

`EF_START_ADDR` / `EF_ERASE_MIN_SIZE` 未配置直接编译报错——把"忘配宏"拦截在编译期，是本文件唯一的 #error。

## 4. 陷阱

1. ENV/IAP/LOG 三个子模块的头文件从不被 include，函数原型靠 `extern` 口口相传——**改子模块签名时编译器不会在这里报错**，链接期才炸；
2. `init_ok` 与 NG 模式 `ef_env_init` 内部自己的 `init_ok`（ef_env.c L226）是两套独立防重入，语义重复但无害；
3. 失败重试意味着日志区可能被反复扫描/清空——初始化失败时先修配置，别硬循环重试。

## 5. 初始化时序量化（NG 模式，SPI NOR @ 30MHz，4KB 扇区）

| 步骤 | Flash 访问 | 耗时量级 |
| --- | --- | --- |
| ef_port_init | 0 | ~0 |
| ef_env_init→ef_load_env | 每扇区读头 16B × 扇区数 + 恢复动作 | 64 扇区 ≈ 数 ms |
| （首次）ef_env_set_default | 全区擦+写默认表 | 64 扇区 ≈ 1~3 s（擦最慢） |
| ef_iap_init | 0 | ~0 |
| ef_log_init | 每扇区读头 12B × 日志扇区数 | 16 扇区 ≈ 1 ms 级 |

**首次上电最慢**（要格式化全区）。看门狗配置须把首启时间算进去；电池设备可分步初始化或把默认表精简。

## 6. RTOS 场景要点

1. init 放主线程启动阶段，与创建 env 锁（ef_port_env_lock 内）先后：**先建锁后 init**；
2. init 成功前其他线程若已开始调 API，会得到 `EF_ENV_INIT_FAILED`——用 init 完成信号量串行化；
3. `init_ok` 非 volatile：init 与业务线程若跨核/低功耗唤醒场景，建议自加内存屏障（单核同优先级路径无问题）。

## 7. 互链

- 执行流详解：Docs/[01-初始化与存储布局](../../../references/filesystems/EasyFlash/Docs/01-初始化与存储布局.md)
- 第 2 步主体：[ef_env.c.md](ef_env.c.md)；第 1 步：[ef_port.c.md](ef_port.c.md)
- 版本号与错误码定义：[ef_def_h.md](ef_def_h.md)
