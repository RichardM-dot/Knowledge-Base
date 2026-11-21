# src_tim —— TIM1 采样节拍定时器

> 对应源文件：`src/tim.c`（共 143 行；配套 `src/tim.h` 共 63 行，不在注释清单内、已有注释）
> 注释级别：B 级（文件头导读块），strip_equiv 校验 PASS
> 工程路径：`/Users/theo-richard/workspace/references/firmware/Power-Meter`

**互链 Docs：** [01-启动流程与外设初始化](../../../references/firmware/Power-Meter/Docs/01-启动流程与外设初始化.md)（3.5 节逐行）、[05-中断系统与数据流总图](../../../references/firmware/Power-Meter/Docs/05-中断系统与数据流总图.md)

---

## 1. 文件职责与位置

TIM1 是全工程的**心跳**：纯计数节拍，不输出 PWM、不级联。每 2 ms（500 Hz）溢出一次 → `TIM1_UP_IRQn`（优先级 0）→ `HAL_TIM_PeriodElapsedCallback`（main.c:77）→ 完成 INA226 采样与双出口上报。`htim1` 是全局句柄，main.c:169 用它启动中断模式。

文件头导读块（tim.c:1-9）给出了周期计算；USER CODE Header 区（:11-28）保留 CubeMX 原始头。

---

## 2. 逐函数表

| 函数 | 行号 | 职责 |
|---|---|---|
| `MX_TIM1_Init` | 45-88 | 配置 PSC/ARR/计数模式、时钟源、主从模式，调 `HAL_TIM_Base_Init` |
| `HAL_TIM_Base_MspInit` | 95-114 | 使能 TIM1 时钟、`TIM1_UP_IRQn` 优先级 0 并使能（HAL 自动回调） |
| `HAL_TIM_Base_MspDeInit` | 121-139 | 关时钟、关中断（运行期不调用） |

`tim.h` 对外暴露 `htim1`（:44）与 `MX_TIM1_Init`（:53）。

---

## 3. 关键代码摘录

### 3.1 定时参数（tim.c:58-64）

```c
  htim1.Instance = TIM1;                              /* 使用 TIM1（高级定时器） */
  htim1.Init.Prescaler = 71;                          /* 72 MHz / 72 = 1 MHz */
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;        /* 向上计数 */
  htim1.Init.Period = 1999;                           /* 2000 个计数 → 2 ms */
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;  /* 死区/滤波时钟不分频 */
  htim1.Init.RepetitionCounter = 0;                   /* 重复计数 0（每次溢出都产生事件） */
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE; /* 关闭预装载 */
```

周期推导：

```text
计数时钟 = 72 MHz ÷ (PSC+1) = 72 MHz ÷ 72 = 1 MHz（1 计数 = 1 µs）
更新周期 = (ARR+1) × 1 µs = 2000 µs = 2 ms → 500 Hz
```

三个容易踩的语义：

- `Prescaler=71` 是"71+1"分频（寄存器从 0 计数）；
- `Period=1999` 是"1999+1"个计数；
- `RepetitionCounter=0` 对**高级定时器** TIM1 必须为 0——RCR 非零时溢出 RCR+1 次才触发一次更新事件，采样周期会悄悄变长。

### 3.2 时钟源与主从（tim.c:71-83）

```c
  /* 时钟源选择内部时钟（CK_INT） */
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  ...
  /* 主从模式配置：本定时器独立运行，不输出触发信号 */
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
```

独立运行、不作为其它定时器的触发源。

### 3.3 NVIC（tim.c:104-108）

```c
    /* 使能 TIM1 时钟 */
    __HAL_RCC_TIM1_CLK_ENABLE();

    /* TIM1 更新中断优先级设为 0（最高），保证 2 ms 采样周期稳定 */
    HAL_NVIC_SetPriority(TIM1_UP_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM1_UP_IRQn);
```

0 级与 DMA1_Channel7 同级：同抢占级互不抢占，保证采样回调不被 DMA 收尾打断，但反过来 DMA 收尾也可能被采样延迟——这正是 VOFA 丢帧分析的时序基础（见 [04](../../../references/firmware/Power-Meter/Docs/04-VOFA协议与串口DMA.md) 第 4 节）。

---

## 4. 时序量化

| 项 | 数值 | 备注 |
|---|---|---|
| 节拍频率 | 500 Hz | 72/72/2000 |
| 中断单次开销（不含业务） | ~3 µs | 入栈 + HAL 两跳 |
| 业务占用 | ~620 µs/拍 | 详见 [05](../../../references/firmware/Power-Meter/Docs/05-中断系统与数据流总图.md) 第 4 节 |
| CPU 占用率 | 31% | 620 µs ÷ 2 ms |

---

## 5. 已知问题（记录在案，不改码）

| # | 问题 | 位置 |
|---|---|---|
| 1 | `AutoReloadPreload` 关闭：运行中改 ARR 立即生效，可能产生半周期毛刺（本工程不动态改 ARR，无影响） | :64 |
| 2 | 主循环空转，节拍完全依赖中断，无看门狗兜底 | 架构层面 |

---

## 6. 修改指引

| 想改 | 位置 |
|---|---|
| 采样周期 | tim.c:59-61（PSC/Period；改后重算 VOFA 负载率与 INA226 转换余量） |
| 提高节拍抖动鲁棒性 | tim.c:107 优先级（已是最高，无需动） |
| 换定时器 | 同步改 stm32f1xx_it.c 的 IRQHandler 与 main.c 启动调用 |
