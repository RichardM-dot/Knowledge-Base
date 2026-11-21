# KB: ef_port.c —— 移植层模板（B级文件头导读 + 移植点注释）

对应源文件：`easyflash/port/ef_port.c`（原始 187 行，注释深注后 219 行；B级导读块 + 各函数移植要点注释，strip_equiv PASS）

## 0. 定位

官方承诺"移植只改这一个文件"。9 个空函数 + 1 个空默认表，每个函数体内留 `/* You can add your code under here. */` 填空点，深注在各填空点追加了移植契约与示例提示。

## 1. 逐函数表（注后行号）

| 函数 | 行号 | 契约 | 深注要点 |
| --- | --- | --- | --- |
| `default_env_set[]` | L57 | 出厂默认参数表 | value_len==0 按字符串；示例已注 |
| `ef_port_init` | L69 | 传出默认表指针与条数 | 不许访问 Flash |
| `ef_port_read` | L88 | 字节 size、写粒度对齐 | 示例：片内 memcpy / SFUD sfud_read |
| `ef_port_erase` | L108 | addr 已断言对齐 | **size 可能 4B，必须向上取整整扇擦除**（深注重点） |
| `ef_port_write` | L131 | 写前须已擦；禁同址重写 | stm32f1 半页编程用 0xFF 补齐（深注示例） |
| `ef_port_env_lock/unlock` | L144 / L155 | 多任务临界区 | 用递归互斥（ENV API 会嵌套调用，深注已提示） |
| `ef_log_debug` | L171 | PRINT_DEBUG 门控 | 未定义时库内空宏 |
| `ef_log_info` | L194 | 运行日志 | 接 printf |
| `ef_print` | L210 | 数据打印 | 逐字符，勿加前缀 |

## 2. 关键摘录（逐字，注后行号）

### 2.1 擦除的移植要点（注后 L111-113）

```c
    /* make sure the start address is a multiple of EF_ERASE_MIN_SIZE */
    EF_ASSERT(addr % EF_ERASE_MIN_SIZE == 0); /* 库侧已保证，可加移植层二次防御 */

    /* You can add your code under here. */
    /* 【移植】size 小于一个扇区时也要擦整扇区（flash_erase 物理特性）；
     * 调用方可能传入 4B 这样的"非整扇 size"，此处必须向上对齐 */
```

背景：`ef_env_legacy_wl.c` 的 `save_cur_using_data_addr` 只传 4B——这是库与移植层之间的隐式契约，忘做兜底是自定义移植最常见的事故源。

### 2.2 写粒度要点（注后 L134-136）

```c
    /* You can add your code under here. */
    /* 【移植】写粒度提醒：stm32f1 片内 Flash 以半页 16 字为单位编程，
     * size 不足时用 0xFF 补齐到整页写；NOR Flash 则支持 1 字节独立写 */
```

### 2.3 默认表条目格式（注后 L51-56）

```c
/*
 * 【深注】出厂默认参数表：ef_env_set_default() 与 ENV 自动升级（EF_ENV_AUTO_UPDATE）
 * 都从这里重放。ef_env.value_len==0 时按 C 字符串处理（strlen 取长）；
 * 需要预置二进制值就填 value_len。示例：
 *   {"wifi_ssid", "my-wifi", 0}, {"device_id", buf, sizeof(buf)}
 */
```

## 3. 移植验收要点

1. init 后串口应出现 `ENV start address is 0x..., size is ... bytes`（EF_DEBUG 需开 PRINT_DEBUG）；
2. 写一次参数后 `ef_get_env_write_bytes` 增量应为节点大小（NG），而不是整区（那是 legacy 的正常值）；
3. erase 忘兜底的症状：legacy-WL 保存后重启丢参；NG 模式一般不会踩到（总是整扇区起擦）。

## 4. 官方 demo 移植对照

`demo/env/stm32f10x/` 的 `ef_port.c` 是最权威的填空答案，可直接对照抄写：

| 填空点 | demo 实现（stm32f10x 片内） | 要点 |
| --- | --- | --- |
| `ef_port_read` | `memcpy(buf, (const uint32_t *)addr, size)` | 片内 Flash 内存映射，读即 memcpy |
| `ef_port_write` | `FLASH_ProgramWord` 半页循环 | 按 32bit 粒度（EF_WRITE_GRAN=32 对应） |
| `ef_port_erase` | `FLASH_ErasePage` + 页数向上取整 | **size 向上取整到整页**（契约示范） |
| `ef_port_env_lock/unlock` | 空（裸机 demo） | RT-Thread 版 demo 用信号量 |
| `ef_log_debug/info/print` | 重定向到 USART1 printf | 三函数都走同一个 rt_klog |

SPI Flash 版（SFUD）的 read/write/erase 全部一行转发 `sfud_read/write/erase`，erase 天然整扇——这就是"gran=1 + 4KB 扇区"组合零负担的原因。

## 5. 移植自检五问

1. `EF_WRITE_GRAN` 与硬件写机制一致吗？（F1=32bit / F4=8bit / 外部 NOR=1bit）
2. erase 对非整扇 size 做向上取整了吗？
3. write 是否保证目标已擦（全 0xFF）且不重复写同址？
4. RTOS 下 lock/unlock 用递归锁了吗？easyflash_init 在锁创建之后调用吗？
5. `EF_START_ADDR + ENV_AREA_SIZE + LOG_AREA_SIZE` 是否落在真实 Flash 容量内且不撞 App 区？

## 6. 地址空间划分示例（4MB SPI NOR）

```text
0x000000 ┌────────────────────────────┐
         │ ENV 区   ENV_AREA_SIZE=64KB │  16 扇区(NG: 15 用 + 1 GC 预备)
0x010000 ├────────────────────────────┤
         │ LOG 区   LOG_AREA_SIZE=128KB│  32 扇区环形
0x030000 ├────────────────────────────┤
         │ IAP 备份区  512KB           │  App 最大 512KB
0x0B0000 ├────────────────────────────┤
         │ 资源区/文件系统/自由         │
0x400000 └────────────────────────────┘
```

各区起点都是 `EF_START_ADDR` 的编译期偏移；`ef_iap_init` 的叠加算术（ENV 区→LOG 区之后）保证 IAP 区永远不与前面重叠——**改任何 SIZE 都要同步重算后面所有区**。

## 7. 互链

- 移植全景与两套完整示例：Docs/[05-移植层](../../../references/filesystems/EasyFlash/Docs/05-移植层.md)
- 4B 擦除调用方：[ef_env_legacy_wl.md](ef_env_legacy_wl.md) 问题 1
- 配置宏：[ef_cfg_h.md](ef_cfg_h.md)
