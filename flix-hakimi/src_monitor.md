# src_monitor.md —— monitor.ino 传感器自检与实时数据流

> **对应源文件：`references/firmware/flix-hakimi-pio/src/monitor.ino`（共 505 行）**
> 纯只读调试：sc 自检 / auxscan/auxdbg 辅助总线 / mon 实时 CSV。全部飞行中被拒。
> 延伸阅读：[src_cli.md](src_cli.md)（入口转发）、[src_laser.md](src_laser.md)（i2cscan 复用其总线扫描）、Docs《07-安全机制与监控日志》§3

## 全局量

| 行号 | 变量 | 说明 |
|---|---|---|
| 61-67 | monitorOn/monitorHz(20)/monitorLast/Lines/Skipped | 数据流状态 |
| 76 | AUX_NACK_BIT | 0x08（I2C_MST_STATUS 的 SLV0_NACK bit3；原误写 0x10 是 SLV1） |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 92-107 | `auxProbe`（static） | 探辅助总线地址：三次读数变化判应答 + NACK 位佐证 |
| 111-120 | `auxDeviceName` | 按地址+芯片 ID 猜器件名 |
| 123-172 | `printAuxScan` | 辅助总线扫描（8 常见地址或全扫 0.8 s） |
| 182-195 | `sampleIMU`（static） | 采 n 帧均值（原始+校准双份） |
| 202-305 | `printSensorCheck` | sc：四项 PASS/FAIL + 汇总 |
| 313-315 | `monitorPrintHeader` | CSV 表头 |
| 318-329 | `monitorSet` | 开/关数据流 |
| 333-360 | `loopMonitor` | 每圈：到点组一行 CSV，**串口忙整行丢弃** |
| 371-373 | `printI2CScan` | 转发 printLaserBusScan（i2cscan 命令） |
| 384-461 | `printAuxDebug` | auxdbg：MPU 辅助主机配置转储 + 自修复 + NACK 自测 |
| 466-500 | `monitorCommand` | 命令入口（cli.ino 转发） |
| 503-505 | `setupMonitor` | 开机一行提示 |

## 关键摘录一：数据流为什么直接写 Serial

```c
// monitor.ino:342-359（核心）
	char line[200];
	int n = snprintf(line, sizeof(line),
		"MON,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%d,%.3f,%.3f,%d,%d,%.0f\n", ...);
	// availableForWrite() 返回串口发送缓冲还能再塞多少字节。
	if (Serial.availableForWrite() < n) {
		monitorSkipped++; // 串口忙：丢掉这一行，不阻塞
		return;
	}
	Serial.write((const uint8_t *)line, n);
```

print() 会同时塞 MAVLink shell 缓冲——数据流每秒几十行会刷爆它。17 列：`t,ax,ay,az,gx,gy,gz,laser_mm,laser_st,laser_ok,ah_on,ah_m,ah_v,landed,armed,loop_hz`。

## 关键摘录二：sc 自检的判定阈值（202-305）

| 项 | 判定 |
|---|---|
| IMU 芯片 | status==0；WHO_AM_I 0x70=MPU-6500（国产仿制，无磁力计属预期） |
| 加速度计 | 200 帧均值 \|a\| 与 1g 差 ±5% 且单轴 <1.2g；**全 0 = FAIL（SPI 链路不通）** |
| 陀螺仪 | 静止残余 \|w\| <0.05 rad/s；全 0 = FAIL；刚上电偏大属正常（零偏未收敛） |
| 激光 | 现场取 3 个样本（各等 300 ms），状态 0/6 有效即 PASS |

## 关键摘录三：auxdbg 的二选一判据（384-461）

回答"辅助总线上没器件"是 (a) 真没接东西还是 (b) MPU 辅助主机没在跑：

1. 走 SPI 直读 MPU 自身寄存器（WHO_AM_I/PWR_MGMT_1/USER_CTRL/I2C_MST_CTRL/SLV0 组/I2C_MST_STATUS）；
2. 发现 I2C_MST_EN(bit5) 没使能 → 就地打开（user|0x20）+ 时钟 0x0D(400kHz) + 回读确认；
3. **NACK 自测**：故意读必然不存在的 0x7F——置 NACK ⇒ 主机正常、总线上确实没人；不置 ⇒ 主机没真发（结论不可下）。
4. 重探 0x76/0x77。

## 陷阱

- `auxProbe` 判据：v00 == v00b 且 v00 != vD0（真应答时读数随寄存器号变化；NACK 时 EXT_SENS_DATA 是陈旧内容，三次同值）。
- `mon` 频率上限 200 Hz（超过主循环节拍无意义）；丢行统计在 MON_STOP 里（lines/skipped）。
- sc 编号从 [4] 跳到 [6]——沿用旧编号不是漏代码（285 注释）。
- auxscan/auxdbg 阻塞 0.6~0.8 s，只能在地面。

## 关键摘录三：auxProbe 的"应答"判据为什么这么绕

```c
// monitor.ino:92-107（核心）
static bool auxProbe(uint8_t addr, uint8_t *idVal, bool *nacked) {
	uint8_t v00 = 0, vD0 = 0, v00b = 0, st = 0;
	bool nack = false;
	imu.auxReadRegisters(addr, 0x00, 1, &v00);
	if (imu.i2cMasterStatus(&st)) nack = nack || ((st & AUX_NACK_BIT) != 0);
	imu.auxReadRegisters(addr, 0xD0, 1, &vD0);
	if (imu.i2cMasterStatus(&st)) nack = nack || ((st & AUX_NACK_BIT) != 0);
	imu.auxReadRegisters(addr, 0x00, 1, &v00b);
	if (idVal != NULL) *idVal = vD0;
	if (nacked != NULL) *nacked = nack;
	return (v00 == v00b) && (v00 != vD0);
}
```

MPU 的辅助总线读不到 NACK 时，EXT_SENS_DATA 里是**上一次的陈旧内容**——单次读"成功"不代表器件在。三次读数的设计：真应答 ⇒ 读数随寄存器号变（v00≠vD0）且回到同一寄存器值一致（v00==v00b）；假应答 ⇒ 三次同值。NACK 位是硬件直接证据，二者互证。

## sc 自检的输出结构（printSensorCheck 的五段）

```text
[1] IMU 芯片     model/WHO_AM_I/status + MPU-6500 说明（无磁力计属预期）
[2] 加速度计     原始/校准两行 + |a| 判定（±5%）+ 单轴 <1.2g + 全 0 特判
[3] 陀螺仪       原始/校准 + 零偏参数 + |w|<0.05 判定 + 全 0 特判
[4] 激光测距     present/model/mode + 现场取 3 个样本逐个打印
[6] 主循环       loopRate/dt/landed/armed/mode（编号跳过 [5]，沿用旧版）
结论             逐项 + "通过 X 项，失败 Y 项" + 激光排障顺序提示
```

判据刻在打印文本里（"判定基准: 静止时合模长应等于 1g = 9.81，允许 ±5%"）——读输出就能核对，不用翻代码。

## 与 print() 通道的关系（文件头 12-15 的取舍）

sc/auxscan 等诊断**复用 print()**（串口+MAVLink shell+网页控制台三处可见），而 mon 数据流**直接写 Serial**（每秒几十行会刷爆 MAVLink shell 缓冲）。同一文件里两种选择并存，判据是"输出量"：一次性诊断几十行可以镜像，持续数据流不行。
