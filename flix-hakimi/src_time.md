# src_time.md —— time.ino 全局时间基准

> **对应源文件：`references/firmware/flix-hakimi-pio/src/time.ino`（共 70 行）**
> 全局 t/dt/loopRate 由本模块每圈维护；全固件的限频、失控判据、积分步长都吃这三个量。
> 延伸阅读：[src_util.md](src_util.md)（Rate/Delay 消费者）、Docs《01-启动流程与主循环时序》

## 全局变量

| 行号 | 变量 | 类型/初值 | 语义 |
|---|---|---|---|
| 30 | `t` | float = NAN | 当前时间（秒），首圈 step() 后才有意义 |
| 32 | `dt` | float | 距上一圈间隔（秒），double 差分后再转 |
| 34 | `loopRate` | float | 实测主循环频率（1 秒窗口统计） |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 38-56 | `step()` | 主循环每圈一次：刷新 t/dt/loopRate，NaN 首圈归零 |
| 60-70 | `computeLoopRate()` | 1 秒窗口统计圈数 |

## 关键摘录

```c
// time.ino:38-53
void step() {
	static double last = NAN; // double 保存上一圈时刻（微秒精度不随 t 增长而丢失）
	double now = (double)esp_timer_get_time() / 1000000.0;
	dt = (float)(now - last); // last 为 NaN 时差值也是 NaN → 下一行归 0（首圈）
	last = now;
	t = (float)now; // 对外仍是 float（各模块的既有语义）

	// 写成 !(dt > 0) 而不是 dt <= 0 是刻意的：NaN 和任何数比较都为假，取反之后 NaN 才能
	// 落进这个分支、被归成 0 —— 首圈 last 还是 NaN 时正是靠它兜住。
	if (!(dt > 0)) {
		dt = 0; // assume dt to be zero on first step and on reset
	}
	computeLoopRate();
}
```

## 为什么不用 micros()（文件头 6-24 行的完整推导）

- `micros()` 是 unsigned long（32 位），71.6 分钟回绕；t 是 float，回绕瞬间所有 `t − last >= 间隔` 的差值变成巨大负数，**冻结到 t 重新追上来**（最长 71.6 分钟）——遥测停发、激光停采、定高进不去、参数不落盘。
- t 仍是 float：float32 在 t≈4295 s 时分辨率约 0.5 ms，所以 **dt 用 double 差分**保住微秒精度。
- loopRate 880~945 Hz 都正常（受激光借总线/网页/日志影响的实际节拍）。

## dt 的消费者与各自保护

| 消费者 | 保护 | 出处 |
|---|---|---|
| pid.h | `(0, dtMax=0.1)` 之外丢弃积分/微分 | pid.h:93 |
| 姿态积分 | 限幅 50 ms | estimate.ino:136-138 |
| 定高预测 | `dt>0.1` 整圈跳过 | althold.ino:185 |
| 激光 | `dt>0.1` 跳过 | laser.ino:1279 |
| 缓降斜坡 | 截到 0.05 s | safety.ino:71 |

## 关键摘录二：computeLoopRate 的 1 秒窗口

```c
// time.ino:60-70
void computeLoopRate() {
	static float windowStart = 0; // static 局部变量只在第一次进入函数时初始化一次
	static uint32_t rate = 0;
	rate++; // 本窗口内的圈数
	if (t - windowStart >= 1) { // 1 second window
		loopRate = rate;
		windowStart = t; // 重置窗口起始时间
		rate = 0;
	}
}
```

窗口统计的意义：loopRate 反映**实际**节拍——受激光借总线（10 Hz × 15 ms ≈ 15% 占空）、网页请求、日志写入影响，876~945 Hz 都是正常的（time.ino:22-23），不是性能不达标。`time` 命令直接显示 t/loopRate/dt 三个值（cli.ino:232-235）。

## t/dt 的完整消费者地图

| 消费形态 | 使用点 |
|---|---|
| `t - last >= 1/hz` 限频 | Rate 类全部实例（log/mavlink/参数落盘）、loopLaser、loopMonitor |
| `t - controlTime > 1` 失控判据 | safety.rcLossFailsafe |
| `t - lastFresh > 0.2` 新鲜度 | imu.imuFreshnessWatchdog |
| dt 直接积分 | PID integral、attitude 积分、ahVelocity/ahHeight |
| `millis()`（并行时间轴） | 激光时间戳 laserLastValidMs、起飞/初始化计时——注意它与 t 是两套时钟，换算处都在调用点显式 /1000.0 |

## 为什么 float 的 t 没有换 double

对外语义不变（所有既有比较都是 float 语义）；double 差分已经把 dt 的精度问题解决（time.ino:15-18 的量化：float32 在 t≈4295 s 时分辨率约 0.5 ms，两个 t 直接相减会有 ±25% 抖动，double 里微秒级时间差精确）。若未来某处需要纳秒级绝对时间，用 esp_timer_get_time() 原值（int64 微秒）而不是升格 t。
