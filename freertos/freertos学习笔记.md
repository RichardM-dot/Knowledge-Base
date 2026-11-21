# FreeRTOS 源码学习总索引

> 本文件是 `Knowledge-Base/freertos/` 目录的总入口。
> 各专题文档负责“这个模块怎么工作”；本文件负责“读源码时按什么顺序判断、
> 遇到一条注释如何定位到真实代码、哪些概念在多个模块之间复用”。
>
> 行号基准：`/Users/theo-richard/workspace/freertos` 工作区，
> tasks.c 共 9398 行、task.h 共 3557 行、list.c 共 568 行、
> port.c（GCC/ARM_CM4F）共 972 行。源码中的中文注释会改变行号，
> 引用前先用 `grep -n` 复核。

---

## 1. 文档地图

| 顺序 | 文档 | 解决的问题 | 前置 |
|---:|---|---|---|
| 0 | [freertosconfigh.md](freertosconfigh.md) | 配置宏如何改变编译结果、平台差异、`#error` 组合 | 无 |
| 1 | [list.md](list.md) | 环形双向链表、哨兵、`pxIndex`、`pvOwner`、`pxContainer` | C 指针 |
| 2 | [tasks.md](tasks.md) | TCB、就绪/延时/挂起表、tick、上下文切换、通知 | list |
| 3 | [queue.md](queue.md) | 队列、信号量、互斥量、优先级继承、ISR 锁 | tasks |
| 4 | [timers.md](timers.md) | 定时器服务任务、命令队列、到期链表 | queue/tasks |
| 5 | [event_groups.md](event_groups.md) | 位集合、无序事件表、广播唤醒 | tasks |
| 6 | [stream_buffer.md](stream_buffer.md) | 字节环、触发阈值、任务通知替代等待链表 | tasks |
| 7 | [croutine.md](croutine.md) | 协程的 `switch` 状态机、共享栈、遗留特性 | tasks |
| 8 | [port_cortex_m4f.md](port_cortex_m4f.md) | MSP/PSP、PendSV/SVC/SysTick、寄存器保存 | tasks |

---

## 2. 读任何一段 FreeRTOS 代码的六个判定

### 2.1 配置判定

先问：这段代码受哪个 `config*` 宏控制？

- 宏默认值通常在 `include/FreeRTOS.h` 的 `#ifndef config...` 段；
- 非法组合通常在同一文件的 `#error` 段；
- 实际值在工程的 `FreeRTOSConfig.h`；
- 不要假设模板文件的值就是当前工程的值。

例子：`configUSE_STATS_FORMATTING_FUNCTIONS` 默认 0（FreeRTOS.h:2793-2795），
但模板文件也定义为 0（template_configuration/FreeRTOSConfig.h:389）。
两者都看，才能确定函数是否编译。

### 2.2 预处理判定

再问：这段代码是否进入翻译单元？

```c
#if ( configUSE_PREEMPTION == 0 )
    /* 协作式分支 */
#else
    /* 抢占式分支 */
#endif
```

- `#if` 的条件在预处理阶段求值；
- `#else` 覆盖的是“前面条件为假”的所有值，不是“默认值”；
- `#endif` 后的注释不参与语法，不能作为嵌套依据；
- 嵌套层级多时，画出生树或使用 `gcc -E`。

### 2.3 宏/函数判定

再问：这个名字是宏还是函数？

```bash
grep -n '#define taskYIELD'        include/task.h
grep -n 'void vTaskSwitchContext'  tasks.c
```

- 宏没有函数符号，`nm`/`readelf` 查不到；
- 宏参数可能被忽略（`(void)(pxTCB)`），也可能被多次求值；
- 宏可能展开为 `do { } while(0)`，也可能展开为空；
- 函数才有调用约定、栈帧、返回地址。

### 2.4 符号判定

再问：定义在哪个文件、哪个条件分支？

- 声明在 `.h`，定义在 `.c`；
- 移植相关宏继续追到 `portmacro.h` / `port.c`；
- 同名函数可能有单核/多核两个定义（如 `vTaskSwitchContext`：
  单核 tasks.c:5532，多核 tasks.c:5620）；
- 静态函数可能有两个定义（如 `prvAddNewTaskToReadyList`：
  单核 tasks.c:2183，多核 tasks.c:2268）。

### 2.5 上下文判定

再问：当前代码在任务、ISR，还是调度器挂起期间执行？

| 上下文 | 可否阻塞 | 可否改就绪表 | 典型互斥 |
|---|---|---|---|
| 任务 | 可以 | 可以（需临界区） | `taskENTER_CRITICAL()` |
| ISR | 不可以 | 有限制，通常转 pending | `portSET_INTERRUPT_MASK_FROM_ISR()` |
| 调度器挂起 | 不可以切换 | 不可以 | `vTaskSuspendAll()` + 临界区 |

`xTaskIncrementTick()` 是反例：函数内部没有临界区，靠调用者提供互斥。
Cortex-M4F 的 SysTick handler 在 port.c:609 关中断，`xTaskResumeAll()`
在 tasks.c:4364 的临界区内补算。

### 2.6 不变量判定

再问：这段代码依赖哪个数据结构不变量？

- 延时表按绝对唤醒 tick 升序；
- 队列/信号量事件表按优先级编码升序；
- 事件组事件表**无序**，`xItemValue` 复用为等待条件；
- 就绪表不按值排序，`pxIndex` 负责同优先级轮转；
- 一个 TCB 的 `xStateListItem` 和 `xEventListItem` 可以同时挂在两条链表；
- `pxContainer == NULL` 表示该项当前不属于任何链表。

不变量被破坏时，现象通常是断言失败、链表计数错误、任务重复唤醒或死锁。

---

## 3. 八个跨模块概念

### 3.1 TCB

`typedef struct tskTaskControlBlock { ... } tskTCB;`
（tasks.c:425-505，`typedef tskTCB TCB_t` 在 tasks.c:511）。

关键字段：

| 字段 | 行 | 作用 |
|---|---:|---|
| `pxTopOfStack` | 427 | 上下文切换的栈顶 |
| `xStateListItem` | 437 | 就绪/延时/挂起表成员 |
| `xEventListItem` | 438 | 队列/事件组等待表成员 |
| `uxPriority` | 439 | 当前优先级（可能被继承） |
| `pxStack` | 440 | 栈低地址 |
| `pcTaskName` | 445 | 调试名称 |
| `ulNotifiedValue[]` | 486 | 通知值 |
| `ucNotifyState[]` | 487 | 通知状态 0/1/2 |
| `ucStaticallyAllocated` | 495 | 分配方式状态码 |

### 3.2 就绪表

`pxReadyTasksLists[configMAX_PRIORITIES]`（tasks.c:531）。

- 每个优先级一条链表；
- `uxTopReadyPriority` 缓存最高非空优先级（tasks.c:562）；
- 插入用 `listINSERT_END`，不排序；
- 选择任务用 `listGET_OWNER_OF_NEXT_ENTRY`，推进 `pxIndex` 实现轮转；
- 运行中的任务仍在这条链表里，`pxCurrentTCB` 只是指向它。

### 3.3 延时表

`pxDelayedTaskList` / `pxOverflowDelayedTaskList`（tasks.c:534-535）。

- 按绝对唤醒 tick 升序；
- 回绕时交换两张表（tasks.c:315-327、5170-5173）；
- `xNextTaskUnblockTime` 缓存最早到期时刻（tasks.c:568）；
- `prvAddCurrentTaskToDelayedList()`（tasks.c:9106）负责插入；
- `xTaskIncrementTick()` 负责摘除（tasks.c:5183-5240）。

### 3.4 事件表

- 队列/信号量：有序，`xEventListItem.xItemValue = configMAX_PRIORITIES - uxPriority`；
- 事件组：无序，`xItemValue` 保存等待条件 + 控制位 + `IN_USE` 位；
- 超时路径必须摘除 `xEventListItem`（tasks.c:5228-5231）；
- 事件路径必须摘除 `xStateListItem`（tasks.c:5861 或 5974）。

### 3.5 任务通知

- 每任务 `configTASK_NOTIFICATION_ARRAY_ENTRIES` 个槽；
- 每槽一个 `uint32_t` 值 + 一个 `uint8_t` 状态；
- 状态：0 未等待、1 等待、2 已收到；
- `eNoAction` 只改状态，不改值；
- 流缓冲区用通知代替等待链表，因此只能单读单写。

### 3.6 栈高水位线

- 创建时用 `0xA5` 填充（tasks.c:1963-1968）；
- `prvTaskCheckFreeStackSpace()` 从栈低地址扫描连续 `0xA5`（tasks.c:6831-6848）；
- 返回值是“剩余未使用栈的最小值”，单位 `StackType_t` 字；
- 最大使用量 = 创建时栈深度 - 返回值；
- 依赖 `memset`，声明在 `<string.h>`，定义在工具链 C 库。

### 3.7 分配方式状态码

- `0`：栈和 TCB 动态；
- `1`：仅栈静态；
- `2`：栈和 TCB 静态；
- 不是位掩码，代码用 `==` 比较；
- 字段只在静态和动态都可能时存在（tasks.c:493-496）。

### 3.8 移植层让出

- `taskYIELD()` → `portYIELD()` → 写 `ICSR.PENDSVSET`（portmacro.h:90-99）；
- `taskYIELD_WITHIN_API()` → 单核 `portYIELD_WITHIN_API()` → `portYIELD`；
- `xPortPendSVHandler()` 完成寄存器保存/恢复（port.c:546）；
- `xPortSysTickHandler()` 调 `xTaskIncrementTick()` 并条件挂 PendSV（port.c:602-629）。

---

## 4. 常见误读与纠正

| 误读 | 实际 |
|---|---|
| `configUSE_PREEMPTION=0` 会启用协程 | 不会；它只关闭任务抢占，协程由 `configUSE_CO_ROUTINES` 控制 |
| 协作式有另一个调度函数 | 没有；同一 `vTaskSwitchContext()`，只是 tick 驱动分支不编译 |
| `#else` 默认是“单核为真” | 不是；`#else` 是前面条件为假，嵌套时要看完整层级 |
| `#endif` 注释能说明条件 | 不能；注释不参与预处理，可能过时或错误 |
| `taskYIELD_TASK_CORE_IF_USING_PREEMPTION` 是函数 | 是宏；单核展开可能完全为空 |
| 单核宏使用 `pxTCB->xTaskRunState` | 单核 TCB 没有该字段，字段只在多核定义 |
| “位标志”一定是位掩码 | tasks.c 的三个分配状态码是 0/1/2 的互斥状态，用 `==` 比较 |
| 高水位线是历史最大使用量 | API 返回剩余未使用最小值；最大使用量需用栈深度减 |
| `0xA5` 有硬件含义 | 没有；是软件约定的填充哨兵 |
| `xItemValue` 永远是优先级 | 延时表是时间；事件组是等待条件；只有队列/信号量事件表是优先级 |
| `xTaskIncrementTick()` 自带临界区 | 没有；互斥由 SysTick handler 或 `xTaskResumeAll` 提供 |
| `pdTRUE` 表示已经切换 | 只表示请求切换；真正切换在 PendSV |
| 事件组事件表按优先级排序 | 事件组使用无序表，`vTaskPlaceOnUnorderedEventList()` |
| 流缓冲区通知会传数据 | 默认 `eNoAction` 只传“事件”，数据在环形缓冲区 |

---

## 5. 源码定位命令

```bash
cd /Users/theo-richard/workspace/freertos

# 函数定义
grep -n '^BaseType_t xTaskIncrementTick' tasks.c
grep -n 'void vTaskSwitchContext' tasks.c
grep -n 'void vTaskPlaceOnEventList' tasks.c
grep -n 'vTaskPlaceOnUnorderedEventList' tasks.c
grep -n 'vTaskRemoveFromUnorderedEventList' tasks.c

# 宏定义
grep -n '#define taskYIELD' include/task.h
grep -n '#define portYIELD' portable/GCC/ARM_CM4F/portmacro.h
grep -n 'configUSE_STATS_FORMATTING_FUNCTIONS' include/FreeRTOS.h tasks.c

# 结构体字段
grep -n 'ucNotifyState\|ucStaticallyAllocated\|xTaskRunState' tasks.c

# 预处理展开（需要工具链）
arm-none-eabi-gcc -E -dM -Iinclude -Iportable/GCC/ARM_CM4F tasks.c | grep configUSE_

# ELF 符号
arm-none-eabi-nm -n firmware.elf | grep -E 'pxCurrentTCB|pxReadyTasksLists'
```

---

## 6. 推荐阅读路线

```text
freertosconfigh.md
  → 理解 configUSE_PREEMPTION / configUSE_TIME_SLICING / configNUMBER_OF_CORES
  → 理解 configUSE_STATS_FORMATTING_FUNCTIONS / configCHECK_FOR_STACK_OVERFLOW

list.md
  → 掌握 ListItem_t / List_t / pxContainer / pxIndex
  → 掌握 vListInsert 与 listINSERT_END 的语义差异

tasks.md
  → 第 1-4 部分：TCB、就绪表、延时表、tick、切换
  → 第五部分：逐条注释问答（配置、宏、通知、栈、分配、名称、tick 逐行）

queue.md
  → 阻塞如何同时挂事件表 + 延时表
  → 超时与事件两条唤醒路径

timers.md / event_groups.md / stream_buffer.md
  → 三种不同的“等待/唤醒”实现
  → 事件组的无序表例外
  → 流缓冲区的通知状态复用

port_cortex_m4f.md
  → PendSV/SVC/SysTick 如何与上述 C 代码衔接
```

---

## 7. 一句话总纲

FreeRTOS 的任务管理可以压缩为四个动作：

1. **配置决定编译分支**：`config*` 宏先决定哪些代码存在；
2. **链表表示状态**：TCB 的两个链表项在就绪/延时/事件/挂起表之间移动；
3. **tick 推进时间**：`xTaskIncrementTick()` 处理到期，置 `xSwitchRequired`；
4. **PendSV 完成切换**：`vTaskSwitchContext()` 选任务，汇编保存/恢复寄存器。

读懂这四步，再回头看任何一条注释，都能定位到它属于哪一步、依赖哪个不变量。
