# src_safety.md —— safety.ino 失控保护

> **对应源文件：`references/firmware/flix-hakimi-pio/src/safety.ino`（共 93 行）**
> 丢信号 1 s → AUTO 自稳缓降 10 s；动杆夺回。descend() 也被失控外的场景复用语义。
> 延伸阅读：[src_control.md](src_control.md)（AUTO 模式语义）、[src_rc.md](src_rc.md)（controlTime 来源）、Docs《07-安全机制与监控日志》§1

## 宏

| 行号 | 名称 | 值 | 说明 |
|---|---|---|---|
| 25 | RC_LOSS_TIMEOUT | 1 | 遥控丢失判定（秒） |
| 26 | DESCEND_TIME | 10 | 缓降总时长（秒） |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 37-40 | `failsafe` | control() 每圈调用：rcLossFailsafe + autoFailsafe |
| 43-50 | `rcLossFailsafe` | controlTime≠0 且已解锁且超时 1 s → descend |
| 53-77 | `descend` | 切 AUTO + 目标水平 + 清前馈 + 推力线性递减 |
| 80-93 | `autoFailsafe` | 四通道任一变化（油门 0.05 死区）→ mode=STAB 夺回 |

## 关键摘录：descend 全文

```c
// safety.ino:53-77
void descend() {
	// ★ 首次进入缓降时清掉三个角速度环的积分（对齐 CF-Drone 的做法）：缓降前的机动可能
	//   把积分留在满偏，不清零的话整个缓降过程会带着一个恒定差动力矩——缓降本该是"干净"的
	//   自稳下降。姿态环是纯 P（无积分），无需清理。
	if (mode != AUTO) {
		rollRatePID.reset();
		pitchRatePID.reset();
		yawRatePID.reset();
	}
	mode = AUTO; // 切自动模式，防止遥控解释逻辑覆盖目标
	attitudeTarget = Quaternion(); // 目标姿态 = 恒等四元数 = 保持水平（自稳缓降）
	// ★ 清掉失控瞬间残留的偏航前馈：它只在 STAB 模式由杆量生成，一旦失控（遥控丢失/自动模式）
	//   就再也没人更新它，若丢失瞬间偏航杆不在中位，缓降全程会带着一个恒定偏航角速度自转。
	ratesExtra = Vector(0, 0, 0);
	float d = (dt > 0) ? (dt < 0.05f ? dt : 0.05f) : 0.0f;
	thrustTarget -= d / DESCEND_TIME; // 推力每秒减 1/DESCEND_TIME（10 秒降完）
	if (thrustTarget < 0) {
		thrustTarget = 0;
		armed = false; // 推力归零后自动上锁
	}
}
```

**线性斜坡的理由**（13-14）：无论丢失时油门在哪，落地/停桨时间上界都是 10 秒且过程连续。

## 触发矩阵（谁会刷新 controlTime → 谁会被判失控）

| 来源 | 刷新点 | 断流后果 |
|---|---|---|
| SBUS 有效帧 | rc.ino:90（失控帧不刷） | 1 s 后缓降 |
| 网页摇杆 | webui.ino:206（applyWebAxes） | 1 s 后缓降 |
| MAVLink MANUAL_CONTROL | mavlink.ino:150 | 1 s 后缓降 |
| MAVLink SET_ATTITUDE_TARGET / ACTUATOR | mavlink.ino:264/305 | GCS 断流也有保护（brownout 已关，否则满油门飞到断电） |
| 从未收到（controlTime==0） | — | **不触发**（纯地面站场景） |

## 陷阱

- descend 每圈被调（失控状态持续时）：dt 截 50 ms 防长阻塞后一次踩掉几十个百分点（67-71）。
- 油门 0.05 死区的原因：静置抖动一直微小变化，没死区会立刻"自我夺回"（19）。
- 与定高的交互：失控切 AUTO 时 altHoldControl 的"mode changed"判据立即交还定高（althold.ino:648），且"RC lost"分支不做无扰交还。
- 全固件最后一处 dt 保护点在此——descend 之前 pid/estimate/althold/laser 都已自保。

## 关键摘录二：autoFailsafe 的快照比较

```c
// safety.ino:80-93
void autoFailsafe() {
	static float roll, pitch, yaw, throttle; // 上一圈的杆量快照
	if (roll != controlRoll || pitch != controlPitch || yaw != controlYaw || abs(throttle - controlThrottle) > 0.05) {
		// controls changed
		if (mode == AUTO) mode = STAB; // regain control by the pilot
	}
	roll = controlRoll;
	pitch = controlPitch;
	yaw = controlYaw;
	throttle = controlThrottle;
}
```

static 局部变量当"上一圈快照"：四个通道里油门用 0.05 死区、其余三个精确比较。每圈都执行（failsafe() 在 control() 里），所以"动杆夺回"的响应延迟是一圈（≈1 ms）。快照在判断**之后**更新——判断用的是上一圈与本圈的差。

## failsafe 与定高/MAVLink 的三方时序

```text
T+0      RC 丢失（最后一帧有效 SBUS）
T+0~1s   controlTime 停止刷新；一切照旧
T+1s     rcLossFailsafe → descend：mode=AUTO、目标水平、推力开始 −0.1/s
         同一圈 altHoldControl 看到模式变化 → 定高退出（"mode changed"）
         altHoldDisengage("RC lost") 走例外分支：不改写 controlThrottle（descend 的斜坡不被拉回）
T+1s~    飞手动任何杆 → autoFailsafe 切回 STAB（descend 不再被调用，油门恢复杆位控制）
T+11s    推力 0 → armed=false 停桨（若飞手一直没动杆）
```

## 与其他停桨路径的对比

| 路径 | 触发 | 停桨方式 |
|---|---|---|
| 本文件 descend | 遥控丢失 1 s | 推力 10 s 线性缓降到 0 再上锁 |
| NaN 兜底 | attitude/rates 无效 | 立即上锁 + 四电机清零（control.ino:392） |
| IMU 看门狗 | 0.2 s 无新数据 / WHO_AM_I 失联 | 立即上锁 + 清零（imu.ino:148 / laser.ino:256） |
| SPI 归还失败 + IMU 死 | laser.busRestoreFails>3 且 IMU 无应答 | 立即上锁 + 清零（laser.ino:1270） |
| MAVLink thrust=0 | 停机指令 | armed=false（mavlink.ino:281） |

原则：**可控的故障缓降、不可控的故障立即停桨**。
