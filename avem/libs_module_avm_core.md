# libs/module/avm_core.c 与 avm_core.h —— 模块池机制核心

> 对应源文件：
> - `references/firmware/Avem/libs/module/avm_core.c`（共 111 行，A级深注）
> - `references/firmware/Avem/libs/module/avm_core.h`（共 70 行，A级深注；本条目合并覆盖，头文件与实现强耦合）
> 互链：[src_main.md](src_main.md)（唯一调用方）｜[libs_module_avm_uart.md](libs_module_avm_uart.md)（模块池[1]）｜
> Docs：`Avem/Docs/02-模块池与主循环.md`

## 1. 覆盖范围说明

本条目按"强耦合合并"原则覆盖 .c 与 .h 两文件：接口协议（avm_module_t）、
模块池数组、SysTick 延时、jump2ISP 是一个整体，拆开读反而碎片化。

## 2. avm_core.h 逐项表（70 行）

| 元素 | 行号 | 说明 |
| --- | --- | --- |
| 文件头导读块 | 3~18 | 职责三件事 + 数据流位置 |
| 前置 typedef | 20~23 | 解 include 循环的关键 |
| 模块头文件汇总 | 25~35 | 8 个 include，一个头看全系统 |
| `struct avm_module_s` | 37~52 | **核心接口协议**（5 字段） |
| 池与核心实例 extern | 54~56 | `avm_modules_pool[]`、`avm_core_module_st` |
| `delay_ms/delay_us` 声明 | 58~60 | SysTick 阻塞延时 |
| `NVIC_GROUPING` | 62~64 | =3，配合 0x07-3 得分组 4 |
| `ISP_ADDR` | 66~67 | 0x1FFFF000 Bootloader 基地址 |
| `jump2ISP` 声明 | 68 | — |

接口协议逐字段（37~52 行注释区的浓缩）：

| 字段 | 类型 | 语义 | 本工程实况 |
| --- | --- | --- | --- |
| `index` | unsigned short | 池内编号 | avm_core_init 第一遍遍历填写 |
| `confs` | unsigned int* | 配置指针 | uart→{72,115200}；core→模块总数 |
| `init_module` | `unsigned char (*)(void*)` | 初始化钩子 | **唯一被调用的钩子** |
| `enable_module` | `unsigned char (*)(void)` | 预留 | 全 NULL |
| `disable_module` | `unsigned char (*)(void)` | 预留 | 全 NULL |

## 3. avm_core.c 逐函数表（111 行）

| 函数 | 行号 | 签名 | 职责 | 调用方 |
| --- | --- | --- | --- | --- |
| `avm_core_module_st` 定义 | 30~36 | 全局实例 | 自管理模块（index=0） | main / 池遍历 |
| `jump2ISP` | 43~48 | `void (void)` | 三步跳 Bootloader | uart 中断收 '>' |
| `delay_ms` | 52~60 | `void (unsigned int)` | SysTick 毫秒阻塞延时 | MOTOR_SETTING 等 |
| `delay_us` | 64~71 | `void (unsigned int)` | 微秒版 | I2C 节拍预留 |
| `avm_modules_pool[]` | 76~83 | 指针数组 | 模块注册表 + NULL 哨兵 | 遍历初始化 |
| `avm_core_init` | 88~111 | `static unsigned char (void*)` | NVIC 分组 + 两遍遍历 | main 经函数指针 |

## 4. 关键摘录

### 4.1 两遍遍历（88~111 行，逐字）

```c
static unsigned char avm_core_init(void *arg) {
	/* 设置 NVIC 中断优先级分组。0x07 是全部位都归抢占优先级；
	 * 减去 NVIC_GROUPING(3) 即留 3 位给子优先级（这是 CMSIS 函数的编码方式） */
	NVIC_SetPriorityGrouping(0x07 - NVIC_GROUPING);

	avm_module_t *p_core = avm_modules_pool[0];
	// Count
	/* 第一遍遍历：统计模块个数并给每个模块填 index 编号 */
	static unsigned char avm_modules_num = 0;
	for(;avm_modules_pool[avm_modules_num] != NULL;avm_modules_num++) {
		avm_modules_pool[avm_modules_num]->index = avm_modules_num;
	}

	p_core->confs = (void *)&avm_modules_num;	// Stored
	// Initial
	/* 第二遍遍历：从 [1] 开始（跳过 core 自己），逐个调用各模块的初始化函数。
	 * 注意变量名 this 只是普通指针名（C 里没有 C++ 的 this）。 */
	avm_module_t **this = &avm_modules_pool[1];
	for(;*this != NULL;this++) {
			(*this)->init_module((*this)->confs);
	}

	return 0;
}
```

要点：
- 第一遍：数模块（=5）+ 填 index。`static` 局部变量让计数器常驻，
  `p_core->confs` 指向它，模块总数成为系统级元信息。
- 第二遍：从 `[1]` 起逐个 `init_module(confs)`——**跳过 [0] 防自递归**。
- `avm_module_t **this` 二级指针遍历法：`*this` 即模块实例指针。

### 4.2 SysTick 毫秒延时（52~60 行，逐字）

```c
void delay_ms(unsigned int t) {
	SysTick->LOAD = 9000 * t;      /* 装入倒计数值 */
	SysTick->VAL = 0;              /* 清零当前值（写任意值都会清 COUNTFLAG） */
	SysTick->CTRL = 0x01;          /* 使能 SysTick，时钟源 = HCLK/8（bit2=0），开始倒数 */
	/* 自旋等待：bit0=ENABLE 仍为 1 且 bit16(COUNTFLAG) 置位（计到 0）时退出 */
	for(unsigned int tmp = SysTick->CTRL;(tmp&0x01)&&(!(tmp&SysTick_CTRL_COUNTFLAG));tmp = SysTick->CTRL);
	SysTick->CTRL = 0;             /* 关闭 SysTick */
	SysTick->VAL = 0;              /* 清计数值 */
}
```

量化：时钟源 HCLK/8 = 9MHz，1ms=9000 tick；**24 位 LOAD 上限 2^24-1，
即 t≤1864**。t=4000 时 LOAD 回卷截断为 36,000,000 mod 16,777,216 =
2,445,568 tick ≈ 0.27s——`MOTOR_SETTING` 的 4s 等待实际只有 0.27s。

### 4.3 jump2ISP（43~48 行，逐字）

```c
void jump2ISP() {
	SysTick->CTRL = 0;                                   /* 1. 关闭 SysTick，防止跳转途中触发中断 */
	SysTick->VAL = 0;                                    /*    清空当前计数值 */
	__set_MSP(*(unsigned int *)ISP_ADDR);                /* 2. 把主栈指针 MSP 设为 Bootloader 区首 4 字节存的栈顶值 */
	((void (*)(void))*((unsigned int *)(ISP_ADDR + 4)))(); /* 3. 取该处第 2 个字（复位向量）并跳转执行 */
}
```

模拟"从系统存储器启动"：Bootloader（0x1FFFF000）的前两个字就是它自己的
初始 MSP 和复位向量——读出来装进 MSP/PC 即完成"换世界"。

## 5. 模块池注册表（76~83 行）

| 槽 | 实例 | 定义处 | init 行为 |
| --- | --- | --- | --- |
| [0] | `avm_core_module_st` | 本文件 30 行 | NVIC 分组 + 统筹 |
| [1] | `avm_uart_module_st` | avm_uart.c 12 行 | USART1 115200 |
| [2] | `avm_i2c_module_st` | avm_i2c.c 23 行 | PB14/15 软件 I2C |
| [3] | `avm_motor_module_st` | avm_motor.c 26 行 | TIM3 PWM + 油门序列 |
| [4] | `avm_mpu_module_st` | avm_mpu6050.c 27 行 | 延时 7ms + MPU 配置 |
| 末尾 | `NULL` | — | 遍历哨兵 |

顺序即依赖：调试口最先（失败可见）、i2c 先于 mpu、motor 的长延时放中间。

## 6. 已记录问题（不改代码）

1. `avm_modules_num` 是 static 局部变量，二次进入会继续累加越界（单次启动安全）。
2. `delay_ms(t>1864)` 回卷（见 4.2），调用点 `MOTOR_SETTING` 的 4000 已踩中。
3. `this` 作变量名在 C++ 化改造时是关键字冲突。
4. 模块 init 返回值无错误传播：mpu 的"在位重试"用 do-while 死等代替错误码
   （avm_mpu6050.c 104~113 行），传感器不接则启动流程永久卡在此。

## 7. 自查命令

```bash
cd references/firmware/Avem
grep -n "avm_core_init\|avm_modules_pool\|delay_ms\|jump2ISP" libs/module/avm_core.c
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_core.c
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_core.h
```

延伸阅读：Docs `02-模块池与主循环.md`（含执行流/数据流图）；
各模块细节 → [libs_module_avm_i2c.md](libs_module_avm_i2c.md)、
[libs_module_avm_motor.md](libs_module_avm_motor.md)、
[libs_module_avm_mpu6050.md](libs_module_avm_mpu6050.md)。
