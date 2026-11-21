# USBPD：USB-PD 胶水层（A 级）

> 对应源文件：`source/Core/Drivers/USBPD.cpp`（共 497 行）
> 头文件：`source/Core/Drivers/USBPD.h`
> 工程：`references/firmware/IronOS` ｜ 关联文档：[03-电源管理与 USB PD](../../../references/firmware/IronOS/Docs/03-电源管理与 USB PD.md)

## 职责

把独立的 `Drivers/usb-pd/` 协议栈接到 IronOS 上：提供时间基准、I2C 回调、
四个 DPM（Device Policy Manager）回调，并根据"烙铁电阻"挑一档最合适的电压。

## 架构

```
POWTask ──> USBPowerDelivery::step()
                │
                ▼
         PolicyEngine (usb-pd/)
           ├─ pdbs_dpm_get_sink_capability()   我要什么
           ├─ pdbs_dpm_evaluate_capability()   从对方能力里挑一档
           └─ EPREvaluateCapabilityFunc()      EPR(>20V) 的同一逻辑
```

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `ms_delay` | 31 | tick 化延时（协议栈回调） |
| `get_ms_timestamp` | 39 | tick → ms |
| `USBPowerDelivery::start` | 66 | FUSB302 初始化 + IRQ |
| `IRQOccured` | 78 | 转发硬件事件 |
| `negotiationInProgress` | 87 | 是否还在等第一份能力集 |
| `negotiationComplete` | 96 | 协商完成或超时 |
| `fusbPresent` | 102 | 芯片探测（缓存结果） |
| `isVBUSConnected` | 118 | VBUS 探测（永久缓存） |
| `sqrtI` | 151 | 整数平方根（AVS 用） |
| `parseCapabilitiesArray` | 194 | 选档核心 |
| `EPREvaluateCapabilityFunc` | 325 | EPR 选档 + 构造 Request |
| `pdbs_dpm_evaluate_capability` | 385 | SPR 选档 + 构造 Request |
| `add_v_record` | 440 | 追加一条 Sink PDO |
| `pdbs_dpm_get_sink_capability` | 455 | 构造 Sink Capabilities |

## 选档判据

```cpp
      int voltage_mv             = PD_PDV2MV(PD_PDO_SRC_FIXED_VOLTAGE_GET(lastCapabilities[i])); // voltage in mV units
      int current_a_x100         = PD_PDO_SRC_FIXED_CURRENT_GET(lastCapabilities[i]);            // current in 10mA units
      // 单位推导：mV / (10 mA) = 0.1 Ω，正好与 tipResistance（0.1 Ω 单位）同量纲，
      // 所以可以直接比较，不需要再乘除 100。
      int min_resistance_ohmsx10 = voltage_mv / current_a_x100;
```

（`source/Core/Drivers/USBPD.cpp:218-222`）

物理含义：`V/I` 是"这档电源能驱动的最小电阻"。若它大于烙铁电阻，说明电流不够，
插上会被拉垮。选档不是"电压越高越好"，而是"电流够用的前提下取最高电压"。

`USBPDMode::DEFAULT` 会把电阻加 0.5 Ω（`tipResistance += 5`），用来补偿线缆压降。

## PPS 与 AVS

PPS（可调压）：

```cpp
        int ideal_voltage_mv = (tipResistance * max_current);
```

（`source/Core/Drivers/USBPD.cpp:264`）

`V = I_max × R`：用满这档电流所需的最低电压。再夹到档位上限、20 V、`USB_PD_VMAX`。

AVS（EPR）：

```cpp
        auto ideal_max_voltage = sqrtI((max_wattage * tipResistance) / 10) * 1000;
```

（`source/Core/Drivers/USBPD.cpp:292`）

`V = sqrt(P × R)`，用整数平方根避免引入浮点库。

## Sink Capabilities

```cpp
  cap->obj[numobj++] = PD_PDO_TYPE_FIXED | PD_PDO_SNK_FIXED_VOLTAGE_SET(PD_MV2PDV(5000)) | PD_PDO_SNK_FIXED_CURRENT_SET(DPM_MIN_CURRENT);
  // Voltages must be in order of lowest -> highest
#if USB_PD_VMAX >= 20
  add_v_record(cap, 9000, numobj);
  numobj++;
  add_v_record(cap, 15000, numobj);
  numobj++;
  add_v_record(cap, 20000, numobj);
  numobj++;
```

（`source/Core/Drivers/USBPD.cpp:462-469`）

第一档必须是 5 V；后续按 `USB_PD_VMAX` 递增。电流用 `I = V/R` 现算，
等于向充电器声明真实负载特性。

## 协商结果

```cpp
    requested_voltage_mv    = bestIndexVoltage;
    powerSupplyWattageLimit = bestIndexVoltage * bestIndexCurrent / 100 / 1000; // Set watts for limit from PSU limit
```

（`source/Core/Drivers/USBPD.cpp:416-417`）

单位：`mV × 10mA = 1e-5 W`，`/100/1000` 后得到 W。这个值在
`getX10WattageLimits()` 里乘 10 参与比较。

## 陷阱

- **`negotiationComplete()` 在芯片不存在时返回 true**：上层据此直接走 QC。
- **`isVBUSConnected()` 结果永久缓存**：运行中 VBUS 不会变，缓存省 I2C 时间。
- **`lastCapabilities[11]` 是共享缓冲**，EPR 与 SPR 回调都会覆写它；
  GUI 调试页读的是"最近一次"的能力集。
- 20 V 被额外夹一次（`voltage_mv <= 20000`），因为部分充电器标 21 V 但带载不稳。

## 相关

- [pow_thread.md](pow_thread.md)：调用者
- [qc3.md](qc3.md)：PD 失败后的退路
- [power_cpp.md](power_cpp.md)：功率换算
