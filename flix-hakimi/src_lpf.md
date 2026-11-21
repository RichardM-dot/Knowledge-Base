# src_lpf.md —— lpf.h 一阶低通滤波器

> **对应源文件：`references/firmware/flix-hakimi-pio/src/lpf.h`（共 93 行）**
> 模板类：同一个类滤 float（PID 微分）与 Vector（角速度）。`y += α(x−y)` 就是 RC 低通的离散化。
> 延伸阅读：[src_estimate.md](src_estimate.md)、[src_pid.md](src_pid.md)、Docs《02-IMU驱动与姿态估计》§3.4

## 逐成员表

| 行号 | 成员 | 说明 |
|---|---|---|
| 43 | `template <typename T>` | 一个类 = 编译期生成两份（float 版与 Vector 版） |
| 50-54 | `alpha` | 平滑系数 α∈(0,1]；1 = 直通；α=0.2@1kHz 精确截止 ≈35.7 Hz |
| 55 | `output` | 内部状态 y[n−1] |
| 57 | 构造 | 初始化列表 `alpha(alpha)`：左成员右参数 |
| 64-77 | `update` | 核心一行 `output += alpha * (input - output)`；α=1 直通；首次用输入初始化状态 |
| 81-83 | `setCutOffFrequency` | `α = 1 − e^(−2π·fc·dt)` |
| 87-89 | `reset` | 清 initialized，下次 update 重新用首个输入初始化 |
| 92 | `initialized`（private） | 首样本初始化标志 |

## 关键摘录

```c
// lpf.h:64-77
	T update(const T input) {
		if (alpha == 1) { // filter disabled
			return input; // α=1 时直通，省去状态维护
		}
		if (!initialized) {
			output = input; // 首次调用：直接用输入初始化内部状态，避免从 0 缓慢爬升
			initialized = true;
		}
		return output += alpha * (input - output); // 核心：y += α·(x - y)
	}
```

## 数学（文件头 10-36 行）

- 差分方程 `y[n] = (1−α)·y[n−1] + α·x[n]`，极点 z = 1−α，α∈(0,1] 恒稳定。
- α 与截止频率：`α = 1 − e^(−2π·fc·dt)`；反算 `fc = −ln(1−α)/(2π·dt)`；粗略估算 fc ≈ α·fs/2π。
- **精确 vs 近似**：α=0.2、fs=1 kHz 时精确解 fc ≈ 35.7 Hz（|H(fc)|=1/√2）；源码注释 "cutoff ~ 40 Hz" 是近似（精确 40 Hz 要 α≈0.222）。

## 全固件三处用法

| 位置 | α | 等效截止 | 目的 | 出处 |
|---|---|---|---|---|
| 角速度滤波 | 0.2 | ≈35.7 Hz | 滤机体振动，供内环 | estimate.ino:111 |
| PID 微分滤波 | 0.2 | ≈35.7 Hz | 差分噪声先滤再乘 D | pid.h:74（经构造参数 dAlpha） |
| 陀螺零偏估计 | 0.001 | ≈0.16 Hz（τ≈1 s） | 只让恒定零偏通过 | imu.ino:174 |

## 陷阱

- **群延迟 ≈ (1−α)/α·dt**：α=0.2、dt=1 ms 时约 4 ms——闭环里"滤波强度 vs 相位裕度"的取舍，调参不能只看噪声。
- `reset()` 后不是清零而是"忘掉"，下次 update 用首个输入重新初始化（不会从 0 爬升）。
- 模板实例化发生在使用点：`LowPassFilter<Vector>`（estimate.ino:111）与 `LowPassFilter<float>`（pid.h:74）各生成一份代码。

## 关键摘录二：setCutOffFrequency 与初始化语义

```c
// lpf.h:81-89
	void setCutOffFrequency(float cutOffFreq, float dt) {
		alpha = 1 - exp(-2 * PI * cutOffFreq * dt);
	}

	void reset() {
		initialized = false;
	}
```

setCutOffFrequency 就是 α 换算公式本身：想要"精确 40 Hz @1 kHz"就 `setCutOffFrequency(40, 0.001)`（得 α≈0.222），而不是凭注释写 0.2。reset 的语义是"忘掉历史"——PID 复位（pid.h:118）与模式切换时调用，下次 update 直接采用新输入初始化，不会从 0 缓慢爬升污染若干圈。

## 调参视角：动 EST_RATES_LPF_A 前后各发生什么

| α | 截止频率 | 群延迟 | 代价/收益 |
|---|---|---|---|
| 1.0 | （直通） | 0 | 陀螺噪声全量进 D 项 → 电机高频抖动、发热 |
| 0.5 | ≈110 Hz | ≈1 ms | 折中偏"跟手" |
| 0.2（默认） | ≈35.7 Hz | ≈4 ms | 默认平衡点 |
| 0.001（下界） | ≈0.16 Hz | ≈1 s | 角速度几乎恒定 → 姿态按恒定角速度漂走（paramRanges 拒绝 0 的原因） |

参数区间 [0.001, 1]（parameters.ino:303）的上下界各有含义：上界 1 = 关滤波；下界 0.001 防"永久冻结"。

## 与 FreeRTOS/通用嵌入式的对照

这一类 EWMA 滤波器是嵌入式最常用的"穷人卡尔曼"：单状态、两行代码、O(1) 内存；当噪声与信号频带分得开时（本工程：振动几十 Hz vs 姿态 <10 Hz），它的效果接近同截止频率的一阶卡尔曼，而不需要调 Q/R。频带叠在一起时才需要升级（定高估计器就是"频带叠了"的例子，所以 althold 用了二状态结构——见 [src_althold.md](src_althold.md)）。
