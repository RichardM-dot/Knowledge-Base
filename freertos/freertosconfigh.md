# FreeRTOSConfig.h 模板配置说明

> 对应文件：`freertos/examples/template_configuration/FreeRTOSConfig.h`

---

## 一、文件大致介绍与重点注意事项

### 1. 文件大致介绍

这个文件是一个通用示例版 `FreeRTOSConfig.h`，用于配置 FreeRTOS 内核的硬件时钟、调度行为、内存分配、中断优先级、可选功能、MPU/TrustZone/FPU/MVE、SMP、钩子函数和 API 裁剪等。

文件注释较全，但它不是针对某个具体 MCU 的最终配置。实际项目应优先使用所用 FreeRTOS port 自带的 `FreeRTOSConfig.h`，再按板级硬件修改。

主要配置块包括：

#### 1.1 硬件描述

- `configCPU_CLOCK_HZ`：内核 tick 中断所用外设时钟，默认 20MHz，是 QEMU 示例值。
- `configSYSTICK_CLOCK_HZ`：Cortex-M 可选，SysTick 与内核不同频时才需要。

#### 1.2 调度行为

- `configTICK_RATE_HZ = 100`：tick 频率 100Hz。
- `configUSE_PREEMPTION = 1`：抢占式调度。
- `configUSE_TIME_SLICING = 0`：不在每个 tick 对同优先级任务做时间片轮转。
- `configMAX_PRIORITIES = 5`：优先级 0 到 4。
- `configMINIMAL_STACK_SIZE = 128`：单位是字，不是字节。
- `configTICK_TYPE_WIDTH_IN_BITS`：TickType_t 为 32 位。

#### 1.3 软件定时器、事件组、流缓冲区

- `configUSE_TIMERS = 1`，需要包含 `timers.c`。
- `configUSE_EVENT_GROUPS = 1`，需要包含 `event_groups.c`。
- `configUSE_STREAM_BUFFERS = 1`，需要包含 `stream_buffer.c`。

#### 1.4 内存分配

- 同时支持静态和动态分配。
- `configTOTAL_HEAP_SIZE = 4096`：默认堆只有 4KB，实际项目通常需要按任务、队列、定时器等重新估算。
- `configHEAP_CLEAR_MEMORY_ON_FREE = 1`：释放时清零，增加开销。
- 未使用独立栈堆和 heap protector。

#### 1.5 中断优先级

```c
configKERNEL_INTERRUPT_PRIORITY = 0
configMAX_SYSCALL_INTERRUPT_PRIORITY = 0
configMAX_API_CALL_INTERRUPT_PRIORITY = 0
```

这几项是最容易出错的地方。

在 Cortex-M 上，中断优先级数值越低优先级越高，而且很多 port 要求按优先级位数左移。这里的 `0` 很可能是占位或示例值，真实硬件上通常不能直接照搬。

#### 1.6 钩子和检查

- 空闲、tick、malloc 失败、daemon 启动钩子都关闭。
- `configCHECK_FOR_STACK_OVERFLOW = 2`：启用较完整的栈溢出检查，需要应用提供 `vApplicationStackOverflowHook()`。
- `configASSERT()` 已定义：断言失败会关中断并死循环。

#### 1.7 MPU、TrustZone、FPU、MVE

```c
configENABLE_MPU = 1
configENABLE_TRUSTZONE = 1
configRUN_FREERTOS_SECURE_ONLY = 1
configENABLE_FPU = 1
configENABLE_MVE = 1
```

这些必须和目标芯片、FreeRTOS port、启动配置严格匹配，不能随便开。

#### 1.8 SMP

- `configNUMBER_OF_CORES` 被注释掉，默认单核。
- 其余 SMP 相关配置存在，但单核下一般不生效。

#### 1.9 功能裁剪和 API 包含

- 任务通知、互斥量、递归互斥量、计数信号量开启。
- 队列集、应用任务标签、POSIX errno 关闭。
- 部分 API 通过 `INCLUDE_*` 宏裁剪。

---

### 2. 重点注意事项

#### 2.1 这是示例文件，不能直接当板级配置用

`configCPU_CLOCK_HZ = 20000000` 是 QEMU 演示默认值。实际项目必须改成驱动 tick 中断的外设时钟频率。否则 `configTICK_RATE_HZ` 对应的实际 tick 周期会错误。

#### 2.2 Cortex-M 中断优先级配置风险最高

`configKERNEL_INTERRUPT_PRIORITY = 0` 和 `configMAX_SYSCALL_INTERRUPT_PRIORITY = 0` 在真实 Cortex-M 项目中通常需要按 port 要求重新设置。

尤其 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 决定哪些中断可以调用 FreeRTOS API。设置错误会导致：

- 在过高优先级中断里调用 FreeRTOS API，破坏内核临界区；
- 或者所有中断都被错误屏蔽或断言失败；
- 优先级数值未按 port 要求移位。

必须参考所用 port 的 `portmacro.h`、启动文件和官方 Cortex-M 说明。

#### 2.3 堆和栈大小很可能不够

`configTOTAL_HEAP_SIZE = 4096` 只有 4KB，稍微多建几个任务、队列、定时器就可能不够。

`configMINIMAL_STACK_SIZE = 128` 和 `configTIMER_TASK_STACK_DEPTH = configMINIMAL_STACK_SIZE` 的单位都是字，不是字节。需要根据 MCU 位宽、任务局部变量、浮点或 `printf` 使用情况重新估算。

#### 2.4 MPU、TrustZone、FPU、MVE 不能盲目开启

当前文件同时开了：

```c
configENABLE_MPU = 1
configENABLE_TRUSTZONE = 1
configRUN_FREERTOS_SECURE_ONLY = 1
configENABLE_FPU = 1
configENABLE_MVE = 1
```

这些配置彼此有关联，也依赖芯片和 port。

例如注释明确说：如果不用 TrustZone 但硬件不能关，应该：

```c
configENABLE_TRUSTZONE = 0
configRUN_FREERTOS_SECURE_ONLY = 1
```

而当前两者都为 1。这个组合在官方 ARMv8-M port 中无法编译，port 源码里有编译期检查：

```c
#if ( ( configRUN_FREERTOS_SECURE_ONLY == 1 ) && ( configENABLE_TRUSTZONE == 1 ) )
    #error TrustZone needs to be disabled in order to run FreeRTOS on the Secure Side.
#endif
```

合法组合只有三种：安全侧运行（`SECURE_ONLY=1` 且 `TRUSTZONE=0`）、非安全侧并支持调用安全侧函数（`0` + `1`）、非安全侧且不支持安全侧调用（`0` + `0`）。当前模板的 `1` + `1` 属于模板自带的示例陷阱。

`configENABLE_MVE` 只适用于 Cortex-M52、Cortex-M55、Cortex-M85、STAR-MC3；M23、M33、M35P 必须未定义或为 0。

MPU v2 wrapper 还要求 `configPROTECTED_KERNEL_OBJECT_POOL_SIZE`、`configSYSTEM_CALL_STACK_SIZE` 等配置与应用程序匹配。

#### 2.5 软件定时器和钩子会引入额外依赖

- `configUSE_TIMERS = 1` 需要包含 `timers.c`，并确保定时器任务栈和队列长度足够。
- `configCHECK_FOR_STACK_OVERFLOW = 2` 需要提供 `vApplicationStackOverflowHook()`，否则链接可能失败。
- `configASSERT()` 当前会关中断死循环，量产或调试策略要明确。

#### 2.6 API 裁剪要与应用代码一致

`INCLUDE_*` 宏为 0 的 API 不能调用。例如 `uxTaskGetStackHighWaterMark`、`xTaskGetHandle`、`xTaskAbortDelay` 等当前被裁剪，如果应用使用会编译或链接失败。

#### 2.7 构建时必须包含对应源文件

- `configUSE_TIMERS = 1` 需要 `timers.c`。
- `configUSE_EVENT_GROUPS = 1` 需要 `event_groups.c`。
- `configUSE_STREAM_BUFFERS = 1` 需要 `stream_buffer.c`。
- 静态分配开启时还需要 static allocation 相关实现，或应用提供空闲任务和定时器任务内存。

### 总结

这个文件适合作为理解 FreeRTOS 配置项的参考模板，但不能直接用于真实硬件。最需要优先核对的是 CPU 时钟、tick 频率、中断优先级、堆栈大小、MPU/TrustZone/FPU/MVE 开关，以及实际纳入构建的源文件和钩子函数。

---

## 二、配置项的详细介绍与主要功能

以下行号从第 1 行 `/*` 开始计数。只列 FreeRTOS 配置宏；头文件保护宏 `FREERTOS_CONFIG_H` 在第 43-44 行，不属于功能配置项，未列入下表。注释中的 `#define` 标注为“注释示例，未生效”。

| 行号 | 配置项 | 当前值/状态 | 详细介绍与主要功能 |
|---:|---|---|---|
| 56 | `configCPU_CLOCK_HZ` | `((unsigned long)20000000)` | 设置驱动内核 tick 中断的外设时钟频率；默认 20MHz 是 QEMU 示例值，实际硬件必须改。 |
| 71 | `configSYSTICK_CLOCK_HZ` | `[Platform specific]` | 注释示例，未生效；Cortex-M 可选，SysTick 与内核不同频时设置 SysTick 时钟频率。 |
| 80 | `configTICK_RATE_HZ` | `100` | 设置 tick 中断频率为 100Hz，决定系统节拍和时间片基本单位。 |
| 85 | `configUSE_PREEMPTION` | `1` | 1 使用抢占式调度；0 使用协作式调度。 |
| 92 | `configUSE_TIME_SLICING` | `0` | 1 允许同优先级就绪任务在每个 tick 轮转；0 不因 tick 在同等优先级任务间切换。 |
| 100 | `configUSE_PORT_OPTIMISED_TASK_SELECTION` | `0` | 1 使用目标硬件优化算法选择下一任务；0 使用通用 C 算法。 |
| 107 | `configUSE_TICKLESS_IDLE` | `0` | 1 启用低功耗无 tick 模式；0 保持 tick 中断始终运行。 |
| 112 | `configMAX_PRIORITIES` | `5` | 可用任务优先级数量，任务优先级范围 0 到 4，0 最低。 |
| 118 | `configMINIMAL_STACK_SIZE` | `128` | 空闲任务栈大小，单位是字不是字节；也常被演示程序复用。 |
| 122 | `configMAX_TASK_NAME_LEN` | `16` | 任务名称最大字符数，包含 NULL 终止符。 |
| 139 | `configTICK_TYPE_WIDTH_IN_BITS` | `TICK_TYPE_WIDTH_32_BITS` | 设置 TickType_t 位宽；当前为无符号 32 位。 |
| 145 | `configIDLE_SHOULD_YIELD` | `1` | 1 时空闲任务会让给同优先级的可运行应用任务；0 时空闲任务用完整个时间片。 |
| 151 | `configTASK_NOTIFICATION_ARRAY_ENTRIES` | `1` | 每个任务通知数组的索引数量；当前每任务 1 个通知槽。 |
| 156 | `configQUEUE_REGISTRY_SIZE` | `0` | 队列注册表可引用的队列/信号量最大数量；仅内核感知调试器需要。 |
| 161 | `configENABLE_BACKWARD_COMPATIBILITY` | `0` | 1 将旧版 FreeRTOS 函数名/类型映射到新名称；当前关闭。 |
| 168 | `configNUM_THREAD_LOCAL_STORAGE_POINTERS` | `0` | 每任务线程局部存储指针数组的索引数量；当前不提供 TLS 指针。 |
| 175 | `configUSE_MINI_LIST_ITEM` | `1` | 1 使用更小的 MiniListItem_t 节省 RAM，但可能违反严格别名规则。 |
| 181 | `configSTACK_DEPTH_TYPE` | `size_t` | xTaskCreate() 栈深度参数及相关栈信息返回所使用的类型。 |
| 188 | `configMESSAGE_BUFFER_LENGTH_TYPE` | `size_t` | 消息缓冲区中存储消息长度字段所使用的类型。 |
| 193 | `configHEAP_CLEAR_MEMORY_ON_FREE` | `1` | 1 时 vPortFree() 释放内存块后将其清零；增加开销但提高安全性。 |
| 200 | `configSTATS_BUFFER_MAX_LENGTH` | `0xFFFF` | vTaskList/vTaskGetRunTimeStats 假定的缓冲区最大长度；新 API 建议显式传长度。 |
| 212 | `configUSE_NEWLIB_REENTRANT` | `0` | 1 为每个任务分配 newlib 可重入结构；需用户提供 stubs 和锁。 |
| 223 | `configUSE_TIMERS` | `1` | 1 在构建中包含软件定时器功能，需要包含 `timers.c`。 |
| 230 | `configTIMER_TASK_PRIORITY` | `(configMAX_PRIORITIES - 1)` | 软件定时器服务任务的优先级。 |
| 237 | `configTIMER_TASK_STACK_DEPTH` | `configMINIMAL_STACK_SIZE` | 软件定时器任务栈大小，单位是字。 |
| 243 | `configTIMER_QUEUE_LENGTH` | `10` | 发送给定时器服务任务的命令队列长度。 |
| 254 | `configUSE_EVENT_GROUPS` | `1` | 1 在构建中包含事件组功能，需要包含 `event_groups.c`。 |
| 265 | `configUSE_STREAM_BUFFERS` | `1` | 1 在构建中包含流缓冲区/消息缓冲区功能，需要包含 `stream_buffer.c`。 |
| 276 | `configSUPPORT_STATIC_ALLOCATION` | `1` | 1 允许使用静态分配内存创建任务、队列等内核对象。 |
| 284 | `configSUPPORT_DYNAMIC_ALLOCATION` | `1` | 1 允许使用动态分配内存创建任务、队列等内核对象。 |
| 290 | `configTOTAL_HEAP_SIZE` | `4096` | heap_1/2/4 使用的 FreeRTOS 堆总字节数；4KB 通常需按应用扩大。 |
| 295 | `configAPPLICATION_ALLOCATED_HEAP` | `0` | 0 由链接器分配堆数组；1 由应用程序提供堆数组。 |
| 303 | `configSTACK_ALLOCATION_FROM_SEPARATE_HEAP` | `0` | 1 任务栈从独立堆分配；需实现 `pvPortMallocStack()` 和 `vPortFreeStack()`。 |
| 308 | `configENABLE_HEAP_PROTECTOR` | `0` | 1 在 heap_4/heap_5 中启用内部堆块指针边界检查和混淆。 |
| 318 | `configKERNEL_INTERRUPT_PRIORITY` | `0` | tick 和上下文切换中断的优先级；Cortex-M 上通常需按 port 要求移位设置。 |
| 326 | `configMAX_SYSCALL_INTERRUPT_PRIORITY` | `0` | 可调用 FreeRTOS API 的中断最高优先级阈值；Cortex-M 上最关键、最易配错的项之一。 |
| 330 | `configMAX_API_CALL_INTERRUPT_PRIORITY` | `0` | `configMAX_SYSCALL_INTERRUPT_PRIORITY` 的别名，具体用哪个取决于 port。 |
| 340 | `configUSE_IDLE_HOOK` | `0` | 1 启用空闲任务钩子，需要应用提供 `vApplicationIdleHook()`。 |
| 341 | `configUSE_TICK_HOOK` | `0` | 1 启用 tick 钩子，需要应用提供 `vApplicationTickHook()`。 |
| 342 | `configUSE_MALLOC_FAILED_HOOK` | `0` | 1 启用内存分配失败钩子，需要提供 `vApplicationMallocFailedHook()`。 |
| 343 | `configUSE_DAEMON_TASK_STARTUP_HOOK` | `0` | 1 启用守护任务启动钩子。 |
| 351 | `configUSE_SB_COMPLETED_CALLBACK` | `0` | 1 为流缓冲区/消息缓冲区实例启用发送和接收完成回调。 |
| 365 | `configCHECK_FOR_STACK_OVERFLOW` | `2` | 上下文切换时检查栈溢出；2 检查栈末尾模式是否被覆盖，需提供溢出回调。 |
| 376 | `configGENERATE_RUN_TIME_STATS` | `0` | 1 收集每个任务的处理时间统计；需要应用提供时钟源。 |
| 382 | `configUSE_TRACE_FACILITY` | `0` | 1 在任务结构中包含跟踪和可视化工具使用的附加字段。 |
| 389 | `configUSE_STATS_FORMATTING_FUNCTIONS` | `0` | 1 包含 `vTaskList()`/`vTaskGetRunTimeStats()`，会引入字符串格式化依赖。 |
| 399 | `configUSE_CO_ROUTINES` | `0` | 1 包含协程功能，需要包含 `croutine.c`。 |
| 404 | `configMAX_CO_ROUTINE_PRIORITIES` | `1` | 应用程序协程可用的优先级数量。 |
| 420 | `configASSERT(x)` | 多行定义 420-426 | 断言宏；x 为 0 时关中断并死循环，用于调试时捕获错误。 |
| 439 | `configINCLUDE_APPLICATION_DEFINED_PRIVILEGED_FUNCTIONS` | `0` | 1 允许应用提供在特权模式执行的函数；仅 Cortex-M MPU port 使用。 |
| 445 | `configTOTAL_MPU_REGIONS` | `8` | 目标硬件实现的 MPU 区域数量，通常 8 或 16；仅 MPU port 使用。 |
| 452 | `configTEX_S_C_B_FLASH` | `0x07UL` | 覆盖 Flash MPU 区域的 TEX/S/C/B 属性默认值。 |
| 459 | `configTEX_S_C_B_SRAM` | `0x07UL` | 覆盖 RAM MPU 区域的 TEX/S/C/B 属性默认值。 |
| 466 | `configENFORCE_SYSTEM_CALLS_FROM_KERNEL_ONLY` | `1` | 1 只允许内核（系统调用）发起提权，更安全，port 推荐为 1；0 允许应用代码提权。注意模板文件里的注释把 0/1 的作用写反了，以 port 源码为准。 |
| 473 | `configALLOW_UNPRIVILEGED_CRITICAL_SECTIONS` | `0` | 1 允许非特权任务进入临界区并屏蔽中断；0 禁止。 |
| 479 | `configUSE_MPU_WRAPPERS_V1` | `0` | 0 使用新版 v2 MPU 包装器；1 使用旧版 v1 包装器。 |
| 487 | `configPROTECTED_KERNEL_OBJECT_POOL_SIZE` | `10` | v2 MPU 包装器下，应用可同时存在的内核对象总数上限。 |
| 495 | `configSYSTEM_CALL_STACK_SIZE` | `128` | v2 MPU 包装器下，每个任务系统调用栈的大小，单位字。 |
| 503 | `configENABLE_ACCESS_CONTROL_LIST` | `1` | 1 启用访问控制列表 ACL；非特权任务默认不能访问其他内核对象。 |
| 513 | `configNUMBER_OF_CORES` | `[Num of available cores]` | 注释示例，未生效；设置可用处理器核心数，默认 1。 |
| 523 | `configRUN_MULTIPLE_PRIORITIES` | `0` | SMP 下 0 只允许不同优先级任务并行，保持高优先级任务优先；1 允许多优先级同时运行。 |
| 531 | `configUSE_CORE_AFFINITY` | `0` | 1 启用核心亲和性功能，可用 `vTaskCoreAffinitySet/Get` API。 |
| 541 | `configTASK_DEFAULT_CORE_AFFINITY` | `tskNO_AFFINITY` | 未指定亲和掩码任务的默认核心亲和性。 |
| 547 | `configUSE_TASK_PREEMPTION_DISABLE` | `0` | 1 允许使用 `vTaskPreemptionDisable/Enable` 单独控制任务抢占。 |
| 553 | `configUSE_PASSIVE_IDLE_HOOK` | `0` | 1 启用 SMP 被动空闲任务钩子，用较少开销添加后台功能。 |
| 559 | `configTIMER_SERVICE_TASK_CORE_AFFINITY` | `tskNO_AFFINITY` | 设置 RTOS 守护/定时器服务任务的核心亲和性。 |
| 566 | `configIDLE_AFFINITY` | `0` | 1 将每个空闲任务固定到对应核心；0 允许空闲任务跑在任意核心。 |
| 575 | `secureconfigMAX_SECURE_CONTEXTS` | `5` | ARMv8-M 安全侧 port 中可调用安全侧的最大任务数；其他 port 不用。 |
| 583 | `configKERNEL_PROVIDED_STATIC_MEMORY` | `1` | 1 内核提供空闲任务和定时器任务的静态内存实现；0 由应用提供。 |
| 593 | `configENABLE_TRUSTZONE` | `1` | 非安全侧运行 FreeRTOS 时启用 ARMv8-M TrustZone 支持。 |
| 600 | `configRUN_FREERTOS_SECURE_ONLY` | `1` | 让整个 FreeRTOS 在安全侧运行；不用 TrustZone 但硬件不能关时使用。 |
| 604 | `configENABLE_MPU` | `1` | 1 启用内存保护单元 MPU；0 禁用。 |
| 608 | `configENABLE_FPU` | `1` | 1 启用浮点单元 FPU；0 禁用。 |
| 616 | `configENABLE_MVE` | `1` | 1 启用 M-Profile 向量扩展 MVE；仅 M52/M55/M85/STAR-MC3，M23/M33/M35P 必须未定义或 0。 |
| 637 | `configCHECK_HANDLER_INSTALLATION` | `1` | 1 增加断言检查 SVC/PendSV 中断处理程序是否正确安装；间接路由时须设 0。 |
| 645 | `configUSE_TASK_NOTIFICATIONS` | `1` | 1 包含任务通知功能。 |
| 646 | `configUSE_MUTEXES` | `1` | 1 包含互斥量功能。 |
| 647 | `configUSE_RECURSIVE_MUTEXES` | `1` | 1 包含递归互斥量功能。 |
| 648 | `configUSE_COUNTING_SEMAPHORES` | `1` | 1 包含计数信号量功能。 |
| 649 | `configUSE_QUEUE_SETS` | `0` | 1 包含队列集功能；当前关闭。 |
| 650 | `configUSE_APPLICATION_TASK_TAG` | `0` | 1 包含应用任务标签功能；当前关闭。 |
| 654 | `configUSE_POSIX_ERRNO` | `0` | 1 启用每任务 `FreeRTOS_errno` 变量，保存该任务最近错误。 |
| 659 | `INCLUDE_vTaskPrioritySet` | `1` | 包含 `vTaskPrioritySet()`，运行中修改任务优先级。 |
| 660 | `INCLUDE_uxTaskPriorityGet` | `1` | 包含 `uxTaskPriorityGet()`，读取任务优先级。 |
| 661 | `INCLUDE_vTaskDelete` | `1` | 包含 `vTaskDelete()`，删除任务。 |
| 662 | `INCLUDE_vTaskSuspend` | `1` | 包含 `vTaskSuspend()`/`vTaskResume()` 等挂起和恢复任务 API。 |
| 663 | `INCLUDE_xTaskDelayUntil` | `1` | 包含 `xTaskDelayUntil()`，实现绝对周期延时。 |
| 664 | `INCLUDE_vTaskDelay` | `1` | 包含 `vTaskDelay()`，实现相对延时。 |
| 665 | `INCLUDE_xTaskGetSchedulerState` | `1` | 包含 `xTaskGetSchedulerState()`，查询调度器运行状态。 |
| 666 | `INCLUDE_xTaskGetCurrentTaskHandle` | `1` | 包含 `xTaskGetCurrentTaskHandle()`，获取当前任务句柄。 |
| 667 | `INCLUDE_uxTaskGetStackHighWaterMark` | `0` | 包含栈高水位查询 API；当前关闭。 |
| 668 | `INCLUDE_xTaskGetIdleTaskHandle` | `0` | 包含获取空闲任务句柄 API；当前关闭。 |
| 669 | `INCLUDE_eTaskGetState` | `0` | 包含 `eTaskGetState()` 查询任务状态 API；当前关闭。 |
| 670 | `INCLUDE_xTimerPendFunctionCall` | `0` | 包含 `xTimerPendFunctionCall()`，在定时器守护任务中执行函数；当前关闭。 |
| 671 | `INCLUDE_xTaskAbortDelay` | `0` | 包含 `xTaskAbortDelay()`，中止任务延时；当前关闭。 |
| 672 | `INCLUDE_xTaskGetHandle` | `0` | 包含 `xTaskGetHandle()`，按名称获取任务句柄；当前关闭。 |
| 673 | `INCLUDE_xTaskResumeFromISR` | `1` | 包含 `xTaskResumeFromISR()`，在中断中恢复任务。 |

---

## 三、在不同硬件平台下如何使用

以下按 **ESP32、STM32、树莓派 Pico（RP2040）** 三个平台分别说明 `FreeRTOSConfig.h` 的关键差异与配置要点。三个平台均不建议直接使用通用示例文件，应优先使用各自 SDK / 生态自带的 FreeRTOS 配置模板。

---

### 1. ESP32（ESP-IDF / Xtensa 双核）

#### 1.1 基本情况

ESP32 使用 Xtensa 架构，默认双核，FreeRTOS 以 SMP 方式运行。ESP-IDF 已经深度封装了 FreeRTOS 配置，很多宏不是在项目里的 `FreeRTOSConfig.h` 中手工定义，而是通过 `menuconfig` 或 `sdkconfig` 生成。

项目中的 `FreeRTOSConfig.h` 通常是 IDF 提供的版本，不应从通用模板复制替换。

#### 1.2 需要重点关注的配置项

##### 1.2.1 `configCPU_CLOCK_HZ`

ESP32 的 FreeRTOS 移植层并不像 Cortex-M 那样直接用这个宏来算 SysTick 重装载值，tick 由 ESP-IDF 的系统定时器或硬件定时器驱动。

但在 IDF 的 `FreeRTOSConfig.h` 中仍会定义该宏用于 port 层参考，实际值应跟随 ESP32 的 CPU 频率（常见 160MHz 或 240MHz）。不要保留通用模板的 `20000000`。

##### 1.2.2 `configTICK_RATE_HZ`

ESP-IDF 默认通常为 100Hz，可通过 `CONFIG_FREERTOS_HZ` 配置，范围 1–1000。Arduino-ESP32 框架下默认常为 1000Hz。

如果使用 ESP-IDF 原生开发，保持 `menuconfig` 中的默认值即可，不要直接在头文件里硬改，否则可能和已编译的 IDF 库不一致。

##### 1.2.3 `configMAX_SYSCALL_INTERRUPT_PRIORITY`（最关键的差异）

ESP32 的中断优先级为 1（最低）到 7（最高）。ESP-IDF 中 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 为 3：只有 1–3 级（低/中优先级）的中断可以调用 FreeRTOS API。

优先级为 4–7 级（高于阈值 3）的中断里调用 `xQueueSendFromISR` 等 FreeRTOS API，会触发断言并导致 Guru Meditation Error / HardFault。

如果应用中有高优先级中断（如 GPIO 中断设为 6 或 7），该 ISR 内不能调用任何 FreeRTOS API。这是 ESP32 上最常见的崩溃原因之一。

ESP-IDF 的 `esp_intr_alloc` 机制会自动处理部分优先级，但手工配置中断时仍需遵守这个边界。

##### 1.2.4 SMP 相关配置

ESP32 双核默认使用 SMP。`configNUMBER_OF_CORES` 在 IDF 中通常为 2。

`configRUN_MULTIPLE_PRIORITIES`、`configUSE_CORE_AFFINITY` 等 SMP 选项由 IDF 的 Kconfig 管理，一般不需要在项目级 `FreeRTOSConfig.h` 中重新定义。

如果手工把 `configNUMBER_OF_CORES` 改成 1，可能和 IDF 预编译库的假设不一致。

##### 1.2.5 堆和内存

ESP32 不使用 `configTOTAL_HEAP_SIZE` 这种单一堆模型。ESP-IDF 使用多堆分配器（DRAM、IRAM 等），`heap_4.c` 的 `configTOTAL_HEAP_SIZE` 在 IDF 中并不作为主要堆大小控制。

不要照搬通用模板的 4096 字节；ESP32 的堆管理由 `esp_heap_caps` 和 IDF 的堆组件负责。

#### 1.3 ESP32 使用建议

使用 ESP-IDF 时，尽量通过 `idf.py menuconfig` 修改 FreeRTOS 相关选项，而不是直接编辑 `FreeRTOSConfig.h`。

特别注意：任何在中断中调用 FreeRTOS API 的 ISR，其中断等级必须不超过 `configMAX_SYSCALL_INTERRUPT_PRIORITY`（ESP-IDF 中为 3，即只能使用 1–3 级）。

如果使用 Arduino-ESP32，`FreeRTOSConfig.h` 已在框架内配置好，一般不需要用户修改；需要调整 tick 频率时优先查找框架提供的宏或 sdkconfig 选项。

---

### 2. STM32（Cortex-M，HAL / CubeMX / 标准外设库）

#### 2.1 基本情况

STM32 是 ARM Cortex-M 内核，`FreeRTOSConfig.h` 的配置方式最接近通用模板，但中断优先级和时钟两项必须按 STM32 的 NVIC 规则改写。

使用 STM32CubeMX 时，`FreeRTOSConfig.h` 由 CubeMX 自动生成，不建议手工大改，应在 CubeMX 的 FreeRTOS 配置界面中调整。

#### 2.2 需要重点关注的配置项

##### 2.2.1 `configCPU_CLOCK_HZ`

STM32 上通常应写成 `SystemCoreClock`，而不是固定数值。

`SystemCoreClock` 在 `SystemClock_Config()` 中根据 RCC 配置被设置为 HCLK（AHB 总线时钟）。例如 STM32F4 跑 168MHz 时，`configCPU_CLOCK_HZ` 应等于 `168000000`。

如果手工写成 `72000000` 但实际 HCLK 是 168MHz，tick 周期会完全错误。

##### 2.2.2 `configTICK_RATE_HZ`

STM32 上常见 1000Hz（1ms 粒度）或 100Hz。CubeMX 默认通常为 1000Hz。

通用模板的 100 可以用，但要根据应用对延时粒度的要求决定。

##### 2.2.3 `configKERNEL_INTERRUPT_PRIORITY` 与 `configMAX_SYSCALL_INTERRUPT_PRIORITY`（最关键）

STM32 的 NVIC 优先级只有高几位有效，且 FreeRTOS 要求写入寄存器的值是**已经移位过的 8 位值**。这是 STM32 上最容易配错的地方。

一个典型的手工移植配置示例是：

```c
configKERNEL_INTERRUPT_PRIORITY = 255
configMAX_SYSCALL_INTERRUPT_PRIORITY = 191
```

这两个值看起来很大，是因为它们已经是“移位后”的形式。如果直接写 0 或 5，很可能触发 FreeRTOS 内部的优先级断言，或者导致中断屏蔽行为异常。

CubeMX 生成的代码通常使用 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`（例如 5）再左移 `(8 - configPRIO_BITS)` 得到最终值。如果手工移植，建议使用这种“库值 + 移位”的写法，而不是硬编码 191 或 255，以免换芯片后出错。

关键规则：任何调用 FreeRTOS API 的中断，其 NVIC 优先级数值必须**大于或等于** `configMAX_SYSCALL_INTERRUPT_PRIORITY` 对应的库值。

在 STM32 中数值越大优先级越低，所以这句话的实际含义是：调用 API 的中断优先级不能比这个阈值更高。

##### 2.2.4 `configPRIO_BITS`

STM32 不同型号的 NVIC 优先级位数不同，常见 4 位（16 级）。

`FreeRTOSConfig.h` 中通常会根据芯片定义 `configPRIO_BITS`。如果这个值不对，`configMAX_SYSCALL_INTERRUPT_PRIORITY` 的移位计算就会全错。

CubeMX 会自动处理，手工移植时必须核对 MCU 参考手册。

##### 2.2.5 堆大小

STM32 上 `configTOTAL_HEAP_SIZE` 是真实生效的。

通用模板的 4096 字节通常偏小，实际项目常设为 10KB–30KB 或更大，根据任务数量、队列深度、是否使用 `printf` 等调整。一个手工移植示例中使用了 `17 * 1024` 字节。

#### 2.3 STM32 使用建议

优先用 CubeMX 生成 `FreeRTOSConfig.h`，然后在 CubeMX 的 FreeRTOS 界面里调整优先级阈值、tick 频率、堆大小。

如果必须手工移植，务必按以下顺序核对：

1. `SystemCoreClock` 是否正确；
2. `configPRIO_BITS` 是否与芯片一致；
3. `configMAX_SYSCALL_INTERRUPT_PRIORITY` 是否按移位规则设置；
4. 所有调用 FreeRTOS API 的 ISR 优先级是否满足约束。

---

### 3. 树莓派 Pico（RP2040，Cortex-M0+ 双核）

#### 3.1 基本情况

RP2040 是双核 Cortex-M0+，FreeRTOS 移植通常使用 Pico SDK 配套的 RP2040 port，或社区维护的 FreeRTOS-RP2040 工程。

它和 STM32 同属 Cortex-M，但 **M0+ 的中断控制器行为和 M3/M4 有重要区别**，不能直接套用 STM32 的 Cortex-M3/M4 配置经验。

#### 3.2 需要重点关注的配置项

##### 3.2.1 `configCPU_CLOCK_HZ`

RP2040 默认系统时钟为 133MHz。在 `FreeRTOSConfig.h` 中通常设置为 `133000000`。

如果超频使用，要同步改成实际频率。不要保留通用模板的 `20000000`。

##### 3.2.2 `configSYSTICK_CLOCK_HZ`（RP2040 的特殊之处）

RP2040 可以选择用固定的 1MHz 时钟驱动 SysTick，而不论系统主频如何。

如果使用这个选项，应将 `configSYSTICK_CLOCK_HZ` 定义为 `1000000`，同时 `configCPU_CLOCK_HZ` 仍设为主频（如 `133000000`）。这样做的好处是 tick 计算不随主频变化而漂移。

是否启用取决于所用 port 的实现；Pico SDK 的 RP2040 port 通常已经处理了这一点，不需要用户在项目级配置中重复定义。

##### 3.2.3 `configMAX_SYSCALL_INTERRUPT_PRIORITY`（与 STM32 的关键差异）

这是 RP2040 上最容易从 STM32 经验误推的地方。

Cortex-M0+ 的 NVIC **不支持像 M3/M4 那样按优先级阈值选择性屏蔽中断**，它只有 PRIMASK（全开/全关）级别的控制。

因此，在一些 RP2040 FreeRTOS port 中，`configMAX_SYSCALL_INTERRUPT_PRIORITY` 要么不被使用，要么语义和 STM32 不同。

一个直接的后果是：不能简单地认为“把中断优先级设得比某个值低就可以安全调用 FreeRTOS API”。

在 RP2040 的 SMP port 中，临界区主要依赖 **spin lock** 来保证双核互斥，而不是像 M3/M4 那样靠 BASEPRI 选择性屏蔽。

如果使用的 port 确实定义了 `configMAX_SYSCALL_INTERRUPT_PRIORITY`，仍需按该 port 的文档设置；但不要直接把 STM32 的 `191/255` 或 `5<<4` 抄过来。没有把握时，保持 port 模板原值，不要手工修改。

##### 3.2.4 `configKERNEL_INTERRUPT_PRIORITY`

在 M0+ 上，这个宏通常也不是像 M3/M4 那样通过 NVIC 优先级寄存器生效。Pico SDK 的 port 会用自己的方式处理 tick 和上下文切换中断。

通用模板的 0 不能直接作为“正确值”的依据。

##### 3.2.5 SMP 与双核

RP2040 是双核，支持 SMP 配置（`configNUMBER_OF_CORES = 2`）。

Pico SDK 的 FreeRTOS port 使用 spin lock 协调双核临界区。如果使用 SMP，`configUSE_CORE_AFFINITY`、`configTICK_CORE` 等选项要按 port 要求配置，不要从单核模板照搬。

##### 3.2.6 堆和栈

RP2040 有 264KB SRAM，`configTOTAL_HEAP_SIZE` 可以比 STM32 的典型值更宽裕，但仍应根据任务和队列实际需求设置。通用模板的 4096 字节偏小。

#### 3.3 Pico 使用建议

优先使用 Pico SDK 配套的 FreeRTOS 示例或 freertos-pi-pico 工程中的 `FreeRTOSConfig.h` 作为基础，而不是通用模板。

核心原则：**不要套用 STM32 Cortex-M3/M4 的 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 移位公式**；RP2040 的 M0+ 内核和 SMP port 对中断/临界区的处理方式不同，保持 port 提供的默认值是最安全的做法。

如果确实需要调整中断行为，应先阅读所用 port 的 `rp2040_config.h` 和 `port.c` 中关于 spin lock 和中断屏蔽的实现。

---

## 四、三个平台的横向对比总结

| 配置项/主题 | ESP32 | STM32 | 树莓派 Pico（RP2040） |
|---|---|---|---|
| `configCPU_CLOCK_HZ` | 跟随 CPU 频率（常见 160/240MHz），由 IDF 管理 | 应写 `SystemCoreClock` | 通常 `133000000` |
| `configTICK_RATE_HZ` | 默认常为 100，可在 menuconfig 调 | 常见 1000 | 常见 100 或 1000，按应用选 |
| 中断优先级阈值 | 用 1–7 等级语义，API 安全阈值为 3（IDF 的 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 为 3），4–7 级 ISR 禁调 API | 必须按 NVIC 移位规则设置（如 191/255 或 library 值左移），调用 API 的 ISR 优先级不能更高 | M0+ 不支持 M3/M4 式的选择性屏蔽，不要照搬 STM32 公式，保持 port 默认值 |
| 堆管理 | 用 IDF 多堆分配，`configTOTAL_HEAP_SIZE` 不是主要控制手段 | 使用传统 FreeRTOS 堆，需要按应用设置足够大的 `configTOTAL_HEAP_SIZE` | 使用传统 FreeRTOS 堆，需要按应用设置足够大的 `configTOTAL_HEAP_SIZE` |
| 配置来源 | 优先用 `menuconfig` / `sdkconfig` | 优先用 CubeMX 生成 | 优先用 Pico SDK 配套 FreeRTOS port 模板 |
| 共同原则 | 不要用通用示例文件直接覆盖平台专用配置 | 不要用通用示例文件直接覆盖平台专用配置 | 不要用通用示例文件直接覆盖平台专用配置 |