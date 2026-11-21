# src_imu.md —— imu.ino IMU 驱动与校准

> **对应源文件：`references/firmware/flix-hakimi-pio/src/imu.ino`（共 299 行）**
> MPU6500（用 MPU9250 库驱动）SPI 驱动、六面校准、陀螺零偏在线估计、两级看门狗之一。
> 延伸阅读：[src_estimate.md](src_estimate.md)（landed 判据在这里被消费）、[src_parameters.md](src_parameters.md)（IMU 校准参数见 [src_imu.md](src_imu.md)）、Docs《02-IMU驱动与姿态估计》

## 全局变量

| 行号 | 变量 | 初值 | 语义 |
|---|---|---|---|
| 62 | `imu` | MPU9250(SPI) | 传 SPI 对象走 SPI（I2C 跑不到 1 kHz） |
| 67 | `imuRotation` | (0,0,π/2) | 安装角欧拉角（参数 IMU_ROT_* 可改） |
| 74/77 | `gyro/gyroBias` | — | 校准后角速度 rad/s / 在线估计的零偏 |
| 80/84/86 | `acc/accBias/accScale` | scale=(1,1,1) | 校准后加速度 m/s² / 六面校准结果 |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 89-93 | `setupIMU` | begin + configureIMU |
| 97-103 | `configureIMU` | ±4g / ±2000dps / DLPF_MAX / 1kHz / 数据就绪中断 |
| 106-127 | `readIMU` | 每圈第一步：阻塞等数据 → 看门狗 → 零偏估计 → 校准 → 旋到机体系 |
| 140-159 | `imuFreshnessWatchdog` | 0.2 s 无新数据 → 上锁停桨（防"陈旧数据闭眼飞"） |
| 164-179 | `calibrateGyroOnce` | Delay(2) 落地确认 + α=0.001 低通估零偏 |
| 192-246 | `calibrateAccel` | 六面各 8 秒；scale 合理性检查与回滚 |
| 249-274 | `calibrateAccelOnce` | 采 1000 样本均值，更新极值，重算 scale/bias |
| 277-281 | `printIMUCalibration` | CLI `imu` 的一部分 |
| 284-299 | `printIMUInfo` | 状态/型号/WHO_AM_I/原始读数 |

## 关键摘录一：读数与坐标变换（readIMU 核心）

```c
// imu.ino:106-127（节选）
void readIMU() {
	imu.waitForData(); // 阻塞等待新数据就绪 —— 这决定了主循环节拍 ≈ IMU 采样率
	imu.getGyro(gyro.x, gyro.y, gyro.z);
	imu.getAccel(acc.x, acc.y, acc.z);
	imuFreshnessWatchdog(); // ★ 数据新鲜度看门狗（见下）
	calibrateGyroOnce(); // 顺手做陀螺零偏在线估计
	acc = (acc - accBias) / accScale;// 校准：acc = (raw - bias) / scale（六面校准的逆变换）
	gyro = gyro - gyroBias;// 真实角速度 = raw_gyro - bias
	Quaternion rotation = Quaternion::fromEuler(imuRotation);
	acc = Quaternion::rotateVector(acc, rotation.inversed());
	gyro = Quaternion::rotateVector(gyro, rotation.inversed());
}
```

坐标变换方向的推导（imu.ino:118-123）：`rotateVector(v, R⁻¹) = R⊗v⊗R⁻¹`，主动旋转 R·v——恰好把芯片系读数按安装旋转 R（芯片系→机体系）转到机体系。

## 关键摘录二：新鲜度看门狗为什么必须存在

```c
// imu.ino:135-137
#define IMU_FRESH_TIMEOUT 0.2f
```

本板 `MPU9250 imu(SPI)` 没接 DRDY 引脚 ⇒ `waitForData()` 走 1 kHz 定时器路径——**IMU 挂掉时主循环不会卡死，而是拿陈旧数据继续输出控制量**（姿态冻结 = 闭眼飞）。这种静默失效必须主动停桨。另一侧的独立防线是 laser.ino 的 imuAliveWatchdog（WHO_AM_I 复核，防"SPI 死了但就绪信号还在跳"）。

## 六面校准数学（imu.ino:14-30 文件头）

```
raw = a·scale + bias
max ≈ +g·scale + bias，min ≈ −g·scale + bias（每轴六朝向各取到一次）
scale = (max − min) / 2g        bias = (max + min) / 2
```

- 采 1000 样本均值消振动（1 kHz ≈ 1 秒）；校准时切 ±2g 最灵敏，结束恢复 ±4g。
- **回滚保护**（229-242）：scale ∉ [0.5,1.5] 判失败回滚——防 (raw−bias)/0 → NaN 永久失效、landed 永远为假。
- **极值是文件级变量且每次开始重置**（181-187）：原 static 版第二次 `ca` 沿用旧极值，错结果还能过检查。

## 陷阱

- `getGyro/getAccel` 用引用参数改写传入变量（108-109），不是返回值。
- 零偏估计被三个条件门控：landed 持续 2 秒 + （未解锁或激光不在位或激光高度 <0.5 m）——飞行中收油门下坠时 |acc| 也会贴 1g，激光高度是"真的在地面"的旁证（164-172）。
- `imu` 命令显示 raw 读数供对比校准效果（284-299）。

## 关键摘录三：calibrateGyroOnce 全文（零偏在线估计）

```c
// imu.ino:164-179
void calibrateGyroOnce() {
	static Delay landedDelay(2); // 必须"landed"持续 2 秒
	bool reallyOnGround = !armed || !laserPresent || (ahHeight < 0.5f);
	if (!landedDelay.update(landed && reallyOnGround)) return; // 只有在确定静止时才进行校准。
	static LowPassFilter<Vector> gyroBiasFilter(0.001);
	gyroBias = gyroBiasFilter.update(gyro); // 更新滤波器
}
```

三个条件缺一不可：landed（estimate 的判据）持续 2 秒；"真的在地面"旁证（三选一：未解锁/激光不在位/激光高度 <0.5 m）；α=0.001 的极慢低通只让"几乎恒定"的分量通过。**零偏估计没有 I 项、没有突跳**——它是纯滤波，收敛速度由 τ≈1 s 决定，刚上电要静置几秒才准。

## 六面校准的操作卡（calibrateAccel 的 6 步，209-227）

| 步骤 | 姿态 | 摆法说明 |
|---|---|---|
| 1/6 | 水平放置 | 机身放平、机头朝前 |
| 2/6 | 机头朝上 | 机头竖直指天 |
| 3/6 | 机头朝下 | 机头竖直指地 |
| 4/6 | 机身左侧向上 | 向右翻倒、右侧贴地 |
| 5/6 | 机身右侧向上 | 向左翻倒、左侧贴地 |
| 6/6 | 机身倒扣 | 顶面朝下 |

每步 pause(8) 静置 8 秒（期间命令行/网页仍响应）；全程约 48 秒。校准中每轴都经历 +1g 与 −1g 各两次以上，min/max 覆盖满量程。webui 的"加速度计六面校准"按钮就是这条链（/cmd?c=ca → doCommand → calibrateAccel）。

## 看门狗矩阵（imu.ino 与 laser.ino 的分工）

| 看门狗 | 位置 | 检测什么 | 为什么两个都要 |
|---|---|---|---|
| imuFreshnessWatchdog | imu.ino:140-159 | 0.2 s 无 new_imu_data | 防拿陈旧数据闭眼飞 |
| imuAliveWatchdog | laser.ino:244-262 | WHO_AM_I 每 50 ms 复核，连续 3 次失败 | SPI 读路径不检查事务结果——MISO 悬空时 INT_STATUS 恒真，第一个看门狗被骗过 |

两个看门狗的触发动作一致：armed=false + motors 清零 + print 告警。第二个必须由 flix.ino 直接调用（不能关在激光门控后），这正是 flix.ino:100 单独一行的原因。

## FlixPeriph 库边界（哪些不在本仓库）

setAccelRange/setGyroRange/setDLPF/setRate/setupInterrupt/waitForData/getGyro/getAccel/auxReadRegisters/i2cMasterStatus/readMpuRegisters 均由 `lib/FlixPeriph` 提供（imu.ino:96 注释明确说明）——本文件只负责"何时调、怎么用返回值、校准数学"。读库行为时去 lib/ 目录（如 waitForData 的定时器路径，imu.ino:130-134 注释指出 IMU.h 的 setupInterruptTimer）。
