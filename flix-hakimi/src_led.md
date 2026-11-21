# src_led.md —— led.ino 板载 LED

> **对应源文件：`references/firmware/flix-hakimi-pio/src/led.ino`（共 53 行）**
> 最小模块：初始化常亮、blinkLED 方波（本工程无调用点）、状态不变不写引脚。
> 延伸阅读：[src_flix.md](src_flix.md)（setLED(true/false) 调用点）

## 常量

| 行号 | 名称 | 值 | 说明 |
|---|---|---|---|
| 17 | BLINK_PERIOD | 500000 | 半周期 µs：0.5 s 翻转一次 → 周期 1 s（1 Hz、50% 占空比） |
| 21-24 | LED_BUILTIN | 2（默认） | #ifndef 提供缺省值的常见写法 |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 27-30 | `setupLED` | pinMode(GPIO2, OUTPUT) |
| 33-44 | `setLED` | 状态不变跳过 digitalWrite（1 kHz 循环能省则省） |
| 51-53 | `blinkLED` | setLED((micros()/BLINK_PERIOD) % 2) 方波 |

## 关键摘录

```c
// led.ino:33-44
void setLED(bool on) {
	static bool state = false;
	if (on == state) {
		return; // don't call digitalWrite if the state is the same
	}
	digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
	state = on;
}
```

## 陷阱

- **blinkLED() 目前没有调用点**（49-50 注释如实说明）：初始化完成后 LED 保持熄灭，不会自动闪烁——LED 不闪不是故障。
- `(micros() / BLINK_PERIOD) % 2`：整数除法先取整，%2 后 0/1 → bool。
- static 局部 state 记住上次电平，配合"不变不写"优化。

## 关键摘录二：blinkLED 的方波生成

```c
// led.ino:51-53
void blinkLED() {
	setLED(micros() / BLINK_PERIOD % 2);
}
```

`micros()` 返回自启动起的微秒数；`/` 是整数除法（先取整），`% 2` 后只可能是 0 或 1——传给 bool 参数时 0→false、1→true。效果：每 0.5 s 翻转一次电平（周期 1 s、占空比 50%）。

## 调用点地图

| 调用点 | 调用 | 时机 |
|---|---|---|
| flix.ino:77 | `setLED(true)` | setup 开始（"初始化中"常亮） |
| flix.ino:86 | `setLED(false)` | setup 结束（之后保持熄灭） |
| （无） | `blinkLED()` | 本工程无调用点，保留作库函数 |

## 设计说明

- #ifndef LED_BUILTIN 三件套（19-24）：给编译期开关提供缺省值——其他板子由编译环境预定义时不冲突。
- "状态未变不写引脚"的意义：digitalWrite 有开销，主循环 1 kHz 下任何被高频调用的路径都要省——与 webui 的 paint()、monitor 的"串口忙丢行"是同一类工程习惯：**高频路径上避免无谓的硬件/IO 操作**。
- LED 不承担运行状态指示（解锁/定高等）——那些状态走网页状态卡与 MAVLink 心跳；LED 只剩"初始化是否完成"一个用途。若要恢复心跳闪烁，在主循环任意处调 blinkLED() 即可（但注意它会与"初始化常亮"语义打架，需要状态机）。
