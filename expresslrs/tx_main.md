# tx_main.cpp —— 发射端主逻辑

对应源文件：`src/src/tx_main.cpp`（共 1920 行）

> 工程：ExpressLRS ｜ 层级：A 级（逐函数中文深注）
> 相关文档：[00-总览与双链路架构](../../../references/firmware/ExpressLRS/Docs/00-总览与双链路架构.md)、
> [01-启动流程与状态机](../../../references/firmware/ExpressLRS/Docs/01-启动流程与状态机.md)、
> [02-OTA 空中协议精读](../../../references/firmware/ExpressLRS/Docs/02-OTA 空中协议精读.md)

---

## 1. 文件职责

TX 是“遥控器 → 空口”的翻译层。它做四件事：

1. 从遥控器/背包/USB 收 CRSF 帧，解析成 16 个通道；
2. 按 Tock 节拍组包（RC / SYNC / 上行 DATA 三选一）并发**射**；
3. 在接收窗口收下行遥测，还原成 LinkStats 与数据，回送遥控器；
4. 维护状态机（noCrossfire → awaitingModelId → disconnected → connected）。

它**不负责**：跳频表生成（`FHSS`）、空口位图打包（`OTA`）、功率曲线（`POWERMGNT`）、
具体协议编解码（`CrsfProtocol`）。这些通过 lib 调用。

---

## 2. 全局对象与常量

| 名称 | 行号 | 类型 | 作用 |
|---|---|---|---|
| `msp` | 69 | `MSP` | 背包/UART 的 MSP 解析器 |
| `config` | 70 | `TxConfig` | EEPROM 配置门面 |
| `TxUSB` | 71 | `Stream*` | USB 串口（可能是 NullStream） |
| `apInputBuffer/apOutputBuffer` | 74-75 | `FIFO<64>` | AirPort 透传双向缓冲 |
| `uartInputBuffer` | 78 | `FIFO<1024>` | MAVLink 上行缓冲 |
| `syncSpamCounter` | 91 | `volatile uint8_t` | 常规同步突发计数 |
| `syncSpamCounterAfterRateChange` | 92 | `volatile uint8_t` | 换速后的同步突发计数 |
| `LqTQly` | 103 | `LQCALC<100>` | 下行遥测成功率 |
| `busyTransmitting` | 105 | `volatile bool` | ISR 与 loop 的互斥标志 |
| `TelemetryRcvPhase` | 113 | `TxTlmRcvPhase_e` | 接收窗口相位 |
| `DataDlReceiver` | 114 | `StubbornReceiver` | 下行数据重组 |
| `DataUlSender` | 115 | `StubbornSender` | 上行数据分片 |
| `ui_devices[]` | 124-143 | `device_affinity_t[]` | 设备表与核心亲和性 |

`syncSpamCounter` 为什么是 `volatile`：它同时被 `timerCallback()`（ISR）与
`SetSyncSpam()`（主循环）读写，编译器不能把它缓存到寄存器。

---

## 3. 函数清单（按调用关系排序，非文件顺序）

| # | 函数 | 行号 | 职责 | 调用者 |
|---|---|---|---|---|
| 1 | `inGeminiMode()` | 163 | 判断是否双射频同发 | 多处 |
| 2 | `switchDiversityAntennas()` | 174 | 翻转天线选择 GPIO | `timerCallback` |
| 3 | `LinkStatsFromOta()` | 193 | 还原下行 LinkStats | `ProcessDownlinkPacket` |
| 4 | `ProcessOtaDataDl()` | 244 | 合并 Gemini 双包并提交 | `ProcessDownlinkPacket` |
| 5 | `ProcessDownlinkPacket()` | 300 | 下行包总入口 | `RXdoneISR` |
| 6 | `UpdateTlmRatioEffective()` | 418 | 计算实际 TLM 比例 | `GenerateSyncPacketData` |
| 7 | `GenerateSyncPacketData()` | 474 | 填 SYNC 字段 | `SendRCdataToRF` |
| 8 | `adjustPacketRateForBaud()` | 516 | 按串口能力降档 | `UARTconnected` |
| 9 | `adjustSwitchModeForAirRate()` | 529 | 按包长修正开关模式 | `OtaUpdateSerializers` 前 |
| 10 | `SetRFLinkRate()` | 547 | 配置射频与定时器 | `ChangeRadioParams` 等 |
| 11 | `SendRCdataToRF()` | 642 | 组包 + 发射 | `timerCallback` |
| 12 | `nonceAdvance()` | 792 | 推进 nonce 与跳频指针 | `timerCallback`（commit 中） |
| 13 | `timerCallback()` | 814 | Tock ISR 总调度 | 定时器 |
| 14 | `UARTdisconnected()` | 875 | 遥控器断开回调 | Handset |
| 15 | `UARTconnected()` | 890 | 遥控器连上回调 | Handset |
| 16 | `ResetPower()` | 916 | 按配置/武装复位功率 | `ChangeRadioParams` |
| 17 | `ChangeRadioParams()` | 946 | 功率 + 速率一起生效 | 多处 |
| 18 | `ModelUpdateReq()` | 960 | 处理模型切换 | CRSF 路由 |
| 19 | `ConfigChangeCommit()` | 987 | 落盘并重配 | `CheckConfigChangePending` |
| 20 | `CheckConfigChangePending()` | 1006 | 安全落盘点 | `loop` |
| 21 | `RXdoneISR()` | 1039 | 收包中断 | 射频驱动 |
| 22 | `TXdoneISR()` | 1061 | 发完中断 | 射频驱动 |
| 23 | `UpdateConnectDisconnectStatus()` | 1128 | 连接/断开判定 | `loop` |
| 24 | `clearOTAQueue()` | 1167 | 清下行队列 | AirPort 切换 |
| 25 | `SetSyncSpam()` | 1176 | UI 改配置后触发同步 | 设备事件 |
| 26 | `SendRxWiFiOverMSP()` | 1191 | 让 RX 进 WiFi | `CheckReadyToSend` |
| 27 | `CheckReadyToSend()` | 1201 | 消费 WiFi 请求 | `loop` |
| 28 | `SendUIDOverMSP()` | 1219 | 发绑定 UID | `EnterBindingMode` |
| 29 | `EnterBindingMode()` | 1236 | 进入绑定 | 按键/UI |
| 30 | `ExitBindingMode()` | 1269 | 退出绑定 | `loop` |
| 31 | `EnterBindingModeSafely()` | 1292 | 对外安全入口 | 设备层 |
| 32 | `ProcessMSPPacket()` | 1305 | 处理 MSP | `ParseMSPData` |
| 33 | `ParseMSPData()` | 1341 | 逐字节喂解析器 | `HandleUARTin` |
| 34 | `HandleUARTout()` | 1359 | AirPort 输出 | `loop` |
| 35 | `HandleUARTin()` | 1383 | 串口输入总入口 | `loop` |
| 36 | `setupSerial()` | 1498 | 建日志口与 USB 口 | `setupTarget` |
| 37 | `setupTarget()` | 1589 | 板级初始化 | `setup` |
| 38 | `setupHardwareFromOptions()` | 1615 | 解析 options.json | `setup` |
| 39 | `setupBindingFromConfig()` | 1638 | 定 UID | `setup` |
| 40 | `cyclePower()` | 1664 | 按键切功率 | 按键 |
| 41 | `checkSendLinkStatsToHandset()` | 1690 | 上报 LinkStats | `loop` |
| 42 | `setup()` | 1726 | 初始化 | Arduino |
| 43 | `loop()` | 1830 | 主循环 | Arduino |

---

## 4. 关键函数深读

### 4.1 `timerCallback()` —— 全机的节拍器（814 行）

```c
void ICACHE_RAM_ATTR timerCallback()
{
  /* If we are busy writing to EEPROM (committing config changes) then we just advance the nonces, i.e. no SPI traffic */
  if (commitInProgress)
  {
    nonceAdvance();
    return;
  }
```

第一道判断是“正在写 EEPROM”。为什么只推进 nonce 而不发射：EEPROM 写会关中断
几十毫秒，此时任何 SPI 操作都可能超时；但 nonce 又必须继续走，否则恢复后
与 RX 的包序号会错开一整段。这个“只走计数、不发包”的折中很精妙。

```c
  // Tx Antenna Diversity
  if ((OtaNonce % ExpressLRS_currAirRate_Modparams->numOfSends == 0 || // Swicth with new packet data
      OtaNonce % ExpressLRS_currAirRate_Modparams->numOfSends == ExpressLRS_currAirRate_Modparams->numOfSends / 2) &&
      TelemetryRcvPhase == ttrpTransmitting) // Only switch when transmitting.  A diversity rx will send tlm back on the best antenna.  So don't switch away from it.
```

三个条件：新包开始、DVDA 组中间、只在发射相位。第三条注释解释了原因：
分集 RX 会从信号最好的天线回传，TX 若在接收窗口切走就会丢遥测。

### 4.2 `SendRCdataToRF()` —— 组包（642 行）

```c
  bool dontSendChannelData = false;
  uint32_t lastRcData = handset->GetRCdataLastRecv();
  if (lastRcData && (micros() - lastRcData > 1000000))
  {
    // The tx is in Mavlink mode and without a valid crsf or RC input.  Do not send stale or fake zero packet RC!
    // Only send SYNC and DATA packets.
    if (config.GetLinkMode() == TX_MAVLINK_MODE)
    {
      dontSendChannelData = true;
    }
    else
    {
      return;
    }
  }
```

“1 秒没收到遥控器数据就不发”是安全设计：发过期通道值比不发更危险
（飞控会以为链路正常）。MAVLink 模式例外，因为它本来就没有 RC 输入。

包类型选择的三级优先：

| 优先级 | 条件 | 结果 |
|---|---|---|
| 1 | `syncSpamCounter` 且 slot 1/2 | SYNC |
| 2 | 常规 sync 到点 且 在同步频道 | SYNC |
| 3 | AirPort / 待发上行 | DATA |
| 4 | 其它 | RC |

### 4.3 `TXdoneISR()` —— 跳频与 RX 窗口（1061 行）

```c
  if (!busyTransmitting)
  {
    return; // Already finished transmission and do not call HandleFHSS() a second time, which may hop the frequency!
  }

  if (connectionState != awaitingModelId)
  {
    const uint8_t modResult = (OtaNonce + 1) % ExpressLRS_currAirRate_Modparams->FHSHopInterval;

    // If TLM enabled and next packet is going to be telemetry, or in LBT mode, start listening to have a large receive window (time-wise)
    const bool nextIsTLM = ExpressLRS_currTlmDenom != 1 && ((OtaNonce + 1) % ExpressLRS_currTlmDenom) == 0;
    const bool doRx = nextIsTLM || LbtIsEnabled;
```

`nextIsTLM` 的判定用 `(OtaNonce+1)` 而不是 `OtaNonce`，因为这里是在“刚发完”的时刻
预判**下一个**包。差一拍就会导致遥测永远收不到。

### 4.4 `UpdateConnectDisconnectStatus()` —— 超时公式（1128 行）

```c
  constexpr unsigned RX_LOSS_CNT = 5;
  // Must be at least 512ms and +2 to account for any rounding down and partial millis()
  const uint32_t msConnectionLostTimeout = std::max((uint32_t)512U,
    (uint32_t)ExpressLRS_currTlmDenom * ExpressLRS_currAirRate_Modparams->interval / (1000U / RX_LOSS_CNT)
    ) + 2U;
```

展开：`1000U / RX_LOSS_CNT = 200`，所以超时 = `TlmDenom * interval / 200` ms。
以 50Hz（20000us）、TLM 1:16 为例：`16 * 20000 / 200 = 1600ms`。
含义是“连续丢 5 个遥测机会”。加 2ms 吸收 millis 的边界误差。

### 4.5 `EnterBindingMode()` —— 顺序决定成败（1236 行）

```c
  hwTimer::stop();
  while (busyTransmitting);

  SendUIDOverMSP();

  OtaCrcInitializer = OTA_VERSION_ID;
  OtaNonce = 0; // Lock the OtaNonce to prevent syncspam packets
  InBindingMode = true; // Set binding mode before SetRFLinkRate() for correct IQ

  SetRFLinkRate(enumRatetoIndexSafe(RATE_BINDING));
  hwTimer::resume();
```

四步顺序都有理由：

| 步 | 动作 | 若顺序错的后果 |
|---|---|---|
| 1 | 停定时器 + 等发射完 | SPI 冲突 |
| 2 | 发 UID（用当前速率） | 切到绑定速率后 UID 包发不出去 |
| 3 | 置 `InBindingMode` | IQ 极性算错，RX 收不到 |
| 4 | `SetRFLinkRate` + 恢复定时器 | — |

---

## 5. 设计要点

1. **ISR 与 loop 的职责切分**：ISR 只做“时序敏感”的事（组包、SPI、跳频），
   其余（串口解析、配置落盘、设备刷新）全在 loop。这让 1000Hz 下 ISR 时长可控。
2. **所有状态迁移走 `setConnectionState()`**：顺带触发设备事件。
3. **配置变更必须“两阶段”**：先标记 modified，再由 loop 在安全点落盘。
4. **绑定/换速/模型切换都靠 SYNC 广播**：TX 不需要知道 RX 是否收到，
   它只负责“多发几次”。

---

## 6. 已知问题与观察（只记录，不改代码）

| 位置 | 现象 | 影响 |
|---|---|---|
| `ProcessOtaDataDl()` 244 行 | Gemini 拼接依赖 `packageIndex` 相等，若两射频丢包不对称会一直等 | 极端情况下下行数据卡住，直到超时重传 |
| `CheckConfigChangePending()` 1006 行 | `while (busyTransmitting);` 是忙等 | 最坏阻塞一个 TOA（约 30ms） |
| `HandleUARTin()` 1383 行 | 变长数组 `uint8_t buf[size]` | size 来自串口可用字节数，上限 1024，安全但占栈 |
| `loop()` 1830 行 | `checkSendLinkStatsToHandset()` 每 100ms 一次 | 高频 CRSF 写入可能挤占串口带宽 |

---

## 7. 互链

- 接收端对应实现：[rx_main.md](rx_main.md)
- 速率表与全局状态：[common_cpp.md](common_cpp.md)、[common_h.md](common_h.md)
- 动态功率：[dynpower_cpp.md](dynpower_cpp.md)
- 延迟执行：[rxtx_common_cpp.md](rxtx_common_cpp.md)
- 协议常量：[crsf_protocol_h.md](crsf_protocol_h.md)
