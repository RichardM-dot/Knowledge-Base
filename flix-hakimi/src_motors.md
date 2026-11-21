# src_motors.md —— motors.ino 电机 PWM 输出

> **对应源文件：`references/firmware/flix-hakimi-pio/src/motors.ino`（共 122 行）**
> LEDC 78 kHz/10bit PWM 直驱 MOSFET；motors[4] 是控制链的最终输出。
> 延伸阅读：[src_control.md](src_control.md)（混控写入者）、Docs《03-串级PID控制与电机混控》§6

## 宏与全局量

| 行号 | 名称 | 值 | 说明 |
|---|---|---|---|
| 32-35 | MOTOR_0..3_PIN | GPIO12/13/14/15 | 0=左后 1=右后 2=右前 3=左前（X 布局） |
| 37-38 | PWM_FREQUENCY/RESOLUTION | 78000/10 | 周期 12.82 µs，占空比 0~1023 |
| 39-44 | PWM_STOP/MIN/MAX | 0/0/1000000/78000 | MAX≈12.8 µs = 恰一个周期（写成"脉宽"形式为兼容 ESC） |
| 46-50 | `motors[4]` | float[4] | 归一化推力 0~1；混控/MAVLink 直写/testMotor 都会写 |
| 53-56 | MOTOR_REAR_LEFT 等常量 | 0~3 | 混控公式用（control.ino extern 引用） |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 59-71 | `setupMotors` | 4×ledcAttach + 立即输出全零（上电保证停转） |
| 74-87 | `getDutyCycle` | NaN 硬闸 → constrain → mapf → 0 硬停 → 1023 满量程 |
| 90-95 | `sendMotors` | 每圈把 motors[4] 写成 PWM |
| 98-100 | `motorsActive` | 任一非 0 即真（含 0.1 怠速）——落地判定与落盘保护复用 |
| 104-122 | `testMotor` | 单电机满推 3 秒（已解锁拒绝；测试前卸桨） |

## 关键摘录：getDutyCycle 的两道硬闸

```c
// motors.ino:74-87
int getDutyCycle(float value) {
	// ★ NaN/Inf 硬闸（最后一道防线）：constrain 是宏，NaN 两个比较都为假、原样放行，
	// round(NaN) 转 int 是未定义行为，可能把任意占空比写进硬件。宁可停转，
	// 也不输出未定义的 PWM。（上游注入源都已整帧拒绝，这里兜住漏网的。）
	if (invalid(value)) value = 0;
	value = constrain(value, 0, 1);
	float pwm = mapf(value, 0, 1, PWM_MIN, PWM_MAX); // 推力 → 脉宽（ESC 场景下）或占空比
	if (value == 0) pwm = PWM_STOP; // 推力 0 一律停转
	float duty = mapf(pwm, 0, 1000000 / PWM_FREQUENCY, 0, (1 << PWM_RESOLUTION) - 1);
	return round(duty);
}
```

两次 mapf 相互抵消 ⇒ 净效果 `duty ≈ 1023·u`（线性无死区）；中间的"脉宽"形式让同一函数在改宏后变成 ESC 协议输出（400 Hz + 1000/2000 µs）。

## 78 kHz 的理由（文件头 22-23）

- 远超可闻上限 20 kHz：空心杯直驱 MOSFET 无啸叫；
- 远高于电机电气时间常数：占空比近视为连续调压。

## 陷阱

- `motorsActive()` 含 0.1 怠速 ⇒ "解锁怠速时不算落地"——影响 landed 与参数落盘。
- `testMotor` 里 `delay(50)`（毫秒，等 LEDC 周期结束更新占空比，ESP32 特性）与 `pause(3)`（秒，本工程函数且保持系统响应）单位不同（116-117）。
- MAVLink 的 SET_ACTUATOR_CONTROL_TARGET 会绕过混控直写 motors[]——依赖 controlTorque 的"力矩无效 return"保证不再被覆盖（见 control.ino:403-408 的死锁推演）。

## 关键摘录二：testMotor 与它的两道防线

```c
// motors.ino:104-122（节选）
void testMotor(int n) {
	if (armed) {
		print("Refusing motor test: drone is armed (disarm first)\n");
		return;
	}
	print("Testing motor %d\n", n);
	motors[n] = 1;
	delay(50); // ESP32 LEDC 需等当前 PWM 周期结束才更新占空比
	sendMotors();
	pause(3); // 满推 3 秒（pause 期间命令行仍可响应）
	motors[n] = 0;
	sendMotors();
	print("Done\n");
}
```

两道防线：已解锁拒绝（此命令能经 MAVLink shell 远程触发，飞行中误发 = 给一个电机满推 3 秒）；CLI 侧 mfr/mfl/mrr/mrl 还要过 commandBlockedWhileArmed（其实 armed 检查已覆盖，双保险）。**测试前务必卸桨**。

## ESC 迁移表（空心杯 MOSFET → 无刷电调）

| 宏 | 直驱值 | ESC 值 | 效果 |
|---|---|---|---|
| PWM_FREQUENCY | 78000 | 400 | 标准舵机/电调刷新率 |
| PWM_MIN | 0 | 1000 | 1.0 ms = 停转脉宽 |
| PWM_MAX | 1000000/78000 | 2000 | 2.0 ms = 满油门脉宽 |

getDutyCycle 的两次 mapf 结构不变：value→脉宽(µs)→占空比计数值。u==0 仍走 PWM_STOP——对 ESC 而言应改理解为"输出停转脉宽"。

## 上电安全时序

setupMotors 在 setup 里排第 6 位（flix.ino:76），且第 69 行 `sendMotors()` 立即输出全零——**上电到第一次输出之间电机是停的**。这是"任何初始化失败都不该导致桨乱转"链条的最后一环（前几环：controlTorque 的 !armed 分支每圈 memset、getDutyCycle 的 NaN 硬闸）。
