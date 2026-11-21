# src_quaternion.md —— quaternion.h 单位四元数库

> **对应源文件：`references/firmware/flix-hakimi-pio/src/quaternion.h`（共 358 行）**
> 姿态估计与控制的数学核心：积分传播、向量旋转、欧拉角互转。
> 延伸阅读：[src_vector.md](src_vector.md)、[src_estimate.md](src_estimate.md)（积分用户）、[src_imu.md](src_imu.md)（坐标变换用户）、Docs《02-IMU驱动与姿态估计》

## 数学要点速览（文件头 9-67 行有完整推导）

| # | 主题 | 结论 |
|---|---|---|
| 1 | 半角公式 | q = (cos θ/2, n̂·sin θ/2)；半角因为 q⊗v⊗q⁻¹ 两次作用 |
| 2 | Hamilton 乘法 | a⊗b ≡ R(a)·R(b)，**非交换**；姿态积分右乘 Δq 因 Δq 是机体系增量 |
| 3 | 旋转向量 | `q⊗v⊗q⁻¹ ≡ R·v`（主动）/ `q⁻¹⊗v⊗q ≡ Rᵀ·v`（坐标变换）——两个名字极易混 |
| 4 | 逆与共轭 | q⁻¹ = q*/\|q\|²；单位四元数下 inversed ≈ 共轭（保留 1/\|q\|² 容忍漂移） |
| 5 | fromBetweenVectors | w = u·v + \|u×v\|，(x,y,z) = u×v，再归一化 |
| 6 | 双覆盖 | q 与 −q 同一旋转；固件避免四元数比较 |
| 7 | 每圈归一化 | 积分截断 + float32 舍入让 \|q\| 漂移；代价一次 sqrt + 4 除法 |
| 8 | 欧拉角 | ZYX 惯例与 ROS tf2 一致；俯仰 ±90° 奇异点单独处理 |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 83/85 | 构造 | 恒等 (1,0,0,0) / 四分量 |
| 91-97 | `fromAxisAngle$` | 轴角 → 四元数（sinNorm = sin(θ/2)/\|axis\|） |
| 101-106 | `fromRotationVector$` | 旋转向量 → 四元数；零向量特判返回恒等（防除零） |
| 110-123 | `fromEuler$` | 欧拉角 → 四元数（展开的标量式，省中间量） |
| 128-141 | `fromBetweenVectors$` | 把 u 转到 v 的最短旋转 |
| 145-155 | `finite/valid/invalid` | NaN 约定 |
| 158-163 | `invalidate()` | 全 NaN = 跳过姿态控制（ACRO/RAW） |
| 167-178 | `norm/normalize` | 范数与归一化 |
| 185-194 | `toAxisAngle` | θ = 2·acos(w)，axis = (x,y,z)/sin(θ/2)；引用参数回写两个结果 |
| 197-203 | `toRotationVector` | 恒等特判返回零向量（sin(0)=0 会除零） |
| 208-234 | `toEuler` | sarg = 2(wy−xz)，±0.99999 两个奇异分支单独处理 |
| 238-266 | `getRoll/getPitch/getYaw/setRoll/setPitch/setYaw` | 单角读写（先转欧拉改一维再转回） |
| 273-287 | `operator* / == / !=` | Hamilton 积（16 乘 12 加）与比较 |
| 291-298 | `inversed` | q⁻¹ = q*/\|q\|² |
| 302-306 | `conjugate(v)` | q⊗v⊗q⁻¹ ≡ R·v（imu.ino 安装旋转用） |
| 314-318 | `conjugateInversed(v)` | q⁻¹⊗v⊗q ≡ Rᵀ·v（estimate 的"上"到机体系用） |
| 322-331 | `rotate$` | a⊗b，默认每圈归一化（默认参数 normalize=true） |
| 335-337 | `rotateVector$` | = q.conjugateInversed(v) 的封装 |
| 341-347 | `between$` | a⊗b⁻¹ 相对旋转 |
| 350-357 | `printTo` | 15 位小数打印 |

## 关键摘录：两种"旋转向量"的方向（全工程最容易混的一步）

```c
// quaternion.h:308-318
	// 用逆旋转向量 v：v' = q⁻¹ ⊗ v ⊗ q（已数值验证 ≡ R(q)ᵀ·v，即按 q 的反向旋转）
	// 两种典型用途（已在固件上下文中核实）：
	//   1. estimate.ino: rotateVector((0,0,1), attitude) —— attitude 为机体→世界，
	//      Rᵀ 把世界系重力方向表示到机体系（坐标变换）；
	//   2. imu.ino: rotateVector(acc, R.inversed()) —— R 为芯片系→机体安装旋转，
	//      (R⁻¹)ᵀ = R，即把芯片系读数主动旋转到机体系。
```

## 使用者地图

| 调用点 | 用的函数 |
|---|---|
| estimate.applyGyro | `rotate(attitude, fromRotationVector(rates·dt))` —— 右乘机体系增量 |
| estimate.applyAcc/applyLevel | `rotateVector(Vector(0,0,1), attitude)` = Rᵀ 把世界上转到机体系 |
| imu.readIMU | `rotateVector(acc, rotation.inversed())` = R 把芯片系读数转到机体系 |
| control.interpretControls | `fromEuler(...)` 生成姿态目标；`getYaw()` 取偏航 |
| cli.reset | `attitude = Quaternion()` 复位为当前朝向 |

## 陷阱

- `conjugate` 与 `conjugateInversed` 名字与语义"反着"：conjugate 对应主动旋转 R，conjugateInversed 对应 Rᵀ——记法见 quaternion.h:32。
- toAxisAngle 的 θ→0 时 sin→0 除零：必须走 toRotationVector（有恒等特判）而不是直接用。
- 欧拉角只在人机界面/日志出现，姿态内部始终四元数传播。

## 关键摘录二：fromBetweenVectors 的"免特判"设计

```c
// quaternion.h:128-141
	static Quaternion fromBetweenVectors(const Vector& u, const Vector& v) {
		float dot = u.x * v.x + u.y * v.y + u.z * v.z;
		float w1 = u.y * v.z - u.z * v.y; // u×v 的三个分量
		float w2 = u.z * v.x - u.x * v.z;
		float w3 = u.x * v.y - u.y * v.x;
		Quaternion ret(
			dot + sqrt(dot * dot + w1 * w1 + w2 * w2 + w3 * w3), // w = u·v + |u×v| = |u||v|(cosθ+1)
			w1, w2, w3);
		ret.normalize();
		return ret;
	}
```

推导（文件头 44-50）：cos(θ/2)=sqrt((1+cosθ)/2)，而 1+cosθ ∝ |u||v|+u·v——所以 w 直接取 u·v+|u×v|（差一个正常数比例，normalize 除掉）。恒等旋转时 u×v=0 且 u·v>0 ⇒ w>0，自然得到 (1,0,0,0)，**不需要为恒等特判**——与 fromRotationVector 的零向量特判形成对比（两条构造路径的退化行为不同）。

## toEuler 的奇异点处理（208-234）

```c
// quaternion.h:216-226（节选）
		float sarg = -2 * (x * z - w * y) / (sqx + sqy + sqz + sqw);
		if (sarg <= -0.99999) {
			euler.x = 0;
			euler.y = -0.5 * PI;
			euler.z = -2 * atan2(y, x);
		} else if (sarg >= 0.99999) {
			euler.x = 0;
			euler.y = 0.5 * PI;
			euler.z = 2 * atan2(y, x);
		} else { /* 标准公式 */ }
```

sarg = 2(wy−xz) = sin(pitch)（归一化后）。|sarg|→1 即俯仰 ±90° 万向节死锁：roll 与 yaw 退化成同一个自由度，标准公式的 asin 溢出/atan2 双参数同时趋零——两个分支分别固定 pitch=∓90°、roll=0、yaw 由 atan2(y,x) 给出。本工程 tiltMax=30°，正常飞行远离奇异点，但 `reset` 姿态、ACRO 翻滚后 `ps` 读数会经过它。

## 本文件在数据流中的位置（一图记牢）

```text
imu.readIMU        ──fromEuler(imuRotation)──▶ 安装旋转 R（芯片系→机体系）
estimate.applyGyro ──rotate(attitude, Δq)──▶  姿态传播（右乘，机体系增量）
estimate.applyAcc  ──rotateVector((0,0,1), attitude)──▶ Rᵀ 把世界上转到机体系（误差参照）
control.interpretControls ──fromEuler(杆量×tiltMax+trim, yawTarget)──▶ 姿态目标
control.controlAttitude   ──rotateVector(up, attitude/Target)──▶ 两个"上"向量 → 旋转向量误差
cli.ps / psq       ──toEuler()/printTo──▶ 人读
```
