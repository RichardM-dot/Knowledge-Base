# src_gpio —— GPIO 端口时钟

> 对应源文件：`src/gpio.c`（共 58 行；配套 `src/gpio.h` 共 59 行，不在注释清单内、已有注释）
> 注释级别：B 级（文件头导读块），strip_equiv 校验 PASS
> 工程路径：`/Users/theo-richard/workspace/references/firmware/Power-Meter`

**互链 Docs：** [01-启动流程与外设初始化](../../../references/firmware/Power-Meter/Docs/01-启动流程与外设初始化.md)（3.1 节）、[00-总览与硬件架构](../../../references/firmware/Power-Meter/Docs/00-总览与硬件架构.md)（2 节硬件拓扑）

---

## 1. 文件职责与位置

全工程没有独立用途的普通 GPIO：CAN/I2C/USART 引脚都在各自外设 MspInit 里配置，PD0/PD1 供 HSE 晶振使用（RCC 直接管）。所以 `MX_GPIO_Init` 只剩"开端口时钟"，为后续 MspInit 的引脚配置铺路。

---

## 2. 逐函数表

| 函数 | 行号 | 职责 |
|---|---|---|
| `MX_GPIO_Init` | 48-54 | 使能 GPIOD/GPIOA/GPIOB 三个端口时钟 |

`gpio.h` 仅声明该函数（:50），文件头（:3-7）说明"没有额外普通 IO 引脚"。

---

## 3. 关键代码摘录（gpio.c:48-54 全文）

```c
void MX_GPIO_Init(void)
{
  /* 使能各 GPIO 端口时钟 */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
}
```

三个端口各自的消费者（引脚在后续 MspInit 配置）：

| 端口 | 引脚 | 消费者 | 配置处 |
|---|---|---|---|
| GPIOD | PD0/PD1 | HSE 8 MHz 晶振 | RCC 硬件行为 |
| GPIOA | PA2/PA3 | USART2 TX/RX | usart.c:99-107 |
| GPIOA | PA11/PA12 | CAN1 RX/TX | can.c:106-114 |
| GPIOA | PA13/PA14 | SWD 调试口 | stm32f1xx_hal_msp.c:82（NOJTAG） |
| GPIOB | PB6/PB7 | I2C1 SCL/SDA | i2c.c:96-99 |

---

## 4. 已知问题（记录在案，不改码）

| # | 问题 | 位置 |
|---|---|---|
| 1 | 端口时钟全开但未列出消费清单，读码需跳转到各 MspInit | 文件头注释已部分补偿 |
| 2 | 若外设从未初始化（如屏蔽 CAN），GPIOA 时钟仍开着，多耗 ~mA 级电流 | :52 |

---

## 5. 修改指引

| 想改 | 位置 |
|---|---|
| 增加普通 IO（LED/按键） | 此函数加 GPIO_Init + 端口时钟已在 |
| 低功耗裁剪 | 删掉未用端口时钟行，确认对应外设也未用 |
