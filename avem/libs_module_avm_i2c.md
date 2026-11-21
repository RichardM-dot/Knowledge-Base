# libs/module/avm_i2c.c（含 avm_i2c.h 宏）—— 软件 I2C 总线

> 对应源文件：
> - `references/firmware/Avem/libs/module/avm_i2c.c`（共 145 行，A级深注）
> - `references/firmware/Avem/libs/module/avm_i2c.h`（共 32 行，**未列入注释范围，未加源码注释**，本条目文字覆盖其宏定义）
> - `references/firmware/Avem/libs/module/avm_bit.h`（共 7 行，未列入注释范围，位带公式 2 行，随本条目说明）
> 互链：[libs_module_avm_mpu6050.md](libs_module_avm_mpu6050.md)（唯一用户）｜
> [libs_module_avm_core.md](libs_module_avm_core.md)（模块池[2]）｜Docs：`Avem/Docs/03-MPU6050与软件I2C.md`

## 1. 覆盖范围说明

avm_i2c.h 不在主控注释清单内（清单只列 avm_i2c.c），故头文件无源码注释，
其宏定义在本条目 2.2 用表格覆盖。avm_bit.h 同理（7 行，2 个宏）。

## 2. 头文件宏表（avm_i2c.h 32 行 + avm_bit.h 7 行）

### 2.1 avm_i2c.h

| 宏 | 行号 | 值/定义 | 说明 |
| --- | --- | --- | --- |
| `SCL_PINNUM` | 5 | 15 | **SCL=PB15** |
| `SDA_PINNUM` | 6 | 14 | **SDA=PB14** |
| `SCL` | 7 | `BIT_ADDR(&(GPIOB->ODR), 15)` | 位带写 ODR bit15 |
| `SDA` | 8 | `BIT_ADDR(&(GPIOB->ODR), 14)` | 位带写 ODR bit14 |
| `READ_SDA` | 12 | `BIT_ADDR(&(GPIOB->IDR), 14)` | 位带读 IDR bit14 |
| `IIC_DELAY()` | 14 | `delay_custom(1)` | 1µs 拍（静态函数，见 3.1） |
| 函数声明 | 21~28 | IIC_init/Start/Stop/Ack/Wait_Ack/Send/Read | 7 个原语 |

### 2.2 avm_bit.h 位带公式（4~5 行，逐字）

```c
#define BITBAND(addr, bitnum) (((addr) & 0xF0000000) + 0x2000000 + (((addr) & 0xFFFFF) << 5) + ((bitnum)<<2))
#define BIT_ADDR(addr, bitnum) *((volatile unsigned long *)(BITBAND((unsigned int)(addr), bitnum)))
```

Cortex-M3 位带别名换算：`别名地址 = 区基址 + 0x02000000 + 字节偏移×32 + 位号×4`。
写这个 32 位字 = 原子写那一位。以 SDA 为例：ODR=0x40010C0C、bit14 →
别名地址 = 0x40000000+0x2000000+(0xC0C<<5)+(14<<2) = 0x42018358。

## 3. avm_i2c.c 逐函数表（145 行）

| 函数 | 行号 | 签名 | 职责 | 备注 |
| --- | --- | --- | --- | --- |
| `avm_i2c_module_st` | 23~29 | 模块实例 | 模块池[2] | init 即 IIC_init 包装 |
| `avm_i2c_init` | 31~34 | `unsigned char (void*)` | 调 IIC_init | 注意**非 static**（与其它模块不一致，见 6.2） |
| `delay_custom` | 37~39 | `static void (volatile unsigned int)` | 4 次空转 ≈1µs | I2C 节拍源 |
| `IIC_init` | 42~58 | `void (void)` | 开时钟+PB14/15 开漏+释放总线 | 49~52 行作者讲开漏妙处 |
| `IIC_Start` | 61~68 | `void (void)` | SCL 高时 SDA 下降沿 | 起始信号 |
| `IIC_Stop` | 71~80 | `void (void)` | SCL 高时 SDA 上升沿 | 停止信号 |
| `IIC_Ack` | 83~92 | `void (unsigned char)` | 主机发 ACK(0)/NAK(1) | 读收尾用 |
| `IIC_Wait_Ack` | 97~112 | `unsigned char (void)` | 等从机第 9 拍应答 | 1=ACK，0=无应答 |
| `IIC_Send` | 116~128 | `void (unsigned char)` | MSB first 发 8 位 | SCL 低放位、高采样 |
| `IIC_Read` | 132~145 | `unsigned char (void)` | MSB first 收 8 位 | 先释放 SDA |

## 4. 关键摘录

### 4.1 总线初始化与开漏论述（42~58 行，逐字）

```c
void IIC_init() {
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;       //GPIOB enable
    GPIOB->CRH &= 0x00FFFFFF;   //reset B13 & B15
    GPIOB->CRH |= 0x77000000;   //Set B13 & B15 b0111
    SDA = 1;
    SCL = 1;

//这里B14和B15设置为开漏输出模式(01), 其实一般都用推挽输出模式。
//开漏输出模式有个"好的Bug": 在输出状态下，输入信号没有关断，后面的路也是畅通的
//最终就会进入输入寄存器，导致在输出状态下，输入功能保持正常。
//如果用推挽输出的话，IIC通讯时每次读取SDA之前都要配置SDA为输入，才能读取IDR
```

注意两套口径（已核实）：代码 `0x77000000` 实配 **PB14/PB15**（CRH bits[27:24]
=pin14、bits[31:28]=pin15，0x7=开漏 50MHz），与宏 SCL=PB15/SDA=PB14 自洽；
行内旧注释"reset B13 & B15"与 49 行"B14和B15"并存，以代码为准。
开漏的价值：输出态仍能读 IDR——主机随时能检查总线（含从机 ACK），
不必来回切换输入输出方向。

### 4.2 Start/Stop（61~80 行，逐字）

```c
void IIC_Start() {
    SDA = 1;      /* 先确保两线都为高（空闲态起点） */
    SCL = 1;
    IIC_DELAY();
    SDA = 0;      /* SDA 先拉低 —— Start 的标志动作 */
    IIC_DELAY();
    SCL = 0;      /* SCL 再拉低，总线进入"数据传输预备"状态 */
}
```

Start = SCL 高电平期间 SDA 制造下降沿；Stop 反之（71~80 行：SCL=0 → SDA=0
→ SCL=1 → SDA=1）。每步之间 `IIC_DELAY()` 是时序成立的关键——**软件 I2C
的全部"协议"都藏在这拍子里**。

### 4.3 等应答（97~112 行，逐字）

```c
unsigned char IIC_Wait_Ack() {
    SCL = 0;
    IIC_DELAY();
    SDA = 1;      /* 释放 SDA（交给从机控制） */
    IIC_DELAY();
    SCL = 1;      /* 第 9 个时钟 */
    IIC_DELAY();
    if(READ_SDA) {         /* SDA 为高 = 从机没有拉低 = 无 ACK */
        SCL = 0;
        IIC_DELAY();
        return 0;
    }

    SCL = 0;      /* 收到 ACK，拉低 SCL 结束第 9 个时钟 */
    return 1;
}
```

调用约定：`avm_mpu6050.c` 52~53 行用 `if(!IIC_Wait_Ack()) LED1 = 1;`——
**返回 0 表示无应答**（注意与直觉相反：函数名是 Wait_Ack，返回的却是
"是否失败"语义的编码）。这是全模块唯一向外暴露的故障信号。

### 4.4 发送一字节（116~128 行，逐字）

```c
void IIC_Send(unsigned char dat) {
    unsigned char i;
    SCL = 0;
    for(i = 0; i < 8; i++) {
        IIC_DELAY();
        SDA = (dat&0x80)>>7;   /* 取最高位放到 SDA */
        dat <<= 1;             /* 左移，准备下一位 */
        IIC_DELAY();
        SCL = 1;               /* SCL 高：从机采样当前位 */
        IIC_DELAY();
        SCL = 0;               /* SCL 低：允许换下一位 */
    }
}
```

## 5. 时序量化

- `delay_custom(n)`：`for(nus*=4; nus; nus--)` 空转，-O0 下每次循环约
  十几条指令，4 次约 1µs@72MHz（近似值，实际受编译优化影响）。
- 一次 `IIC_Send`：8 位 × 3 拍 ≈ 24µs；一次 `MPU_Sigle_Read` 事务约
  9 字节 ≈ 200µs 量级；六轴 12 次事务 ≈ 1~2ms——**读数是控制环的最大
  时间开销**（见 Docs/07 陷阱：限制上位机请求频率 ≤200Hz）。
- SCL 周期 ≈ 2~3µs（300~500kHz），在 MPU6050 的 400kHz 快速模式上限附近。

## 6. 已记录问题（不改代码）

1. **行内旧注释口径**：44~45 行"reset B13 & B15"与代码实配 PB14/15 不符
   （0x77000000 = pin14+pin15），以代码与逻辑分析仪为准。
2. `avm_i2c_init` 没有 `static`：其它模块的 init 包装都是 static，此文件
   漏写（符号 avm_i2c_init 全局可见，无实害但风格不一致）。
3. 速率受编译优化影响：`delay_custom` 是空循环，-O3 下可能被优化得更短
   （volatile 防止完全删除），换优化等级后 I2C 速度会漂。
4. 无总线超时：SDA 被从机意外钳低时，Start 永远起不来（死等），只能复位。

## 7. 自查命令

```bash
cd references/firmware/Avem
grep -n "IIC_Start\|IIC_Stop\|IIC_Wait_Ack\|IIC_Send\|IIC_Read\|IIC_init\|delay_custom" libs/module/avm_i2c.c
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_i2c.c
```

延伸阅读：Docs `03-MPU6050与软件I2C.md`（位带推导 + sequenceDiagram 全事务时序）。
