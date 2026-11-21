# src_laser.md —— laser.ino VL53L1X 驱动与软件 I2C 总线层

> **对应源文件：`references/firmware/flix-hakimi-pio/src/laser.ino`（共 1293 行）**
> 四层结构：软件 I2C 波形层 → 寄存器读写层 → ULD 驱动层 → 命令诊断层。核心工程难题：SPI 引脚借还。
> 延伸阅读：[src_althold.md](src_althold.md)（数据消费者）、[src_vl53l1x_regs.md](src_vl53l1x_regs.md)、[src_vl53l1x_default_config.md](src_vl53l1x_default_config.md)、Docs《04-激光定高子系统》

## 分层地图

| 层 | 行号段 | 函数 |
|---|---|---|
| ① 软件 I2C | 163-362 | laserBitDelay / I2cStart / I2cStop / I2cWriteByte / I2cReadByte / ImuAlive / imuAliveWatchdog / BusAcquire / BusRelease / I2cWrite16Reg / I2cRead16Reg / I2cRead8Reg / I2cPing |
| ② 寄存器读写 | 364-398 | laserRead / Read16 / Write / Write16 / Write32（16 位地址、大端、失败计 laserCommErrors） |
| ③ 驱动 | 409-812 | 预算数值表 / Get(Snap/Set)TimingBudget / (Get/Set)DistanceMode / ReadInterruptPolarity / DataReady / InitSensor / (Start/Stop)Continuous / MapStatus / PrintStatusLabel / ParseResults / PollOnce |
| ④ 诊断 | 814-1178 | printLaserBusScan / SelfTest / DumpRegisters / DumpDss / SetDss / printLaserInfo / PeriodMs / SetEnabled / laserCommand |
| ⑤ 主流程 | 1180-1293 | setupLaser / loopLaser |

## 关键全局量

| 行号 | 变量 | 说明 |
|---|---|---|
| 113-117 | laserRate/laserBudgetMs/laserMode | 10 Hz / 50 ms / LONG（改模式与预算需 off→on） |
| 120-124 | laserPresent/Enabled/Trained/IntPolarity/BudgetUsedMs | 器件与驱动状态 |
| 130-146 | laserLastValidFlag/LastValidMs/LastValidDistanceMm/DistanceMm/PeakMcps 等 | 结果缓存（无效帧不覆盖） |
| 152-157 | laserSamples/NotReady/CommErrors/InitMs/BusRestoreFails/BusBorrows | "到底通不通"的计数 |

## 关键摘录一：借/还总线的照抄级细节

```c
// laser.ino:276-287（laserBusRelease 注释即说明书）
//   ① 只能用**无参** SPI.begin()。若把 GPIO5 作为 SS 传进去，SPI 外设会接管该脚，
//      驱动后面手动 digitalWrite(CS) 就压不动片选 —— 症状是 MPU 全程不响应。
//   ② SPIClass::begin() 第一句是 "if (_spi) return true;"，已启动就直接返回 ——
//      所以"再调一次 SPI.begin()"不会把引脚重新挂回外设。必须先 end()（它会
//      spiDetachSCK/MISO/MOSI 并把 _spi 置空）再 begin()。
void laserBusRelease() {
	digitalWrite(LASER_MPU_CS_PIN, HIGH);
	pinMode(LASER_MPU_CS_PIN, OUTPUT);
	SPI.end();
	SPI.begin();
```

一次取样的算术（文件头 18-26 行）：270 bit × 45 µs ≈ 12.2 ms + 归还时序 ≈ 15~16 ms ⇒ 限频 20 Hz（50 Hz 时 SPI 引脚 75% 时间被摘走，控制环退化 20 Hz）。

## 关键摘录二：初始化九步（laserInitSensor 531-604）

型号校验(0xEACC) → 软复位等 bit0 → **91 字节配置块(0x2D~0x87)** → 上拉按 AVDD → **写 DSS_TARGET_RATE=0x0A00（0x0024 不在配置块内，本模块复位默认 0）** → 读中断极性 → 振荡器参数 → **训练测距** → VHV 收尾(0x09/0x00) → 模式+预算。四个 ★ 的"少了会怎样"见 Docs/04 §2 表。

## 关键摘录三：数据就绪与结果解析

```c
// laser.ino:513-515
	static bool laserDataReady() {
		return (laserRead(GPIO__TIO_HV_STATUS) & 0x01) == laserIntPolarity;
	}
	// 极性从 GPIO_HV_MUX__CTRL bit4 读出（506-508），不能写死——Pololu 版写死 bit0==0 在本模块正好相反

// laser.ino:758-762（有效性闸门）
	bool ok = (laserRangeStatusMapped == 0 || laserRangeStatusMapped == 6) &&
	          (laserDistanceMm >= 30) && (laserDistanceMm <= 4000);
	laserLastValidFlag = ok;
```

17 字节映射（683 函数头注释）：[0]=状态(&0x1F)、[3][4]=SPAD 数(8.8 定点)、[7][8]=环境光、[13][14]=**距离 mm**、[15][16]=峰值信号（9.7 定点 /128 = MCPS）。状态映射表 laserMapStatus（631-648）：raw 9→0 有效、raw 19→6 有效未缠绕检查。距离不做 Pololu 的 2011/2048 刻度补偿（726 注释）。

## 关键摘录四：loopLaser 的安全阀与门控

```c
// laser.ino:1245-1293（骨架）
	if (!laserPresent || !laserEnabled) return;            // 门控
	if (laserBusRestoreFails > LASER_BUS_FAULT_LIMIT) {    // 连续 3 次归还失败
		laserPresent = false; laserEnabled = false;
		altHoldDisengage("laser bus fault");
		if (laserImuAlive()) { ...停激光继续飞...; return; }  // 独立复核 IMU
		armed = false; memset(motors, 0, sizeof(motors)); sendMotors();  // 确认失联才停桨
		return;
	}
	if (!(dt > 0) || dt > 0.1f) return;                    // dt 跳变保护
	if (t - last < 1.0f / rate) return;                    // 限频（上限 20 Hz）
	laserPollOnce();                                       // 借总线取一帧
```

## 关键摘录五：imuAliveWatchdog（独立的 SPI 链路看门狗）

```c
// laser.ino:236-243（注释）
// 为什么必须有它：SPI 驱动的读路径不检查事务结果（invensense_imu.cpp
// 的 ReadRegisters 对 SPI 无条件 return true），MISO 悬空/链路断掉时读回全 0xFF，
// INT_STATUS 寄存器的 RAW_DATA_RDY 位恒为真 → imu.ino 的 0.2 s 新鲜度看门狗被骗过，
// 姿态静默冻结 —— 这里的 WHO_AM_I 复核是唯一能抓"SPI 死了但数据就绪信号还在跳"的
// 独立手段（读回 0xFF ≠ 0x70/0x71/0x73，连续 3 次失败即判定失联）。
```

节奏：主循环 50 圈 ≈50 ms 一次；必须由 flix.ino 直接调用（不能关在 laserPresent 门控后）。

## 陷阱

- `laserRead` 失败返回 0——"真的读到 0"要用 laserCommErrors 是否增长区分（368-374）。
- 周期必须 ≥ 预算 + 10 ms（laserPeriodMs 1101-1112），否则测量被下一帧打断、时间戳不可信。
- `laser dss` 的"每 SPAD 回报率"≈0.065 MCPS/SPAD 正常；实际 SPAD≈1.5 个 = 老坑复发。
- 页面 JS 不相关——本文件纯固件；`laser read` 阻塞最多 0.9 s，飞行中被拒。
