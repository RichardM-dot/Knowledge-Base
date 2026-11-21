# src_VOFA —— VOFA+ justfloat 协议与 DMA 发送

> 对应源文件：`src/VOFA.c`（共 63 行）、`src/VOFA.h`（共 25 行）
> 注释级别：A 级（逐函数中文深注），strip_equiv 校验 PASS
> 工程路径：`/Users/theo-richard/workspace/references/firmware/Power-Meter`

**互链 Docs：** [04-VOFA协议与串口DMA](../../../references/firmware/Power-Meter/Docs/04-VOFA协议与串口DMA.md)（帧格式逐字节与 HAL_BUSY 分析）、[05-中断系统与数据流总图](../../../references/firmware/Power-Meter/Docs/05-中断系统与数据流总图.md)

---

## 1. 文件职责与位置

VOFA+ 是 PC 端波形上位机，justfloat 协议 = **N 个 float（小端）+ 4 字节帧尾哨兵 `00 00 80 7F`**。本文件用 union 共享内存免转换组帧，经 USART2 + DMA1_Channel7 每 2 ms 发 1 帧 20 字节。数据流位置：采集链最末端的"给人看"出口（另一出口是 CAN）。

调用链：`TIM1 回调 main.c:101` → `VOFA_justfloat(shunt_volt, bus_volt, current, power)` → `HAL_UART_Transmit_DMA(&huart2, ...)`。

---

## 2. 帧格式（VOFA.h:5-10 与 VOFA.c:38-43 注释汇总）

| 字节 | 内容 | 备注 |
|---|---|---|
| 0..3 | float a（分流电压 V） | 小端 |
| 4..7 | float b（总线电压 V） | 小端 |
| 8..11 | float c（电流 A） | 小端 |
| 12..15 | float d（功率 W） | 小端 |
| 16..19 | `0x00 0x00 0x80 0x7F` | 小端 +∞ 的位型，切帧哨兵 |

VOFA+ 的 justfloat 控件按帧尾自动切帧绘图，不需要额外包头。

---

## 3. 逐函数/对象表（VOFA.c）

| 对象/函数 | 行号 | 职责 |
|---|---|---|
| `union main data` | 25-29 | 20 字节共享内存：float 视图 data1[4] / 字节视图 data2[20] |
| `VOFA_justfloat` | 45-63 | 写 4 个 float → 写帧尾 → 启动 DMA 发送 |

仅 2 个代码对象，是全工程最小的模块。

---

## 4. 关键代码摘录

### 4.1 union 共享内存（VOFA.c:25-29）

```c
union main
{
    float   data1[4];   /* 4 路 float 数据 */
    uint8_t data2[20];  /* 20 字节原始发送缓冲区 */
} data;
```

原理：写 `data1[]` 后读 `data2[]` 得到 float 的小端字节序列，无需 memcpy 或指针强转。成立前提：STM32 小端架构 + `data1` 由编译器保证 4 字节对齐。标签 `main` 与 `main()` 函数不冲突（C 的标签命名空间独立），原工程名字保留（:22-23 注释已说明）。

### 4.2 组帧与发送（VOFA.c:45-63）

```c
void VOFA_justfloat(float a, float b, float c, float d)
{
    uint8_t i = 0;

    /* 依次把 4 个 float 写入 union 的 float 数组 */
    data.data1[i++] = a;
    data.data1[i++] = b;
    data.data1[i++] = c;
    data.data1[i++] = d;

    /* 写入 justfloat 帧尾：小端表示的 0x7F800000 */
    data.data2[16] = 0x00;
    data.data2[17] = 0x00;
    data.data2[18] = 0x80;
    data.data2[19] = 0x7f;

    /* 通过 USART2 的 DMA 通道发送整帧 20 字节（非阻塞） */
    HAL_UART_Transmit_DMA(&huart2, data.data2, sizeof(data.data2));
}
```

三个要点：

1. **帧尾每帧重写**：float 写入只覆盖 0..15，帧尾恒定但重写是低成本保险；
2. **返回值被忽略**：DMA 忙时返回 `HAL_BUSY`，本帧静默丢弃（不累积、下帧恢复）；
3. **全局单缓冲**：DMA 发送期间若再次写入会撕裂，本工程时序天然安全（发送 1.74 ms < 周期 2 ms），改动采样周期前要重新算账。

---

## 5. 带宽与时序量化

| 项 | 数值 | 计算 |
|---|---|---|
| 帧率 | 500 帧/s | 2 ms 周期 |
| 数据率 | 10000 B/s | 500 × 20 B |
| 串口上限 | 11520 B/s | 115200 ÷ 10 bit/字节 |
| **负载率** | **86.8%** | 10000 ÷ 11520 |
| 单帧 DMA 时长 | 1.736 ms | 20 × 10 ÷ 115200 |
| 安全余量 | 0.264 ms | 2 ms − 1.736 ms |

余量被 TIM1 中断延迟吃掉时（I2C 偶发重试、同级 DMA 收尾占用）触发 HAL_BUSY 丢帧，VOFA+ 上表现为波形偶发 2 ms 缺点。

---

## 6. 已知问题（记录在案，不改码）

| # | 问题 | 位置 |
|---|---|---|
| 1 | `HAL_UART_Transmit_DMA` 返回值忽略，丢帧无统计 | :62 |
| 2 | 全局单缓冲，理论撕裂窗口存在（当前时序安全） | :25-29 |
| 3 | 变量 `i` 用后即弃（直接下标亦可） | :47 |
| 4 | union 标签 `main` 与入口函数同名（合法但费解） | :25 |

---

## 7. 修改指引

| 想改 | 位置 |
|---|---|
| 通道数/含义 | `VOFA_justfloat` 签名与 data1 维数（同步改 VOFA.h 注释） |
| 丢弃帧统计 | :62 检查返回值后计数 |
| 波特率 | usart.c:57（改后重算负载率是否 <100%） |
| 采样率（降低负载） | tim.c:59-61 |
