# src_pid.md —— pid.h 离散 PID 控制器

> **对应源文件：`references/firmware/flix-hakimi-pio/src/pid.h`（共 124 行）**
> 内外环两级共 6 个实例共用此类：矩形积分、微分先低通、积分限幅、dtMax 保护。
> 延伸阅读：[src_control.md](src_control.md)（6 个实例与调法）、[src_lpf.md](src_lpf.md)（微分滤波）、Docs《03-串级PID控制与电机混控》

## 逐成员表

| 行号 | 成员 | 说明 |
|---|---|---|
| 67-69 | `p, i, d, windup, dtMax` | 增益 + 积分限幅 + 最大允许间隔 |
| 71-72 | `derivative, integral` | 微分项值（诊断）与积分累积器 |
| 74-75 | `lpf` | 微分项低通（dAlpha 构造；0.2 ≈ 35.7 Hz） |
| 77-84 | 构造函数 | 默认参数 windup=0, dAlpha=1, dtMax=0.1；初始化列表 |
| 87-110 | `update(error)` | 返回控制量 u |
| 113-119 | `reset()` | 清积分/微分/滤波器状态（解锁、模式切换时调） |
| 122-123 | `prevError, prevTime`（private） | 差分与 dt 计算用，初值 NaN |

## 关键摘录：update 的完整逻辑

```c
// pid.h:87-110
	float update(float error) {
		float dt = t - prevTime;
		if (dt > 0 && dt < dtMax) {
			// 正常情况：按步长积分；微分项先算差商再过低通滤波
			integral += error * dt;
			derivative = lpf.update((error - prevError) / dt); // compute derivative and apply low-pass filter
		} else {
			// 首次调用（prevTime 为 NaN）、长时间停顿（dt ≥ dtMax）或时钟异常：
			// 积分、微分从零开始，避免陈旧状态产生一次冲击
			integral = 0;
			derivative = 0;
		}
		prevError = error;
		prevTime = t;
		return p * error + constrain(i * integral, -windup, windup) + d * derivative; // PID
		// 注：windup=0 时 constrain(x, 0, 0) 恒为 0 —— 因此不用积分限幅的 PID
		// 构造时必须令 i = 0（本固件中 yawRatePID 等就是 i=0）
	}
```

## ★ 最重要的陷阱：windup=0 = 积分整项被禁

`constrain(x, 0, 0)` 恒等于 0。后果链：

1. yawRatePID、yawPID 构造时不传 windup（默认 0）且 i=0 —— 正确；
2. rollPID/pitchPID 构造为 `(ROLL_P, 0, 0)` —— windup 默认 0 ⇒ **姿态环积分被禁**；
3. 因此参数表里的 CTL_R_I/CTL_P_I/CTL_R_D/CTL_P_D **改了不起作用**（parameters.ino:81-90 如实标注）；
4. 要给姿态环带积分必须改控制律（构造时 windup 传非 0），不是调参数的事。

## 6 个实例一览（control.ino:163-171）

| 实例 | 构造 | 用途 |
|---|---|---|
| rollRatePID / pitchRatePID | (0.05, 0.1, 0.001, 0.3, 0.2) | 角速度内环（全功能） |
| yawRatePID | (0.3, 0, 0) | 偏航内环（P-only） |
| rollPID / pitchPID | (6, 0, 0) | 姿态外环（P-only） |
| yawPID | (3, 0, 0) | 偏航外环（P-only） |

ahPID（althold.ino:169）= (1.0, 0.25, 0, 0.15)：定高高度环，D 项不吃微分而是直接乘估计垂速（`delta = ahPID.update(error) − dGain·ahVelocity`，althold.ino:693）。

## 积分的四个"清零时机"

| 时机 | 位置 | 原因 |
|---|---|---|
| 未解锁每圈 | control.ino:415-416 | 地面憋劲的积分不能带进解锁 |
| 激光确认贴地每圈 | control.ino:327-331 | 地面误差同号会卷到满偏 |
| 失控缓降首次进入 | safety.ino:58-60 | 缓降要"干净" |
| 一键起飞前 | althold.ino:497-499 | 带满偏积分起飞必侧翻 |

## 稳定性数学（文件头 51-56 行）

一阶对象配 PI：闭环 `s² + K·P·s + K·I = 0`，Routh–Hurwitz ⇔ K·P > 0 且 K·I > 0。带 D 升三阶，判据 a₂a₁ > a₀（定高的 K·D·P > I 就是这一条的特例，althold.ino:453-461 运行期自检）。

## 关键摘录二：构造函数与默认参数

```c
// pid.h:77-84
	PID(float p, float i, float d, float windup = 0, float dAlpha = 1, float dtMax = 0.1) :
		p(p), i(i), d(d), windup(windup), lpf(dAlpha), dtMax(dtMax) {}
```

三个默认值的含义：windup=0（积分被禁，见上）；dAlpha=1（微分不滤波——直通，yawRatePID 用这个）；dtMax=0.1 s（超过 100 ms 的间隔丢弃积分/微分）。成员 lpf 由 dAlpha 在初始化列表里构造——对象成员必须在初始化列表构造，不能在函数体里赋值。

## update 的四种路径（dt 分布决定）

| dt 落在哪 | 行为 |
|---|---|
| (0, 0.1) | 正常：积分累加、差商滤波 |
| =0（同圈重复） | 走 else：积分/微分清零 |
| ≥0.1（长阻塞后） | 走 else：积分/微分清零，只出 P |
| NaN（首圈 prevTime） | 走 else：同上 |

注意 dt 的 NaN 语义：`dt > 0 && dt < dtMax` 对 NaN 恒为 false——自然落进 else 分支，无需显式判断（与 time.ino 的 `!(dt>0)` 写法互补）。

## 调参时的诊断口

- `ctl` 命令直接打印 integral 值与 windup（control.ino:498-500）；
- `derivative` 成员可被调试器/watch 观察（71 行注释"仅诊断用"）；
- 积分顶满（=±windup/1，i·integral 达限）说明误差长期同号——地面上是"憋劲"、空中是持续配平不足。
