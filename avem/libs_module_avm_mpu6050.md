# libs/module/avm_mpu6050.c 与 avm_mpu6050.h —— 六轴驱动与姿态解算

> 对应源文件：
> - `references/firmware/Avem/libs/module/avm_mpu6050.c`（共 241 行，A级深注）
> - `references/firmware/Avem/libs/module/avm_mpu6050.h`（共 130 行，A级深注；合并覆盖）
> 互链：[libs_module_avm_i2c.md](libs_module_avm_i2c.md)（底层总线）｜
> [libs_module_avm_pid.md](libs_module_avm_pid.md)（g_Roll 的消费方）｜
> [libs_module_avm_uart.md](libs_module_avm_uart.md)（调用点）｜Docs：`Avem/Docs/03-…`、`04-姿态解算.md`

## 1. 覆盖范围说明

.c = I2C 寄存器操作 + 六轴换算 + **Mahony 四元数互补滤波**（全工程最核心
算法）；.h = 寄存器地图 + 零偏标定 + Kp/Ki/halfT 参数。一体成篇。

## 2. avm_mpu6050.h 逐项表（130 行）

| 元素 | 行号 | 说明 |
| --- | --- | --- |
| 文件头导读 | 3~17 | 寄存器地图/标定/参数三分区 |
| LED0/LED1 位带宏 | 20~24 | PA8 / PD2 |
| `MPU6050_ADDR` | 30~32 | 0xD0 = 0x68<<1（写方向） |
| 配置寄存器 | 34~38 | SMPLRT_DIV/CONFIG/GYRO_CONFIG/ACCEL_CONFIG |
| 数据寄存器 | 40~58 | 0x3B~0x48（加速度/温度/陀螺，大端 H+L） |
| PWR_MGMT_1/WHO_AM_I | 60~63 | 睡眠开关 / 0x68 在位探针 |
| `IMU_SOFTWARE_FIXED` | 66~68 | 零偏补偿开关 |
| 零偏宏 | 70~79 | 陀螺 {1.5853,0.7926,2.0121}°/s、加速度 Z 0.2f |
| `IMU_NOT_CONNECTED` | 81~83 | WHO_AM_I != 0x68 的判据宏 |
| `SixAxis` | 87~99 | 六轴统一载体（float ×6） |
| `Kp/Ki/halfT` | 101~109 | 100.0f / 0.002f / 0.001f（Mahony 参数） |
| 全局输出声明 | 111~114 | `g_Yaw,g_Pitch,g_Roll`、`avm_euler` |
| 函数声明 | 116~128 | 7 个接口 |

`Kp/Ki/halfT` 的口径：halfT=0.001 → 假设 2ms（500Hz）解算周期；Kp=100
意味着姿态失配 1 rad 叠加 100（°/s 量纲）修正；Ki=0.002 慢速消恒定漂移。

## 3. avm_mpu6050.c 逐函数表（241 行）

| 函数 | 行号 | 签名 | 职责 |
| --- | --- | --- | --- |
| `avm_mpu_module_st` | 27~33 | 模块实例 | 模块池[4] |
| `avm_mpu_init` | 35~41 | `static unsigned char (void*)` | 延时 7ms + MPU_init |
| `MPU_Sigle_Write` | 49~62 | `void (uchar, uchar)` | 写单寄存器（无 ACK 时 LED1 亮） |
| `MPU_Sigle_Read` | 68~85 | `unsigned char (unsigned)` | 复合格式读单寄存器 |
| `MPU_GetData` | 88~93 | `short (uchar)` | H<<8\|L 拼 16 位有符号 |
| `MPU_init` | 104~113 | `void (void)` | 配置四件套 + WHO_AM_I 重试 |
| `MPU6050_getStructData` | 121~140 | `void (pSixAxis)` | 读六轴→物理量→零偏 |
| `g_Yaw/g_Pitch/g_Roll/avm_euler` | 161~162 | 全局定义 | 解算输出与数据缓存 |
| `IMU_Comput` | 178~241 | `void (SixAxis)` | **Mahony 四元数解算（7 步）** |
| （被注释）`MPU6050_debug` | 141~157 | — | 串口打印调试痕迹 |

## 4. 关键摘录

### 4.1 寄存器配置（104~113 行，逐字）

```c
void MPU_init() {
    IIC_init();
    do {
        MPU_Sigle_Write(PWR_MGMT_1, 0x00);
        MPU_Sigle_Write(SMPLRT_DIV, 0x07);
        MPU_Sigle_Write(CONFIG, 0x06);
        MPU_Sigle_Write(GYRO_CONFIG, 0x18);
        MPU_Sigle_Write(ACCEL_CONFIG, 0x01);
    } while(IMU_NOT_CONNECTED);      /* WHO_AM_I != 0x68 就一直重试 */
}
```

配置账本：退出睡眠 / 采样分频 1kHz/(1+7)=125Hz / DLPF 5Hz 最重档 /
陀螺 ±2000°/s（16.4 LSB/(°/s)）/ 加速度 **±4g**（A2A1A0=001，8192 LSB/g）。
`IMU_NOT_CONNECTED`（.h 77 行）= `MPU_Sigle_Read(WHO_AM_I)!=0x68`，
传感器不接则整轮重试、启动卡死——最朴素的自诊断。

### 4.2 Mahony 七步（178~241 行的分段锚点）

| 步骤 | 行号 | 代码锚 | 数学本质 |
| --- | --- | --- | --- |
| 1 加速度归一化 | 187~193 | `sqrt(ax²+ay²+az²)` 后逐项除 | 只信方向不信幅值 |
| 2 理论重力 | 197~199 | `vx=2(q1q3-q0q2)` 等 | 旋转矩阵 R 第三列 |
| 3 叉积误差 | 203~205 | `ex=aY*vz-aZ*vy` 等 | ā×v̂ = 轴×sinθ |
| 4a 误差积分 | 209~211 | `g_exInt += ex*Ki` | PI 的 I 项 |
| 4b 补偿陀螺 | 215~217 | `gX += Kp*ex + g_exInt` | PI 的 P 项+I 项 |
| 5 四元数积分 | 222~225 | `q0 += (-q1gX-q2gY-q3gZ)*halfT` | q̇=½q⊗ω 一阶欧拉 |
| 6 归一化 | 229~233 | 除以模长 | 保持单位四元数 |
| 7 转欧拉角 | 238~240 | `asin`/`atan2` ×57.3 | ZYX 反解，rad→度 |

理论重力三行的推导（197~199 行）：惯性系单位重力 (0,0,1) 经四元数旋转到
机体系 = 旋转矩阵第三列；vz 的两种写法 `q0²-q1²-q2²+q3²` 与 `1-2(q1²+q2²)`
在模长=1 时等价（第 6 步保证）。

### 4.3 换算与零偏（121~140 行，逐字）

```c
void MPU6050_getStructData(pSixAxis cache) {
    cache->gX = (float)MPU_GetData(GYRO_XOUT_H)/16.4f;
    cache->gY = (float)MPU_GetData(GYRO_YOUT_H)/16.4f;
    cache->gZ = (float)MPU_GetData(GYRO_ZOUT_H)/16.4f;

    cache->aX = (float)MPU_GetData(ACCEL_XOUT_H)/1671.83f;
    cache->aY = (float)MPU_GetData(ACCEL_YOUT_H)/1671.83f;
    cache->aZ = (float)MPU_GetData(ACCEL_ZOUT_H)/1671.83f;

#ifdef IMU_SOFTWARE_FIXED
    /* 软件校零：把实测零偏加回去（数值在 avm_mpu6050.h，静止放置标定所得） */
    cache->gX += G_X_OFFSET;
    cache->gY += G_Y_OFFSET;
    cache->gZ += G_Z_OFFSET;

    cache->aX += A_X_OFFSET;
    cache->aY += A_Y_OFFSET;
    cache->aZ += A_Z_OFFSET;
#endif
}
```

## 5. 已记录问题（不改代码）

1. **加速度换算口径**：除数 1671.83 ≈ 16384/9.8 是 **±2g 量程换算 m/s²** 的
   系数，而 ACCEL_CONFIG 实际写 0x01（±4g，8192 LSB/g）。因 IMU_Comput 第
   一步就归一化，绝对刻度被约掉、姿态解算不受影响；但若拿该值当真实
   m/s²/g 用则是错的。avm_mpu6050.c 118 行既有注释"±8g 量程 4096 LSB/g"
   与代码同样不自洽（0x01 是 ±4g）——按寄存器手册口径为准。
2. **步骤 5 非严格欧拉积分**：222~225 行右侧的 q 值在同轮被更新（算 q1 时
   q0 已是新值），严格实现应先用旧值快照。500Hz 下误差 O(h²) 可忽略。
3. **°/s 与 rad/s 混用**：halfT 是秒，角速度是 °/s，等价于把 Kp/halfT 联动
   缩放 57.3——参数配套调好，单独改一半就会失衡。
4. **norm 除零无防护**：加速度全零（I2C 失败）时产生 NaN 污染四元数。
5. **Yaw 必然漂移**：无磁力计，ez 误差无绝对参考（物理极限非 bug）。
6. `halfT=0.001` 假设 500Hz，实际解算频率 = 上位机 '$' 请求频率（失配则
   四元数推进速度成比例偏差）。

## 6. 自查命令

```bash
cd references/firmware/Avem
grep -n "IMU_Comput\|MPU6050_getStructData\|MPU_Sigle_Write\|MPU_Sigle_Read\|MPU_GetData\|MPU_init" libs/module/avm_mpu6050.c
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_mpu6050.c
bash /Users/theo-richard/workspace/.qa/strip_equiv_check.sh . libs/module/avm_mpu6050.h
```

延伸阅读：Docs `03-MPU6050与软件I2C.md`（驱动层）、`04-姿态解算.md`
（算法推导+classDiagram/sequenceDiagram）。
