# dynpower.cpp —— 动态发射功率

对应源文件：`src/src/dynpower.cpp`（共 319 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读 + 本条目逐函数表）
> 相关文档：[03-链路质量与自适应速率](../../../references/firmware/ExpressLRS/Docs/03-链路质量与自适应速率.md)

---

## 1. 文件职责

按对端回传的 LQ/RSSI/SNR 实时调整发射功率，目标“刚好够用”。TX 侧做完整决策树，
RX 侧只做“固定功率 / 跟随 TX”二选一。

---

## 2. 常量表

| 宏 | 行号 | 值 | 含义 |
|---|---|---|---|
| `DYNPOWER_LQ_BOOST_THRESH_DIFF` | 46 | 20 | LQ 相对均值骤降多少 → 拉满 |
| `DYNPOWER_LQ_BOOST_THRESH_MIN` | 47 | 50 | LQ 绝对值低于多少 → 拉满 |
| `DYNPOWER_LQ_MOVING_AVG_K` | 48 | 8 | LQ 滑动平均窗口（2 的幂） |
| `DYNPOWER_LQ_THRESH_UP` | 49 | 85 | 兜底升档的 LQ 上限 |
| `DYNPOWER_RSSI_CNT` | 52 | 5 | RSSI 均值样本数 |
| `DYNPOWER_RSSI_THRESH_UP` | 53 | 15 | 灵敏度 +15dB 以下 → 升 |
| `DYNPOWER_RSSI_THRESH_DN` | 54 | 21 | 灵敏度 +21dB 以上 → 降 |
| `DYNPOWER_LQ_THRESH_DN` | 57 | 95 | 降功率要求的最低 LQ |
| `SNR1dB` | 58 | `SNR_SCALE(1.0)` | 1dB 的定点值 |

15/21 之间的 6dB 是迟滞带，防止在阈值附近抖动。

---

## 3. 内部工具

### 3.1 `MovingAvg<K, SHIFT>`（60 行）

```c
template<uint8_t K, uint8_t SHIFT>
class MovingAvg
{
public:
  void init(uint32_t v) { _shiftedVal = v << SHIFT; };
  void add(uint32_t v) {  _shiftedVal = ((K - 1) * _shiftedVal + (v << SHIFT)) / K; };
  uint32_t getValue() const { return _shiftedVal >> SHIFT; };
```

定点滑动平均：内部保存 `value << 16`，避免整数除法丢精度。
K=8 时 `(7*old + new)/8`，编译器会优化成移位与乘法。

### 3.2 静态状态（72-77 行）

| 变量 | 类型 | 用途 |
|---|---|---|
| `dynpower_mavg_lq` | `MovingAvg<8,16>` | LQ 滑动平均 |
| `dynpower_mean_rssi` | `MeanAccumulator<int32_t,int8_t,-128>` | RSSI 直均值 |
| `dynpower_stat_snr` | `StdevAccumulator` | SNR 均值 + 标准差 |
| `dynpower_updated` | `int8_t` | ISR 写入的最新 SNR/哨兵 |
| `dynpower_last_linkstats_millis` | `uint32_t` | 上次遥测时间 |
| `dynpower_curr_rf_idx` | `uint8_t` | 当前速率档，用于检测换档 |

---

## 4. 函数清单

| 函数 | 行号 | 调用者 | 说明 |
|---|---|---|---|
| `DynamicPower_SetToConfigPower()` | 81 | 内部 | 设为用户配置功率 |
| `DynamicPower_Init()` | 86 | `setup()` | 复位平均与状态 |
| `DynamicPower_TelemetryUpdate(snr)` | 92 | `LinkStatsFromOta`（ISR） | 记录本拍 SNR 或 MISSED |
| `DynamicPower_Update(now)` | 98 | `loop()` | 决策树主函数 |
| `DynamicPower_UpdateRx(init)` | 296 | `loop()` / `setupRadio` | RX 功率策略 |

---

## 5. 决策树（`DynamicPower_Update`，98 行）

按代码顺序：

| # | 条件 | 动作 | 行号 |
|---|---|---|---|
| 1 | RSSI ≥ -5dBm | `decPower()` 防过载 | 111 |
| 2 | 未开启动态功率 且 RSSI ≤ -20 | 补回配置功率 | 121 |
| 3 | 武装断链 或 Boost 通道低 | 拉满配置功率 | 135 |
| 4 | 上一拍丢遥测 且 武装 且 有余量 | `incPower()` | 152 |
| 5 | LQ 骤降 20 或 ≤50 | 拉满 | 186 |
| 6a | 用 RSSI 判据（阈值 -127） | 5 点均值比较 | 204-216 |
| 6b | 用 SNR 判据 | 均值 ± kσ | 239-270 |
| 7 | LQ ≤ 85 且前面都没动作 | `incPower()` | 281 |

### 5.1 过载保护

```c
  // power is too strong and saturate the RX LNA
  if (newTlmAvail && (rssi >= -5))
  {
    DBGVLN("-power (overload)");
    POWERMGNT::decPower();
  }
```

-5dBm 已经强到让 LNA 饱和。这是“贴脸飞反而失控”的根因。

### 5.2 SNR 自适应阈值

```c
      int32_t scale_factor_numerator = 13;
      if (lq_current < 100) {
        // scale factor will be 1 (=-0.25 sd for power up threshold) when LQ is 85
        scale_factor_numerator = std::max((int32_t)(scale_factor_numerator - ((100 - lq_current) * (scale_factor_numerator - 1)) / 15), (int32_t)1);
      }
```

| LQ | 分子 | 等效 k |
|---|---|---|
| 100 | 13 | 3.25σ |
| 92 | 7 | 1.75σ |
| 85 | 1 | 0.25σ |

LQ 越差越激进，符合“远处宁可多花功率”的直觉。

### 5.3 升功率的等效增益

```c
    while ((snrScaled <= snr_stat_threshold_up) && (powerHeadroom > 0) && isSignalBad)
    {
      POWERMGNT::incPower();
      // Every power doubling will theoretically increase the SNR by 3dB, but closer to 2dB in testing
      snrScaled += SNR_SCALE(2);
      --powerHeadroom;
    }
```

每次升档“假装”SNR +2dB 再比较，属于安全侧偏差（实测增益 2dB < 理论 3dB）。

---

## 6. RX 侧：`DynamicPower_UpdateRx()`（296 行）

```c
  if (config.GetPower() != PWR_MATCH_TX)
  {
    POWERMGNT::setPower((PowerLevels_e)config.GetPower());
  }
  else
  {
    static uint8_t powerLevel = 0;
    if (linkStats.uplink_TX_Power != powerLevel)
    {
      powerLevel = linkStats.uplink_TX_Power;
      PowerLevels_e newPower = crsfPowerToPower(linkStats.uplink_TX_Power);
      DBGLN("Matching TX power %u", newPower);
      POWERMGNT::setPower(newPower);
    }
    else if (initialize)
    {
      POWERMGNT::setPower(POWERMGNT::getDefaultPower());
    }
  }
```

`static powerLevel` 缓存上一次的 TX 功率档，只在变化时写 PA。
`initialize` 分支保证启动时即使值相等也设一次默认功率。

---

## 7. 设计要点与观察

1. **早退式决策树**：顺序即优先级，新增判据必须想清楚插在哪。
2. **三种统计各司其职**：滑动平均（LQ）、直均值（RSSI）、均值+标准差（SNR）。
3. **安全侧偏差**：升功率预估 2dB/档，低于理论值；降功率要求 LQ≥95。
4. **ISR 只写一个字节**：`dynpower_updated` 是 `int8_t`，写入是原子的。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 111 行 | 过载保护不受“动态功率开关”控制，固定功率下也会降档 |
| 152 行 | 丢遥测升档依赖 `isArmed`，未武装时链路差也不会升 |
| 239 行 | `scale_factor_numerator` 在 LQ=100 时恒为 13，不再随 SNR 调整 |

---

## 8. 互链

- 接口：[dynpower_h.md](dynpower_h.md)
- 调用方：[tx_main.md](tx_main.md)、[rx_main.md](rx_main.md)
- 策略详解：[03-链路质量与自适应速率](../../../references/firmware/ExpressLRS/Docs/03-链路质量与自适应速率.md)
