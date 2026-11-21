# bma.cpp —— BMA423 封装层实现（逐函数精读）

> 对应源文件：`src/bma.cpp`（共 392 行，含中文深注；注释前 288 行）
> 级别：A级（逐函数中文深注）
> 关联 Docs：[03-BMA400与中断唤醒](../../../references/firmware/Watchy/Docs/03-BMA400与中断唤醒.md)
> 关联 KB：[bma_h.md](bma_h.md)、[watchy_cpp.md](watchy_cpp.md)

---

## 1. 文件定位

Bosch 官方驱动（bma4.c/bma423.c，第三方不注释）之上的一层薄皮：
注入 I2C 回调、包装常用 API、维护 `__IRQ_MASK` 中断快照。
使用方只有 `Watchy::_bmaConfig()`（冷启动）与表盘（读步数/朝向/温度）。

## 2. 逐函数表

| # | 函数 | 注释后行 | 职责 | 关键点 |
|---|---|---|---|---|
| 1 | 构造/析构 | L16/23 | 指针清空 | __init=false |
| 2 | `begin()` | L36 | 注入回调→复位→灌固件→配 INT1 | 幂等；config file ~1KB |
| 3 | `softReset()` | L97 | 写 RESET 魔数 | 单独调后需重走 begin |
| 4 | `shutDown()`/`wakeUp()` | L107/114 | advance_power_save 开关 | 语义与名字相反 |
| 5 | `getErrorCode()` | L119 | 错误寄存器 | — |
| 6 | `getStatus()` | L125 | 芯片状态 | — |
| 7 | `getSensorTime()` | L131 | 39bit 传感器时间 | 时序调试 |
| 8 | `selfTest()` | L137 | 加速度自检 | — |
| 9 | `getDirection()` | L150 | 6 朝向判定 | 轴重映射后的坐标系 |
| 10 | `readTemperature()` | L188 | 芯片温度 | 0x80=无效→返回 0 |
| 11 | `readTemperatureF()` | L204 | 华氏版 | 0 原样返回 |
| 12 | `getAccel()` | L218 | 三轴原始值 | 失败时 acc=0 |
| 13 | `getAccelEnable()` | L229 | 使能位查询 | — |
| 14 | `disableAccel()/enableAccel()` | L235/241 | 加速度计开关 | 特性引擎的前提 |
| 15 | `setAccelConfig()/getAccelConfig()` | L248/252 | Acfg 读写 | ODR/量程/带宽 |
| 16 | `setRemapAxes()` | L259 | 轴重映射 | 适佩戴方向 |
| 17 | `resetStepCounter()` | L266 | 步数清零 | 表盘 0 点自动调 |
| 18 | `getCounter()` | L274 | 读步数 uint32 | 失败也返 0（陷阱） |
| 19 | `setINTPinConfig()` | L282 | INT 引脚电气形态 | — |
| 20 | `getINT()` | L293 | 读 INT 状态快照 | 读后芯片自动清零 |
| 21 | `getIRQMASK()` | L297 | 返回快照 | — |
| 22 | `disableIRQ()/enableIRQ()` | L303/311 | INT1 映射开关 | 默认 STEP_CNTR_INT |
| 23 | `enableFeature()` | L322 | 特征引擎开关 | STEP_CNTR 先开 detector |
| 24-28 | `isStepCounter/isDoubleClick/isTilt/isActivity/isAnyNoMotion` | L332-345 | 快照位测试 | 无 I2C |
| 29-33 | `enable*Interrupt()` ×5 | L349-374 | 中断→INT1 映射 | 薄封装 |
| 34 | `getActivity()` | L380 | 活动分类字符串 | 需 enable ACTIVITY |

## 3. 关键摘录

### 3.1 begin() 的幂等保护（L36-44）

```cpp
  /* 幂等保护：重复 begin 直接当成功；任一回调整除同样拒绝 */
  if (__init || readCallBlack == nullptr || writeCallBlack == nullptr ||
      delayCallBlack == nullptr) {
    return true;
  }
```

`__init` 置位后 begin 恒 true——深睡唤醒路径不会（也不该）重配芯片。
固件载入（L71-75）：

```cpp
  /* 载入特征引擎固件：计步/翻腕/抬腕唤醒算法都在其中 */
  if (bma423_write_config_file(&__devFptr) != BMA4_OK) {
    DEBUG("BMA423 Write Config FAIL\n");
    return false;
  }
```

`read_write_len = 8`（L55）限定单次 I2C 事务 8 字节，驱动据此分片
搬运 1KB 特征固件——begin 耗时主要在这里（数百 ms 量级）。

### 3.2 朝向判定（L150-178）

```cpp
  uint16_t absX = abs(acc.x);
  uint16_t absY = abs(acc.y);
  uint16_t absZ = abs(acc.z);

  if ((absZ > absX) && (absZ > absY)) {
    if (acc.z > 0) {
      return DIRECTION_DISP_DOWN;
    } else {
      return DIRECTION_DISP_UP;
    }
  } else if ((absY > absX) && (absY > absZ)) {
```

"最大分量轴定主方向，符号定正反面"。注意 z>0 映射到 DISP_DOWN：
重力方向定义下的屏幕朝向语义，与表壳贴装的重映射参数配套理解。

### 3.3 温度换算（L188-201）

```cpp
  int32_t data = 0;
  bma4_get_temperature(&data, BMA4_DEG, &__devFptr);
  float res = (float)data / (float)BMA4_SCALE_TEMP;
  /* 0x80 - temp read from the register and 23 is the ambient temp added.
   * If the temp read from register is 0x80, it means no valid
   * information is available */
  if (((data - 23) / BMA4_SCALE_TEMP) == 0x80) {
    res = 0;
  }
```

1/512 ℃/LSB（BMA4_SCALE_TEMP=512）、偏置 23；0x80 无效值换算后恰好 0，
与真 0℃ 冲突——固件约定 0 即无效（退化天气显示 0℃ 的假象，容忍性设计）。

### 3.4 中断消费（L293-295、L332-345）

```cpp
bool BMA423::getINT() {
  return bma423_read_int_status(&__IRQ_MASK, &__devFptr) == BMA4_OK;
}
...
bool BMA423::isStepCounter() {
  return (bool)(BMA423_STEP_CNTR_INT & __IRQ_MASK);
}
```

一次 I2C 快照 + 多次位测试。芯片侧"读后自清"，同一事件只能消费一次。

### 3.5 enableFeature 的特判（L322-330）

```cpp
bool BMA423::enableFeature(uint8_t feature, uint8_t enable) {
  if ((feature & BMA423_STEP_CNTR) == BMA423_STEP_CNTR) {
    bma423_step_detector_enable(enable ? BMA4_ENABLE : BMA4_DISABLE,
                                &__devFptr);
  }
  return (BMA4_OK == bma423_feature_enable(feature, enable, &__devFptr));
}
```

计步要先开 step_detector 再开计数器——封装把两步合并，调用方无感。

## 4. 陷阱与记录（不改代码）

- **`getCounter()` 失败返 0**：I2C 故障与"真 0 步"不可区分（L274-279）。
- **`readTemperature()` 的 0℃ 歧义**：无效值与真 0℃ 同返回 0。
- **`abs(acc.x)` 的 -32768 边界**：int 绝对值后存 uint16_t，理论上 INT16_MIN
  溢出（实际加速度达不到）。
- **`shutDown()/wakeUp()` 命名反直觉**：advance_power_save=DISABLE 才是停机
  （Bosch 语义），封装未改名——用前读注释。
- **`__IRQ_MASK` 初值 0**：begin 后未 getINT 前调 is*() 全 false，不报错。
- **getActivity 需先 enable ACTIVITY**：stock `_bmaConfig` 未启用，恒 "None"。
- 官方驱动层（bma4.c/bma423.c/bma4_defs.h，约 2.5 万行）为 Bosch 版权代码，
  本次按 C 级不注释；想深入特征引擎读 `bma423_write_config_file` 与
  `bma423_map_interrupt` 即可。
