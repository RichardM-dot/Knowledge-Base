# src_rc.md —— rc.ino SBUS 遥控驱动与校准

> **对应源文件：`references/firmware/flix-hakimi-pio/src/rc.ino`（共 202 行）**
> SBUS 读帧/归一化/通道映射 + 8 步交互校准。control* 四个全局量的定义处。
> 延伸阅读：[src_parameters.md](src_parameters.md)（RC_* 参数）、[src_safety.md](src_safety.md)（controlTime 消费者）、Docs《05-遥控链路与参数系统》

## 全局变量

| 行号 | 变量 | 语义 |
|---|---|---|
| 46-54 | `rc(Serial2)`、`channels[16]`、`channelZero[16]`、`channelMax[16]` | SBUS 对象与原始值/校准表 |
| 56-63 | `controlRoll/Pitch/Yaw/Throttle/Mode/Time` | 飞手输入 [-1,1]（油门 [0,1]）；Mode 未配置=NaN；Time=最后有效帧时刻 |
| 66 | `rollChannel` 等 5 个 | 通道映射（float 存参数表；负数/NaN=未配置） |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 69-72 | `setupRC` | rc.begin() |
| 75-94 | `readRC` | 读新帧；**失控帧只更新 channels 不刷 controlTime** |
| 99-123 | `normalizeRC` | 跨距保护 + mapf 无钳位归一化 + 通道映射（上下界检查） |
| 129-150 | `calibrateRC` | 8 步交互校准（`cr`） |
| 154-183 | `calibrateRCChannel` | 单步：读 30 次、拷贝、找变化最大通道绑定 |
| 190-192 | `rcCalValue`（static） | 打印校准表时按通道取值（上下界检查防越界读） |
| 195-202 | `printRCCalibration` | 校准结果表 |

## 关键摘录一：失控帧防线（本文件的命门）

```c
// rc.ino:87-91
		if (rc.isFailsafe()) return true;
		normalizeRC();
		// t 是 time.ino 维护的全局时间（秒），util.h 里有它的 extern 声明。
		controlTime = t; // 记录更新时刻，供失控保护判断
```

多数接收机失联后仍持续发"格式正常但带 failsafe 标志"的帧：绝不能刷新 controlTime（否则 t−controlTime 永远 <1 s，失控保护被整体击穿），也不能让外推垃圾进控制链（172 经无钳位 mapf 外推成 −30° 目标 + 油门 0，恰好命中上锁手势）。

## 关键摘录二：跨距保护

```c
// rc.ino:107-110
		float span = channelMax[i] - channelZero[i];
		controls[i] = (span > 100.0f || span < -100.0f) ? mapf(channels[i], channelZero[i], channelMax[i], 0, 1) : 0.0f;
```

行程不足 ±100（校准残缺/参数改坏/NVS 脏值）当"未配置"填 0——跨距 5 时 mapf 无钳位外推会把满舵放大成 160+ 的失控控制量。零位/最大值含 NaN 时 span 也是 NaN，两个比较都为假 → 同样落 0（顺带堵 NaN）。

## 关键摘录三：通道号的上下界

```c
// rc.ino:118-122
	controlRoll = (rollChannel >= 0 && rollChannel < 16) ? controls[(int)rollChannel] : 0;
```

通道号是参数表 float，运行期可改任意值：**必须同时判上下界**再转 int 下标——只判 `>= 0` 时 `p RC_ROLL 1e9` 会越界读栈（113-117 注释明确说明这是本轮修的点）。

## 8 步校准原理（13-41 文件头 + 129-183）

步骤 2/3 记录中位（zero），步骤 4~7 依次推油门/偏航/俯仰/横滚到极限——**程序自动找出"与基准差异最大的通道"（差 >10/2048 才有效）并绑定**，无需约定通道顺序；步骤 8 绑定模式开关。结果存 RC_ZERO_n/RC_MAX_n（0~15 全部持久化，parameters.ino:148-166——原版只存 0~7，重启后未持久化通道除零变 NaN）。

## 陷阱

- 归一化语义：摇杆中位 0、满舵 ±1（相反侧靠 mapf 无钳位外推）；油门 channelZero=最低位所以 [0,1]。
- `calibrateRCChannel` 的 `for (i<30) readRC()`：pause 期间尽量刷新帧（157）。
- `cr` 期间系统照常跑（pause 语义）但控制环停——飞行中被拒。

## 关键摘录四：calibrateRCChannel 的自动识别

```c
// rc.ino:154-183（核心段）
void calibrateRCChannel(float *channel, uint16_t in[16], uint16_t out[16], const char *str) {
	print("%s", str);
	pause(3);
	for (int i = 0; i < 30; i++) readRC(); // try update 30 times max
	memcpy(out, channels, sizeof(channels));
	if (channel == NULL) return; // no channel to calibrate
	// Find channel that changed the most between in and out
	int ch = -1, diff = 0;
	for (int i = 0; i < 16; i++) {
		if (abs(out[i] - in[i]) > diff) {
			ch = i;
			diff = abs(out[i] - in[i]);
		}
	}
	if (ch >= 0 && diff > 10) { // difference threshold is 10
		*channel = ch;               // 解引用赋值：结果带回调用者
		channelZero[ch] = in[ch];
		channelMax[ch] = out[ch];
	} else {
		*channel = NAN; // 没检测到操作 → 该功能不绑定
	}
}
```

设计要点：不预设"油门一定是通道 2"这类约定——推油门这一步动到的就是油门通道，程序找变化最大的那个绑定。diff>10（满量程 2048 的 0.5%）防手抖误绑定。channelZero 记"操作前"、channelMax 记"操作后"——摇杆类得到对称行程，油门类得到最低位→最高位。

## control* 五个量的完整写入者与优先级

| 写入者 | 位置 | 优先级（主循环顺序决定） |
|---|---|---|
| readRC（SBUS 有效帧） | rc.ino:88-90 | 最先，每帧 7~14 ms |
| applyWebAxes（网页摇杆） | webui.ino:201-206 | 第二，readRC 之后覆盖 SBUS |
| handleMavlink MANUAL_CONTROL | mavlink.ino:144-148 | 事件驱动，写完由下一圈沿用直到被覆盖 |
| interpretControls（偏航死区） | control.ino:254 | 每圈可把小 yaw 杆量抹 0 |
| altHoldDisengage（交还改写） | althold.ino:585-586 | 退出瞬间改写 controlThrottle 一帧 |

接管期间（webui/mavlink 摇杆）手势禁用（control.ino:247-250）、controlMode 置 NaN（模式开关失效）。
