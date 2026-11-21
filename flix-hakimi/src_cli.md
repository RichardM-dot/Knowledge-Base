# src_cli.md —— cli.ino 串口命令行

> **对应源文件：`references/firmware/flix-hakimi-pio/src/cli.ino`（共 395 行）**
> print() 三路输出、pause() 活着等、命令解释器与飞行中禁用表。三条人机入口共用同一套判据。
> 延伸阅读：[src_monitor.md](src_monitor.md)（命令转发）、[src_parameters.md](src_parameters.md)（p 命令后端）、Docs《06-地面站链路》§1

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 48-94 | `motd` | 欢迎信息 + 全命令表（36 分支的总览在 189-198 注释） |
| 98-112 | `print` | 可变参数 vsnprintf 1000 B → 串口 + mavlinkPrint + webLogAppend |
| 116-129 | `pause` | 阻塞等待但跑 step/handleInput/mavlink/webui（不跑控制环） |
| 137-150 | `commandBlockedWhileArmed` | 飞行中禁用表（三入口共用） |
| 160-171 | `doCommand` | 重入保护外壳（commandRunning static） |
| 174-360 | `doCommandInner` | 切分 + 36 分支解释执行 |
| 363-395 | `handleInput` | 首次打 motd（网页静音）+ 串口逐字符拼行 |

## 关键摘录一：print 三路镜像

```c
// cli.ino:98-112
void print(const char* format, ...) {
	char buf[1000];
	va_list args;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	Serial.print(buf);
#if WIFI_ENABLED
	mavlinkPrint(buf); // 追加到 MAVLink 发送缓冲（mavlink.ino）
	webLogAppend(buf); // 追加到网页控制台镜像缓冲（webui.ino）
#endif
}
```

后果：长输出（log dump ≈126 KB、参数全表 ≈1.8 KB）会同时撑爆三份缓冲——所以长输出命令在飞行中被拒，MAVLink 侧 4 KB 封顶。

## 关键摘录二：飞行中禁用表（137-150）

```c
// cli.ino:137-150（节选）
bool commandBlockedWhileArmed(const String& command, const String& arg0) {
	return command == "ca" || command == "cr" ||                 // 校准：pause() 期间控制环停摆数十秒
	       command == "sc" ||                                    // 自检：连采 200 帧 IMU + 激光采样，会冻结控制环
	       command == "auxdbg" ||                                // 转储并**改写** MPU 内部 I2C 主机配置
	       command == "sys" ||                                   // 输出 ~90 ms（串口无 TX 缓冲，逐字节阻塞）
	       (command == "p" && arg0 == "") ||                     // 全参数转储约 1.8 KB（单条查询仍允许）
	       command == "i2cscan" || command == "auxscan" ||       // 总线扫描：抢占 SPI 引脚 + 阻塞
	       (command == "laser" && arg0 != "") ||                 // 激光子命令会借总线/改高度源
	       (command == "log" && arg0 == "dump");                 // 阻塞式导出
}
```

串口 CLI、MAVLink shell（SERIAL_CONTROL）、网页 /cmd 三条入口共用——原来只有网页拦 ca，串口侧裸奔（SERIAL_CONTROL 让串口命令同样可被远程触发）。

## 关键摘录三：命令重入保护

```c
// cli.ino:160-171
void doCommand(String str, bool echo = false) {
	static bool commandRunning = false;
	if (commandRunning) {
		print("上一条命令还在执行，请等它跑完再发\n");
		return;
	}
	commandRunning = true;
	doCommandInner(str, echo);
	commandRunning = false;
}
```

为什么必须防重入：pause() 内部仍轮询串口与 MAVLink——一条 ca（pause(8)×6）执行期间又收到命令会**递归**进入，每层压 1000 字节 print 缓冲，主循环任务栈 8 KB 几层打穿复位。

## 命令分支速查（doCommandInner）

| 组 | 分支 |
|---|---|
| 参数 | p（查/设，toFloat 失败=0 → parameterCheck 拒绝）、pd、preset |
| 状态 | time/ps/psq/imu/rc/wifi/mot/ctl/sys（sys 含 FreeRTOS 任务表 + delete[] 配对） |
| 转发 | sc/auxscan/mon/auxdbg/i2cscan → monitorCommand；laser → laserCommand（arg0+arg1 拼接） |
| 定高 | ah（无参=状态）/ ah off / ah <cm> |
| 解锁/模式 | arm（油门>0.05 拒绝）/ disarm / raw/stab/acro/auto |
| 校准/测试 | cr/ca/mfr/mfl/mrr/mrl |
| 日志/系统 | log [dump] / reset（attitude=Quaternion()，地面专用）/ reboot |

## 陷阱

- `arg1` 可含空格（strtok(NULL, "") 取到串尾）——`laser dss 2 200` 靠它传全尾巴（260-265）。
- `toFloat()` 解析失败返回 0：设参数后必须回读确认（214-216）。
- motd 打印时 webLogMuted=true——约 1.3 KB 会把网页控制台里"开机自检输出"的开头挤掉（367-378）。
- 命令名大小写不敏感（toLowerCase），**参数名敏感**（CTL_R_P 不能小写）。

## 关键摘录四：handleInput 的逐字符收集

```c
// cli.ino:363-394（核心）
void handleInput() {
	static bool showMotd = true;
	static String input;
	if (showMotd) { ... print motd（网页静音）... showMotd = false; }
	while (Serial.available()) {
		char c = Serial.read();
		if (c == '\n' || c == '\r') {
			if (input.length() > 0) doCommand(input); // 收到换行 → 执行整行命令
			input.clear();
		} else if (input.length() < CMD_LINE_MAX - 1) {
			input += c;  // 超长字符直接丢弃（控制流丢弃，无显式语句）
		}
	}
}
```

非阻塞设计：每圈最多清空串口缓冲里现存的字节，没有数据立即返回——这就是命令行能"寄生"在 1 kHz 主循环里的原因。127 字符上限 + splitString 的定长缓冲，两道防线让"一串没有换行的长输入"打不穿 8 KB 栈（util.h:112-118 同款防御）。

## sys 命令的任务表（328-348）怎么读

```text
Num  Task                Stack  Prio  Core  CPU%
- 栈列是 usStackHighWaterMark（历史最低剩余字节数）——越接近 0 越危险
- Core 列 "*" 表示未绑核（tskNO_AFFINITY）
- CPU% = ulRunTimeCounter / (totalRunTime/100)（整数除法，先除 100 当"1%"单位）
```

主循环任务（loopTask）的栈水位是判断"还能不能加功能"的第一指标；CPU% 异常高时先怀疑 mon 数据流/串口吞吐。

## 三条入口的行为差异对照

| | 串口 CLI | MAVLink shell | 网页 /cmd |
|---|---|---|---|
| 入口函数 | handleInput | SERIAL_CONTROL → doCommand(echo=true) | /cmd → 队列 → doCommand |
| 白名单 | 无（全命令） | 无 | {ca, sc, laser} |
| 飞行中禁用表 | 生效 | 生效 | 生效 |
| 重入保护 | doCommand commandRunning | 同左 | 另有 webCmdRunning + 单格队列 |
| 输出回哪 | 三路镜像 | 三路镜像（4 KB 封顶） | print 三路镜像（网页增量拉取） |

同一张 commandBlockedWhileArmed 判据表是三条入口的"最小共识"——加新危险命令时只改它一处。
