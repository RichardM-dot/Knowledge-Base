# bma.h —— BMA423 封装层声明

> 对应源文件：`src/bma.h`（共 118 行，含中文深注；注释前 91 行）
> 级别：A级（逐声明深注）
> 关联 Docs：[03-BMA400与中断唤醒](../../../references/firmware/Watchy/Docs/03-BMA400与中断唤醒.md)
> 关联 KB：[bma_cpp.md](bma_cpp.md)

---

## 1. 文件组成

| 区块 | 注释后行 | 内容 |
|---|---|---|
| 导入 | L5-13 | ARDUINO 分支引 Arduino.h；核心是 `#include "bma423.h"`（Bosch） |
| 方向枚举 | L15-24 | DIRECTION_* 6 朝向 |
| 类型别名 | L26-28 | Accel（bma4_accel）/ Acfg（bma4_accel_config） |
| 类声明 | L30-118 | BMA423 全部方法与私有成员 |

## 2. 方向枚举（L15-24）

| 值 | 枚举 | 语义 |
|---|---|---|
| 0 | DIRECTION_TOP_EDGE | 顶边朝上 |
| 1 | DIRECTION_BOTTOM_EDGE | 底边朝上 |
| 2 | DIRECTION_LEFT_EDGE | 左边朝上 |
| 3 | DIRECTION_RIGHT_EDGE | 右边朝上 |
| 4 | DIRECTION_DISP_UP | 屏面朝上 |
| 5 | DIRECTION_DISP_DOWN | 屏面朝下 |

判定算法见 [bma_cpp.md](bma_cpp.md) §3.2；表盘可用它做横竖屏或"翻腕切页"。

## 3. 方法分组（行号为注释后）

| 组 | 方法 | 行 | 一句话 |
|---|---|---|---|
| 生命周期 | begin（含默认地址参数） | L40 | 注入回调+灌固件+配 INT1 |
| 电源 | softReset/shutDown/wakeUp/selfTest | L52-56 | 停机语义见 KB bma_cpp |
| 朝向 | getDirection | L60 | 6 朝向 |
| 加速度计 | set/getAccelConfig、getAccel、getAccelEnable、disable/enableAccel | L63-68 | ODR/量程/原始三轴 |
| 中断 | setINTPinConfig、getINT、getIRQMASK、disable/enableIRQ | L71-75 | 引脚形态+快照+映射 |
| 中断查询 | isStepCounter/isDoubleClick/isTilt/isActivity/isAnyNoMotion | L77-81 | 对 __IRQ_MASK 位测试 |
| 计步 | resetStepCounter、getCounter | L84-85 | 芯片自主累计 |
| 温度 | readTemperature、readTemperatureF | L88-89 | 1/512 ℃/LSB |
| 调试 | getErrorCode/getStatus/getSensorTime | L92-94 | — |
| 特性 | getActivity、setRemapAxes、enableFeature | L97-99 | — |
| 中断映射开关 | enableStepCountInterrupt 等 ×5 | L101-106 | 默认参数对应 stock 三件套 |

## 4. 私有成员（L108-118）

| 成员 | 作用 |
|---|---|
| `__readRegisterFptr`/`__writeRegisterFptr` | I2C 回调（Watchy 的 static 成员注入） |
| `__delayCallBlackFptr` | ms 延时回调（Arduino delay） |
| `__address` | I2C 地址（默认 0x18） |
| `__IRQ_MASK` | 最近一次 getINT() 的中断快照 |
| `__init` | begin 成功标志（幂等保护） |
| `__devFptr` | Bosch 驱动设备上下文 bma4_dev |

双下划线前缀是作者习惯（保留）；`__devFptr` 挂着全部回调与分片参数，
是驱动层的"对象"。

## 5. 使用范式（表盘作者视角）

```cpp
extern RTC_DATA_ATTR BMA423 sensor;   // Watchy.h 已声明

uint32_t steps = sensor.getCounter();          // 每分钟读步数
sensor.resetStepCounter();                     // 0 点清零
bool tilted = sensor.getINT() && sensor.isTilt(); // 清醒时消费中断
uint8_t dir  = sensor.getDirection();          // 朝向切屏
float  t     = sensor.readTemperature();       // 环境粗温
```

深睡计步不靠本类的方法调用（芯片自己跑），本类只是"清醒后的读数窗口"。

## 6. 与 Bosch 原生 API 的映射

- `getAccel` ↔ `bma4_read_accel_xyz`
- `enableFeature` ↔ `bma423_feature_enable`（+ step_detector 特判）
- `enable*Interrupt` ↔ `bma423_map_interrupt(BMA4_INTR1_MAP, ..., en)`
- `getINT` ↔ `bma423_read_int_status`

想越过封装用冷门特性（ANY_NO_MOTION 阈值等），直接调 bma423.h 的
原生函数并传 `&__devFptr` 等价物即可（第三方头不在注释范围）。
