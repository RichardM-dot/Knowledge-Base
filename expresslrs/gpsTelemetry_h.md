# gpsTelemetry.h —— GPS 状态快照

对应源文件：`src/include/gpsTelemetry.h`（共 55 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[06-WiFi 配置与固件升级](../../../references/firmware/ExpressLRS/Docs/06-WiFi 配置与固件升级.md)

---

## 1. 文件职责

定义一个与串口/Web 栈完全解耦的 POD 结构 `gps_telemetry_t`，
让 GPS 驱动把“当前状态 + 最新定位”一次性交给 Web UI 或遥测上报方。

设计约束（文件头注释写明）：

> Deliberately a plain POD with no dependency on the serial or web stacks
> so it can live in the global include path.

所以它只 `#include <stdint.h>`，可以被任何模块（包括 native 单测）包含。

---

## 2. 字段表

| 字段 | 行号 | 类型 | 单位/编码 | 说明 |
|---|---|---|---|---|
| `state` | 31 | `uint8_t` | `gpsState_e` | 探测/配置状态机当前状态 |
| `baud` | 32 | `uint32_t` | bps | 已锁定的波特率 |
| `canConfigure` | 33 | `bool` | — | TX 线是否接好（能回配） |
| `protocol` | 34 | `uint8_t` | 0/1/2 | 未知/NMEA/UBX |
| `ubxConfigured` | 35 | `bool` | — | 是否接受 UBX 自动配置 |
| `usedValset` | 36 | `bool` | — | true=VALSET，false=CFG-MSG |
| `navIntervalMs` | 37 | `uint16_t` | ms | 请求的导航间隔 |
| `updateIntervalMs` | 38 | `uint16_t` | ms | 实测帧间隔 |
| `satellites` | 39 | `uint8_t` | 颗 | 卫星数 |
| `fixType` | 40 | `uint8_t` | 0/2/3 | 无/2D/3D |
| `fixValid` | 41 | `bool` | — | 定位可用 |
| `lat` | 42 | `int32_t` | 度 ×1e7 | 纬度 |
| `lon` | 43 | `int32_t` | 度 ×1e7 | 经度 |
| `altCm` | 44 | `int32_t` | cm | 海拔 |
| `speedKmh100` | 45 | `int32_t` | km/h ×100 | 地速 |
| `heading100` | 46 | `uint16_t` | 度 ×100 | 航向 |
| `timeValid` | 47 | `bool` | — | UTC 时间可用 |
| `year` | 48 | `uint16_t` | 年 | |
| `month/day/hour/minute/second` | 49-53 | `uint8_t` | — | 时间分量 |
| `ageMs` | 54 | `uint32_t` | ms | 距最后一帧；0xFFFFFFFF 表示从未收到 |

---

## 3. 定点编码的理由

| 量 | 编码 | 为什么不用 float |
|---|---|---|
| 经纬度 | ×1e7 | CRSF GPS 帧本身就是这个精度 |
| 高度 | cm | 避免 0.01m 的浮点误差 |
| 速度 | km/h ×100 | 与 CRSF 的 0.1km/h 对齐 |
| 航向 | 度 ×100 | 同上 |

嵌入式端浮点运算要么慢（软浮点）要么占 Flash（硬浮点库），
整数定点在 8 位/32 位 MCU 上都更快且可预测。

---

## 4. 读取顺序建议

```cpp
gps_telemetry_t g;
if (getGpsTelemetry(g) && g.fixValid && g.ageMs < 2000) {
    // 使用 g.lat / g.lon
}
```

三步校验：

1. `getGpsTelemetry()` 返回 true 表示驱动在跑；
2. `fixValid` 表示有定位；
3. `ageMs < 2000` 表示数据新鲜（GPS 冷启动可能几分钟没数据）。

---

## 5. 使用方

| 使用方 | 文件 | 用途 |
|---|---|---|
| `getGpsTelemetry()` | `rx_main.cpp` 251 行 | 对外暴露快照 |
| Web `/status.json` | `devWIFI.cpp` 约 392 行 | 页面显示 GPS |
| 遥测上报 | `SerialGPS` | 转发给飞控/地面站 |

---

## 6. 设计要点与观察

1. **纯 POD**：可直接 `memcpy`/按值传递，无虚函数、无构造。
2. **不 include 驱动头**：避免循环依赖，这是它能放在全局 include 路径的原因。
3. **`ageMs` 用 0xFFFFFFFF 哨兵**：`millis()` 回绕时会短暂出现极大值，
   读取方要按无符号处理。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 40 行 | NMEA 协议对任何定位都报 3，无法区分 2D/3D |
| 54 行 | `ageMs` 未定义“0 表示刚收到”还是“未开始计时” |

---

## 7. 互链

- 使用：[rx_main.md](rx_main.md)
- 接口声明：[rxtx_intf_h.md](rxtx_intf_h.md)
