# libs/module/avm_motor.c 与 avm_motor.h —— TIM3 PWM 电机输出

> 对应源文件：
> - `references/firmware/Avem/libs/module/avm_motor.c`（共 92 行，A级深注）
> - `references/firmware/Avem/libs/module/avm_motor.h`（共 76 行，A级深注；合并覆盖）
> 互链：[libs_module_avm_pid.md](libs_module_avm_pid.md)（写入 CCR 的一方）｜
> [libs_module_avm_core.md](libs_module_avm_core.md)（delay_ms 依赖）｜Docs：`Avem/Docs/06-电机PWM混控.md`

## 1. 覆盖范围说明

.c 是标准库外的**纯寄存器级 TIM3 PWM 配置**（学习"定时器 PWM 从零配置"
的最小完整示例），.h 是通道宏/油门界限/上电序列宏。二者一体成篇。

## 2. avm_motor.h 逐项表（76 行）

| 元素 | 行号 | 说明 |
| --- | --- | --- |
| 文件头导读 | 3~18 | 宏地图 + 数据流位置 |
| `MOTOR_NORMAL_STARTUP` | 22~23 | 上电序列模式开关 |
| `MOTOR1~MOTOR4` | 26~31 | = `TIM3->CCR1~CCR4`；引脚 PA6/PA7/PB0/PB1 |
| `MOTOR_ALL` | 33~34 | 四路连续赋值宏 |
| `THROTTLE_MAX/MIN/MID` | 36~41 | 3600/1620/1650 计数 = 2ms/0.9ms/0.917ms |
| `MOTOR_SETTING()` | 43~67 | 上电油门序列（**多行 \ 宏，禁插行内注释**） |
| `motor_PWM_Init` 声明 | 70~72 | 含频率公式注释 |

⚠️ 本文件的红线案例：`MOTOR_SETTING` 是 6 行 `\` 续行宏，**任何在宏体内
插入注释的行为都会改变宏展开**（strip 校验也会失败）。本工程注释在宏体
外（43~51 行说明块）。

## 3. avm_motor.c 逐函数表（92 行）

| 函数 | 行号 | 签名 | 职责 |
| --- | --- | --- | --- |
| `avm_motor_module_st` | 26~32 | 模块实例 | 模块池[3]注册 |
| `avm_motor_init` | 34~37 | `static unsigned char (void*)` | 执行 `MOTOR_SETTING()` |
| `motor_PWM_Init` | 46~92 | `void (unsigned short, unsigned short)` | TIM3 四通道 PWM 全套寄存器配置 |

## 4. 关键摘录

### 4.1 PWM 频率推导（数值账本）

```
入参: motor_PWM_Init(36000, 40)
ARR = 36000-1 = 35999, PSC = 40-1 = 39   （68~69 行，写"实际值-1"）
计数时钟 = 72MHz / (39+1) = 1.8MHz
PWM 频率 = 1.8MHz / (35999+1) = 50Hz（周期 20ms）
高电平   = CCR / 1.8MHz
  THROTTLE_MAX 3600 → 2ms   （电调满油门）
  THROTTLE_MIN 1620 → 0.9ms （电调怠速/最低）
  THROTTLE_MID 1650 → ~0.92ms（待命位）
```

### 4.2 寄存器配置六步（46~92 行摘要）

| 步骤 | 行号 | 写入 | 量化含义 |
| --- | --- | --- | --- |
| 1 开时钟 | 51~53 | APB1ENR.TIM3EN、APB2ENR.IOPAEN/IOPBEN | TIM3 挂 APB1、GPIOA/B 挂 APB2 |
| 2 GPIO | 57~65 | CRL 0xBB000000 / 0x000000BB | 0xB=复用推挽 50MHz；ODR 预置高 |
| 3 时基 | 68~69 | ARR=35999、PSC=39 | 50Hz / 36000 级分辨率 |
| 4 通道模式 | 73~80 | CCMR1/2：`6<<4`、`1<<3`… | OCxM=110 PWM 模式1；OCxPE=1 预装载 |
| 5 通道使能 | 83~86 | CCER bit0/4/8/12 | CC1E~CC4E 接通引脚 |
| 6 启动 | 89~90 | CR1=0x80 再 |=1 | ARPE=1（ARR 预装载）+ CEN=1 |

**预装载（OCxPE）的意义**：PID 每周期写 CCR，预装载让新值在下个周期边界
原子生效，避免正在输出的波形被撕裂（超宽脉冲电调会误判）。
末行注释（91 行）提醒：TIM1/TIM8 高级定时器还需 BDTR.MOE，TIM3 不用。

### 4.3 上电序列宏（avm_motor.h 52~67 行，逐字）

```c
#ifdef MOTOR_NORMAL_STARTUP
    #define MOTOR_SETTING() do {\
        motor_PWM_Init(36000,40);\
		MOTOR_ALL = THROTTLE_MIN;\
        delay_ms(4000);\
		MOTOR_ALL = THROTTLE_MID;\
    } while(0)
#else
    #define MOTOR_SETTING() do {\
        motor_PWM_Init(36000,40);\
		MOTOR_ALL = THROTTLE_MAX;\
        delay_ms(3000);\
		MOTOR_ALL = THROTTLE_MIN;\
        delay_ms(8000);\
    } while(0)
#endif
```

当前分支：PWM 就绪 → 四路 MIN → 等 4s（电调自检"哔哔"）→ MID 待命。
备用分支兼容"满油门上电做行程校准"的电调。**注意 delay_ms(4000) 超出
SysTick 24 位上限回卷为约 0.27s**（推导见 [libs_module_avm_core.md](libs_module_avm_core.md) 4.2）。

## 5. 混控消费端（衔接 avm_pid.c 88~100 行）

```
CCR(MOTOR2) = clamp(CCR2 + output, MIN, MAX)
CCR(MOTOR4) = clamp(CCR4 - output, MIN, MAX)
```

对角差动 → 滚转力矩，总升力近似不变。完整四轴混控矩阵（Roll/Pitch/Yaw/
Thrust 四分量叠加）是本模块的扩展练习方向。

## 6. 已记录问题（不改代码）

1. **PWM 频率注释勘误**：avm_motor.c 44 行示例注释"arr=100, psc=144 → 500Hz"
   按公式实为 5000Hz；本工程实际配置是 50Hz。以公式 `72MHz/((arr+1)*(psc+1))` 为准。
2. `MOTOR_SETTING` 的 4s 等待实际约 0.27s（delay_ms 回卷），电调自检可能不完整。
3. `THROTTLE_MID=1650` 与 MIN 仅差 30 计数（≈17µs），"待命位"几乎贴地。
4. GPIO 配置用魔数 0xBB 而非 CMSIS 位定义（可用 `GPIO_CRL_MODE7_1` 等改写，
   仅作学习笔记）。
5. 四路电机只有两路被 PID 使用（MOTOR2/4）；MOTOR1/3（CCR1/CCR3）配置好但
   恒为上电序列写入的值。

## 7. 自查命令

```bash
cd references/firmware/Avem
grep -n "motor_PWM_Init\|MOTOR_SETTING\|THROTTLE" libs/module/avm_motor.c libs/module/avm_motor.h
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_motor.c
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_motor.h
```

延伸阅读：Docs `06-电机PWM混控.md`（寄存器位段账本 + 混控推导 + 示波器验收点）。
