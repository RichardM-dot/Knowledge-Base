# src/startup.c —— 上电启动代码（纯 C 启动文件）

> 对应源文件：`references/firmware/Avem/src/startup.c`（共 395 行）
> 注释分级：A级-关键段（向量表结构 / __Init_Data / Reset_Handler 已深注；
> weak 声明区 45~114 行仅导读不逐行展开）
> 互链：[src_main.md](src_main.md)（启动终点）｜`linker.ld`（符号来源）｜
> Docs：`Avem/Docs/01-启动流程精读.md`

## 1. 文件概览

用纯 C 实现的启动文件（多数工程用汇编）。承担三件事：**定义向量表**、
**初始化 C 运行内存**、**从复位跳到 main**。

| 区段 | 行号 | 内容 |
| --- | --- | --- |
| 文件头导读 | 1~20 | 启动流程三步总述 |
| 类型与链接符号 | 22~36 | `intfunc`/`WEAK` + `_sidata` 等 6 个 linker.ld 符号 |
| 前向声明 | 38~43 | Reset_Handler 带 `__interrupt__` 属性 |
| weak 声明区 | 45~114 | 70 个中断处理函数弱声明 |
| **向量表** | 116~270 | 【关键段 1】`g_pfnVectors[]`，.isr_vectors 段 |
| **__Init_Data** | 271~295 | 【关键段 2】.data 拷贝 + .bss 清零 |
| **Reset_Handler** | 296~314 | 【关键段 3】VTOR/SystemInit/main |
| weak 别名区 | 315~388 | `#pragma weak X = Default_Handler` ×70 |
| Default_Handler | 390~395 | 死循环兜底 |

## 2. 逐函数表

| 函数/对象 | 行号 | 签名/形式 | 职责 | 调用时机 |
| --- | --- | --- | --- | --- |
| `g_pfnVectors` | 132~270 | `void (* const [])(void)` | 向量表：[0]初始 MSP、[1]复位入口、[2..15]内核异常、[16..83]外设 IRQ | 硬件自动加载 |
| `__Init_Data` | 282~295 | `void (void)` | .data 从 Flash 拷到 RAM；.bss 清零 | Reset_Handler 第一行 |
| `Reset_Handler` | 303~314 | `void (void)` | 内存初始化→VTOR→SystemInit→main | 复位入口（表[1]） |
| `Default_Handler` | 392~395 | `void (void)` | 未实现中断的死循环兜底 | 被弱别名指向 |

## 3. 关键段摘录

### 3.1 向量表头两项（132~135 行，逐字）

```c
__attribute__ ((section(".isr_vectors")))
void (* const g_pfnVectors[])(void) = {
 (intfunc)((unsigned long)&_estack), /* The stack pointer after relocation */
 Reset_Handler,
```

- 表[0] 是**栈顶值不是函数**，用 `intfunc`（22 行 typedef）完成数据→函数
  指针的类型伪装。`_estack` = RAM 末尾 = 0x20010000（linker.ld 25 行）。
- `.isr_vectors` 段被 linker.ld 35 行 `KEEP(*(.isr_vectors))` 钉在 0x08000000，
  29 行 ASSERT 强制 0x80 对齐（VTOR 要求 128 字对齐）。
- 外设段 [16]=WWDG（166 行）~[83]=DMA2_Channel4_5（258 行），共 68 个 IRQ，
  顺序不可动；USART1 在 [53]（230 行，IRQ53）。

### 3.2 __Init_Data（282~295 行，逐字）

```c
void __Init_Data(void) {
	unsigned long *src, *dst;
	/* copy the data segment into ram */
	src = &_sidata;
	dst = &_sdata;
	/* src==dst 说明链接器把 .data 直接放进了 RAM（无需搬运），直接跳过 */
	if (src != dst)
		while(dst < &_edata)
			*(dst++) = *(src++);
	/* zero the bss segment */
	dst = &_sbss;
	while(dst < &_ebss)
		*(dst++) = 0;
}
```

`.data` 的 `>RAM AT>FLASH`（linker.ld 105 行）双地址布局是这段代码存在的
理由：`_sidata` 是 LMA（Flash 里初值镜像），`_sdata.._edata` 是 VMA（RAM
运行区）。**没有它，所有"已初始化全局变量"上电是随机值**。

### 3.3 Reset_Handler（303~314 行，逐字）

```c
void Reset_Handler(void) {
	__Init_Data(); /* Initialize memory, data and bss */
	extern u32 _isr_vectors_offs; /* the offset to the vector table in ram */
	/* 设置向量表基址：VTOR 低 7 位恒为 0（要求 128 字对齐），
	 * 0x1FFFFF80 掩码即保留对齐位、清掉其它位。本工程向量表在 Flash，
	 * _isr_vectors_offs=0，结果即 0x08000000 */
	SCB->VTOR = 0x08000000 | ((u32)&_isr_vectors_offs & (u32)0x1FFFFF80); /* set interrupt vector table address */
	SystemInit(); /* configure the clock */
	main(); /* start execution of the program */
	/* main 返回即死等复位——裸机程序没有"退出"的概念 */
	while(1) {}
}
```

四步：备内存 → 指向量表 → 配时钟（ST 库，HSE 8MHz×9=72MHz）→ 进 main。
`_isr_vectors_offs` 由 linker.ld 34 行导出（本工程 = 0）。

## 4. weak 机制（315~388 行）

`#pragma weak X = Default_Handler` 把每个未实现的中断别名到死循环：
链接着到强符号（如 `avm_uart.c` 59 行的 USART1_IRQHandler）则别名失效。
70 个别名对应向量表 70 个处理函数——**"不实现也不跑飞"**。

## 5. 已记录问题（不改代码）

1. **NMI 别名笔误**：320 行 `#pragma weak MMI_Handler = Default_Handler`
   疑似 NMI_Handler 笔误。NMI 只有 45 行的弱声明、无定义无别名 → 向量表
   [2] 实际为 0。触发 NMI 即异常跳转。
2. **向量表尾部 RAM 启动项**：268 行 `(intfunc)0xF1E0F85F` 是 HD 型号
   RAM 启动预留（作者原注释 269 行），Flash 启动时不会用到。
3. **栈无显式保护**：linker.ld 只保证 `_min_stack=0x100` 的链接检查，
   栈与 .bss 末端的距离没有硬约束，加深调用栈前要 `arm-none-eabi-size`
   看剩余 RAM。

## 6. 自查命令

```bash
cd references/firmware/Avem
grep -n "g_pfnVectors\|__Init_Data\|Reset_Handler\|MMI_Handler" src/startup.c
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . src/startup.c
```

阅读路径：Docs `01-启动流程精读.md`（图文）→ 本文（逐行）→
[libs_module_avm_core.md](libs_module_avm_core.md)（main 之后的系统初始化）。
