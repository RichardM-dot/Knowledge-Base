# src_log.md —— log.ino 内存环形日志

> **对应源文件：`references/firmware/flix-hakimi-pio/src/log.ino`（共 127 行）**
> 100 Hz × 8 s × 17 列 ≈ 54 KB 静态 DRAM 环形缓冲；调参数据的唯一来源。
> 延伸阅读：[src_util.md](src_util.md)（Rate）、[src_control.md](src_control.md)（被记录的状态）、Docs《07-安全机制与监控日志》§4

## 宏与容量算术

| 行号 | 名称 | 值 | 说明 |
|---|---|---|---|
| 26 | LOG_RATE | 100 | 采样频率 Hz |
| 33 | LOG_DURATION | 8 | **为什么不是 10**：静态 DRAM(dram0_0_seg≈160KB) 已近上限，列 14→17 后保 10 s 会链接溢出 |
| 34 | LOG_SIZE | 800 | 宏无括号：`LOG_SIZE + 1` 展开成 `8*100 + 1`（陷阱示范） |
| 79 | `logBuffer[800][17]` | float | 800×17×4 B ≈ 54 KB |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 85-88 | `prepareLogData` | 写入前把两个四元数转欧拉角存缓存（表取地址用） |
| 91-110 | `logData` | 未解锁 return；100 Hz 限频；解引用写一行；指针回卷 |
| 113-117 | `printLogHeader` | CSV 表头 |
| 120-127 | `printLogData` | 全量 CSV（t==0 跳过空记录） |

## 17 列定义（53-72）

| 列 | 变量 | 来源文件 |
|---|---|---|
| t | &t | time.ino |
| rates.x/y/z | &rates.* | estimate.ino |
| ratesTarget.x/y/z | &ratesTarget.* | control.ino |
| attitude.x/y/z | &attitudeEuler.*（欧拉缓存） | log.ino 缓存 ← estimate |
| attitudeTarget.x/y/z | &attitudeTargetEuler.* | 同上 ← control |
| thrustTarget | &thrustTarget | control.ino |
| ahTarget/ahHeight/ahVelocity | &ahTargetCm 等 | althold.ino |

**表存变量地址（零拷贝）**：列直接对应飞行状态量；代价是列必须在编译期固定。

## 关键摘录

```c
// log.ino:91-110（核心）
void logData() {
	if (!armed) return; // 未解锁不记录
	static int logPointer = 0; // 环形写指针
	static Rate period(LOG_RATE); // 100 Hz 限频
	if (!period) return;
	prepareLogData();
	for (int i = 0; i < logColumns; i++) {
		logBuffer[logPointer][i] = *logEntries[i].value; // 解引用写入一行
	}
	logPointer++;
	if (logPointer >= LOG_SIZE) {
		logPointer = 0; // 写满回卷，覆盖最旧数据
	}
}
```

## 用途与读法（文件头 17-18）

- 调内环：rates 是否跟随 ratesTarget；
- 调外环：attitude 相对 attitudeTarget 的滞后；
- 定高复核：ahTarget/ahHeight/ahVelocity 是否跟随、定高是否中途掉出；
- `log dump` 800 行同步写串口约 **12 秒阻塞**——飞行中被 commandBlockedWhileArmed 拒绝（log.ino:15-16）。

## 陷阱

- 空记录判据 `t==0`（未解锁时缓冲保持 0）。
- `logColumns = sizeof(logEntries)/sizeof(logEntries[0])`——加列不用改数字（74-76）。
- attitude 列是**弧度**（61 行注释提醒度↔弧度）。
- dump 输出与 MAVLink 的 LOG_REQUEST_DATA 共用同一份 logBuffer（后者单次限 20 行）。

## 关键摘录二：LogEntry 与零拷贝设计

```c
// log.ino:44-47
struct LogEntry {
	const char *name;
	float *value;
};
```

表里存的是**变量地址**而非拷贝：写日志就是 `*logEntries[i].value` 解引用——被记录的变量每圈更新，100 Hz 到点时读到的就是当下值。同一模式在 parameters.ino 的 Parameter 表复用（`float *variable`）。代价与约束：列在编译期固定、变量必须静态存活（不能指局部变量）、attitude 这类结构体要先转成标量缓存（prepareLogData，85-88）。

## 与 monitor 数据流的分工

| | logData（本文件） | loopMonitor（monitor.ino） |
|---|---|---|
| 频率 | 100 Hz | 1~200 Hz 可调 |
| 去向 | 内存环形缓冲（事后 log dump / MAVLink 拉取） | USB 串口实时行（可能丢行） |
| 目的 | 事后精读调参 | 实时观察/画图 |
| 列数 | 17（含定高三项） | 17（含激光与 loop_hz，不同列集） |
| 前置条件 | armed 才记录 | mon on 才输出 |

## dump 数据的典型分析动作

1. `log dump` > log.csv；第一行是表头；
2. 内环调参：同一时间轴上画 rates.x 与 ratesTarget.x——跟随差大 → 加 P；高频毛刺 → 加滤波；
3. 外环：attitude.x 相对 attitudeTarget.x 的滞后；
4. 定高复核：ahHeight 是否贴住 ahTarget、ahVelocity 峰值、thrustTarget 有无长时间顶格（饱和退出前兆）；
5. 配合 tools/fft.py 对 rates 列做频谱——机体振动频率上的尖峰提示滤波/机架问题。
