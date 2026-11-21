# src_parameters.md —— parameters.ino 参数系统

> **对应源文件：`references/firmware/flix-hakimi-pio/src/parameters.ino`（共 430 行）**
> 88 条参数表（指针表）+ NVS 持久化 + 双层取值保护。改参数 = 直接改飞行变量。
> 延伸阅读：[src_cli.md](src_cli.md)（p/pd/preset 命令）、[src_mavlink.md](src_mavlink.md)（PARAM_SET）、[src_motors.md](src_motors.md)（落盘保护依赖 motorsActive）、Docs《05-遥控链路与参数系统》§2

## 数据结构

| 行号 | 名称 | 说明 |
|---|---|---|
| 60-64 | `struct Parameter` | name(≤15 字符 NVS 键限) + float* variable + value(缓存) |
| 68-172 | `parameters[]` | 88 条注册表：CTL 24 / IMU 9 / EST 4 / AH 13 / LASER 3 / RC 35 |
| 178 | `parameterDefaults[]`（static） | 上电记录编译期默认值（NVS 覆盖后就丢了，所以先存一份） |
| 280-283 | `struct ParamRange` | name + 闭区间 lo/hi |
| 285-325 | `paramRanges[]` | 41 条显式区间 |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 181-237 | `setupParameters` | 开 NVS("flix") 逐条读；键不存在才写默认；比对 NVS vs 编译默认并告警 |
| 243-245 | `parametersCount` | 全工程唯一"条数口径"（MAVLink 协议要用） |
| 248-257 | `getParameterName/getParameter(int)` | 按序号访问（带边界检查） |
| 260-268 | `getParameter(name)` | 按名取值，找不到返回 NAN |
| 329-361 | `parameterCheck` | 四种结果码：未知/非有限/越界/OK（RC_* 有专属域检查） |
| 365-374 | `setParameter` | 检查通过直接写 *variable（下一圈生效） |
| 378-392 | `syncParameters` | 1 Hz 比对缓存；motorsActive 时绝不写 Flash |
| 397-415 | `printParameterDiff` | `pd`：NVS vs 固件默认差异（IMU_ 单列） |
| 418-422 | `printParameters` | `p`：全表打印 |
| 425-430 | `resetParameters` | `preset`：clear() + ESP.restart() |

## 关键摘录一：NVS 语义陷阱（文件头 16-27 的完整论述）

```c
// parameters.ino:204-208
		if (!storage.isKey(parameter.name)) {
			storage.putFloat(parameter.name, factoryDefault); // 键不存在 → 写入编译期默认值
		}
		*parameter.variable = storage.getFloat(parameter.name, factoryDefault); // 覆盖内存变量
```

**键存在就以 NVS 为准** ⇒ 改源码默认值对已经跑过的板子不生效（实踩：AH_KV 新默认 0.8 被 NVS 里旧固件写入的 1.2 顶掉）。对策：上电比对告警（186-236，IMU_ 校准除外）+ `pd` 随时查 + `preset` 重置。已删参数键仍留在 NVS（无害，preset 清）。

## 关键摘录二：parameterCheck 的检查顺序

```c
// parameters.ino:329-361（骨架，行号真实）
	if (!known) return PARAM_NOT_FOUND;                 // 334
	if (!valid(value)) return PARAM_NOT_FINITE;         // 335 ① NaN/Inf 一律拒绝
	if (strncmp(name, "RC_ZERO_", 8) == 0 || strncmp(name, "RC_MAX_", 7) == 0) {
		if (!(value >= 0 && value <= 2047)) return PARAM_OUT_OF_RANGE;  // SBUS 11bit 值域
		return PARAM_OK;
	}
	static const char *rcChannelNames[] = {"RC_ROLL", ...};
	for (auto rcName : rcChannelNames) {
		if (!(value >= 0 && value < 16)) return PARAM_OUT_OF_RANGE;     // 通道号物理域
	}
	for (auto &range : paramRanges) {
		if (!(value >= range.lo && value <= range.hi)) return PARAM_OUT_OF_RANGE;  // 41 条闭区间
	}
	return PARAM_OK;
```

为什么 NaN 必须拒：`thrustTarget < 0.1` 对 NaN 恒为假，控制环保护整段失效。为什么负增益必须拒：负反馈变正反馈；0/负限幅让 constrain 上下界互换、返回值反而顶到上限（28-33）。

## 关键摘录三：落盘保护

```c
// parameters.ino:378-392
void syncParameters() {
	static Rate rate(1);
	if (!rate) return; // sync once per second
	if (motorsActive()) return; // don't use flash while flying, it may cause a delay
	for (auto &parameter : parameters) {
		if (parameter.value == *parameter.variable) continue;
		if (isnan(parameter.value) && isnan(*parameter.variable)) continue; // NaN != NaN 特判
		storage.putFloat(parameter.name, *parameter.variable);
		parameter.value = *parameter.variable;
	}
}
```

NVS 写入可能阻塞数百毫秒——飞行中写 Flash 会破坏 1 kHz 节拍，所以只在电机停时落盘。NaN 与任何值（含自身）比较为 false，"两边都是 NaN = 没变化"要单独判断。

## paramRanges 关键条目（285-325）

| 条目 | 区间 | 理由 |
|---|---|---|
| CTL_ 增益 | 0~50 | 负增益 = 正反馈 |
| CTL_TILT_MAX | 0.01~1.5 | 负值让倾角指令反向 |
| EST_RATES_LPF_A | 0.001~1 | 0 会让角速度冻结在首次采样值 |
| AH_TMIN | 0.11~0.5 | 硬性 >0.1 姿态环门槛 |
| AH_TKO_RAMP | 0.05~0.5 | 0.30 起捕获基准偏高 >0.10；≥1.5 空中误中止 |
| LASER_RATE | 1~20 | 借总线 15 ms 物理上限 |
| IMU_ACC_SCALE_* | 0.5~1.5 | 校准正常落点 |

不检查的：RC_ZERO/RC_MAX（值域另有专属检查）、IMU_ACC_BIAS（可负）、IMU_ROT（任意安装角）。

## 陷阱

- 参数名 ≤15 字符（Preferences 键限制）。
- 偏航姿态环 P-only ⇒ 保护表里**没有** CTL_Y_I/CTL_Y_D——不要凭对称性补（287-289）。
- `p` 设的值立即生效，落盘等 1 秒且要电机停——改完等 ≥1 秒再断电。
