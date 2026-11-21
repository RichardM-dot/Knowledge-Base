# src_althold.md —— althold.ino 激光定高

> **对应源文件：`references/firmware/flix-hakimi-pio/src/althold.ino`（共 741 行）**
> 高度源唯一：VL53L1X（无气压计）。二状态互补滤波估计器 + 高度环 + 一键起飞 + 无扰交还。
> 延伸阅读：[src_laser.md](src_laser.md)（ahCorrect 调用者）、[src_control.md](src_control.md)、Docs《04-激光定高子系统》

## 宏与参数

| 行号 | 宏/参数 | 默认 | 说明 |
|---|---|---|---|
| 75-99 | AH_P/I/D/WU/MAX/TMIN/KH/KV/TO/VZ/TGTMAX | 1.0/0.25/0.35/0.15/0.25/**0.12**/0.45/0.80/0.5/0.5/3.0 | 增益与门限（TMIN 必须 >0.1） |
| 89-90 | AH_HMIN/AH_HMAX | 0.12/3.50 | 高度可信区间 |
| 93-99 | AH_SLEW/TILT_MAX/VMAX_ABS/INNOV_BASE/INNOV_VZ/REANCHOR_N | 0.5/0.26/2.5/0.15/0.30/5 | 滑行/倾角/垂速/野值门限 |
| 100-109 | AH_OUT_LO/OUT_DESC/OUT_HI/HANDBACK_S | 0.25/0.20/0.85/1.0 | 输出区间与过渡 |
| 113 | AH_WHY_UNREACHABLE | 字符串 | 饱和退出的原因标识（strcmp 认它，不做无扰交还） |
| 115-127 | AH_TKO_* | — | 一键起飞参数组 |
| 130-142 | ahP...ahTkoTimeout | 同宏 | 可调参数（注册进参数表） |

## 状态变量（145-169）

`ahEnabled/ahTargetM(设定与瞬时参考分离：ahTargetSetM/ahTargetM)/ahBaseThrottle(基准)/ahLastThrust(供交还)/ahHeight/ahVelocity/ahEstInit/ahSatSince/ahOutlierStreak/ahHandback(+From+Start)/ahTakeoff(+Thrust+Start+GroundM)`；`ahPID(1.0, 0.25, 0, 0.15)` —— D 不吃微分，直接乘估计垂速。

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 172-177 | `ahResetEstimate` | 锚定估计器（h:=测量、v:=0） |
| 183-190 | `ahPredictStep` | 1 kHz 入口：aUp = (R·acc).z − g，守卫后调 ahPredict |
| 216-274 | `ahPredict` | 纯数学预测半步（前向欧拉 v、半隐式欧拉 h、双钳位） |
| 293-350 | `ahCorrect` | 10 Hz 修正半步：野值门限 + KH/KV 拉向测量 |
| 357-370 | `ahTimeoutEff` | 自适应超时 = max(参数, 3 采样周期, 预算约束) |
| 373-387 | `ahLaserAge/ahLaserDataOk` | 数据新鲜度（按最后**有效**样本计时） |
| 394-396 | `ahOnGround` | 角速度环积分闸门判据 |
| 401-547 | `altHoldEngage` | 准入检查（分支 A 地面起飞 / 分支 B 空中进入） |
| 552-596 | `altHoldDisengage` | 退出 + 无扰交还（两个例外分支） |
| 601-611 | `ahHandbackStep` | 1 秒推力线性过渡 |
| 617-637 | `ahTakeoffStep` | 开环斜坡 → 离地捕获基准转闭环 |
| 640-723 | `altHoldControl` | 退出判据链 + slew 滑行 + 控制律 + 三层限幅 |
| 726-741 | `printAltHoldInfo` | CLI `ah` |

## 关键摘录一：预测半步的守卫与积分

```c
// althold.ino:221-251（核心行）
	if (!ahEstInit) return;                    // 未锚定不推进
	if (!(dtStep > 0)) return;                 // ★ NaN 防御写法（NaN<=0 是 false 漏进来）
	if (invalid(aUp)) return;                  // NaN/Inf 宁停不猜（NaN 会永久污染状态）
	ahVelocity += aUp * dtStep;                // 前向欧拉
	ahHeight += ahVelocity * dtStep;           // 半隐式欧拉：用刚更新的 v，顺序不能换
	if (ahVelocity > 5.0f) ahVelocity = 5.0f;  // ±5 钳位（D 项受限的最坏 = 固定满推 0.25）
	else if (ahVelocity < -5.0f) ahVelocity = -5.0f;
	if (ahHeight > 6.0f) ahHeight = 6.0f;      // [−1, 6]：−1 容忍落地噪声，硬钳 0 会造正偏置
	else if (ahHeight < -1.0f) ahHeight = -1.0f;
```

## 关键摘录二：野值门限（本模块最重要的安全措施）

```c
// althold.ino:318-332（核心）
	float innovLimit = AH_INNOV_BASE + fabsf(ahVelocity) * AH_INNOV_VZ;
	if (fabsf(e) > innovLimit) {
		ahOutliers++;
		if (++ahOutlierStreak >= AH_REANCHOR_N) {
			ahOutlierStreak = 0;
			ahResetEstimate(hMeas);
			ahReanchors++;
			print("定高：激光连续 %d 帧突变 -> 估计器重新锚定到 %.2f m\n", AH_REANCHOR_N, (double)hMeas);
		}
		return;   // 本帧不参与修正
	}
	ahOutlierStreak = 0;
	ahHeight += kh * e;
	ahVelocity += kv * e;
```

门限自适应：0.15 m + |v̂|×0.30 s（快飞时放宽，否则正常机动全被判野值）。没有它：一次 0.10 m 假样本（手/桌面挡光）→ h 跳 0.045 m、v 跳 0.08 → D 项顶满 ±0.25 → 约 0.23 m 非指令爬升。

## 关键摘录三：控制律与三层限幅

```c
// althold.ino:688-721（核心）
	float error = ahTargetM - ahHeight;
	...
	float delta = ahPID.update(error) - dGain * ahVelocity;
	// 饱和计时 2 s → altHoldDisengage(AH_WHY_UNREACHABLE)（不做无扰交还）
	delta = constrain(delta, -pmax, pmax);
	float outLo = (tmin > AH_OUT_LO) ? tmin : AH_OUT_LO;
	float cap = ahBaseThrottle - AH_OUT_DESC;
	if (outLo > cap) outLo = (cap > tmin) ? cap : tmin;   // 保住下降权限 + 不低于 tmin
	thrustTarget = constrain(ahBaseThrottle + delta, outLo, AH_OUT_HI);
```

稳定性：闭环 `s³ + K·D·s² + K·P·s + K·I = 0`，Routh–Hurwitz ⇔ **K·D·P > I**（K = g/u_hover）——进入前运行期自检（457-461）。

## 无扰交还的两个例外（583-594）

- "RC lost"：descend 斜坡正在进行，改写会拉回定高推力；
- AH_WHY_UNREACHABLE：最后推力是"够不着的饱和值"，交还它等于把错误输出再平滑 1 秒。
其余退出：controlThrottle := ahLastThrust + 1 秒斜坡（SBUS 下一帧 7~14 ms 会覆盖杆位，飞手在定高期间可能压死油门杆——只有一帧有效期，必须斜坡）。

## 陷阱

- `ah` 输出的三个诊断计数：有效样本 / 剔除野值 / 重锚定——野值偶发正常，连续涨要查环境。
- 起飞阶段判据专属（651-659）：倾角 >20°、超时 6 s、推力顶满未离地 → 中止并**直接停桨**（还在地面，不需要交还过渡）。
- Short 模式下高度上限自动降为 1.2 m（429）。
- 改目标不阶跃：瞬时参考从当前高度按 0.5 m/s 滑（531 + 678-685），阶跃会冲出自己的可信区间。
