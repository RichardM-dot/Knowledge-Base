# rx_main.cpp —— 接收端主逻辑

对应源文件：`src/src/rx_main.cpp`（共 2635 行）

> 工程：ExpressLRS ｜ 层级：A 级（逐函数中文深注）
> 相关文档：[01-启动流程与状态机](../../../references/firmware/ExpressLRS/Docs/01-启动流程与状态机.md)、
> [02-OTA 空中协议精读](../../../references/firmware/ExpressLRS/Docs/02-OTA 空中协议精读.md)、
> [03-链路质量与自适应速率](../../../references/firmware/ExpressLRS/Docs/03-链路质量与自适应速率.md)

---

## 1. 文件职责

RX 是“空口 → 飞控”的翻译层，同时是三件事的协调者：

1. **解包**：CRC 校验 → 按类型分发（RC / SYNC / DATA）→ 写 `ChannelData`；
2. **定时**：Tock 跳频、Tick 结算 LQ、PFD 相位校准、遥测窗口；
3. **输出**：把通道数据交给 CRSF/SBUS/SUMD/PWM 驱动，把遥测塞回空口。

它比 TX 多两个负担：**相位锁定**（否则无法跟拍）与**多协议串口**（八种输出）。

---

## 2. 关键全局状态

| 名称 | 行号 | 作用 |
|---|---|---|
| `antenna` / `geminiMode` | 122-123 | 当前天线与双射频标志 |
| `ota_crc` | 126 | CRC16 计算器 |
| `eeprom` / `config` | 127-128 | 配置存储 |
| `crsfRouter` / `crsfReceiver` / `otaConnector` | 130-132 | CRSF 路由三件套 |
| `DataDlSender` / `DataUlReceiver` | 159 / 164 | 下行分片发送 / 上行重组 |
| `telemetryBurstCount/Max` | 161-162 | 遥测连发计数 |
| `LQCalc` / `LQCalcDVDA` | 174-175 | 单发/多连发的 LQ 窗口 |
| `LPF_Offset` / `LPF_OffsetDx` | 170-171 | 相位与相位变化率滤波 |
| `LPF_UplinkRSSI0/1` | 177-178 | 每天线 RSSI 滤波 |
| `SnrMean` | 179 | SNR 均值 |
| `scanIndex` / `ExpressLRS_nextAirRateIndex` | 181-182 | 速率轮询下标 |
| `RXtimerState` | 186 | 定时器锁定状态 |
| `LastValidPacket` / `LastSyncPacket` | 198-199 | 两个超时基准 |
| `cycleInterval` / `RFmodeLastCycled` | 210-211 | 轮询节拍 |
| `BindingModeRequest` | 231 | 来自 OTA/按键的绑定请求 |

---

## 3. 函数清单

| # | 函数 | 行号 | 职责 |
|---|---|---|---|
| 1 | `getLq()` | 241 | 对外暴露 LQ |
| 2 | `getGpsTelemetry()` | 251 | GPS 状态快照 |
| 3 | `checkGeminiMode()` | 261 | 决定是否双射频 |
| 4 | `minLqForChaos()` | 279 | “可信连接”的 LQ 下限 |
| 5 | `getRFlinkInfo()` | 304 | RSSI/SNR/天线写入 linkStats |
| 6 | `SetRFLinkRate()` | 375 | 配置射频与 cycleInterval |
| 7 | `HandleFHSS()` | 453 | 跳频执行体 |
| 8 | `LinkStatsToOta()` | 502 | LinkStats 位域压缩 |
| 9 | `HandleSendDataDl()` | 560 | 组下行遥测包 |
| 10 | `HandleFreqCorr()` | 681 | 晶振补偿步进 |
| 11 | `updatePhaseLock()` | 733 | PFD 相位/频率校准 |
| 12 | `HWtimerCallbackTick()` | 781 | 半拍：结算 LQ |
| 13 | `switchAntenna()` | 811 | 切换天线并清滤波 |
| 14 | `updateDiversity()` | 831 | 分集决策状态机 |
| 15 | `HWtimerCallbackTock()` | 911 | 整拍：跳频+分集+遥测+校准 |
| 16 | `LostConnection()` | 964 | 掉线复位 |
| 17 | `TentativeConnection()` | 1006 | 进入试探连接 |
| 18 | `GotConnection()` | 1040 | 升级为已连接 |
| 19 | `ProcessRfPacket_RC()` | 1073 | 处理通道包 |
| 20 | `OnELRSBindMSP()` | 1113 | 写入绑定 UID |
| 21 | `ProcessRfPacket_DataUl()` | 1138 | 处理上行数据 |
| 22 | `updateSwitchModePendingFromOta()` | 1194 | 开关模式两段式确认 |
| 23 | `ProcessRfPacket_SYNC()` | 1239 | SYNC 四层校验与对齐 |
| 24 | `ProcessRFPacket()` | 1335 | 所有包的总入口 |
| 25 | `RXdoneISR()` | 1448 | 收包中断 |
| 26 | `TXdoneISR()` | 1472 | 发完中断（立即回 RX） |
| 27 | `DataUlReceiveComplete()` | 1490 | 上行数据完成后分发 |
| 28 | `setupSerial()` | 1526 | 主串口协议选择 |
| 29 | `setupSerial1()` | 1700 | 第二串口（ESP32） |
| 30 | `serialShutdown()` | 1805 | 关闭串口 |
| 31 | `reconfigureSerial()` | 1822 | 重配串口 |
| 32 | `setupConfigAndPocCheck()` | 1834 | 三插拔计数 |
| 33 | `setupTarget()` | 1860 | 板级初始化 |
| 34 | `setupBindingFromConfig()` | 1881 | 载入 UID |
| 35 | `setupRadio()` | 1905 | 射频初始化与首档 |
| 36 | `updateTelemetryBurst()` | 1953 | 计算连发上限 |
| 37 | `cycleRfMode()` | 1979 | 未连接时轮询档位 |
| 38 | `EnterBindingMode()` | 2016 | 进入绑定 |
| 39 | `ExitBindingMode()` | 2056 | 退出绑定 |
| 40 | `updateBindingMode()` | 2102 | 五个绑定条件判定 |
| 41 | `EnterBindingModeSafely()` | 2185 | 对外入口 |
| 42 | `checkSendLinkStatsToFc()` | 2221 | 100ms 上报飞控 |
| 43 | `debugRcvrLinkstats()` | 2248 | 调试 CSV |
| 44 | `debugRcvrSignalStats()` | 2281 | 每秒射频统计 |
| 45 | `updateSwitchMode()` | 2330 | 应用开关模式切换 |
| 46 | `CheckConfigChangePending()` | 2349 | 配置落盘 |
| 47 | `resetConfigAndReboot()` | 2383 | 清配置重启 |
| 48 | `setup()` | 2406 | 初始化 |
| 49 | `main_loop()` / `loop()` | 2507 / 2509 | 主循环 |
| 50 | `reset_into_bootloader()` | 2624 | 进下载模式 |

---

## 4. 关键函数深读

### 4.1 `ProcessRFPacket()` —— 时间锚点的计算（1335 行）

```c
    // The extEvent defines where TOCK timer ISR is to be synced to, i.e. where the packet period begins.
    // For rates where the TOA is longer than half the packet period schedule the TOCK for rougly 1x TOA before
    // the TX's end of the period so telemetry is received by the TX in the correct period. For all others,
    // schedule TOCK to be PACKET_TO_TOCK_SLACK (us) after RX packet reception.
    int32_t slack = std::max(ExpressLRS_currAirRate_Modparams->interval - 2 * ExpressLRS_currAirRate_RFperfParams->TOA, (int32_t)PACKET_TO_TOCK_SLACK);
    PFDloop.extEvent(beginProcessing + slack);
```

这是全文件最重要的三行。它把“收到包的时刻”换算成“本拍开始的时刻”，
PFD 用这个基准算相位偏差。算错的直接后果是遥测发不到 TX 的接收窗口里。

`PACKET_TO_TOCK_SLACK` 定义在 84 行，值为 200us。

### 4.2 `ProcessRfPacket_SYNC()` —— 四层校验（1239 行）

```c
    // Verify the first byte of the binding ID, which should always match
    if (otaSync->UID4 != UID[4])
        return false;

    // The third byte will be XORed with inverse of the ModelId if ModelMatch is on
    // Only require the first 18 bits of the UID to match to establish a connection
    // but the last 6 bits must modelmatch before sending any data to the FC
    if ((otaSync->UID5 & ~MODELMATCH_MASK) != (UID[5] & ~MODELMATCH_MASK))
        return false;
```

前两层是“是不是我的 TX”，第三层是“这个速率我支持吗”，第四层是“要不要重新对齐”。
注意第二层只比较高 2 位（`~MODELMATCH_MASK` 即 `~0x3F`），低 6 位留给 ModelMatch，
在 `modelMatched` 里单独比对。

```c
    if (connectionState == disconnected
        || OtaNonce != otaSync->nonce
        || FHSSgetCurrIndex() != otaSync->fhssIndex
        || connectionHasModelMatch != modelMatched)
    {
        FHSSsetCurrIndex(otaSync->fhssIndex);
        OtaNonce = otaSync->nonce;
        TentativeConnection(now);
        // connectionHasModelMatch must come after TentativeConnection, which resets it
        connectionHasModelMatch = modelMatched;
        return true;
    }
```

`connectionHasModelMatch` 的赋值必须在 `TentativeConnection()` 之后，因为后者会
把它重置为 false。这个顺序错误会导致 ModelMatch 永远不生效。

### 4.3 `HWtimerCallbackTock()` —— 一拍做六件事（911 行）

```c
void ICACHE_RAM_ATTR HWtimerCallbackTock()
{
    PFDloop.intEvent(micros()); // our internal osc just fired

    if (ExpressLRS_currAirRate_Modparams->numOfSends > 1 && !(OtaNonce % ExpressLRS_currAirRate_Modparams->numOfSends))
    {
        if (LQCalcDVDA.currentIsSet())
        {
            crsfRCFrameAvailable();
            if (teamraceHasModelMatch)
                servoNewChannelsAvailable();
        }
        else
        {
            crsfRCFrameMissed();
        }
    }
    ...
    OtaNonce++;
    HandleFHSS();
    updateDiversity();
    bool tlmSent = HandleSendDataDl();
    updatePhaseLock();
```

顺序的理由：

| 顺序 | 动作 | 为什么在这个位置 |
|---|---|---|
| 1 | `PFDloop.intEvent()` | 记录本机时基，越早越准 |
| 2 | DVDA 组结算 | 要在跳频前决定这一组算不算收到 |
| 3 | `sendImmediateRC()` | 串口输出有固定节拍 |
| 4 | `OtaNonce++` | 后续所有判断都用新 nonce |
| 5 | `HandleFHSS()` | 跳频要在发遥测前 |
| 6 | `updateDiversity()` | 可能要写 GPIO，放在射频操作之间 |
| 7 | `HandleSendDataDl()` | 用新频率发 |
| 8 | `updatePhaseLock()` | 最后校准，不影响本拍已定的时序 |

### 4.4 `LostConnection()` 里的 250us 等待（964 行）

```c
        if (hwTimer::running)
        {
            while(micros() - PFDloop.getIntEventTime() > 250); // time it just after the tock()
            hwTimer::stop();
        }
```

这个 `while` 看起来像死循环，实际条件是“距离上次 Tock 已超过 250us”。
它等到 Tock **执行完之后**再停定时器，避免在 ISR 中途停止造成状态不一致。
注意条件是 `>`，所以最多自旋 250us。

### 4.5 `updateBindingMode()` 的五个分支（2102 行）

```c
    // Exit binding mode if the config has been modified, indicating UID has been set
    if (InBindingMode && config.IsModified())
    {
        ExitBindingMode();
    }
```

第一个分支最容易被忽略：绑定成功的标志不是“收到 UID”，而是“config 被改过”。
因为 `OnELRSBindMSP()` 只写 RAM，`config.IsModified()` 变真就说明 UID 已更新，
此时退出绑定并落盘。

其余四个分支：

| 顺序 | 条件 | 动作 |
|---|---|---|
| 2 | LR1121/LR2021 且绑定中且超 125ms | 在 900M/2.4G 间切换绑定频率 |
| 3 | `PowerOnCounter >= 3` | 进入绑定（三插拔） |
| 4 | `!OtaUidIsBound(UID)` | 进入绑定（新机） |
| 5 | `BindingModeRequest` | 已连接则先断开再绑 |

---

## 5. 设计要点

1. **双回调分工**：Tick 只做 LQ 结算（轻），Tock 做射频与遥测（重）。
   这样即使 Tock 偶尔超时，LQ 统计仍然准确。
2. **相位环两速**：未锁定时用 `raw>>1`（激进），锁定后用 `offset>>2`（平滑）。
3. **所有输出驱动统一接口**：`SerialIO` 子类 + `servoNewChannelsAvailable()`，
   主逻辑不关心具体协议。
4. **绑定与配置强耦合**：UID 写入走 `config`，绑定退出由 `config.IsModified()` 驱动。

---

## 6. 已知问题与观察（只记录，不改代码）

| 位置 | 现象 | 影响 |
|---|---|---|
| `LostConnection()` 964 行 | 忙等最多 250us | ISR 期间调用会拉长中断延迟 |
| `ProcessRFPacket()` 1335 行 | `slack` 可能为负（极高速档） | 已用 `std::max` 兜底为 200us |
| `setupSerial()` 1526 行 | 八种协议全在一个函数里 | 可读性差，但避免虚函数开销 |
| `updateDiversity()` 831 行 | 用函数内 static 保存状态 | 不可重入，但只在 Tock 调用 |
| `loop()` 2509 行 | `(int32_t)` 强转做 millis 回绕比较 | 49 天回绕安全 |

---

## 7. 互链

- 发射端对应实现：[tx_main.md](tx_main.md)
- 状态机与启动：[01-启动流程与状态机](../../../references/firmware/ExpressLRS/Docs/01-启动流程与状态机.md)
- OTA 解包细节：[02-OTA 空中协议精读](../../../references/firmware/ExpressLRS/Docs/02-OTA 空中协议精读.md)
- LQ/dynpower：[03-链路质量与自适应速率](../../../references/firmware/ExpressLRS/Docs/03-链路质量与自适应速率.md)
- 串口协议：[04-CRSF 协议对接](../../../references/firmware/ExpressLRS/Docs/04-CRSF 协议对接.md)
