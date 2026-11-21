# libs/module/avm_pid.c 与 avm_pid.h —— 单轴串级 PID

> 对应源文件：
> - `references/firmware/Avem/libs/module/avm_pid.c`（共 104 行，A级深注）
> - `references/firmware/Avem/libs/module/avm_pid.h`（共 69 行，A级深注；合并覆盖）
> 互链：[libs_module_avm_mpu6050.md](libs_module_avm_mpu6050.md)（Feedback 数据源）｜
> [libs_module_avm_motor.md](libs_module_avm_motor.md)（Channel 输出端）｜
> [libs_module_avm_uart.md](libs_module_avm_uart.md)（调用点）｜Docs：`Avem/Docs/05-串级PID.md`

## 1. 覆盖范围说明

.h（参数宏 + 结构体）与 .c（唯一算法函数）一体成篇。这是全飞控的灵魂文件：
**角度外环 PID + 角速度内环 PD**，输出差动叠加到对角两电机。

## 2. avm_pid.h 逐项表（69 行）

| 元素 | 行号 | 说明 |
| --- | --- | --- |
| 文件头导读 | 3~17 | 参数分区说明 + KI=0 设计意图 |
| 外环参数 | 19~22 | `OUTTER_LOOP_KP/KI/KD` = 3 / 1 / 0.3f |
| 内环参数 | 24~27 | `INNER_LOOP_KP/KI/KD` = 1 / 0 / 0.3f |
| `SUM_ERRO_MAX/MIN` | 29~32 | ±900，**旧版遗留，当前未引用** |
| `PID_IMAX/IMIN` | 34~37 | ±30，外环积分限幅（生效中） |
| `pid_st` 结构体 | 39~59 | 单轴运行时状态（11 字段） |
| `extern avm_pid` | 61~62 | 全局唯一实例（Roll 轴） |
| 函数声明 | 64~67 | `pid_SingleAxis` + 预留 `pid()` |

`pid_st` 字段速查（47~59 行定义）：

| 字段 | 类型 | 语义 |
| --- | --- | --- |
| `InnerLast` / `OutterLast` | float | 上周期角速度 / 角度（D 项差分记忆） |
| `Feedback` | float* | → `g_Roll`（实际角度） |
| `Gyro` | float* | → `avm_euler.gX`（°/s） |
| `Error/p/i/d` | float | 中间量；p 复用为内环比例项 |
| `output` | short | 外环阶段=期望角速度；内环阶段=PWM 增量 |
| `Channel1/2` | __IO uint16_t* | → `TIM3->CCR2 / CCR4` |

## 3. avm_pid.c 逐函数表（104 行）

| 函数/对象 | 行号 | 职责 |
| --- | --- | --- |
| 被注释的旧版字段说明 | 26~36 | 作者从全局变量进化到结构体的痕迹 |
| `avm_pid` 实例 | 42~49 | 指定初始化器绑定 Feedback/Channel/Gyro |
| `pid_SingleAxis` | 56~104 | 单轴串级 PID 一步（全文唯一函数） |

## 4. 关键摘录（三段式）

### 4.1 外环（56~73 行，逐字）

```c
void pid_SingleAxis(pid_pst temp, float setPoint) {
    /* ---- 外环（角度环）---- */
    temp->Error = *temp->Feedback - setPoint;      // 误差 = 实际角度 - 目标角度
//Outter Loop PID
    /* 积分累积误差，并做积分限幅（anti-windup 抗饱和）：
     * 若不限幅，起飞前长时间有静差会让积分爆表，起飞瞬间猛烈修正（俗称积分饱和） */
    temp->i += temp->Error;
    if (temp->i > PID_IMAX)
		temp->i = PID_IMAX;
    else if (temp->i < PID_IMIN)
		temp->i = PID_IMIN;

    /* 微分项用"本次角度 - 上次角度"的后向差分近似（不用真实导数，抗噪声） */
    temp->d = *temp->Feedback - temp->OutterLast;

    /* 外环输出 = 期望的角速度修正量（Kp/Ki/Kd 是外环三参数） */
    temp->output = (short)(OUTTER_LOOP_KP * (temp->Error) + OUTTER_LOOP_KI * temp->i + OUTTER_LOOP_KD * temp->d);
    temp->OutterLast = *temp->Feedback; //Save Old Data
```

### 4.2 内环（75~82 行，逐字）

```c
//Inner Loop PD
    /* ---- 内环（角速度环，纯 PD）---- */
    /* p 这里语义是"期望角速度 + 实测角速度 = 角速度误差"（作者复用变量名，注意别混淆）：
     * 期望角速度即外环输出 output，实测角速度取负号即 +Gyro 的效果 */
    temp->p = temp->output + *temp->Gyro;
    /* 内环 D：角速度的后向差分（本次 - 上次） */
    temp->d = *temp->Gyro - temp->InnerLast;
    /* 内环输出 = 最终 PWM 增量 */
    temp->output = (short)(INNER_LOOP_KP * temp->p + INNER_LOOP_KD * temp->d);
```

**易错点**：`p = output + Gyro` 的 `+` 是"误差=期望-实际"整体取反的写法，
与 4.3 的 Channel2 用 `-` 配对后闭环方向才正确。读码别把 `p` 当增益。

### 4.3 差动混控（84~103 行，逐字）

```c
    /* ---- 差动混控与限幅 ----
     * 通道1 加 output、通道2 减 output：同轴上两颗电机反向增减即产生滚转力矩。
     * 两次限幅把占空比钉死在 [THROTTLE_MIN, THROTTLE_MAX]，
     * 防止输出越界导致电机满转/停转失控。 */
    if (*temp->Channel1+temp->output > THROTTLE_MAX)
		*temp->Channel1 = THROTTLE_MAX;
    else if (*temp->Channel1+temp->output < THROTTLE_MIN)
		*temp->Channel1 = THROTTLE_MIN;
    else
		*temp->Channel1 += (short)temp->output;

    if (*temp->Channel2-temp->output > THROTTLE_MAX)
		*temp->Channel2 = THROTTLE_MAX;
    else if
		(*temp->Channel2-temp->output < THROTTLE_MIN) *temp->Channel2 = THROTTLE_MIN;
    else
		*temp->Channel2 -= (short)temp->output;

    /* 保存本次角速度，供下一周期内环 D 项做差分 */
    temp->InnerLast = *temp->Gyro;
```

混控公式：`CCR2 = clamp(CCR2+out, 1620, 3600)`，`CCR4 = clamp(CCR4-out, 1620, 3600)`。
96~98 行 `else if` 的换行是原作者格式痕迹（条件在下一行），逻辑对称。

## 5. 参数整定速查（docs/README.md 作者长文的浓缩）

| 步骤 | 动什么 | 判据 |
| --- | --- | --- |
| 1 先调内环（去外环） | `INNER_LOOP_KP` | 打舵跟手、回中略回弹 |
| 2 | `INNER_LOOP_KD` | 回中震荡立刻停，但抓机不抽搐 |
| 3（可选） | `INNER_LOOP_KI` | 手感变柔，抗重力分量 |
| 4 再调外环 | `OUTTER_LOOP_KP` | 倾斜后快速回平不过冲 |
| 5 | `OUTTER_LOOP_KI` | 静置偏角被慢慢磨平 |

注意：本工程 setPoint 恒 0（avm_uart.c 66 行），且每改宏需重编译。

## 6. 已记录问题（不改代码）

1. `output`（short）本身无限幅，极端增益组合下可能溢出回卷；CCR 限幅兜底。
2. `i` 无落地/解锁复位：炸机重上电后积分残留，修正方向可能与直觉相反。
3. 只有 Roll 轴实例；Pitch/Yaw 需自行加实例（结构体已支持）。
4. `SUM_ERRO_MAX/MIN` 是死参数（未引用），勿误调。
5. 单位混用：角度误差是"度"，输出是"CCR 计数"，参数量纲由作者配套——
   改采样频率或传感器量程需整体重调。

## 7. 自查命令

```bash
cd references/firmware/Avem
grep -n "pid_SingleAxis\|OUTTER_LOOP\|INNER_LOOP\|PID_IMAX" libs/module/avm_pid.c libs/module/avm_pid.h
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_pid.c
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_pid.h
```

延伸阅读：Docs `05-串级PID.md`（含 classDiagram/sequenceDiagram 与调参落地版）。
