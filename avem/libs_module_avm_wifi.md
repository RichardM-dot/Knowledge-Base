# libs/module/avm_wifi.c 与 avm_wifi.h —— Wi-Fi 数传（预留模块）

> 对应源文件：
> - `references/firmware/Avem/libs/module/avm_wifi.c`（共 62 行，B级：文件头导读已完成）
> - `references/firmware/Avem/libs/module/avm_wifi.h`（共 22 行，B级：文件头导读已完成；合并覆盖）
> 互链：[libs_module_avm_uart.md](libs_module_avm_uart.md)（同款 USART 驱动套路对照）｜
> [libs_module_avm_core.md](libs_module_avm_core.md)（模块池——本模块未注册）｜Docs：`Avem/Docs/07-链路通信与调参指南.md`

## 1. 文件定位（B级导读浓缩）

给飞控预留的 ESP8266 AT 指令数传接口：USART3（PB10=TX/PB11=RX）驱动 +
AT 配置序列（AP 模式 + TCP 服务）。**未挂入 avm_modules_pool，当前工程
不初始化**——是"扩展练习题"而非可用功能。驱动代码与 avm_uart.c 同构，
是学习"同一套路在不同总线时钟下移植"的对照样本。

## 2. avm_wifi.h 逐项表（22 行）

| 元素 | 行号 | 说明 |
| --- | --- | --- |
| 文件头导读块 | 3~11 | 职责 + WIFI_PCLK2=36 的时钟陷阱说明 |
| `WIFI_PCLK2` | 12~13 | **36**（USART3 挂 APB1，36MHz；非 USART1 的 72） |
| `WIFI_BOUND` | 14 | 115200 |
| 函数声明 | 16~20 | init/sendSingleByte/sendData/sendCmd/Config |
| `extern void delay` | 21 | 依赖的旧版延时（avm_mpu6050.h 也有同名声明） |

## 3. avm_wifi.c 逐函数表（62 行）

| 函数 | 行号 | 签名 | 职责 |
| --- | --- | --- | --- |
| `wifi_init` | 10~36 | `void (void)` | USART3 波特率/GPIO/复位/中断配置（复刻 uart_init） |
| `wifi_sendSingleByte` | 38~41 | `void (unsigned char)` | 写 DR 死等 TC |
| `wifi_sendData` | 43~46 | `void (char*)` | 发字符串 |
| `wifi_sendCmd` | 48~52 | `void (char*)` | 发字符串 + CRLF（AT 指令格式） |
| `wifi_Config` | 54~62 | `void (void)` | init + 3 条 AT 指令建立 TCP 服务 |

## 4. 关键摘录

### 4.1 波特率基准的时钟陷阱（10~18 行，逐字）

```c
void wifi_init() {
    float temp;
    unsigned short mantissa;
    unsigned short fraction;
    temp = (float)(WIFI_PCLK2 * 1000000) / (WIFI_BOUND * 16);
    mantissa = temp;
    fraction = (temp - mantissa) * 16;
    mantissa <<= 4;
    mantissa += fraction;
```

与 `uart_init`（avm_uart.c 31 行）逐行同构，唯一差别是基准时钟：
USART1 挂 APB2（72MHz），USART3 挂 **APB1（36MHz）**。填 72 会得到
双倍波特率——**抄外设配置代码前先核对外设挂在哪条总线**（本条目最重要的
教训）。RCC 行（20~21 行）也印证：`IOPBEN`（APB2）+ `USART3EN`（APB1）。

### 4.2 AT 配置序列（54~62 行，逐字）

```c
void wifi_Config() {
    wifi_init();
    wifi_sendCmd("AT+CWMODE=2");
    delay(50);
    wifi_sendCmd("AT+CIPMUX=1");
    delay(50);
    wifi_sendCmd("AT+CIPSERVER=1,80");
    delay(50);
}
```

三条 AT 指令：`CWMODE=2`（ESP8266 做 AP）→ `CIPMUX=1`（允许多连接）→
`CIPSERVER=1,80`（开 TCP 服务，端口 80）。之后手机/电脑连上该热点、
向 192.168.4.1:80 发数据即数传。`delay(50)` 是 `avm_wifi.h` 21 行声明的
**另一个 delay**（与 avm_core.c 的 delay_ms 不同名不同体，注意区分），
给 ESP8266 处理指令的间隙。

### 4.3 发送原语（38~52 行，逐字）

```c
void wifi_sendSingleByte(unsigned char cmd) {
    USART3->DR = cmd;
    while((USART3->SR & 0x40) == 0);
}

void wifi_sendData(char *cmd) {
	while(*cmd)
		wifi_sendSingleByte(*cmd++);
}

void wifi_sendCmd(char *cmd) {
    wifi_sendData(cmd);
    wifi_sendSingleByte(0x0D);
    wifi_sendSingleByte(0x0A);
}
```

与 avm_uart.c 93~96 行（USART1 版）同款阻塞发送：写 DR、死等 SR bit6(TC)。

## 5. 接入系统的方法（扩展路径）

1. 在 `avm_wifi.c` 定义 `avm_module_t avm_wifi_module_st = {0, NULL, wifi_Config 包装, NULL, NULL};`
2. 模式配置收进包装函数（模块池签名是 `unsigned char (*)(void*)`）
3. `avm_core.c` 76~83 行的池数组插入 `&avm_wifi_module_st`（NULL 之前）
4. 仿照 USART1_IRQHandler 写 USART3_IRQHandler 解析 TCP 透传数据（协议可
   复用 '$'/命令字设计）

## 6. 已记录问题（不改代码）

1. `wifi_init` 里使能了 RXNE 中断（32~33 行 `CR1 |= 1<<5`）但**没有写
   USART3_IRQHandler、也没有 NVIC_EnableIRQ(USART3_IRQn)**——中断向量落
   Default_Handler 死循环，若真收到数据会整机卡死。启用本模块前必须补
   中断处理（或删掉这两行）。
2. `delay(50)` 依赖的头文件声明散落（avm_wifi.h 20 行），与 delay_ms 体系
   并存易混淆。
3. GPIO 配置（23~24 行）复刻 uart_init 的 0x8B0 魔数套路，PB10/PB11 各 4 位。
4. 无 AT 响应解析：sendCmd 后不读回包，配置失败无感知。

## 7. 自查命令

```bash
cd references/firmware/Avem
grep -n "wifi_init\|wifi_Config\|wifi_sendCmd\|USART3" libs/module/avm_wifi.c libs/module/avm_wifi.h
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_wifi.c
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_wifi.h
```

延伸阅读：Docs `07-链路通信与调参指南.md` 第 5 节（本模块的定位）；
对照读 [libs_module_avm_uart.md](libs_module_avm_uart.md) 看同构差异。
