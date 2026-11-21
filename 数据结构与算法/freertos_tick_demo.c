/* ============================================================================
 * freertos_tick_demo.c
 * 教学版：保留 FreeRTOS xTaskIncrementTick 的原始写法、变量名和任务处理流程
 * ==========================================================================*/
#include <stdio.h>    /* 标准输入输出：printf / snprintf / fopen 等，本文件用来打印链表状态和 tick 日志 */
#include <stdlib.h>   /* 标准库：calloc / malloc / free / exit 等，本文件用 calloc 分配 TCB_t */
#include <string.h>   /* 字符串与内存：memset / memcpy / strlen 等，本文件给 TCB_t 清零时用 */
#include <stdint.h>   /* 定宽整数：uint32_t / int8_t / uintptr_t 等，本文件用 uint32_t 定义 TickType_t */

/* ============================================================================
 * 1. 类型与配置（名字与 FreeRTOS 保持一致）
 * ==========================================================================*/
typedef uint32_t TickType_t;  // 时钟节拍类型，FreeRTOS 默认 32 位，可在 FreeRTOSConfig.h 中配置为 16 位或 64 位
typedef long BaseType_t;    // 基础类型，FreeRTOS 默认 32 位，可在 FreeRTOSConfig.h 中配置为 16 位
typedef unsigned long UBaseType_t; // 无符号基础类型，FreeRTOS 默认 32 位，可在 FreeRTOSConfig.h 中配置为 16 位


#define pdFALSE ((BaseType_t)0) // 布尔值假
#define pdTRUE  ((BaseType_t)1) // 布尔值真
#define pdPASS  pdTRUE //成功返回值
#define pdFAIL  pdFALSE // 失败返回值
/*1.pd 是 portable define 的缩写，FreeRTOS 的命名约定前缀，专门用来标记"宏定义"（#define 出来的东西）。
 *  它跟具体功能无关，纯粹是编码风格，让读代码的人一眼看出"这是个宏，不是函数或变量"。
 *2.(BaseType_t) 0        // 把整型常量 0 强制转换成 BaseType_t 类型
 *  ((BaseType_t)0)       // 外面再套一对括号，保护宏展开时的优先级
 */


#define configUSE_PREEMPTION 1               // 是否启用抢占式调度，1=启用，0=禁用
#define configNUMBER_OF_CORES 1              // 核心数，FreeRTOS SMP 版本可配置为 2 或更多
#define configUSE_TIME_SLICING 1             // 是否启用时间片轮转，1=启用，0=禁用
#define configUSE_TICK_HOOK 1                // 是否启用 tick 钩子函数，1=启用，0=禁用
#define configUSE_TASK_PREEMPTION_DISABLE 1  // 是否启用任务抢占禁用功能，1=启用，0=禁用
#define configMAX_PRIORITIES 5               // 最大优先级数，FreeRTOS 默认 5，可在 FreeRTOSConfig.h 中配置为更大值


#define portMAX_DELAY     ((TickType_t)0xffffffffUL) // 最大延时值，FreeRTOS 默认 32 位，可在 FreeRTOSConfig.h 中配置为 16 位或 64 位
#define portGET_CORE_ID() ((BaseType_t)0)            // 获取当前核心 ID，FreeRTOS SMP 版本可返回 0、1 或更多
/*0xffffffffUL大小写完全等价*/


/* trace 宏：教学版全部为空 */
#define traceENTER_xTaskIncrementTick()
#define traceTASK_INCREMENT_TICK( x )
#define traceRETURN_xTaskIncrementTick( x )
/*1. trace=追踪；ENTER=进入函数，TASK=任务事件，INCREMENT=递增，TICK=节拍，RETURN=返回；都是调试钩子，空宏=不追踪。
  2. 括号里的 x 是宏形参，不是“打叉”：无 x=0 个参数；有 x=1 个参数，调用时传 xTickCount 或 xSwitchRequired。*/

/* 空宏，教学版不做覆盖率测试 */
#define mtCOVERAGE_TEST_MARKER() do{ } while(0)/* 空宏，教学版不做覆盖率测试 */

/* ============================================================================
 * 2. 链表结构（哨兵 + 环形 + 有序，和 FreeRTOS 一致）
 * ==========================================================================*/
struct xLIST_ITEM;
struct xMINI_LIST_ITEM;
struct xLIST;

typedef struct xLIST_ITEM ListItem_t;         // 链表项类型
typedef struct xMINI_LIST_ITEM MiniListItem_t; // 小链表项类型
typedef struct xLIST List_t;                   // 链表类型

/*链表项：值/前后/宿主/容器*/
struct xLIST_ITEM{
  TickType_t xItemValue; // 链表项值，通常是任务的唤醒时间或优先级
  ListItem_t *pxNext; // 指向下一个链表项的指针
  ListItem_t *pxPrevious; // 指向上一个链表项的指针
  void *pvOwner; // 指向拥有者的指针，通常是任务控制块 TCB_t
  List_t *pxContainer; // 指向包含此链表项
};

/*迷你项：值、前后指针，作哨兵*/
struct xMINI_LIST_ITEM{
  TickType_t xItemValue; // 链表项值，通常是任务的唤醒时间或优先级
  ListItem_t *pxNext; // 指向下一个链表项的指针
  ListItem_t *pxPrevious; // 指向上一个链表项的指针
};

/*链表控制块：项数、索引、尾哨兵*/
struct xLIST{
  UBaseType_t uxNumberOfItems; // 链表项数量，表示链表中实际存储的链表项个数
  ListItem_t *pxIndex; // 链表索引指针，指向当前链表项，用于遍历链表
  MiniListItem_t xListEnd; // 链表尾哨兵项，标记链表结束
};

/* ============================================================================
 * 3. TCB（任务控制块）
 * ==========================================================================*/
typedef struct taskTaskControlBlock TCB_t; // 任务控制块类型

struct taskTaskControlBlock{
  ListItem_t      xStateListItem; // 任务状态链表项，表示任务在就绪、阻塞或挂起状态的链表项
  ListItem_t      xEventListItem; // 任务事件链表项，表示任务在等待事件时的链表项
  UBaseType_t     uxPriority; // 任务优先级，表示任务的优先级等级，数值越大优先级越高
  BaseType_t      xPreemptionDisable; // 任务抢占禁用标志，表示任务是否允许被抢占，1=禁用，0=允许
  const char *    pcTaskName; // 任务名称，指向任务名称字符串的指针，用于调试和日志输出
};
/*FreeRTOS 要能在不支持 C99、没有 <stdbool.h> 的老编译器和各种 8/16/32 位 MCU 上编译，因此不使用bool而是使用BaseType_t*/

/* ============================================================================
 * 4. 链表宏（名字与 FreeRTOS 一致）
 * ==========================================================================*/
#define listGET_OWNER_OF_HEAD_ENTRY( pxList ) \
    ( ( TCB_t * ) ( ( &( ( pxList )->xListEnd ) )->pxNext->pvOwner ) ) // 获取链表头节点的宿主指针，并强制转换为 TCB_t * 类型，表示获取链表中第一个任务控制块

#define listGET_LIST_ITEM_VALUE( pxItem ) \
    ( ( pxItem )->xItemValue ) // 获取链表项的值，表示获取链表项中存储的任务唤醒时间或优先级

#define listLIST_IS_EMPTY( pxList ) \
    ( ( ( pxList )->uxNumberOfItems == ( UBaseType_t ) 0 ) ? pdTRUE : pdFALSE ) // 判断链表是否为空，如果链表项数量为 0，则返回 pdTRUE，否则返回 pdFALSE

#define listCURRENT_LIST_LENGTH( pxList ) \
    ( ( pxList )->uxNumberOfItems ) // 获取链表的当前长度，表示获取链表中实际存储的链表项个数

#define listLIST_ITEM_CONTAINER( pxItem ) \
    ( ( pxItem )->pxContainer ) // 获取链表项所属的链表，表示获取链表项所在的链表控制块指针

#define listREMOVE_ITEM( pxItem ) \
    uxListRemove( pxItem ) // 从链表中移除指定的链表项，表示将链表项从其所属的链表中删除，并更新链表的相关指针和计数
/* 宏 listGET_OWNER_OF_HEAD_ENTRY 语法说明：
 * 普通 C 表达式 + #define 包装，不是特殊语法：
 *   \            行继续符，宏太长换行续写，编译器视为同一行
 *   ( pxList )   每个参数套括号，防止实参是 a+1 时优先级出错
 *   ( ... )      宏体整体套括号，保证展开后作为整体参与外层运算
 *   ( TCB_t * )  强制类型转换，把 void * 转成 TCB_t *
 *
 * 逐层拆类型：
 *   pxList                         List_t *          指向链表控制块
 *   (pxList)->xListEnd             MiniListItem_t    链表尾哨兵（值，不是指针）
 *   &((pxList)->xListEnd)          MiniListItem_t *  取哨兵地址
 *   ...->pxNext                    ListItem_t *      哨兵的 pxNext，指向第一个真实节点
 *   ...->pxNext->pvOwner           void *            第一个节点的宿主指针
 *   (TCB_t *)...                   TCB_t *           强转成任务控制块指针
 *
 * 语义：取哨兵地址 → 走 pxNext 到第一个真实节点 → 取 pvOwner → 强转 TCB_t *
 * 即“拿到链表头节点所属的任务控制块”。
 */


 /* ============================================================================
 * 5. 全局变量（名字与 FreeRTOS 一致）
 * ==========================================================================*/
TickType_t      xTickCount            = 0; // 全局时钟节拍计数器，表示系统自启动以来的时钟节拍总数
TickType_t      xNextTaskUnblockTime  = portMAX_DELAY; // 下一个任务的唤醒时间，表示系统中最早需要唤醒的任务的时钟节拍值，初始值为最大延时值
UBaseType_t     uxSchedulerSuspended  = 0; // 调度器挂起标志，表示调度器是否被挂起，1=挂起，0=运行
TickType_t      xPendedTicks          = 0; // 待处理的时钟节拍数，表示系统中等待处理的时钟节拍数量

static List_t   xDelayedTaskList1; // 延迟任务链表 1，表示存储延迟任务的链表，用于管理处于阻塞状态的任务
static List_t   xDelayedTaskList2; // 延迟任务链表 2，表示存储延迟任务的链表，用于管理处于阻塞状态的任务

List_t *        pxDelayedTaskList         = NULL; // 指向当前延迟任务链表的指针，表示当前正在使用的延迟任务链表
List_t *        pxOverflowDelayedTaskList = NULL; // 指向溢出延迟任务链表的指针，表示当前正在使用的溢出延迟任务链表，用于管理延迟时间超过最大值的任务

List_t          pxReadyTasksLists[ configMAX_PRIORITIES ]; // 就绪任务链表数组，表示存储就绪任务的链表数组，每个优先级对应一个链表，用于管理处于就绪状态的任务

TCB_t *         pxCurrentTCB          = NULL; // 当前运行任务的任务控制块指针，表示当前正在执行的任务的任务控制块
TCB_t *         pxCurrentTCBs[ configNUMBER_OF_CORES ]; // 当前运行任务的任务控制块指针数组，表示每个核心当前正在执行的任务的任务控制块
BaseType_t      xYieldPendings[ configNUMBER_OF_CORES ]; // 每个核心的任务切换挂起标志数组，表示每个核心是否有任务切换请求挂起，1=挂起，0=无挂起

unsigned long   ulTickHookCount       = 0; // tick 钩子函数调用计数器，表示系统自启动以来 tick 钩子函数被调用的总次数

/* ============================================================================
 * 6. 链表函数实现
 * ==========================================================================*/

void vListInitialise( List_t * const pxList ) // 初始化链表控制块
{
    pxList->pxIndex = ( ListItem_t * ) &( pxList->xListEnd );
    pxList->xListEnd.xItemValue = portMAX_DELAY;
    pxList->xListEnd.pxNext = ( ListItem_t * ) &( pxList->xListEnd );
    pxList->xListEnd.pxPrevious = ( ListItem_t * ) &( pxList->xListEnd );
    pxList->uxNumberOfItems = ( UBaseType_t ) 0U;
}

void vListInitialiseItem( ListItem_t * const pxItem ) // 初始化链表项
{
    pxItem->pxContainer = NULL;
    pxItem->pvOwner = NULL;
    pxItem->xItemValue = 0;
    pxItem->pxNext = NULL;
    pxItem->pxPrevious = NULL;
}

/* 按 xItemValue 升序插入 */
void vListInsert( List_t * const pxList, ListItem_t * const pxNewListItem )
{
    ListItem_t * pxIterator;
    const TickType_t xValueOfInsertion = pxNewListItem->xItemValue;

    if( xValueOfInsertion == portMAX_DELAY )
    {
        pxIterator = pxList->xListEnd.pxPrevious;
    }
    else
    {
        for( pxIterator = ( ListItem_t * ) &( pxList->xListEnd );
             pxIterator->pxNext->xItemValue <= xValueOfInsertion;
             pxIterator = pxIterator->pxNext )
        {
        }
    }

    pxNewListItem->pxNext = pxIterator->pxNext;
    pxNewListItem->pxNext->pxPrevious = pxNewListItem;
    pxNewListItem->pxPrevious = pxIterator;
    pxIterator->pxNext = pxNewListItem;
    pxNewListItem->pxContainer = pxList;
    ( pxList->uxNumberOfItems )++;
}

/* 插入到 pxIndex 之前（尾部），用于就绪链表 FIFO */
// 当 pxIndex 指向 xListEnd 时，“插在 xListEnd 前面”就是链表逻辑尾部——这就是注释说“尾部 FIFO”的原因。
void vListInsertEnd( List_t * const pxList, ListItem_t * const pxNewListItem )
{
    ListItem_t * const pxIndex = pxList->pxIndex;

    pxNewListItem->pxNext = pxIndex;
    pxNewListItem->pxPrevious = pxIndex->pxPrevious;
    pxIndex->pxPrevious->pxNext = pxNewListItem;
    pxIndex->pxPrevious = pxNewListItem;
    pxNewListItem->pxContainer = pxList;
    ( pxList->uxNumberOfItems )++;
}

UBaseType_t uxListRemove( ListItem_t * const pxItemToRemove ) // 从链表中移除指定的链表项，并返回链表中剩余的项数
{
    List_t * const pxList = pxItemToRemove->pxContainer;

    pxItemToRemove->pxNext->pxPrevious = pxItemToRemove->pxPrevious;
    pxItemToRemove->pxPrevious->pxNext = pxItemToRemove->pxNext;

    if( pxList->pxIndex == pxItemToRemove )
    {
        pxList->pxIndex = pxItemToRemove->pxPrevious;
    }

    pxItemToRemove->pxContainer = NULL;
    ( pxList->uxNumberOfItems )--;
    return pxList->uxNumberOfItems;
}

/* ============================================================================
 * 7. 内核辅助函数（名字与 FreeRTOS 一致）
 * ==========================================================================*/

/* prvAddTaskToReadyList( pxTCB )
 * 作用：把任务挂入就绪链表。
 * 展开：vListInsertEnd( &pxReadyTasksLists[pxTCB->uxPriority], &pxTCB->xStateListItem )
 *   实参1：按任务优先级选出对应就绪链表（List_t *）
 *   实参2：任务 TCB 内的状态链表项（ListItem_t *），同一时刻只挂一条状态链表
 * 命名：prv=私有宏，AddTask=加任务，ToReadyList=到就绪链表
 * 语义：vListInsertEnd 把新项插到 pxIndex 之前（逻辑尾部），四指针拼接+计数++
 * 为何用宏：热路径零调用开销；参数套括号防宏展开优先级错误
 */
#define prvAddTaskToReadyList( pxTCB ) \
    vListInsertEnd( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ), \
                    &( ( pxTCB )->xStateListItem ) )

static void prvResetNextTaskUnblockTime( void ) // 重置下一个任务唤醒时间
{
    if( listLIST_IS_EMPTY( pxDelayedTaskList ) != pdFALSE )
    {
        xNextTaskUnblockTime = portMAX_DELAY;
    }
    else
    {
        xNextTaskUnblockTime = pxDelayedTaskList->xListEnd.pxNext->xItemValue;
    }
}

void taskSWITCH_DELAYED_LISTS( void ) // 交换延迟链表
{
    List_t * pxTemp;

    pxTemp = pxDelayedTaskList;
    pxDelayedTaskList = pxOverflowDelayedTaskList;
    pxOverflowDelayedTaskList = pxTemp;

    prvResetNextTaskUnblockTime();
}

/* 多核才调用；单核保留以便切换 configNUMBER_OF_CORES 后编译 */
void prvYieldForTask( TCB_t * pxTCB )
{
    BaseType_t xCoreID;

    for( xCoreID = 0; xCoreID < ( BaseType_t ) configNUMBER_OF_CORES; xCoreID++ )
    {
        if( pxTCB->uxPriority > pxCurrentTCBs[ xCoreID ]->uxPriority )
        {
            xYieldPendings[ xCoreID ] = pdTRUE;
        }
    }
}

void prvYieldCore( BaseType_t xCoreID ) // 请求指定核心切换任务
{
    ( void ) xCoreID;
}

void vApplicationTickHook( void ) // tick 钩子函数，用户可在此处添加自定义的定时处理逻辑
{
    ulTickHookCount++;
}

/* ============================================================================
 * 8. xTaskIncrementTick —— 原文逐字保留
 * ==========================================================================*/

BaseType_t xTaskIncrementTick( void )
{
    /* pxTCB：临时指针，用来指向"当前从延时链表里取出来的那个任务"的 TCB。
     * 只在下面"叫醒到期任务"的循环内部使用，循环外无意义。临时借这个指针去
     * 操作该任务的内核链表项、优先级和就绪状态，从而把到期任务叫醒 */
    TCB_t * pxTCB;

    /* xItemValue：临时变量，保存"当前延时链表头任务"的醒来时刻（item 值）。
     * 用来和当前节拍比较，判断它是否已经到期。（节拍由硬件定时器周期性终端驱动，是系统的时基）
       进入tick中断调用 xTaskIncrementTick()把全局变量 xTickCount 加 1 */
    TickType_t xItemValue;

/* xSwitchRequired：本函数唯一的输出。初值 pdFALSE 表示"默认不需要切换"。
 * 只要满足任一条件（高优先级任务被唤醒 / 同优先级时间片轮转 / 其他核请求 yield），
 * 就会被置为 pdTRUE，告诉移植层"中断退出时请触发一次上下文切换"。 
 * 上下文切换：将当前任务的CPU现场保存到它自己的TCB中 ，从另一个任务的栈/TCB中恢复它的CPU现场接着运行
 *           是一次有开销的保存/恢复现场的过程
 * yield：主动退出CPU
 * 同优先级时间片轮转：A、B 两个同优先级任务都就绪时，A 先运行一个时间片；tick 中断到来发现 A 的时间片用完且 B 就绪，
 *                 就标记需要切换，中断退出时保存 A 的 CPU 现场并恢复 B 的现场，让 B 运行一个时间片；B 的时间片用完后再切回 A，
 *                 A 从上次保存的断点继续运行。这样 A、B 轮流使用 CPU，避免同优先级任务饿死；暂停 A 不是终止 A，A 的现场被保存，恢复后不会丢状态。
 *时间片轮转对高优先级任务的实时性没有太大影响，主要是影响同级任务之间的延迟抖动，如果某一个任务对抖动特别敏感应该给其更高的优先级 */
    BaseType_t xSwitchRequired = pdFALSE;

    /* 调试追踪钩子：进入函数时触发。教学版为空宏，零开销。 */
    traceENTER_xTaskIncrementTick();

    /* 调试追踪钩子：节拍递增事件，把当前 tick 值传给钩子做记录。
     * 注意这里传的是"递增前"的 xTickCount。 */
    traceTASK_INCREMENT_TICK( xTickCount );

    /* ====================================================================
     * 外层门控：调度器是否被挂起？
     * uxSchedulerSuspended == 0 表示调度器正常运行，走"快路径"。
     * 否则说明有任务正在修改内核链表（vTaskSuspendAll），此时绝不能动链表，
     * 只能记账，走 else 分支的"慢路径"。
     * ================================================================== */
    if( uxSchedulerSuspended == ( UBaseType_t ) 0U )
    {
        /* 本地常量副本：先算出"递增后的节拍值"。
         * 用 const 局部变量有两个好处：
         *   1. 避免后面反复读全局 xTickCount（编译器可放寄存器）；
         *   2. 保证本代码块内这个值不会被意外改动，判断逻辑稳定。 */
        const TickType_t xConstTickCount = xTickCount + ( TickType_t ) 1;

        /* 真正推进全局节拍计数。从这里开始，系统时间 +1。 */
        xTickCount = xConstTickCount;

        /* 回绕检测：32 位无符号 tick 加到 0 表示溢出回绕（约 49.7 天一次）。
         * 回绕时必须交换"延时链表"和"溢出延时链表"，否则按唤醒时刻排序的
         * 链表会失去有序性（数值上小的唤醒时刻其实在"未来"）。 */
        if( xConstTickCount == ( TickType_t ) 0U )
        {
            /* 交换两条延时链表指针，并重置 xNextTaskUnblockTime。 */
            taskSWITCH_DELAYED_LISTS();
        }
        else
        {
            /* 没回绕，什么都不做。mtCOVERAGE_TEST_MARKER 是覆盖率测试标记，
             * 教学版为空，仅用于统计"这个 else 分支被走到过"。 */
            mtCOVERAGE_TEST_MARKER();
        }

        /* ================================================================
         * 第二道门控：现在的时间是否 >= "下一个该醒任务"的时间？
         * 这是本函数最关键的优化：
         *   - 绝大多数 tick，xNextTaskUnblockTime 在未来，这个 if 不成立，
         *     函数直接跳过整个叫醒循环，开销只有一次比较。
         *   - 只有真正有任务到期的那个 tick，才会进入下面的 for 循环。
         * ============================================================== */
        if( xConstTickCount >= xNextTaskUnblockTime )
        {
            /* 消费式循环：把"本 tick 所有到期的任务"逐个叫醒。
             * 用 for(;;) 而不是普通 for，是因为不知道要叫醒几个，
             * 靠内部两个 break 退出：
             *   - 延时链表空     → break
             *   - 队头还没到点   → break
             * 每次 break 前都会维护 xNextTaskUnblockTime。 */
            for( ; ; )
            {
                /* 出口 A：延时链表为空，说明没有任务在睡。
                 * 把 xNextTaskUnblockTime 设为最大可能值，
                 * 让外层 if( xConstTickCount >= xNextTaskUnblockTime )
                 * 在以后的很长时间内都不成立，直接跳过整个循环。 */
                if( listLIST_IS_EMPTY( pxDelayedTaskList ) != pdFALSE )
                {
                    xNextTaskUnblockTime = portMAX_DELAY;
                    break;
                }
                else
                {
                    /* 链表非空，继续往下取队头。 */
                    mtCOVERAGE_TEST_MARKER();
                }

                /* 取延时链表头节点，并通过 pvOwner 强转回 TCB_t *。
                 * 延时链表按"醒来时刻"升序排列，所以队头就是"最早该醒"的任务。 */
                pxTCB = listGET_OWNER_OF_HEAD_ENTRY( pxDelayedTaskList );

                /* 取该任务状态链表项的 item 值——这个值就是"它该醒来的时刻"。 */
                xItemValue = listGET_LIST_ITEM_VALUE( &( pxTCB->xStateListItem ) );

                /* 出口 B：队头任务还没到点。
                 * 因为链表有序，队头没到，后面更不可能到，直接 break。
                 * 同时把 xNextTaskUnblockTime 更新为这个队头的醒来时刻——
                 * 它就是"下次该进这个循环的时间"，作为下次 tick 的入场券。 */
                if( xConstTickCount < xItemValue )
                {
                    xNextTaskUnblockTime = xItemValue;
                    break;
                }
                else
                {
                    /* 队头已到期，继续往下叫醒它。 */
                    mtCOVERAGE_TEST_MARKER();
                }

                /* -------- 叫醒一个任务的三步 -------- */

                /* 第 1 步：从延时链表（睡着队列）里摘除状态链表项。 */
                listREMOVE_ITEM( &( pxTCB->xStateListItem ) );

                /* 第 2 步：如果该任务还挂在某个事件链表上（比如同时在等队列/信号量），
                 * 也要从事件链表摘除。因为"超时"和"事件到达"是二选一的唤醒源，
                 * 超时先到，就要取消事件等待，避免任务被唤醒两次。 */
                if( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) != NULL )
                {
                    listREMOVE_ITEM( &( pxTCB->xEventListItem ) );
                }
                else
                {
                    /* 该任务没在等事件（纯延时），无需摘事件链表。 */
                    mtCOVERAGE_TEST_MARKER();
                }

                /* 第 3 步：把任务挂到它优先级对应的就绪链表。
                 * 展开为 vListInsertEnd(&pxReadyTasksLists[pxTCB->uxPriority],
                 *                       &pxTCB->xStateListItem)。 */
                prvAddTaskToReadyList( pxTCB );

                /* 抢占判断：只有开启抢占（configUSE_PREEMPTION == 1）才需要。
                 * 关闭抢占时，叫醒任务只让它进就绪队列，不会立即切换。 */
                #if ( configUSE_PREEMPTION == 1 )
                {
                    /* 单核分支。 */
                    #if ( configNUMBER_OF_CORES == 1 )
                    {
                        /* 只有被叫醒任务的优先级"严格高于"当前任务，才立即切换。
                         * 相等不切（留给下面的时间片逻辑处理），低于更不切。
                         * 注意：单核只有一个 CPU，当前任务就是 pxCurrentTCB。 */
                        if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
                        {
                            xSwitchRequired = pdTRUE;
                        }
                        else
                        {
                            /* 优先级不高于当前任务，本 tick 不因它而切换。 */
                            mtCOVERAGE_TEST_MARKER();
                        }
                    }
                    /* 多核分支：不能简单比较，要判断该任务应该跑在哪个核上，
                     * 可能需要在别的核上触发 yield（发核间中断）。 */
                    #else
                    {
                        prvYieldForTask( pxTCB );
                    }
                    #endif
                }
                #endif
                /* 循环继续，处理下一个到期任务，直到链表空或队头未到期。 */
            }
        }

        /* ====================================================================
         * 时间片轮转判断（与上面的"叫醒"完全独立）。
         * 条件：既开启抢占，又开启时间片。
         * 即使本 tick 一个人都没叫醒，只要同优先级就绪任务 > 1，也要切换，
         * 实现"同优先级任务轮流使用 CPU"。
         * ================================================================== */
        #if ( ( configUSE_PREEMPTION == 1 ) && ( configUSE_TIME_SLICING == 1 ) )
        {
            /* 单核分支。 */
            #if ( configNUMBER_OF_CORES == 1 )
            {
                /* 当前任务所在优先级的就绪链表里，任务数 > 1？
                 * 是 → 说明有同优先级兄弟在等，本 tick 让出 CPU，置切换标志。 */
                if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ pxCurrentTCB->uxPriority ] ) ) > 1U )
                {
                    xSwitchRequired = pdTRUE;
                }
                else
                {
                    /* 同优先级只有自己一个，不需要轮转。 */
                    mtCOVERAGE_TEST_MARKER();
                }
            }
            /* 多核分支：每个核单独判断自己的当前任务所在优先级链表长度。 */
            #else
            {
                BaseType_t xCoreID;

                /* 遍历所有核，各自检查是否需要时间片轮转。
                 * 需要则给对应核的 xYieldPendings 置位，
                 * 由后面第九关统一聚合处理。 */
                for( xCoreID = 0; xCoreID < ( ( BaseType_t ) configNUMBER_OF_CORES ); xCoreID++ )
                {
                    if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ pxCurrentTCBs[ xCoreID ]->uxPriority ] ) ) > 1U )
                    {
                        xYieldPendings[ xCoreID ] = pdTRUE;
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }
                }
            }
            #endif
        }
        #endif

        /* ====================================================================
         * Tick 钩子：给应用层一个"每 tick 回调一次"的机会。
         * 关键细节：如果 xPendedTicks > 0，说明当前这些 tick 是
         * xTaskResumeAll() 在补算挂起期间漏掉的节拍，而挂起期间钩子已经调过了，
         * 这里再调就会重复，所以用 xPendedTicks == 0 挡住。
         * ================================================================== */
        #if ( configUSE_TICK_HOOK == 1 )
        {
            if( xPendedTicks == ( TickType_t ) 0 )
            {
                vApplicationTickHook();
            }
            else
            {
                /* 补算期间不重复调用钩子。 */
                mtCOVERAGE_TEST_MARKER();
            }
        }
        #endif

        /* ====================================================================
         * 收尾聚合：把前面各处留下的"yield 请求"汇总到 xSwitchRequired。
         * 单核：只需看 xYieldPendings[0]（单核只有核 0）。
         * 多核：遍历所有核，本核置 xSwitchRequired，其他核发核间中断。
         * ================================================================== */
        #if ( configUSE_PREEMPTION == 1 )
        {
            /* 单核分支：核心 ID 恒为 0。 */
            #if ( configNUMBER_OF_CORES == 1 )
            {
                /* 如果前面某处（时间片轮转等）给核 0 置了 yield 请求，
                 * 就要求中断退出时切换。 */
                if( xYieldPendings[ 0 ] != pdFALSE )
                {
                    xSwitchRequired = pdTRUE;
                }
                else
                {
                    mtCOVERAGE_TEST_MARKER();
                }
            }
            /* 多核分支：需要区分"本核"和"其他核"。 */
            #else
            {
                BaseType_t xCoreID, xCurrentCoreID;

                /* 获取当前执行本函数的核 ID。 */
                xCurrentCoreID = ( BaseType_t ) portGET_CORE_ID();

                /* 遍历所有核，检查各自的 yield 请求。 */
                for( xCoreID = 0; xCoreID < ( BaseType_t ) configNUMBER_OF_CORES; xCoreID++ )
                {
                    /* 若开启了"任务级抢占禁用"，还要检查该核当前任务是否禁用了抢占。
                     * 禁用抢占的任务不能被抢占，跳过。 */
                    #if ( configUSE_TASK_PREEMPTION_DISABLE == 1 )
                        if( pxCurrentTCBs[ xCoreID ]->xPreemptionDisable == pdFALSE )
                    #endif
                    {
                        if( xYieldPendings[ xCoreID ] != pdFALSE )
                        {
                            /* 是本核的请求 → 直接要求本核切换。 */
                            if( xCoreID == xCurrentCoreID )
                            {
                                xSwitchRequired = pdTRUE;
                            }
                            /* 是其他核的请求 → 本核不能替它切，
                             * 给它发核间中断（IPI），让它自己调度。 */
                            else
                            {
                                prvYieldCore( xCoreID );
                            }
                        }
                        else
                        {
                            /* 该核没有 yield 请求。 */
                            mtCOVERAGE_TEST_MARKER();
                        }
                    }
                }
            }
            #endif
        }
        #endif
    }
    /* ========================================================================
     * 慢路径：调度器被挂起。
     * 此时不能动任何内核链表，也不能推进 xTickCount，
     * 只把"欠下的节拍"记在 xPendedTicks 里，
     * 等 xTaskResumeAll() 恢复调度器时统一补算。
     * ====================================================================== */
    else
    {
        /* 欠账 +1。 */
        xPendedTicks += 1U;

        /* 注意：即使调度器挂起，tick 钩子仍然按固定间隔调用，
         * 保证钩子看到的节拍节奏不变。 */
        #if ( configUSE_TICK_HOOK == 1 )
        {
            vApplicationTickHook();
        }
        #endif
    }

    /* 调试追踪钩子：返回前把"是否要切换"传给钩子记录。 */
    traceRETURN_xTaskIncrementTick( xSwitchRequired );

    /* 返回给移植层：pdTRUE 表示中断退出时请触发一次上下文切换（PendSV）。 */
    return xSwitchRequired;
}

/* ============================================================================
 * 9. 测试辅助
 * ==========================================================================*/

void prvInitKernel( void ) // 初始化内核全局变量和链表
{
    int i;

    xTickCount = 0;
    xNextTaskUnblockTime = portMAX_DELAY;
    uxSchedulerSuspended = 0;
    xPendedTicks = 0;
    ulTickHookCount = 0;

    vListInitialise( &xDelayedTaskList1 );
    vListInitialise( &xDelayedTaskList2 );
    pxDelayedTaskList = &xDelayedTaskList1;
    pxOverflowDelayedTaskList = &xDelayedTaskList2;

    for( i = 0; i < configMAX_PRIORITIES; i++ )
    {
        vListInitialise( &pxReadyTasksLists[ i ] );
    }

    for( i = 0; i < configNUMBER_OF_CORES; i++ )
    {
        pxCurrentTCBs[ i ] = NULL;
        xYieldPendings[ i ] = pdFALSE;
    }

    pxCurrentTCB = NULL;
}

TCB_t * prvCreateTask( const char * name, UBaseType_t priority, TickType_t delay ) // 创建一个任务控制块（TCB），并初始化其属性，包括任务名称、优先级和延迟时间
{
    TCB_t * pxTCB = ( TCB_t * ) calloc( 1, sizeof( TCB_t ) );

    pxTCB->pcTaskName = name;
    pxTCB->uxPriority = priority;
    pxTCB->xPreemptionDisable = pdFALSE;

    vListInitialiseItem( &( pxTCB->xStateListItem ) );
    vListInitialiseItem( &( pxTCB->xEventListItem ) );
    pxTCB->xStateListItem.pvOwner = pxTCB;
    pxTCB->xEventListItem.pvOwner = pxTCB;

    if( delay > 0 )
    {
        TickType_t xWakeTime = xTickCount + delay;
        pxTCB->xStateListItem.xItemValue = xWakeTime;

        if( xWakeTime < xTickCount )   /* 溢出 */
        {
            vListInsert( pxOverflowDelayedTaskList, &( pxTCB->xStateListItem ) );
        }
        else
        {
            vListInsert( pxDelayedTaskList, &( pxTCB->xStateListItem ) );
        }

        if( listLIST_IS_EMPTY( pxDelayedTaskList ) == pdFALSE )
        {
            TickType_t xHead = pxDelayedTaskList->xListEnd.pxNext->xItemValue;
            if( xHead < xNextTaskUnblockTime )
            {
                xNextTaskUnblockTime = xHead;
            }
        }
    }

    return pxTCB;
}

void prvPrintList( List_t * pxList, const char * name ) // 打印链表中所有任务的名称，便于调试和观察任务状态
{
    ListItem_t * pxItem = pxList->xListEnd.pxNext;

    printf( "  %s: [", name );
    while( pxItem != ( ListItem_t * ) &( pxList->xListEnd ) )
    {
        TCB_t * pxTCB = ( TCB_t * ) pxItem->pvOwner;
        printf( "%s ", pxTCB->pcTaskName );
        pxItem = pxItem->pxNext;
    }
    printf( "]\n" );
}

void prvPrintReady( void ) // 打印就绪列表中所有任务的名称
{
    int i;
    for( i = 1; i < configMAX_PRIORITIES; i++ )
    {
        char buf[ 32 ];
        snprintf( buf, sizeof( buf ), "ready[%d]", i );
        prvPrintList( &pxReadyTasksLists[ i ], buf );
    }
}

void prvPrintDelayed( void ) // 打印延时列表中所有任务的名称
{
    prvPrintList( pxDelayedTaskList, "delayed     " );
    prvPrintList( pxOverflowDelayedTaskList, "overflow    " );
}

/* ============================================================================
 * 10. 测试主函数
 * ==========================================================================*/

void test_basic_wakeup( void ) // 测试基本唤醒和时间片轮转
{
    int i;

    printf( "\n===== 测试 1：基本唤醒 + 时间片 =====\n" );
    prvInitKernel();

    TCB_t * taskA = prvCreateTask( "A", 2, 3 );
    TCB_t * taskB = prvCreateTask( "B", 1, 5 );
    TCB_t * taskC = prvCreateTask( "C", 3, 2 );

    TCB_t * current = prvCreateTask( "Cur", 1, 0 );
    prvAddTaskToReadyList( current );
    pxCurrentTCB = current;
    pxCurrentTCBs[ 0 ] = current;

    printf( "初始: xTickCount=%u, xNextTaskUnblockTime=%u\n",
            ( unsigned ) xTickCount, ( unsigned ) xNextTaskUnblockTime );
    prvPrintDelayed();
    prvPrintReady();

    for( i = 0; i < 7; i++ )
    {
        BaseType_t xSwitchRequired = xTaskIncrementTick();

        printf( "\ntick %d: xTickCount=%u, next=%u, switch=%d\n",
                i + 1,
                ( unsigned ) xTickCount,
                ( unsigned ) xNextTaskUnblockTime,
                ( int ) xSwitchRequired );
        prvPrintDelayed();
        prvPrintReady();
    }

    ( void ) taskA; ( void ) taskB; ( void ) taskC;
}

void test_wrap_around( void ) // 测试节拍回绕
{
    int i;

    printf( "\n===== 测试 2：节拍回绕 =====\n" );
    prvInitKernel();

    xTickCount = 0xFFFFFFFEUL;

    TCB_t * taskD = prvCreateTask( "D", 2, 3 );   /* wake = 0x00000001，溢出 */

    TCB_t * current = prvCreateTask( "Cur", 1, 0 );
    prvAddTaskToReadyList( current );
    pxCurrentTCB = current;
    pxCurrentTCBs[ 0 ] = current;

    printf( "初始: xTickCount=0x%08X, next=0x%08X\n",
            ( unsigned ) xTickCount, ( unsigned ) xNextTaskUnblockTime );
    prvPrintDelayed();

    for( i = 0; i < 4; i++ )
    {
        BaseType_t xSwitchRequired = xTaskIncrementTick();

        printf( "\ntick %d: xTickCount=0x%08X, next=0x%08X, switch=%d\n",
                i + 1,
                ( unsigned ) xTickCount,
                ( unsigned ) xNextTaskUnblockTime,
                ( int ) xSwitchRequired );
        prvPrintDelayed();
        prvPrintReady();
    }

    ( void ) taskD;
}

void test_time_slicing( void ) // 测试同优先级时间片轮转
{
    int i;

    printf( "\n===== 测试 3：同优先级时间片轮转 =====\n" );
    prvInitKernel();

    TCB_t * current = prvCreateTask( "Cur", 1, 0 );
    TCB_t * peer    = prvCreateTask( "Peer", 1, 0 );

    prvAddTaskToReadyList( current );
    prvAddTaskToReadyList( peer );
    pxCurrentTCB = current;
    pxCurrentTCBs[ 0 ] = current;

    printf( "ready[1] 有 2 个任务，延时链表为空\n" );

    for( i = 0; i < 2; i++ )
    {
        BaseType_t xSwitchRequired = xTaskIncrementTick();

        printf( "tick %d: xTickCount=%u, switch=%d（应为 1）\n",
                i + 1,
                ( unsigned ) xTickCount,
                ( int ) xSwitchRequired );
    }

    ( void ) peer;
}

void test_scheduler_suspended( void ) // 测试调度器挂起期间的 tick 补算
{
    int i;

    printf( "\n===== 测试 4：调度器挂起 + xPendedTicks 补算 =====\n" );
    prvInitKernel();

    TCB_t * current = prvCreateTask( "Cur", 1, 0 );
    prvAddTaskToReadyList( current );
    pxCurrentTCB = current;
    pxCurrentTCBs[ 0 ] = current;

    TCB_t * taskE = prvCreateTask( "E", 3, 2 );

    uxSchedulerSuspended = 1;
    printf( "调度器挂起，调用 3 次 tick...\n" );

    for( i = 0; i < 3; i++ )
    {
        BaseType_t xSwitchRequired = xTaskIncrementTick();
        printf( "  挂起中 tick %d: xTickCount=%u, xPendedTicks=%u, switch=%d\n",
                i + 1,
                ( unsigned ) xTickCount,
                ( unsigned ) xPendedTicks,
                ( int ) xSwitchRequired );
    }

    uxSchedulerSuspended = 0;
    printf( "调度器恢复，开始补算 xPendedTicks=%u...\n",
            ( unsigned ) xPendedTicks );

    while( xPendedTicks > 0 )
    {
        BaseType_t xSwitchRequired = xTaskIncrementTick();
        printf( "  补算 tick: xTickCount=%u, xPendedTicks=%u, switch=%d\n",
                ( unsigned ) xTickCount,
                ( unsigned ) xPendedTicks,
                ( int ) xSwitchRequired );
        xPendedTicks--;
    }

    printf( "最终 ready 状态:\n" );
    prvPrintReady();
    printf( "Tick Hook 调用总次数 = %lu（挂起期间 3 次，补算期间不调用）\n",
            ulTickHookCount );

    ( void ) taskE;
}

int main( void )
{
    test_basic_wakeup();
    test_wrap_around();
    test_time_slicing();
    test_scheduler_suspended();

    return 0;
}


