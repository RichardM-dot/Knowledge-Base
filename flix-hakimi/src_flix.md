# src_flix.md —— flix.ino 主入口

> **对应源文件：`references/firmware/flix-hakimi-pio/src/flix.ino`（共 122 行）**
> 主入口：`setup()`/`loop()`。主循环的顺序就是整个固件的数据流。
> 延伸阅读：[src_time.md](src_time.md)（t/dt 基准）、[src_estimate.md](src_estimate.md)、[src_control.md](src_control.md)、Docs《01-启动流程与主循环时序》

## 逐函数/逐块表

| 行号 | 名称 | 级别 | 职责 |
|---|---|---|---|
| 1-5 | 文件头 | — | 版权 + 中文导读：.ino 拼接机制、主循环数据流 |
| 12-36 | 主循环顺序注释块 | — | 每圈 16 步的"为什么" + 三条顺序依赖 |
| 38-47 | include 与 WIFI_ENABLED | — | `#define WIFI_ENABLED 1`：置 0 时 wifi/mavlink/webui 整段不编译 |
| 50-65 | extern 全局状态 | — | t/dt、control*、gyro/acc/rates/attitude/landed/motors、imuAliveWatchdog 声明 |
| 70-88 | `setup()` | A | 按依赖顺序初始化 10 个模块 |
| 91-122 | `loop()` | A | 主循环：每圈 16 步固定顺序 |

## 关键摘录

**setup() 的依赖顺序**（70-88）——为什么参数最先、IMU 先于激光：

```c
// flix.ino:70-77
void setup() {
	Serial.begin(115200);//初始化串口通讯并设置波特率
	print("Initializing flix\n");
	disableBrownOut(); // 关欠压复位：满油门电压跌落不至于重启
	setupParameters(); // 从 Flash 读参数（PID 增益、校准值等），先于所有用到参数的模块
	setupLED();// 点亮默认引脚，与下面的setLED(true);一样为默认引脚GPIO2
	setupMotors();//定义在 motors.ino 中，先于所有用到电机的模块
	setLED(true); // 点亮 LED 作为"初始化中"指示
```

**loop() 前半段**（91-102）——三条不能调换的依赖都在这里：

```c
// flix.ino:91-99
void loop() {
	readIMU(); // 等待并读取最新 IMU 数据（阻塞到数据就绪 → 天然形成循环节拍）
	step(); // 更新 t、dt、loopRate —— 必须在读数之后、任何用 dt 的模块之前
	readRC(); // 读 SBUS（有新帧才更新控制量）
	applyWebAxes(); // 网页虚拟摇杆活动期间接管控制量（webui.ino；放在 readRC 之后才压得住 SBUS）
	estimate(); // 姿态估计：角速度积分 + 加速度计重力修正
	ahPredictStep(); // 定高估计器预测：用当圈姿态算世界系垂向加速度，推进 h、v（1 kHz）
	loopLaser(); // 激光测距：取样本；有效样本会顺手修正定高估计器（ahCorrect）
	             // 顺序：先预测再修正 —— 修正用的必须是最新预测出来的状态
```

## 依赖关系（谁必须排在谁后面）

1. `step()` 之后才能用 `dt`（所有积分器）；
2. `ahPredictStep()` 必须在 `estimate()` 之后（用当圈姿态）、`loopLaser()` 之前（先预测后修正）；
3. `applyWebAxes()` 必须在 `readRC()` 之后（网页杆量压住 SBUS）；
4. `imuAliveWatchdog()` 必须在 `loopLaser()` 之后（激光事务同步归还了 SPI 引脚），且不受激光在位门控——独立看 IMU 死活。

## 陷阱

- extern 声明不能带初值（会变定义，重定义报错，`flix.ino:50-55`）。
- `#define WIFI_ENABLED` 是编译期开关：0 时对应代码**整段不存在**，不是运行时 if。
- 改 setup 顺序的雷区见 Docs/01 §1 表。

## 关键摘录二：loop() 后半段（104-122）

```c
// flix.ino:104-122（节选）
	// `control();` = 每 1 kHz 跑一次的闭环控制流水线：先把遥控杆量解释成目标
	//（期间失控保护可能接管、定高可能把油门杆值替换成自己算的推力），然后"姿态环（外、慢、只用 P）
	// "算出该转多快、"角速度环（内、快、完整 PID）"算出该出多大力矩，最后"力矩分配（混控矩阵）"
	// 把"总推力 + 三轴力矩"解成四个电机的推力值，交给下一行的 `sendMotors()` 写成 PWM

	sendMotors(); // 把 motors[4] 写成 PWM
	handleInput(); // 处理 USB 串口命令行
#if WIFI_ENABLED
	processMavlink(); // 收发 MAVLink（遥测输出、地面站控制）
	loopWebUI(); // 处理 WiFi 网页界面请求（非阻塞，空闲时开销为微秒级）
#endif
	logData(); // 按 100 Hz 采样写入内存日志环形缓冲
	syncParameters(); // 每秒一次，把改动过的参数写入 Flash（仅未解锁时）
	loopMonitor(); // 调试数据流（未开启时立即返回；串口忙时丢行，不阻塞主循环）
}
```

后半段全是"非关键路径"：日志/网络/命令行都在控制输出之后——即使它们偶发变慢（如 laser 借总线的 15 ms 落在 loopLaser 自己那一圈），也只是推迟下一圈，不影响已写出的 PWM。

## loop 各步的调用去向（一表查全）

| 主循环行 | 实现文件 | KB 条目 |
|---|---|---|
| readIMU | imu.ino:106 | [src_imu.md](src_imu.md) |
| step | time.ino:38 | [src_time.md](src_time.md) |
| readRC | rc.ino:75 | [src_rc.md](src_rc.md) |
| applyWebAxes | webui.ino:193 | [src_webui.md](src_webui.md) |
| estimate | estimate.ino:115 | [src_estimate.md](src_estimate.md) |
| ahPredictStep | althold.ino:183 | [src_althold.md](src_althold.md) |
| loopLaser | laser.ino:1245 | [src_laser.md](src_laser.md) |
| imuAliveWatchdog | laser.ino:244 | [src_laser.md](src_laser.md) |
| control | control.ino:210 | [src_control.md](src_control.md) |
| sendMotors | motors.ino:90 | [src_motors.md](src_motors.md) |
| handleInput | cli.ino:363 | [src_cli.md](src_cli.md) |
| processMavlink / loopWebUI | mavlink.ino:36 / webui.ino:817 | [src_mavlink.md](src_mavlink.md) / [src_webui.md](src_webui.md) |
| logData / syncParameters / loopMonitor | log.ino:91 / parameters.ino:378 / monitor.ino:333 | 各条 |

## WIFI_ENABLED 的三处联动

1. flix.ino:44 定义为 1；置 0 后 setupWiFi/setupWebUI/processMavlink/loopWebUI 四个调用点消失；
2. wifi.ino/mavlink.ino/webui.ino 整体在 `#if WIFI_ENABLED` 内（webui.ino:834-845 还提供空实现，让 control.ino 等处的调用点不用条件编译就能链接）；
3. mavlinkStickTime 定义在 control.ino（不在 mavlink.ino）正是为了第 2 点——WiFi 关闭时手势判据 `mavlinkSticksInControl()` 仍可链接且恒为 false。
