// 本文件为带前后哨兵的双向循环列表，可作为freertos中的list文件逻辑参考
#include <stdio.h>
#include <stdlib.h>

typedef int ElemType;

typedef struct Node {
    ElemType data;
    struct Node *prev;
    struct Node *next;
} Node;

typedef struct {
    Node *head;   /* 头哨兵 */
    Node *tail;   /* 尾哨兵 */
} List;

/* ---------- 初始化 ---------- */
void initList(List *L) {
    L->head = (Node *)malloc(sizeof(Node)); // 用 malloc 在堆上分配一块正好能装下一个 Node 结构体的内存，
                                            // 把返回的 void* 强制转换成 Node* 类型，再赋给 L->head，让它指向这个新节点
    L->tail = (Node *)malloc(sizeof(Node));
    if (!L->head || !L->tail) { perror("malloc"); exit(1); } // 只要有一个 malloc 失败（对应指针为 NULL），就进入 if 执行 perror("malloc"); exit(1);
                                                             // || 只要有一个为真if条件就为真，只要有一个分配失败就进入{}
    L->head->prev = L->tail;
    L->head->next = L->tail;
    L->tail->prev = L->head;
    L->tail->next = L->head;   /* 首尾相接成环 */
}
//        head                          tail
//   ┌───────────┐                 ┌───────────┐
//   │ prev: ────┼────────────────▶│ prev:  ?  │
//   │ next: ────┼────────────────▶│ next:  ?  │
//   └───────────┘                 └───────────┘
//        ▲   ▲                          │   │
//        │   └──────────────────────────┘   │
//        │        （head 的两个指针 → tail）  ｜
//        └──────────────────────────────────┘
//                （tail 的两个指针 → head）

/* ---------- 在 pos 前插入（内部工具） ---------- */
static Node *insertBefore(Node *pos, ElemType val) {// 返回类型是 Node *
    Node *node = (Node *)malloc(sizeof(Node));
    if (!node) { perror("malloc"); exit(1); }

    node->data = val;
    node->prev = pos->prev;
    node->next = pos;
    pos->prev->next = node;
    pos->prev = node;
    return node;
}
/* 
   状态 1：插入前的初始状态（pos->prev 就是 P）
   ┌───────┐   next   ┌───────┐
   │   P   │─────────▶│  pos  │
   │       │◀─────────│       │
   └───────┘   prev   └───────┘


   状态 2：【5 行全写】—— 正确情况
   ┌───────┐   next   ┌───────┐   next   ┌───────┐
   │   P   │─────────▶│ node  │─────────▶│  pos  │
   │       │◀─────────│data:val│◀─────────│       │
   └───────┘   prev   └───────┘   prev   └───────┘
   结果：四条箭头两两配对，完美闭环。


   状态 3：【只写前 3 行】—— 错误情况（没写后 2 行）
   ┌───────┐   next   ┌───────┐
   │   P   │─────────▶│  pos  │  ← P->next 仍指着 pos！
   │       │◀─────────│       │  ← pos->prev 仍指着 P！
   └───────┘   prev   └───────┘
       ▲                  ▲
       │ prev             │ next
   ┌───────┐              │
   │ node  │──────────────┘
   │data:val│
   └───────┘
   结果：node 指向了 P 和 pos，但 P 和 pos 没指向 node。
         node 成了“孤岛”，遍历永远跳过它，还会内存泄漏！
*/

/* ---------- 头插 / 尾插 ---------- */
Node *pushFront(List *L, ElemType val) {
    return insertBefore(L->head->next, val);   /* 插在第一个真实节点前 */
}

Node *pushBack(List *L, ElemType val) {
    return insertBefore(L->tail, val);         /* 插在尾哨兵前 */
}

/* ---------- 删除节点 ---------- */
void eraseNode(List *L, Node *node) {
    if (node == L->head || node == L->tail) return;  /* 保护哨兵 */
    node->prev->next = node->next;
    node->next->prev = node->prev;
    free(node);
}
/*
   假设要删除的节点是 node，它的前驱节点是 P（即 node->prev），
   后继节点是 N（即 node->next）。

   状态 1：删除前的初始状态
   ┌───────┐   next   ┌───────┐   next   ┌───────┐
   │   P   │─────────▶│ node  │─────────▶│   N   │
   │       │◀─────────│       │◀─────────│       │
   └───────┘   prev   └───────┘   prev   └───────┘

   执行第 1 句：node->prev->next = node->next;
   等价于：P->next = N;  （让 P 的 next 跳过 node，直接指向 N）

   执行第 2 句：node->next->prev = node->prev;
   等价于：N->prev = P;  （让 N 的 prev 跳过 node，直接指回 P）

   状态 2：执行完前两句（node 已经被“架空”）
   ┌───────┐                  next                  ┌───────┐
   │   P   │───────────────────────────────────────▶│   N   │
   │       │◀───────────────────────────────────────│       │
   └───────┘                  prev                  └───────┘

   此时 P 和 N 已经完美互连，node 彻底变成了一个没人指向的“孤岛”。

   执行第 3 句：free(node);
   状态 3：彻底删除，内存回收，链表闭环完好
   ┌───────┐                  next                  ┌───────┐
   │   P   │───────────────────────────────────────▶│   N   │
   │       │◀───────────────────────────────────────│       │
   └───────┘                  prev                  └───────┘
*/

/* ---------- 遍历 ---------- */
void printForward(const List *L) {
    printf("正向: ");
    for (Node *p = L->head->next; p != L->tail; p = p->next)
        printf("%d ", p->data);
    printf("\n");
}

void printBackward(const List *L) {
    printf("反向: ");
    for (Node *p = L->tail->prev; p != L->head; p = p->prev)
        printf("%d ", p->data);
    printf("\n");
}

/* ---------- 辅助 ---------- */

// isEmpty：判断链表是否为空
// 因为头哨兵 head 和尾哨兵 tail 是一开始就创建好的，如果链表里没有真实节点，那么 head 的 next 就会直接指向 tail
// 空表：  head ──────next─────▶ tail
//        head ◀────prev────── tail
int isEmpty(const List *L)  { return L->head->next == L->tail; }

int length(const List *L) {
    int n = 0;
    // 从第一个真实节点（head->next）开始，只要没碰到尾哨兵（p != L->tail），计数器 n 就加 1，然后 p 往后走一步
    for (Node *p = L->head->next; p != L->tail; p = p->next) n++;
    return n;
}

void clearList(List *L) {
    Node *p = L->head->next;
    while (p != L->tail) {
        Node *next = p->next; // ★ 关键：先存下一个节点的地址
        free(p);              // 释放当前节点
        p = next;             // 移动到下一个节点
    }
    L->head->next = L->tail;   // 恢复空表状态：头指尾
    L->tail->prev = L->head;   // 恢复空表状态：尾指头
}

// 彻底毁掉链表
void destroyList(List *L) {
    clearList(L);              // 先清空所有真实节点
    free(L->head);             // 销毁头哨兵
    free(L->tail);             // 销毁尾哨兵
    L->head = L->tail = NULL;  // ★ 关键：置空防止野指针
}

/* ---------- 测试 ---------- */
int main(void) {
    List L;
    initList(&L);

    for (int i = 1; i <= 5; i++) pushBack(&L, i * 10);
    pushFront(&L, 0);

    printForward(&L);      /* 0 10 20 30 40 50 */
    printBackward(&L);     /* 50 40 30 20 10 0 */
    printf("长度: %d\n", length(&L));

    eraseNode(&L, L.head->next);   /* 删除首个真实节点 0 */
    eraseNode(&L, L.tail->prev);   /* 删除末个真实节点 50 */
    printForward(&L);              /* 10 20 30 40 */

    destroyList(&L);
    return 0;
}