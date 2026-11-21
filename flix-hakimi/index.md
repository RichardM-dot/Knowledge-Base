# flix-hakimi 知识库索引

> 工程：`references/firmware/flix-hakimi-pio`（ESP32 激光定高微型四轴飞控，Arduino 框架，src/ 下 .ino 模块化 + 纯数学头文件）
> 本目录一个源文件一条 KB；深度文章在工程 `Docs/`（9 篇，00~08）。
> 源码已带逐函数中文深注（A级 7 文件 / B级 18 文件），读码时以源码注释为第一手，本库做索引与跨文件串联。

## 按数据流顺序读

| # | 条目 | 对应源文件 | 一句话 |
|---|---|---|---|
| 1 | [src_flix.md](src_flix.md) | flix.ino（122 行） | 主入口，主循环顺序 = 数据流 |
| 2 | [src_time.md](src_time.md) | time.ino（70 行） | t/dt/loopRate，esp_timer + double 差分 |
| 3 | [src_util.md](src_util.md) | util.h（174 行） | mapf/valid + Rate/Delay |
| 4 | [src_vector.md](src_vector.md) | vector.h（251 行） | FLU 向量 + rotationVectorBetween |
| 5 | [src_quaternion.md](src_quaternion.md) | quaternion.h（358 行） | 四元数：积分/旋转/欧拉角 |
| 6 | [src_lpf.md](src_lpf.md) | lpf.h（93 行） | y += α(x−y) 一阶低通 |
| 7 | [src_pid.md](src_pid.md) | pid.h（124 行） | 离散 PID；windup=0 禁积分陷阱 |
| 8 | [src_imu.md](src_imu.md) | imu.ino（299 行） | MPU6500 + 六面校准 + 零偏估计 |
| 9 | [src_estimate.md](src_estimate.md) | estimate.ino（210 行） | 互补滤波姿态估计 |
| 10 | [src_laser.md](src_laser.md) | laser.ino（1293 行） | 软件 I2C 借总线 + VL53L1X ULD 驱动 |
| 11 | [src_vl53l1x_regs.md](src_vl53l1x_regs.md) | vl53l1x_regs.h（1215 行） | 1186 条寄存器地址表（数据） |
| 12 | [src_vl53l1x_default_config.md](src_vl53l1x_default_config.md) | vl53l1x_default_config.h（41 行） | ST ULD 91 字节配置块（数据） |
| 13 | [src_althold.md](src_althold.md) | althold.ino（741 行） | 定高估计器 + 高度环 + 起飞/退出 |
| 14 | [src_control.md](src_control.md) | control.ino（505 行） | 串级 PID 四阶段 + 混控防饱和 |
| 15 | [src_motors.md](src_motors.md) | motors.ino（122 行） | LEDC 78kHz PWM 输出 |
| 16 | [src_rc.md](src_rc.md) | rc.ino（202 行） | SBUS 归一化 + 8 步校准 |
| 17 | [src_safety.md](src_safety.md) | safety.ino（93 行） | 失控缓降 10 s + 动杆夺回 |
| 18 | [src_parameters.md](src_parameters.md) | parameters.ino（430 行） | 88 条参数表 + NVS + 范围保护 |
| 19 | [src_cli.md](src_cli.md) | cli.ino（395 行） | print 三路镜像 + 命令解释器 |
| 20 | [src_monitor.md](src_monitor.md) | monitor.ino（505 行） | sc 自检 + mon 数据流 |
| 21 | [src_mavlink.md](src_mavlink.md) | mavlink.ino（410 行） | MAVLink v1 遥测与控制 |
| 22 | [src_wifi.md](src_wifi.md) | wifi.ino（99 行） | SoftAP（MAC 后缀）+ UDP 14550 |
| 23 | [src_webui.md](src_webui.md) | webui.ino（845 行） | 手机网页控制台 |
| 24 | [src_log.md](src_log.md) | log.ino（127 行） | 100 Hz×8 s 环形日志 |
| 25 | [src_led.md](src_led.md) | led.ino（53 行） | LED 指示（无 blink 调用点） |

## 与 Docs 的对应

| Docs 文章 | 主 KB |
|---|---|
| 00-总览与硬件架构 | 全部 |
| 01-启动流程与主循环时序 | src_flix / src_time / src_util |
| 02-IMU驱动与姿态估计 | src_imu / src_estimate / src_quaternion / src_vector / src_lpf |
| 03-串级PID控制与电机混控 | src_pid / src_control / src_motors |
| 04-激光定高子系统 | src_laser / src_althold / src_vl53l1x_* |
| 05-遥控链路与参数系统 | src_rc / src_parameters |
| 06-地面站链路 | src_mavlink / src_wifi / src_webui / src_cli |
| 07-安全机制与监控日志 | src_safety / src_monitor / src_log / src_led / src_time |
| 08-实测调参与飞行指南 | src_parameters / src_althold / src_control |
