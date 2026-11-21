# src_control.md —— control.ino 控制核心与混控

> **对应源文件：`references/firmware/flix-hakimi-pio/src/control.ino`（共 505 行）**
> 串级 PID 四阶段 + desaturate 防饱和混控。输入 control*（rc/webui/mavlink），输出 motors[4]。
> 延伸阅读：[src_pid.md](src_pid.md)、[src_motors.md](src_motors.md)、[src_althold.md](src_althold.md)（油门改写者）、[src_safety.md](src_safety.md)、Docs《03-串级PID控制与电机混控》

## 全局状态

| 行号 | 变量 | 语义 |
|---|---|---|
| 143-148 | `RAW/ACRO/STAB/AUTO`、`mode=STAB`、`armed=false` | 模式常量与状态 |
| 151-159 | `attitudeTarget/ratesTarget/ratesExtra/torqueTarget/thrustTarget` | NaN = 该级跳过 |
| 163-171 | 6 个 PID 实例 + maxRate + tiltMax | 见 [src_pid.md](src_pid.md) |
| 183-184 | `trimRoll/trimPitch` | 软件配平（CTL_TRIM_*） |
| 202 | `mavlinkStickTime` | 定义在此而非 mavlink.ino：WiFi 关闭时仍可链接 |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 205-207 | `mavlinkSticksInControl` | 1 秒内收到过 MANUAL_CONTROL 即接管中 |
| 210-220 | `control` | 主入口：解释→失控→定高→交还→外环→内环→混控 |
| 225-285 | `interpretControls` | 模式解释、解锁手势、偏航死区、三模式目标生成 |
| 292-311 | `controlAttitude` | 外环：up 向量旋转向量误差 + 偏航欧拉角差 |
| 314-340 | `controlRates` | 内环：离地闸门 + 三轴 PID |
| 359-380 | `desaturate` | 混控防饱和：保总推力、等比缩力矩偏差 |
| 388-456 | `controlTorque` | NaN 停桨→未解锁停桨+清积分→怠速→混控→钳位 |
| 460-468 | `getModeName` | CLI 显示 |
| 476-505 | `printControlState` | `ctl` 命令：姿态/目标/角速度/力矩/积分/电机 |

## 关键摘录一：外环误差的几何（controlAttitude）

```c
// control.ino:292-311（节选）
	if (!armed || attitudeTarget.invalid() || thrustTarget < 0.1) return; // skip attitude control
	const Vector up(0, 0, 1); // 世界系"上"
	Vector upActual = Quaternion::rotateVector(up, attitude);
	Vector upTarget = Quaternion::rotateVector(up, attitudeTarget);
	Vector error = Vector::rotationVectorBetween(upTarget, upActual);
	ratesTarget.x = rollPID.update(error.x) + ratesExtra.x;
	ratesTarget.y = pitchPID.update(error.y) + ratesExtra.y;
	float yawError = wrapAngle(attitudeTarget.getYaw() - attitude.getYaw());
	ratesTarget.z = yawPID.update(yawError) + ratesExtra.z;
```

- up 向量只能表达横滚/俯仰误差——纯偏航不改变"上"，所以偏航单独用欧拉角差（最短角差）。
- 外环只有 P：P=6 意为"每弧度姿态误差给 6 rad/s 角速度指令"。负反馈方向已数值验证（299-304 注释）。

## 关键摘录二：desaturate（本工程相对上游 flix 的增强）

```c
// control.ino:360-379（核心）
	float avg = (a + b + c + d) * 0.25f;             // ≈ 总推力（力矩项两两抵消）
	float maxVal = max(max(a, b), max(c, d));
	float minVal = min(min(a, b), min(c, d));
	float scale = 1.0f;
	if (maxVal > 1.0f && maxVal > avg) {
		scale = min(scale, (1.0f - avg) / (maxVal - avg)); // 上界：最高电机压回 1
	}
	if (minVal < 0.0f && avg > minVal) {
		scale = min(scale, avg / (avg - minVal));          // 下界：最低电机抬回 0
	}
	if (scale < 1.0f) { a = avg + (a - avg) * scale; ... }
```

逐电机截断的问题：低推力（刚离地 0.15~0.3）时任何力矩都截断，总推力非对称掉落——低推力姿态发散的结构性放大器。desaturate 保平均（不掉高）、保方向、线性削幅度。

## 关键摘录三：controlTorque 的顺序就是安全性

```c
// control.ino:388-432（骨架 + 行号）
	if (attitude.invalid() || rates.invalid()) {          // 392 NaN 兜底：宁可停桨
		if (armed) { armed = false; print("!! 姿态估计无效（NaN）：已上锁停桨..."); }
		memset(motors, 0, sizeof(motors));
		return;
	}
	if (!armed) {                                          // 409 必须在"力矩无效 return"之前
		memset(motors, 0, sizeof(motors));
		rollRatePID.reset(); ...六个全清...
		return;
	}
	if (!torqueTarget.valid()) return;                     // 420
	if (thrustTarget < 0.1) { motors[0..3] = 0.1; return; } // 423 怠速
	// 439-442 混控四式 → 446 desaturate → 452-455 constrain
```

顺序反了的后果（403-408 注释推演）：MAVLink 直写电机把 torqueTarget 置无效后，"力矩无效就 return"会让 disarm 的 memset 永不执行——电机保持最后推力，只能断电。

## 混控矩阵与正交性（文件头 57-80 行推导）

```
前左 = T + τx − τy + τz      前右 = T − τx − τy − τz
后左 = T + τx + τy − τz      后右 = T − τx + τy + τz
四式相加 τ 项两两抵消 ⇒ ΣF = 4T（油门与力矩解耦）
物理符号：+τx 向右滚；+τy 低头；+τz 顺时针（与 +yaw 杆一致）
```

## 陷阱

- `mode` 命令改的模式会被 interpretControls 的开关解释覆盖（本版三档全 STAB，226-231）。
- `abs(controlYaw) < 0.1 → 0` 的偏航死区直接改写 controlYaw（254）。
- 手势窗口：油门 ∈ (−0.05, 0.20) 且 |yaw| > 0.50；网页/MAVLink 摇杆接管期间禁用（247-250）。
- `ctl` 读法：|τ| ≥ 推力 ⇒ 必然有电机被钳到 0；积分顶 ±0.30 = 地面憋满。
