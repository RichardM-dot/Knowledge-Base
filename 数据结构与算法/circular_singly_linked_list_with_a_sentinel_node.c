// 本文件为单哨兵的双向循环列表，可作为freertos中的list文件的主要逻辑参考

#include <stdio.h>
#include <stdlib.h> // 通用工具库：提供 malloc/free、exit、atoi/strtol、rand/srand、qsort、abs 等通用函数

typedef unsigned long TickType_t;
#define portMax_DELAY ((TickType_t)-1) // 括号防止宏展开时的优先级问题，TickType_t作为 unsigned long的类型别名
                                       // 
                                       // C 标准规定“把整数转换成无符号类型时，按 2^N 取模”，而 -1 mod 2^N = 2^N - 1，
                                       // 所以结果是无符号类型的最大值。编译器在编译期就把这个常量算出来了，
                                       // 通常直接变成全 1 的立即数（写在指令里面的常数）

/*
 * 链表节点 Node 与链表控制块 List 的定义。
 *
 * Node 既用作真实数据节点，也用作 List 内嵌的哨兵节点。
 * List 采用单哨兵循环链表：
 *   - xListEnd 是内嵌的哨兵，不额外分配；
 *   - pxIndex 是遍历游标，指向某个 Node（通常从哨兵开始）；
 *   - count 记录当前链表中真实数据节点的个数，不包含哨兵。
 */

typedef struct Node // 不使用typedef的话每次定义需要多写一个struct
{
    TickType_t xItemValue;
    struct Node *pxPrevious;
    struct Node *pxNext;
    void *pvOwner; // 这里存一个地址，但现在不去规定它指向什么类型，由使用它的代码自己决定。
                   // 任何对象指针都可以隐式转成 void *，反过来从 void * 转回具体类型指针，需要显式强制转换。
} Node;

typedef struct //List 不需要自引用，也没有被其他结构体反向引用，所以标签可以省；但省标签不是必须的，加上也无害。
{
    Node xListEnd;
    Node *pxIndex;  // 一个指针指向结构体
    unsigned count; // 这个链表里当前实际挂了多少个数据节点的计数器
} List;

/********************初始化空链表********************/
void list_init(List *l){
    l->xListEnd.xItemValue = portMax_DELAY; // 哨兵的排序值恒为最大，有序插入时天然排在所有真实节点之后
    l->xListEnd.pxNext     = &l->xListEnd;  // 空表：哨兵的 next 指向自己
    l->xListEnd.pxPrevious = &l->xListEnd;  // 空表：哨兵的 prev 指向自己
    l->pxIndex             = &l->xListEnd;  // 遍历游标初始指向哨兵（空表时没有真实节点可遍历）
    l->count               = 0;             // 当前节点数为0（FreeRTOS 原字段名是 uxNumberOfItems）
}

/********************查询********************/
// 是否为空链表
int list_is_empty(const List *l){
    return l->count==0;
}

// 查看链表逻辑首节点
Node *list_first(const List *l){
    return l->xListEnd.pxNext;
}

// 查看链表逻辑尾节点
Node *list_last(const List *l){
    return l->xListEnd.pxPrevious;
}

/********************纯指针插入********************/
/* 在 pos 前面插入 n：pos 可以是首节点、尾节点、中间节点、哨兵 */
void list_insert_before(Node *pos, Node *n)
{
    n->pxPrevious = pos->pxPrevious;
    n->pxNext     = pos;

    pos->pxPrevious->pxNext = n;
    pos->pxPrevious         = n;
}

/* 在 pos 后面插入 n：pos 同上 */
void list_insert_after(Node *pos, Node *n)
{
    n->pxNext     = pos->pxNext;
    n->pxPrevious = pos;

    pos->pxNext->pxPrevious = n;
    pos->pxNext             = n;
}

/********************删除********************/
/*邻居互指，删除首尾，中间项完全同一段代码*/
void list_unlink(List *l, Node *n){ /*c语言值传递必须使用指针而不是直接定义一个链*/
    /* 如果游标正指向被删节点，先把它挪到前一个节点，避免悬空 */
    if (l->pxIndex == n){
        l->pxIndex = n->pxPrevious;
    }

    n->pxPrevious->pxNext = n->pxNext;
    n->pxNext->pxPrevious = n->pxPrevious;

    /* 可选：置空便于发现悬空指针；FreeRTOS 原版不置空 */
    /* n不在链上了但是n的前后还指着节点，可以去置空*/
    n->pxNext     = NULL;
    n->pxPrevious = NULL;

    l->count--;

}

/********************按 xItemValue 升序插入********************/
/* 从逻辑首节点开始找第一个 xItemValue > n->xItemValue 的节点，插到它前面。走到哨兵就说明 n 最大，插到哨兵前，也就是尾部。
 * 哨兵值 portMax_DELAY 保证循环一定能在哨兵处停下。注意：p 不是新节点，n 才是要插入的新节点 */
void list_insert_sorted(List *l, Node *n){
    Node *p =l->xListEnd.pxNext;
    /*当p 还没绕回哨兵” 并且 p 这个节点的值不比 n 大时指针接着走，回到哨兵的话n插在尾部，没回到哨兵出现了第一个比n大的排序值n插在这个排序值对应节点的前面*/
    while(p != &l->xListEnd && p->xItemValue <= n->xItemValue){
        p = p->pxNext;
    }
    list_insert_before(p, n);
    l->count++;
}

/* 插到逻辑头部 */
void list_insert_head(List *l, Node *n){
    list_insert_before(l->xListEnd.pxNext, n);
    l->count++;
}

/* 插到逻辑尾部：插到哨兵前 */
void insert_list_tial(List *l, Node *n){
    list_insert_after(l->xListEnd.pxPrevious, n);
    l->count++;
}

/* FreeRTOS vListInsertEnd：插到 pxIndex 前。  就绪链表轮转时用这个，让新节点插到“当前游标之前”。 */
void list_insert_end(List *l, Node *n)
{
    list_insert_before(l->pxIndex, n);
    l->count++;
}

/* 删除并返回逻辑首节点，空表返回 NULL */
Node *list_remove_head(List *l){
    if (list_is_empty(l))
    {
        return NULL;
    }

    Node *n = l->xListEnd.pxNext;
    list_unlink(l, n);
    return n; // n没有被free所以可以反回
}

/* ===================== 遍历 ===================== */
void list_traverse(const List *l)
{
    const Node *p = l->xListEnd.pxNext;

    printf("count=%u: ", l->count);
    while (p != &l->xListEnd)
    {
        printf("%lu ", (unsigned long)p->xItemValue); // %lu = unsigned long（无符号长整型）
        p = p->pxNext;
    }
    printf("\n");
}

void list_traverse_reverse(const List *l)
{
    const Node *p = l->xListEnd.pxPrevious;

    printf("reverse: ");
    while (p != &l->xListEnd)
    {
        printf("%lu ", (unsigned long)p->xItemValue);
        p = p->pxPrevious;
    }
    printf("\n");
}

/* ===================== pxIndex 轮转 ===================== */
/* 对应 FreeRTOS 的 listGET_OWNER_OF_NEXT_ENTRY：
 * 游标先往后走一格，如果走到哨兵，再走一格跳过哨兵。
 * 返回节点的 pvOwner。空表返回 NULL。 */
/*list_get_owner_of_next_entry 的参数是 List *l（非 const），因为它要改 l->pxIndex。
调用一次，游标就前进一次。不能把它当“取当前节点 owner”用，它是“取下一个节点的 owner 并前进”*/
void *list_get_owner_of_next_entry(List *l)
{
    if (list_is_empty(l))
    {
        return NULL;
    }

    l->pxIndex = l->pxIndex->pxNext;

    if (l->pxIndex == &l->xListEnd)
    {
        l->pxIndex = l->pxIndex->pxNext;
    }

    return l->pxIndex->pvOwner;
}

/* 返回当前游标所指节点的 pvOwner */
void *list_get_owner_of_index(const List *l)
{
    if (l->pxIndex == &l->xListEnd)
    {
        return NULL;
    }
    return l->pxIndex->pvOwner;
}

/* ===================== 测试 ===================== */
typedef struct
{
    Node node;   /* 必须放第一个成员，方便把 Node* 和 Item* 互转 */
    int  id;
} Item;

int main(void)
{
    List l;
    list_init(&l);

    Item items[5] = {
        { .node = { .xItemValue = 30, .pvOwner = &items[0] }, .id = 0 },
        { .node = { .xItemValue = 10, .pvOwner = &items[1] }, .id = 1 },
        { .node = { .xItemValue = 50, .pvOwner = &items[2] }, .id = 2 },
        { .node = { .xItemValue = 20, .pvOwner = &items[3] }, .id = 3 },
        { .node = { .xItemValue = 40, .pvOwner = &items[4] }, .id = 4 },
    };

    printf("== 空表 ==\n");
    list_traverse(&l);
    printf("is_empty=%d\n\n", list_is_empty(&l));

    printf("== 按值升序插入 30,10,50,20,40 ==\n");
    for (int i = 0; i < 5; i++)
    {
        list_insert_sorted(&l, &items[i].node);
    }
    list_traverse(&l);
    list_traverse_reverse(&l);
    printf("\n");

    printf("== 删除首节点（10） ==\n");
    Node *removed = list_remove_head(&l);
    printf("removed value=%lu\n", (unsigned long)removed->xItemValue);
    list_traverse(&l);
    printf("\n");

    printf("== 删除中间节点（30） ==\n");
    list_unlink(&l, &items[0].node);
    list_traverse(&l);
    printf("\n");

    printf("== 删除尾节点（50） ==\n");
    list_unlink(&l, &items[2].node);
    list_traverse(&l);
    printf("\n");

    printf("== 插到头部（5） ==\n");
    Item head_item = { .node = { .xItemValue = 5, .pvOwner = NULL }, .id = 5 };
    head_item.node.pvOwner = &head_item;
    list_insert_head(&l, &head_item.node);
    list_traverse(&l);
    printf("\n");

    printf("== pxIndex 轮转取 owner ==\n");
    for (int i = 0; i < 6; i++)
    {
        Item *owner = (Item *)list_get_owner_of_next_entry(&l);
        printf("owner id=%d\n", owner ? owner->id : -1);
    }

    printf("\n== 删除全部 ==\n");
    while (!list_is_empty(&l))
    {
        list_remove_head(&l);
    }
    list_traverse(&l);
    printf("is_empty=%d\n", list_is_empty(&l));

    return 0;
}

























/*
 * ============================================================================
 * pvOwner 是做什么的
 * ============================================================================
 *
 * 在 FreeRTOS 的链表节点 ListItem_t 里，pvOwner 的官方注释是：
 *
 *   "Pointer to the object (normally a TCB) that contains the list item."
 *
 * 意思是：
 *   指向“包含这个链表项的对象”，通常是一个 TCB（任务控制块）。
 *
 * 也就是说：
 *   任务控制块 TCB 里内嵌了一个链表节点。
 *   当这个节点被挂到就绪列表、延时列表等链表上时，
 *   pvOwner 就反过来指向那个 TCB。
 *   这样，链表节点和宿主对象之间形成了一个双向链接。
 *
 * 图：
 *
 *   TCB (Task A)
 *   ┌──────────────┐
 *   │ ...          │
 *   │ xStateListItem ──┐   （TCB 里内嵌的 ListItem_t）
 *   │ ...          │   │
 *   └──────────────┘   │
 *                      ▼
 *                ListItem_t
 *                ┌──────────────┐
 *                │ pxNext       │
 *                │ pxPrevious   │
 *                │ pvOwner ─────┼──→ 指回 Task A 的 TCB
 *                └──────────────┘
 *
 * ============================================================================
 * void * 到底是什么
 * ============================================================================
 *
 * void *pvOwner 是一个泛型指针，用来从链表节点反向指回“拥有”这个节点的对象。
 * void * 不是“什么类型都不是”，而是“暂时不指定具体类型”，
 * 等真正使用时再强制转换回正确的类型。
 *
 * void * 是“无类型指针”，也叫通用指针：
 *   - 它只是一个地址，不携带“这个地址上是什么类型”的信息；
 *   - 任何对象指针都可以隐式转成 void *；
 *   - 从 void * 转回具体类型指针时，需要显式强制转换。
 *
 * 所以：
 *
 *   void *pvOwner;
 *
 * 的意思是：
 *   这里存一个地址，但现在不去规定它指向什么类型，
 *   由使用它的代码自己决定。
 *
 * ============================================================================
 * 它在调度里怎么被使用
 * ============================================================================
 *
 * 最典型的场景是任务切换。
 *
 * FreeRTOS 的就绪列表里存放的是链表节点，
 * 但调度器真正需要操作的是 TCB。
 * pvOwner 让“从节点找到 TCB”变成一次指针访问：
 *
 *   pxTCB = (TCB_t *) pxList->pxIndex->pvOwner;
 *
 * 在源码里，listGET_OWNER_OF_NEXT_ENTRY() 这个宏本质上就是：
 *
 *   pxIndex = pxIndex->pxNext;
 *   if (pxIndex == &list->xListEnd) {
 *       pxIndex = pxIndex->pxNext;
 *   }
 *   pxTCB = (TCB_t *) pxIndex->pvOwner;
 *
 * 也就是通过 pxIndex->pvOwner 拿到下一个要运行的任务的 TCB。
 *
 * 所以 pvOwner 的作用可以总结为：
 *   - 让链表节点知道“我是谁的节点”；
 *   - 让调度器从链表节点直接拿到 TCB，不需要额外的查找结构；
 *   - 这是 FreeRTOS 实现 O(1) 任务切换的关键设计之一。
 *
 * ============================================================================
 * 为什么非要用 void *，不用具体类型
 * ============================================================================
 *
 * 因为链表是通用的。
 *
 * FreeRTOS 的链表并不只用来放任务。
 * 延时列表放的是任务，但事件列表、队列等待列表等场景，
 * 节点可能属于不同的宿主对象。
 *
 * 如果 pvOwner 写成：
 *
 *   struct tskTaskControlBlock *pvOwner;
 *
 * 链表代码就被绑定到任务模块上了，无法复用。
 *
 * 如果写成：
 *
 *   void *pvOwner;
 *
 * 链表代码本身就不依赖任何具体类型；
 * 挂任务时它就是 TCB，挂别的对象时它就是别的对象。
 *
 * 所以 void * 是：
 *   把类型推迟到使用者那里再确定的一种手段。
 *   在真正用到的地方，比如调度器里，
 *   会显式转回 TCB_t *。
 *
 * ============================================================================
 * 一句话总结
 * ============================================================================
 *
 * void *pvOwner 是一个反向指针：
 *   链表节点用它指回自己的宿主对象（通常是任务的 TCB）。
 *
 * void * 让链表保持通用，不绑定具体类型；
 * 而“从节点到 TCB”的转换，
 * 正是 FreeRTOS 实现快速任务切换的基础。
 */




/********************纯指针插入********************/
/*
 * 在 pos 前面插入 n。pos 可以是首节点、尾节点、中间节点、哨兵。
 * 不维护 count，由调用者维护。
 *
 * 初始：
 *        prev = pos->pxPrevious                    pos
 *        +-------------+                         +-------------+
 *        |    prev     | ---- pxNext ----------> |     pos     |
 *        |             | <--- pxPrevious ------- |             |
 *        +-------------+                         +-------------+
 *
 * 目标：
 *        prev                  n                  pos
 *        +-------+          +-------+          +-------+
 *        | prev  |--next--> |   n   |--next--> |  pos  |
 *        |       |<-prev--- |       |<-prev--- |       |
 *        +-------+          +-------+          +-------+
 *
 * 四步拆解：
 *
 *   步骤 1：n->pxPrevious = pos->pxPrevious;
 *            （n 的前驱先接旧前驱 prev；旧链 prev<->pos 未动）
 *
 *        prev                    pos
 *        +------+               +------+
 *        | prev | --next------> | pos  |
 *        |      | <--prev------ |      |
 *        +------+               +------+
 *           ^
 *           | n.prev
 *        +------+
 *        |  n   |
 *        +------+
 *
 *   步骤 2：n->pxNext = pos;
 *            （n 的后继接 pos；此时 n 已指好前后，但 prev/pos 还没指回 n）
 *
 *        prev                    pos
 *        +------+               +------+
 *        | prev | --next------> | pos  |   <-- 旧链还在
 *        |      | <--prev------ |      |
 *        +------+               +------+
 *           ^                      ^
 *           | n.prev               | n.next
 *        +-----------------------------+
 *        |              n              |
 *        +-----------------------------+
 *
 *   步骤 3：pos->pxPrevious->pxNext = n;
 *            （此时 pos->pxPrevious 还是旧的 prev，所以等价 prev->next = n）
 *
 *        prev ---next---> n
 *        +------+        +------+
 *        | prev |        |  n   |
 *        +------+        +------+
 *                            \--next--> pos
 *
 *   步骤 4：pos->pxPrevious = n;
 *            （pos 的前驱从 prev 改成 n，接入完成）
 *
 *        prev ---next---> n ---next---> pos
 *        +------+        +------+        +------+
 *        | prev |        |  n   |        | pos  |
 *        +------+        +------+        +------+
 *           ^               ^               ^
 *           |               |               |
 *         prev.prev      n.prev          pos.prev
 *
 * 关键顺序：
 *   第 3 步必须用“旧的 pos->pxPrevious”，所以第 3 步必须在第 4 步之前。
 *   若先执行第 4 步 pos->pxPrevious = n，第 3 步就变成 n->pxNext = n，自环。
 *
 * 为什么不需要边界特判：
 *   pos = 首节点：pos->pxPrevious 是哨兵，步骤 3 自动改 sent->next = n
 *   pos = 尾节点：pos->pxPrevious 是前一个真实节点，正常
 *   pos = 哨兵  ：n 插到尾部，pos->pxPrevious 是旧尾节点，正常
 *   pos = 空表  ：pos->pxPrevious 是哨兵自己，步骤 3 改成 sent->next = n，
 *                 步骤 4 改成 sent->pxPrevious = n，空表插入同一条路径
 *   所有情况 pos->pxPrevious 永远存在，永远可解引用，所以没有 if。
 */

/*
 * 在 pos 后面插入 n。pos 可以是首节点、尾节点、中间节点、哨兵。
 * 不维护 count，由调用者维护。
 *
 * 与 insert_before 完全镜像：before 用 prev，after 用 next。
 *
 * 初始：
 *        pos                    next = pos->pxNext
 *        +-------------+                         +-------------+
 *        |     pos     | ---- pxNext ----------> |    next     |
 *        |             | <--- pxPrevious ------- |             |
 *        +-------------+                         +-------------+
 *
 * 目标：
 *        pos                  n                  next
 *        +-------+          +-------+          +-------+
 *        |  pos  |--next--> |   n   |--next--> | next  |
 *        |       |<-prev--- |       |<-prev--- |       |
 *        +-------+          +-------+          +-------+
 *
 * 四步拆解：
 *
 *   步骤 1：n->pxNext = pos->pxNext;
 *            （n 的后继先接旧后继 next）
 *
 *        pos                    next
 *        +------+               +------+
 *        | pos  | --next------> | next |
 *        |      | <--prev------ |      |
 *        +------+               +------+
 *                                  ^
 *                                  | n.next
 *                               +------+
 *                               |  n   |
 *                               +------+
 *
 *   步骤 2：n->pxPrevious = pos;
 *            （n 的前驱接 pos；旧链 pos<->next 还在）
 *
 *        pos                    next
 *        +------+               +------+
 *        | pos  | --next------> | next |   <-- 旧链还在
 *        |      | <--prev------ |      |
 *        +------+               +------+
 *           ^                      ^
 *           | n.prev               | n.next
 *        +-----------------------------+
 *        |              n              |
 *        +-----------------------------+
 *
 *   步骤 3：pos->pxNext->pxPrevious = n;
 *            （此时 pos->pxNext 还是旧的 next，所以等价 next->prev = n）
 *
 *        pos ---next---> n
 *        +------+        +------+
 *        | pos  |        |  n   |
 *        +------+        +------+
 *           ^                |
 *           +--- n.prev      \--next--> next
 *
 *   步骤 4：pos->pxNext = n;
 *            （pos 的后继从 next 改成 n，接入完成）
 *
 *        pos ---next---> n ---next---> next
 *        +------+        +------+        +------+
 *        | pos  |        |  n   |        | next |
 *        +------+        +------+        +------+
 *           ^               ^               ^
 *           |               |               |
 *         pos.prev       n.prev          next.prev
 *
 * 关键顺序：
 *   第 3 步必须用“旧的 pos->pxNext”，所以第 3 步必须在第 4 步之前。
 *   若先执行第 4 步 pos->pxNext = n，第 3 步就变成 n->pxPrevious = n，自环。
 *
 * 为什么不需要边界特判：
 *   pos = 首节点：pos->pxNext 是第二个节点或哨兵，正常
 *   pos = 尾节点：pos->pxNext 是哨兵，步骤 3 自动改 sent->prev = n
 *   pos = 哨兵  ：n 插到首部，pos->pxNext 是旧首节点，正常
 *   pos = 空表  ：pos->pxNext 是哨兵自己，步骤 3 改成 sent->prev = n，
 *                 步骤 4 改成 sent->pxNext = n，空表插入同一条路径
 *   所有情况 pos->pxNext 永远存在，永远可解引用，所以没有 if。
 */


