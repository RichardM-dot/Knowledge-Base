# libs/module/avm_uart.c 与 avm_uart.h —— USART1 链路（协议 + 控制触发）

> 对应源文件：
> - `references/firmware/Avem/libs/module/avm_uart.c`（共 177 行，B级：文件头导读已完成）
> - `references/firmware/Avem/libs/module/avm_uart.h`（共 39 行，B级：文件头导读已完成；合并覆盖）
> 互链：[libs_module_avm_core.md](libs_module_avm_core.md)（jump2ISP）｜
> [libs_module_avm_mpu6050.md](libs_module_avm_mpu6050.md)、[libs_module_avm_pid.md](libs_module_avm_pid.md)（中断里的调用链）｜
> Docs：`Avem/Docs/07-链路通信与调参指南.md`

## 1. 文件定位（B级导读浓缩）

USART1 不只是调试口——**'$' 命令触发的接收中断是整个飞控控制循环的执行点**。
主要函数：`uart_init`（寄存器级波特率）、`USART1_IRQHandler`（协议解析+
控制三连）、`uart_Float2Char/uart_short2char/uart_int2char`（无 printf 的
数值输出）、`uart_sendData/uart_sendStr`（阻塞发送）。

## 2. avm_uart.h 逐项表（39 行）

| 元素 | 行号 | 说明 |
| --- | --- | --- |
| 文件头导读块 | 3~17 | 职责 + 协议约定 + 数据流位置 |
| 函数声明 | 20~26 | 6 个 API + IRQHandler |
| `extern avm_uart_module_st` | 28 | 模块池[1] |
| `UART_CR()` | 30~33 | 发 CRLF 宏 |
| `UART_CLEAR()` | 35 | ANSI 清屏 |
| `TOKEN` | 37 | **'$'——一帧数据请求** |

## 3. avm_uart.c 逐函数表（177 行）

| 函数 | 行号 | 签名 | 职责 |
| --- | --- | --- | --- |
| `avm_uart_conf` | 10 | `{72, 115200}` | pclk2(MHz) 与波特率 |
| `avm_uart_module_st` | 12~18 | 模块实例 | confs 指向 conf 数组 |
| `avm_uart_init` | 21~24 | `static unsigned char (void*)` | 解包 conf 调 uart_init |
| `uart_init` | 27~57 | `void (unsigned int, unsigned int)` | BRR/GPIO/复位/中断配置 |
| `USART1_IRQHandler` | 59~91 | 中断函数 | **协议解析 + 控制循环 + 回传** |
| `uart_sendData` | 93~96 | `void (unsigned char)` | 写 DR 死等 TC |
| `uart_short2char` | 98~107 | `void (short)` | 5 位十进制（预置"00000"+加数字） |
| `uart_int2char` | 109~118 | `void (unsigned int)` | 10 位十进制 |
| `uart_Float2Char` | 120~172 | `unsigned char (float)` | 符号+整数反转+4 位小数 |
| `uart_sendStr` | 174~177 | `void (char*)` | 循环 sendData |

## 4. 关键摘录

### 4.1 波特率计算（27~45 行节选，逐字）

```c
    temp = (float)(pclk2*1000000)/(bound*16);
    mantissa = temp;
    fraction = (temp - mantissa) * 16;
    mantissa <<= 4;
    mantissa += fraction;
```

公式 `USARTDIV = fclk/(16×bound)`，BRR = 整数<<4 | 小数×16。
72MHz/115200：temp=39.0625 → mantissa=39（<<4=624）、fraction=1 → BRR=625。
校验 72e6/(16×625)=115200 ✓。**pclk2 入参是 MHz**（conf 里填 72）。

### 4.2 中断协议处理（59~91 行，逐字）

```c
void USART1_IRQHandler(void) {
	if(USART1->SR & USART_SR_RXNE) {
		const char cmd = USART1->DR;	// 读取串口接收寄存器来清除 RXNE 标志
		switch (cmd) {
			case TOKEN:
				MPU6050_getStructData(&avm_euler);
				IMU_Comput(avm_euler);
                pid_SingleAxis(&avm_pid, 0);

				uart_Float2Char(g_Pitch);
				uart_sendStr("@");

				uart_Float2Char(g_Roll);
				uart_sendStr("@");

				uart_Float2Char(g_Yaw);
                uart_sendStr("@");

                uart_short2char(*avm_pid.Channel1);
                uart_sendStr("@");

                uart_short2char(*avm_pid.Channel2);
				uart_sendStr("\n\r");
				break;
			case '>':	// Jump to bootloader
				uart_sendStr("Running bootloader...");
				jump2ISP();
				// NOTE: running bootloader
			default:	//其它按键
				break;
		}
	}
}
```

要点：
- 读 DR 即清 RXNE（STM32 机制），不读则中断风暴。
- `case TOKEN` 里是**完整控制循环**：采样→解算→PID→回传，全程在中断上下文。
  `pid_SingleAxis(&avm_pid, 0)` 的 setPoint 恒 0（水平悬停，无遥控通道）。
- `case '>'` 无 break：依赖 jump2ISP 不返回，fall-through 到 default 无实害。
- 回传帧：`Pitch@Roll@Yaw@CCR2@CCR4\n\r`（ASCII，'@' 分隔）。

### 4.3 阻塞发送原语（93~96 行，逐字）

```c
void uart_sendData(unsigned char data) {
    USART1->DR = data;
    while((USART1->SR & 0x40) == 0);
}
```

`SR bit6=TC`。115200 下一字节 87µs，一帧约 40 字节 ≈ 3.5ms 纯阻塞——
限制单次 '$' 处理约 5ms（含 I2C 1~2ms），上位机请求频率应 ≤200Hz。

### 4.4 浮点转字符串（120~172 行）

流程：负数取反+发 '-'（128~131）→ 整数部分逆序入数组（146~151）→ 数组内
反转（152~157）→ **固定 4 位小数、截断不四舍五入**（161~164）→ 整串发送。
返回发送长度。`uart_short2char` 用"预置 '00000' 逐位加数字"技巧（103~105
行 `cache[i] += k/bit[i]%10`），负数输入结果错误（只用于恒正的 CCR 值）。

## 5. 协议总表

| 命令 | 方向 | 动作 | 响应 |
| --- | --- | --- | --- |
| `$` | 上位机→飞控 | 采样+解算+PID+发送 | `P@R@Y@CCR2@CCR4\n\r` |
| `>` | 上位机→飞控 | jump2ISP 进 Bootloader | "Running bootloader..." 后断连 |
| 任意 | 飞控→上位机 | — | 数值帧 '@' 分隔 |

## 6. 已记录问题（不改代码）

1. **控制循环在中断里**（64~66 行）：1~2ms I2C + 3.5ms 阻塞发送都在 ISR，
   阻塞其它中断、栈深难控。工程化应搬进主循环/定时器，ISR 只置标志。
2. **ORE 溢出未处理**：请求快于处理速度时 SR.ORE 置位丢字符，代码未清。
3. `uart_short2char` 负数输出错误（见 4.4），`uart_int2char` 同款逻辑同款限制。
4. `uart_Float2Char` 无四舍五入，第 4 位小数有系统性偏差。
5. `case '>'` 依赖 jump2ISP 不返回——若日后返回会 fall-through（当前无实害）。
6. `USART1->SR;` 空读（52 行）清 TC/TXE 的写法是旧固件库时代的痕迹。

## 7. 自查命令

```bash
cd references/firmware/Avem
grep -n "USART1_IRQHandler\|uart_init\|uart_Float2Char\|uart_short2char\|TOKEN" libs/module/avm_uart.c libs/module/avm_uart.h
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_uart.c
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_uart.h
```

延伸阅读：Docs `07-链路通信与调参指南.md`（协议图 + 调参落地步骤 + 频率约束推导）。
