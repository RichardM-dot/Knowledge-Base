# src_estimate.md —— estimate.ino 姿态估计（互补滤波）

> **对应源文件：`references/firmware/flix-hakimi-pio/src/estimate.ino`（共 210 行）**
> 三步互补滤波：陀螺积分 + 落地重力修正 + 飞行弱假设回正。全固件最核心的状态量 attitude 在这里产出。
> 延伸阅读：[src_quaternion.md](src_quaternion.md)、[src_vector.md](src_vector.md)、[src_imu.md](src_imu.md)、[src_control.md](src_control.md)（误差来源）、Docs《02-IMU驱动与姿态估计》

## 全局变量

| 行号 | 变量 | 初值 | 语义 |
|---|---|---|---|
| 74 | `rates` | — | 滤波后机体角速度 rad/s（内环输入） |
| 76 | `attitude` | — | 姿态四元数（机体系→世界系），全固件最核心状态 |
| 78 | `landed` | false | 未解锁且 \|acc−1g\|<10% 时为真 |
| 85/88 | `ACC_AIR_FADE/ACC_AIR_RATE_FADE` | 0.15/1.0 | 飞行中修正的合模长/转速淡出阈值 |
| 90/93/106 | `accWeight/accAirWeight/levelWeight` | 0.003/**0**/0.0002 | 三个可调权重（EST_* 参数） |
| 111 | `ratesFilter` | LPF\<Vector\>(0.2) | 角速度低通 ≈35.7 Hz |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 115-119 | `estimate` | 主入口：applyGyro → applyAcc → applyLevel |
| 122-140 | `applyGyro` | 滤波 + 积分传播（dt 限幅 50 ms） |
| 147-194 | `applyAcc` | landed 判定 + 落地/飞行两种修正权重与门控 |
| 202-210 | `applyLevel` | 飞行中朝水平缓慢拉回（τ≈5 s） |

## 关键摘录一：landed 判据（被三个模块复用的"在地面"唯一依据）

```c
// estimate.ino:158
	landed = (thrustTarget < 0.1f) && abs(accNorm - ONE_G) < ONE_G * 0.1f;
```

用"控制器没在给推力"而不是"电机没在转"：解锁后的 0.1 怠速也算在转，用它判 landed 会一解锁就永久为假——陀螺零偏估计被**永久冻结**（本板实测原始零偏 0.05~0.09 rad/s = 3~5°/s，带着未收敛零偏飞姿态必漂）。消费者：imu.ino 零偏校准、parameters.ino 落盘保护（间接）。

## 关键摘录二：互补滤波的等效时间常数（文件头 20-62 行推导）

- 积分路径（高通）：`attitude ← attitude ⊗ Δq(rates·dt)`；
- 修正路径（低通）：`attitude ← attitude ⊗ Δq(k·rotationVectorBetween(acc, up))`——比例反馈，一步 δ←δ(1−k)，k=0.003 @1 kHz ⇒ **τ ≈ 1/(0.003×1000) ≈ 0.33 s，交叉频率 ≈0.48 Hz**：快于 0.5 Hz 陀螺主导，慢于它加速度计主导。
- 为什么不加 I：零偏已由 imu.ino 的 α=0.001 低通显式估掉，姿态环只需比例修正——两条回路职责清晰。

## 关键摘录三：applyAcc 的飞行分支双门控

```c
// estimate.ino:164-178（飞行分支核心）
		if (thrustTarget < 0.1f) return;              // 怠速/未起飞：姿态环本来也被整段跳过
		float dev = fabsf(accNorm - ONE_G) / ONE_G;   // 0 = 平稳飞行；越大说明机动/振动越强
		float fade = 1.0f - dev / ACC_AIR_FADE;       // 偏离 ACC_AIR_FADE（15%）处完全淡出
		if (!(fade > 0)) return;
		if (fade > 1.0f) fade = 1.0f;
		float rgate = 1.0f - rates.norm() / ACC_AIR_RATE_FADE;
		...
		fade *= rgate;
		w = ((valid(accAirWeight) && accAirWeight > 0) ? accAirWeight : 0.0f) * fade;
```

**默认 EST_ACC_AIR=0 = 飞行中修正整体关闭**（2026-09-29 起）。原因（93-105 行长注释）：加速度计测比力，协调倾斜加速时机体系"上"偏约 2θ；以 τ≈0.67 s 灌进姿态估计会与横向运动形成 1~3 s 自激振荡（"离地一点点就左右摇摆"）。对齐 CF-Drone 与上游 flix 的实飞验证配置；恒定漂移改由 CTL_TRIM 配平消。

## applyGyro 的 dt 限幅

```c
// estimate.ino:136-138
	float dtGyro = dt;
	if (!(dtGyro > 0)) dtGyro = 0;          // 首圈 dt=0、时钟异常
	else if (dtGyro > 0.05f) dtGyro = 0.05f;
```

全固件唯一没有 dt 保护的积分器的保护就在这里：log dump/ca 等命令阻塞数秒后第一圈若直接积分，姿态会被一次转过数十弧度。超出的时间丢弃——丢的是"没有姿态数据的那段时间"。

## 陷阱

- `up = rotateVector((0,0,1), attitude)` 是 Rᵀ 变换（坐标变换）——方向对不对已数值验证（188-189）。
- applyLevel 在持续倾斜飞行时会把估计带偏一点（真实倾角比杆量略大）——上游 flix 的原始取舍；设 0 退化纯积分。
- correction 方向 = cross(acc, up) 归一化（右手定则），收敛方向已验证。

## EST_* 三个权重的调参表

| 参数 | 默认 | 建议范围（paramRanges） | 动了会怎样 |
|---|---|---|---|
| EST_ACC_WEIGHT | 0.003 | 0~1 | 大 → 落地收敛快但振动污染姿态；小 → 漂移收敛慢（τ=1/(k·1000)） |
| EST_ACC_AIR | 0 | 0~0.01 | >0 启用飞行修正（自动叠加双门控）；每加一档（0.0005→0.001→0.0015）与机动耦合风险回升一档 |
| EST_LEVEL | 0.0002 | 0~0.02 | 大 → 真实倾角被糊掉（持续倾斜飞时估计被拉平）；0 → 纯陀螺积分会慢慢斜掉 |
| EST_RATES_LPF_A | 0.2 | 0.001~1 | 见 [src_lpf.md](src_lpf.md) 的调参表 |

## landed 的三个消费者（改判据前先看这张表）

| 消费者 | 用法 | 改坏的症状 |
|---|---|---|
| 本文件 applyAcc | landed 用快权重（真重力参考） | 落地漂移收敛变慢 |
| imu.calibrateGyroOnce | Delay(2) 输入之一 | 零偏永不收敛（姿态持续漂） |
| monitor.printSensorCheck | 显示 landed 状态（cli `imu`） | 仅显示问题 |
| （间接）parameters.syncParameters | motorsActive 是另一路，不直接用 landed | — |

landed 判据 `thrustTarget < 0.1 && |acc−1g| < 10%` 与姿态环跳过门槛 `thrustTarget < 0.1` 是**同一个数**——这不是巧合：没给推力时姿态环跳过、加速度计可信，两者天然同步。

## 与上游 flix 的差异清单（读上游源码前必看）

1. applyLevel（飞行回正）为上游原有、本工程曾缺失现已补回（11-13）；
2. EST_ACC_AIR 默认 0（上游 accAirWeight 原值更大），并新增 ACC_AIR_FADE/ACC_AIR_RATE_FADE 双门控（81-88）；
3. landed 判据从"电机未转"改为"thrustTarget<0.1"（150-155）；
4. applyGyro 新增 50 ms dt 限幅（130-135）；
5. rotationVectorBetween 的同向 bug 修复在 vector.h（影响本文件 applyAcc 的修正方向）。
