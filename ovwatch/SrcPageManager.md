会互相影响，但要说清楚“影响的是什么”。

## 1. 参数 `stack` 本身是局部变量，不共享

```c
static void page_stack_init(PageStack_t* stack) {
    stack->top = 0;
}

static uint8_t page_stack_push(PageStack_t* stack, Page_t* page) {
    if (stack->top >= MAX_DEPTH)
        return -1;
    stack->pages[stack->top++] = page;
    return 0;
}
```

这两个函数里的 `stack` 都是**函数参数**，是各自函数栈帧里的局部变量。  
`page_stack_init` 里的 `stack` 和 `page_stack_push` 里的 `stack` **不是同一个变量**，它们只是两个指针变量，可能存着相同的地址。

所以：

- 参数变量本身：互不影响，函数返回就销毁。
- 参数指向的那块 `PageStack_t` 内存：**如果传的是同一个地址，就会互相影响**。

## 2. 为什么会影响：它们操作的是同一块内存

调用方式通常是这样：

```c
PageStack_t PageStack;   // 全局或某个地方定义的堆栈对象

page_stack_init(&PageStack);          // 传 &PageStack
page_stack_push(&PageStack, &pageA);  // 也传 &PageStack
```

内存关系：

```text
page_stack_init 的 stack ──┐
                           ├──> PageStack 这块内存
page_stack_push 的 stack ──┘
```

两个函数里的 `stack` 指针值相同，都指向 `PageStack`。  
所以：

```c
stack->top = 0;          // 改的是 PageStack.top
stack->pages[stack->top++] = page;  // 改的也是 PageStack.pages 和 PageStack.top
```

`page_stack_init` 把 `PageStack.top` 清零后，`page_stack_push` 再调用时看到的 `stack->top` 就是 0。  
`page_stack_push` 把 `top` 加 1 后，下次再 push 看到的就是 1。  
**这正是设计意图：两个函数共同操作同一个堆栈对象。**

## 3. 如果传不同的 stack，就互不影响

```c
PageStack_t stackA;
PageStack_t stackB;

page_stack_init(&stackA);
page_stack_push(&stackB, &pageA);
```

这里 `page_stack_init` 操作的是 `stackA`，`page_stack_push` 操作的是 `stackB`，互不影响。

所以关键不是“函数名相同参数名相同”，而是：**调用时传进去的指针是否指向同一个对象**。

## 4. 为什么参数要用 `PageStack_t*` 而不是 `PageStack_t`

如果写成：

```c
static void page_stack_init(PageStack_t stack) {
    stack.top = 0;
}
```

这是**值传递**，函数里改的是副本，函数返回后外面的 `PageStack.top` 根本没变。  
而且 `PageStack_t` 里有个数组，值传递会拷贝整个数组，浪费空间。

用指针：

```c
static void page_stack_init(PageStack_t* stack) {
    stack->top = 0;
}
```

才能修改调用者那边的真实对象。  
C 语言没有引用传递，想“在函数里改外面的结构体”，就要传地址。

## 5. 两个函数的分工

它们不是“两个函数各操作一个不同的 stack”，而是：

- `page_stack_init`：负责把堆栈**重置为空**
- `page_stack_push`：负责往**同一个堆栈**里**压入页面**

这是对同一个数据结构的不同操作，类似：

```c
stack_init(&s);
stack_push(&s, &pageA);
stack_push(&s, &pageB);
```

执行后：

```text
s.top = 2
s.pages[0] = &pageA
s.pages[1] = &pageB
```

## 6. 一个容易忽略的点：`top` 的含义

这段代码里：

```c
stack->pages[stack->top++] = page;
```

是“先使用 `top` 作为下标，再自增”。  
所以如果 `top == 0`：

```c
stack->pages[0] = page;
stack->top = 1;
```

`top` 表示**当前元素个数 / 下一个可写位置**。  
栈顶元素是：

```c
stack->pages[stack->top - 1]
```

不是 `stack->pages[stack->top]`。这个在写 pop 时要特别注意。

## 7. 还有一个细节：返回类型

```c
static uint8_t page_stack_push(...)
{
    if (stack->top >= MAX_DEPTH)
        return -1;
    ...
    return 0;
}
```

返回类型是 `uint8_t`，`return -1` 会被转换成 `255`。  
如果调用方写：

```c
if (page_stack_push(&s, &p) < 0)
```

因为 `uint8_t` 是无符号的，这个判断**永远不成立**。  
要么返回类型改成 `int8_t` / `int`，要么调用方判断 `== 255` 或 `!= 0`。  
这是这段代码里实际存在的小坑。

## 一句话总结

两个函数里的 `stack` 参数变量互不相干，但它们**指向同一块 `PageStack_t` 内存**；只要调用时传的是同一个堆栈对象的地址，`init` 和 `push` 就会互相影响——`init` 清空 `top`，`push` 修改 `pages` 和 `top`，这正是它们配合工作的方式。传不同地址就操作不同堆栈，互不影响。可以这么理解：**这段代码的核心确实是“用栈管理页面导航”**，但要注意——**LVGL 本身不是在操作栈，LVGL 只负责显示屏幕和动画；栈是你自己在应用层实现的一套“页面历史/页面路由”机制。**

换句话说：

> **栈决定“当前逻辑页面是谁”，LVGL 负责“把这个页面的 screen 显示出来”。**

---

## 1. 这段代码里，栈操作体现在哪

你的 `PageStack_t` 就是一个页面栈：

```c
typedef struct {
    Page_t* pages[MAX_DEPTH];  // 页面指针数组
    uint8_t top;               // 栈顶/元素个数
} PageStack_t;
```

这几个函数分别对应栈操作：

| 函数 | 栈操作 | 作用 |
|---|---|---|
| `Pages_init` | 初始化 + push | 清空栈，压入 Home |
| `Page_Load` | push | 加载新页面到栈顶 |
| `Page_Back` | pop | 弹出当前页面，回到上一个 |
| `Page_Back_Bottom` | 连续 pop | 一路弹到栈底 Home |
| `Page_Get_NowPage` | 取栈顶 | 获取当前页面 |
| `page_stack_push` | push | 入栈 |
| `page_stack_pop` | pop | 出栈 |

所以从导航逻辑上看，**它就是一个标准的栈式页面管理器**。

---

## 2. 栈里存的不是 LVGL 对象，而是 `Page_t*`

栈里存的是：

```c
Page_t* pages[MAX_DEPTH];
```

`Page_t` 是“页面描述”：

```c
typedef struct {
    void (*init)(void);      // 页面初始化
    void (*deinit)(void);    // 页面反初始化
    lv_obj_t **page_obj;     // 指向 LVGL 对象指针数组
} Page_t;
```

而真正给 LVGL 显示的是：

```c
lv_scr_load_anim(*newPage->page_obj, ...);
```

这里 `newPage->page_obj` 是 `lv_obj_t**`，`*newPage->page_obj` 取出第一个 `lv_obj_t*`，通常就是该页面的 **screen 对象**。

所以层次是：

```text
PageStack_t
  └── pages[] : Page_t*
        └── Page_t
              ├── init / deinit
              └── page_obj : lv_obj_t**
                    └── *page_obj : lv_obj_t*  →  LVGL screen
```

**栈管理的是 `Page_t*`，LVGL 管理的是 `lv_obj_t*`。**

---

## 3. 逐个函数看，它们怎么配合栈和 LVGL

### `Pages_init`

```c
page_stack_init(&PageStack);
page_stack_push(&PageStack, &Page_Home);
Page_Home.init();
lv_disp_load_scr(*Page_Home.page_obj);
```

流程：

1. 栈清空，`top = 0`
2. 压入 Home，`top = 1`
3. 初始化 Home 页面
4. 让 LVGL 显示 Home 的 screen

此时栈：

```text
pages[0] = &Page_Home
top = 1
```

---

### `Page_Load`

```c
if (PageStack.top >= MAX_DEPTH - 1) return;

if (PageStack.top > 0) {
    PageStack.pages[PageStack.top - 1]->deinit();
}

page_stack_push(&PageStack, newPage);
newPage->init();
lv_scr_load_anim(*newPage->page_obj, ...);
```

流程：

1. 检查栈是否快满
2. 当前栈顶页面 `deinit`
3. 新页面入栈
4. 新页面 `init`
5. LVGL 切换到新页面 screen

栈变化示例：

```text
加载前：
pages[0] = Home
pages[1] = Menu
top = 2

Page_Load(&Page_Setting) 后：
pages[0] = Home
pages[1] = Menu
pages[2] = Setting
top = 3
```

这就是 **push**。

---

### `Page_Back`

```c
if (page_stack_is_empty(&PageStack)) return;

page_stack_pop(&PageStack);

if (page_stack_is_empty(&PageStack)) {
    page_stack_push(&PageStack, &Page_Home);
    page_stack_push(&PageStack, &Page_Menu);
    Page_Menu.init();
    lv_scr_load_anim(*Page_Menu.page_obj, ...);
} else {
    Page_t *previous_page = PageStack.pages[PageStack.top - 1];
    previous_page->init();
    lv_scr_load_anim(*previous_page->page_obj, ...);
}
```

流程：

1. 弹出当前页面
2. 如果弹完栈空了，就重新压入 Home 和 Menu，显示 Menu
3. 如果栈没空，取新的栈顶页面，`init` 后显示

栈变化示例：

```text
返回前：
pages[0] = Home
pages[1] = Menu
pages[2] = Setting
top = 3

page_stack_pop 后：
pages[0] = Home
pages[1] = Menu
top = 2

previous_page = pages[1] = Menu
显示 Menu
```

这就是 **pop + 取栈顶**。

---

### `Page_Back_Bottom`

```c
while (PageStack.top > 1)
    page_stack_pop(&PageStack);

PageStack.pages[PageStack.top - 1]->init();
lv_scr_load_anim(*PageStack.pages[PageStack.top - 1]->page_obj, ...);
```

流程：

1. 一直 pop，直到只剩栈底页面
2. 初始化栈底页面
3. 显示栈底页面

栈变化：

```text
返回前：
pages[0] = Home
pages[1] = Menu
pages[2] = Setting
pages[3] = About
top = 4

循环 pop 后：
pages[0] = Home
top = 1

显示 Home
```

这就是 **连续 pop 到栈底**。

---

### `Page_Get_NowPage`

```c
return get_top_page(&PageStack);
```

取栈顶页面，也就是当前逻辑页面。

---

## 4. LVGL 在里面的角色

LVGL 只出现在这些地方：

```c
lv_scr_load_anim(*page->page_obj, ...);
lv_disp_load_scr(*Page_Home.page_obj);
```

它们做的是：

- 把某个 `lv_obj_t*`（screen）加载为当前显示屏幕
- 可选地带切换动画

LVGL **不知道你的 `PageStack_t` 存在**。  
它只知道“我要显示这个 screen”。

所以整体是两层：

```text
应用层：PageStack_t + Page_t     ← 页面栈、导航历史、init/deinit
        ↓
LVGL 层：lv_obj_t / screen       ← 实际控件、屏幕、动画
```

可以类比浏览器：

- 栈 = 浏览历史（前进/后退）
- LVGL = 真正把网页画出来

---

## 5. 这段代码里值得注意的几个点

### ① `Page_Load` 的栈满判断不一致

```c
if (PageStack.top >= MAX_DEPTH - 1) return;
```

但 `page_stack_push` 里是：

```c
if (stack->top >= MAX_DEPTH) return -1;
```

一个允许到 `MAX_DEPTH-1`，一个允许到 `MAX_DEPTH`。  
如果是有意预留一层，要注释清楚；否则容易让人误解。

### ② `Page_Back` 没有 `deinit` 当前页面

`Page_Load` 在压新页面前会：

```c
PageStack.pages[PageStack.top - 1]->deinit();
```

但 `Page_Back` 只是：

```c
page_stack_pop(&PageStack);
```

如果 `page_stack_pop` 内部没有调用 `deinit`，那被弹出的页面就没有反初始化，可能造成资源/状态残留。  
**需要确认 `page_stack_pop` 的实现。**

### ③ `init` 可能被重复调用

`Page_Back` 里对 `previous_page->init()` 又调用了一次。  
如果 `init` 不是幂等的（比如重复创建控件），可能会出问题。  
常见做法是：`init` 只创建一次，返回页面时用 `resume`/`show` 之类的函数。

### ④ `*page_obj` 默认第一个对象是 screen

```c
lv_scr_load_anim(*page->page_obj, ...);
```

这依赖 `page_obj` 数组的第 0 个元素是 screen。  
这是约定，要保证每个页面的 `page_obj[0]` 都是 screen 对象。

---

## 6. 一句话总结

**是的，这段代码的核心就是“页面栈管理”：用 push/pop 维护页面历史，用栈顶决定当前页面。但 LVGL 本身不操作这个栈，它只负责把栈顶 `Page_t` 里的 `lv_obj_t*` screen 显示出来。栈是应用层的导航模型，LVGL 是底层的显示层。**

---

## 相关条目与文档

- 谁在调用 `Page_Back`：[`TaskSrcuser_KeyTask.md`](TaskSrcuser_KeyTask.md)
- 页面栈与 GUI 架构：[`Docs/03-页面管理器与GUI架构.md`](../../../references/firmware/OV-Watch/Docs/03-页面管理器与GUI架构.md)
- 任务上下文约束：[`Docs/02-FreeRTOS任务系统详解.md`](../../../references/firmware/OV-Watch/Docs/02-FreeRTOS任务系统详解.md)