# rxtx_intf.h —— 设备层调用主程序的接口

对应源文件：`src/include/rxtx_intf.h`（共 47 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[01-启动流程与状态机](../../../references/firmware/ExpressLRS/Docs/01-启动流程与状态机.md)

---

## 1. 文件职责

设备（`dev*.h` 里的 LED/按键/WiFi/屏幕）经常要触发主程序动作。
如果每个设备都自己写 `extern` 声明，签名一改就全炸。本文件把这类跨模块
函数集中声明，并用 `TARGET_TX`/`TARGET_RX` 裁剪。

原始注释（文件第 1-5 行）说明了设计意图：

```c
/***
 * This file defines the interface from device units to functions in
 * either rx_main or tx_main (or rxtx_common but exposed to other units)
 * Use this instead of drectly declaring externs in your unit
 ***/
```

---

## 2. 接口清单

| 函数 | 行号 | 可用侧 | 实现位置 | 作用 |
|---|---|---|---|---|
| `EnterBindingModeSafely()` | 29 | TX + RX | 两侧各一份 | 安全进入绑定 |
| `scheduleRebootTime(ms)` | 30 | TX + RX | `rxtx_common.cpp` 126 | 预约重启 |
| `SetSyncSpam()` | 36 | 仅 TX | `tx_main.cpp` 1173 | 触发同步突发 |
| `getLq()` | 44 | 仅 RX | `rx_main.cpp` 241 | 读实时 LQ |
| `getGpsTelemetry(out)` | 46 | 仅 RX | `rx_main.cpp` 251 | GPS 快照 |

### 2.1 同名不同实现

`EnterBindingModeSafely()` 在两侧都有，但行为完全不同：

| 侧 | 行为 |
|---|---|
| TX | 直接调 `EnterBindingMode()`（自己会停定时器） |
| RX | 检查 `serialUpdate`/`wifiUpdate`/`connected` 三种状态再决定 |

这就是为什么必须由本文件统一声明——设备层不需要知道自己在哪一侧，
链接器会自动选对实现。

### 2.2 条件编译

```c
/***
 * TX interface
 ***/
#if defined(TARGET_TX)
void SetSyncSpam();
#endif

/***
 * RX interface
 ***/
#if defined(TARGET_RX)
#include "gpsTelemetry.h"
uint8_t getLq();
// Fills out with a snapshot of the GPS driver state, or returns false if no GPS is running
bool getGpsTelemetry(gps_telemetry_t &out);
#endif
```

`gpsTelemetry.h` 只在 RX 分支 include，避免 TX 编译时拉入不需要的结构。

---

## 3. 调用关系

| 调用者 | 被调 | 场景 |
|---|---|---|
| `devButton` | `EnterBindingModeSafely` | 按键绑定 |
| `devWIFI` | `EnterBindingModeSafely` | Web UI 点 Bind |
| `devWIFI` | `scheduleRebootTime` | 保存后重启 |
| `devTXLUA` | `SetSyncSpam` | Lua 改参数 |
| `devRXLUA` | `getLq` | Lua 显示 LQ |
| `devWIFI`（RX） | `getGpsTelemetry` | 状态页显示 GPS |

---

## 4. 设计要点与观察

1. **单一入口**：所有跨模块调用集中在此，改签名只改一处。
2. **按目标裁剪**：避免在 TX 上引用不存在的 RX 符号。
3. **不含实现**：纯声明，无 inline、无宏，链接期解析。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 29 行 | 两侧签名相同但语义不同，容易误解 |
| 46 行 | 需要 `gpsTelemetry.h`，但 include 放在 `#if` 内，靠调用方自己保证顺序 |

---

## 5. 互链

- 实现：[tx_main.md](tx_main.md)、[rx_main.md](rx_main.md)、[rxtx_common_cpp.md](rxtx_common_cpp.md)
- GPS 结构：[gpsTelemetry_h.md](gpsTelemetry_h.md)
- 状态机：[01-启动流程与状态机](../../../references/firmware/ExpressLRS/Docs/01-启动流程与状态机.md)
