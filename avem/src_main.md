# src/main.c —— 飞控程序入口

> 对应源文件：`references/firmware/Avem/src/main.c`（共 45 行）
> 注释分级：A级（逐函数深注已完成，文件头 1~15 行 + 函数头 19~26 行）
> 互链：[src_startup.md](src_startup.md)（谁调用了 main）｜[libs_module_avm_core.md](libs_module_avm_core.md)（main 调了什么）

## 1. 文件概览

main.c 是全飞控**最短的 C 文件**（45 行，一半是注释与被注释的调试代码），
只做一件事：`avm_core_module_st.init_module(NULL)` 一句话触发全系统初始化，
然后空转。真正的控制循环在 `USART1_IRQHandler`（`libs/module/avm_uart.c`
59~91 行），由上位机 '$' 命令驱动。

| 元素 | 行号 | 说明 |
| --- | --- | --- |
| 文件头导读块 | 1~15 | 本工程注释规范的标准开头 |
| `#include <avm_core.h>` | 17 | 一个头文件看全所有模块 |
| `main()` 函数头注释 | 19~26 | A 级函数头（含调用时机说明） |
| `int main()` | 27 | 入口 |
| 被注释的调试代码 | 28~37、40~41 | 作者的调试痕迹（保留学习） |
| `init_module(NULL)` | 39 | **全文件唯一有效语句** |
| `while(1){}` | 43~44 | 空转主循环 |

## 2. 逐函数表

| 函数 | 行号 | 签名 | 职责 | 返回 |
| --- | --- | --- | --- | --- |
| `main` | 27~45 | `int main(void)` | 触发模块池初始化后死循环 | 永不返回（返回后被 startup 的 while(1) 接住） |

## 3. 关键摘录（src/main.c 38~44 行，逐字）

```c
	/* 一句话启动整个系统：core 模块按模块池顺序初始化其余全部模块 */
	avm_core_module_st.init_module(NULL);
	// avm_uart_module_st.init_module(avm_uart_module_st.confs);
	// uart_init(72, 115200);

	while(1) {
	}
```

- 通过**函数指针成员**调用而非直接调 `avm_core_init()`：体现模块接口
  （`avm_core.h` 46~52 行）的解耦——main 只认识协议不认识实现。
- 被注释的 `uart_init(72, 115200)` 与模块池里 uart 模块的 confs（72,115200）
  完全一致——说明作者是从"手工逐个初始化"进化到"模块池统一初始化"的，
  两行注释正是重构痕迹。
- 28~37 行注释掉的 MPU 自检/电机测试代码是裸机调试手法的活教材。

## 4. 设计要点与陷阱

1. **主循环为什么空**：教学版把控制节拍交给上位机 '$' 请求（见
   [libs_module_avm_uart.md](libs_module_avm_uart.md) 3.1 节）。若要独立
   运行，最小改动是把 `avm_uart.c` 64~66 行的三连调用搬进定时中断。
2. **main 返回会怎样**：`src/startup.c` 311 行调用 main 后紧跟 `while(1){}`
   （313 行），main 的 `int` 返回值无人消费——裸机没有"进程退出"。
3. **int main() 无 return**：C99 允许 main 省略 return（隐式返回 0），
   -Wall -Werror 下不告警。
4. **argc/argv 缺省**：`int main()` 与 `int main(void)` 等价（嵌入式约定）。

## 5. 阅读路径

上电到 main 的全过程 → [src_startup.md](src_startup.md)；
init_module 内部（模块池遍历）→ [libs_module_avm_core.md](libs_module_avm_core.md)；
Docs 总览 → `Avem/Docs/00-总览与硬件架构.md`、启动精读 → `Avem/Docs/01-启动流程精读.md`。
