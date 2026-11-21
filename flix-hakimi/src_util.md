# src_util.md —— util.h 工具集

> **对应源文件：`references/firmware/flix-hakimi-pio/src/util.h`（共 174 行）**
> 数学辅助（mapf/wrapAngle/valid）、ESP32 寄存器操作、字符串切分、Rate/Delay 两个小工具类。
> 延伸阅读：[src_time.md](src_time.md)（t 消费者）、[src_cli.md](src_cli.md)（splitString 用户）、[src_parameters.md](src_parameters.md)（PARAM_* 结果码）

## 常量与宏

| 行号 | 名称 | 值 | 说明 |
|---|---|---|---|
| 36 | `ONE_G` | 9.80665 | 标准重力 m/s²（校准与落地判据基准） |
| 42-43 | `LASER_MODE_SHORT/LONG` | 1/2 | 距离模式编号（放这里因 althold 字母序在 laser 前） |
| 50-53 | `PARAM_OK/NOT_FOUND/NOT_FINITE/OUT_OF_RANGE` | 0~3 | 参数检查结果码（cli 与 mavlink 共用） |
| 101 | `CMD_LINE_MAX` | 128 | 命令行最大长度（含 NUL） |

## 逐函数/逐类表

| 行号 | 名称 | 签名要点 | 职责 |
|---|---|---|---|
| 58-65 | `mapf` | 线性映射 | **不钳位**（rc.ino 靠外推出 −1）；in_max==in_min 返回 out_min 防除零 |
| 68-70 | `invalid` | `!isfinite(x)` | NaN 与 ±Inf 都算无效 |
| 73-75 | `valid` | `isfinite(x)` | 全固件统一判据，**不能写 `== NAN`**（NaN≠NaN） |
| 79-88 | `wrapAngle` | 归一化到 (−π, π] | 偏航误差取最短角差；边界 ±π 不再修正 |
| 94-98 | `disableBrownOut` | REG_CLR_BIT | 关欠压复位，防满油门电压跌落重启 |
| 109-125 | `splitString` | 按空格切 3 token | 定长缓冲 128（原 VLA 会打穿 8 KB 栈）；strtok 就地修改 |
| 133-151 | `class Rate` | `operator bool` | 限频触发器：`if (rate)` 每 1/hz 秒放行一次 |
| 158-174 | `class Delay` | `update(bool)` | 布尔信号持续 delay 秒才放行，中断重新计时 |

## 关键摘录

```c
// util.h:58-65
float mapf(float x, float in_min, float in_max, float out_min, float out_max) {
	// 防除零：遥控通道没标定时 channelZero/channelMax 都可能是 0，
	// 除零会产生 Inf/NaN 并一路传进控制量（控制环遇到 invalid 会整段跳过 → 失去自稳）。
	if (in_max == in_min) {
		return out_min;
	}
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
```

```c
// util.h:144-150（Rate 的类型转换运算符）
	operator bool() {
		if (t - last >= 1 / rate) {
			last = t;
			return true;
		}
		return false;
	}
```

## 使用模式（全固件复用点）

| 类 | 使用点 |
|---|---|
| Rate | 日志 100 Hz（log.ino:96）、参数落盘 1 Hz（parameters.ino:380）、MAVLink 1/10 Hz（mavlink.ino:52） |
| Delay | 陀螺零偏校准的"落地 2 秒"确认（imu.ino:165） |

## 陷阱

- Rate 的 last 初值为 0：上电后要等 t ≥ 1/hz 才第一次放行。
- Delay 的 start 用 NAN 表"计时未开始"（update 里 isnan 判断）。
- `wrapAngle` 在恰好 ±π 时不修正（fmod 后仅 >π 或 <−π 才调整）——比较时注意边界。
- static 局部变量 vs static 全局/函数：前者跨调用保留值，后者只在本编译单元可见（.ino 拼接后"本文件"= 整个固件）。

## 关键摘录二：wrapAngle 与 Delay 全文

```c
// util.h:79-88
float wrapAngle(float angle) {
	angle = fmodf(angle, 2 * PI);
	if (angle > PI) {
		angle -= 2 * PI;
	} else if (angle < -PI) {
		angle += 2 * PI;
	}
	return angle;
}

// util.h:165-173
	bool update(bool on) {
		if (!on) {
			start = NAN; // 信号断开，重置计时
			return false;
		} else if (isnan(start)) {
			start = t; // 信号刚接通，记录起始时刻
		}
		return t - start >= delay; // 持续时间够长才放行
	}
```

wrapAngle 的唯一调用者是 control.controlAttitude 的偏航误差（control.ino:308）——没有它，yaw 在 ±180° 附近时 `yawTarget − yaw` 会差出近 2π，控制器"绕远路"。Delay 的防抖语义：中途一旦变 false 计时清零——"落地静止 2 秒"必须是连续的 2 秒。

## PARAM_* 结果码的三方契约

| 结果码 | 产生（parameters.parameterCheck） | 消费 |
|---|---|---|
| PARAM_OK=0 | 全部检查通过 | cli 打印确认；mavlink 回显写入值 |
| PARAM_NOT_FOUND=1 | 名字不在 88 条表内 | cli "Parameter not found" |
| PARAM_NOT_FINITE=2 | NaN/Inf | cli/mavlink 拒绝（NaN 会击穿 thrustTarget<0.1 类保护） |
| PARAM_OUT_OF_RANGE=3 | 越出专属域或 41 条区间 | cli "out of allowed range"；mavlink 回显实际值 |

定义在 util.h（而非 parameters.ino）的原因：cli.ino/mavlink.ino 的字母序在 parameters.ino 之前，宏必须对它们可见（46-49 注释）——.ino 拼接顺序决定声明位置，这是全工程反复出现的模式（同类：LASER_MODE_* 也在 util.h）。

## disableBrownOut 的寄存器操作

```c
// util.h:94-98
void disableBrownOut() {
	REG_CLR_BIT(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_BROWN_OUT_ENA);
}
```

REG_CLR_BIT 是 ESP-IDF 宏，展开成对内存映射寄存器地址的读-改-写。为什么敢关：满油门时电流可达数安培、电池电压瞬间跌落，芯片默认在 2.4x V 触发 brownout 复位——空中重启 = 停桨坠落。代价是真正的掉电也不再保护，由电池容量与飞行时间管理兜底。
