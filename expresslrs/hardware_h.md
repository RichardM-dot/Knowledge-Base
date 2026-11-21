# hardware.h —— 硬件描述符枚举

对应源文件：`src/include/hardware.h`（共 208 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)

---

## 1. 文件职责

定义 180 多个 `nameType` 枚举值，每个值对应 `hardware.json` 的一个键。
运行时用 `hardware_pin()/hardware_flag()/hardware_int()/hardware_float()` 取值，
实现“一份固件支持几百种板子”。

---

## 2. 枚举分组

| 分组 | 行号范围 | 代表值 |
|---|---|---|
| 自定义标记 | 27 | `HARDWARE_customised` |
| 串口 | 28-32 | `HARDWARE_serial_rx/tx`、`serial1_rx/tx` |
| 射频 | 34-55 | `radio_busy`、`radio_dio0/1`、`radio_miso/mosi`、`radio_nss`、`radio_rst`、`radio_sck`、`radio_dcdc`、`radio_rfo_hf`、`radio_rfsw_ctrl`、`radio_tcxo` |
| 天线 | 57-58 | `ant_ctrl`、`ant_group` |
| 功率 | 60-132 | `power_enable`、`power_rxen/txen`、`power_min/max/default`、`power_values`、`power_values_dual`、`power_pdet` |
| 输入 | 134-160 | `joystick`、`five_way1..3`、`button`、`button2` |
| 灯光 | 162-185 | `led`、`led_blue/green/red`、`led_rgb`、`ledidx_rgb_*` |
| 屏幕 | 187-205 | `screen_cs/dc/mosi/rst/sck/sda`、`screen_type`、`screen_bl` |
| 背包 | 207-221 | `use_backpack`、`backpack_boot/en`、`passthrough_baud` |
| I2C/杂项 | 223-236 | `i2c_scl/sda`、`misc_gsensor_int`、`misc_fan_en/pwm/tacho` |
| PWM | 238-242 | `pwm_outputs`、`pwm_outputs_count`、`pwm_out_only` |
| 电压采样 | 244-286 | `vbat*`、`vsrc1/2/3*`（各含 offset/scale/atten/cal） |
| VTX | 288-303 | `vtx_nss/miso/mosi/sck`、`vtx_amp_pwm/vpd/vref` |
| 结束哨兵 | 305 | `HARDWARE_LAST` |

---

## 3. 接口

```c
bool hardware_init();
int hardware_pin(nameType name);
bool hardware_flag(nameType name);
int hardware_int(nameType name);
float hardware_float(nameType name);
const int16_t* hardware_i16_array(nameType name);
const uint16_t* hardware_u16_array(nameType name);
```

| 接口 | 返回 | 缺省行为 |
|---|---|---|
| `hardware_pin` | 引脚号或 -1 | 未定义 → `UNDEF_PIN` |
| `hardware_flag` | bool | 未定义 → false |
| `hardware_int` | int | 未定义 → 0 |
| `hardware_float` | float | 未定义 → 0.0 |
| `hardware_i16_array` | 指针 | 未定义 → nullptr |

---

## 4. 命名约定

| 后缀/前缀 | 含义 | 例子 |
|---|---|---|
| `_2` | 第二颗射频/第二路电源 | `radio_nss_2`、`power_txen_2` |
| `_count` | 数组长度 | `pwm_outputs_count` |
| `_invert` | 低电平有效 | `led_blue_invert` |
| `_values` | 功率曲线数组 | `power_values`、`power_values_dual` |
| `_pdet` | 功率检测（定向耦合器） | `power_pdet`、`pdet_slope` |
| `vsrc1/2/3` | 第 1/2/3 路电压源 | `vsrc2_scale` |

---

## 5. 设计要点与观察

1. **枚举顺序无关**：全部按名字取值，插入新值不影响旧 JSON。
2. **`HARDWARE_LAST` 必须是最后一个**：用于数组边界与校验。
3. **类型靠命名区分**：`_pin` 不写在枚举里（枚举本身不区分类型），
   靠调用的函数决定解释方式。同一个键用错函数会得到 0/-1。
4. **JSON 键与枚举同名**：`HARDWARE_radio_nss` ↔ `"radio_nss"`，
   转换逻辑在 `options.cpp` 里（不在本文件）。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 25 行 | 枚举无显式赋值，新增必须追加在 `HARDWARE_LAST` 之前 |
| 全文 | 缺少“单位”说明（mV? 0.1V?），只能从使用处反推 |

---

## 6. 互链

- 编译期闸门：[targets_h.md](targets_h.md)
- 平台配置：[05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)

---

## 7. 一份最小 hardware.json 示例

下面是一个 2.4G TX 模块的最小可用配置（字段名即枚举名去掉 `HARDWARE_` 前缀）：

```json
{
  "serial_rx": 13,
  "serial_tx": 13,
  "radio_miso": 12,
  "radio_mosi": 11,
  "radio_sck": 10,
  "radio_nss": 9,
  "radio_busy": 8,
  "radio_dio1": 7,
  "radio_rst": 6,
  "radio_dcdc": true,
  "power_txen": 5,
  "power_rxen": 4,
  "power_values": [-18, -15, -12, -9, -6, -3, 0, 3],
  "power_min": 0,
  "power_max": 7,
  "power_default": 3,
  "led": 15,
  "button": 0
}
```

对照关系：

| JSON 键 | 枚举 | 读取函数 |
|---|---|---|
| `radio_nss` | `HARDWARE_radio_nss` | `hardware_pin()` |
| `radio_dcdc` | `HARDWARE_radio_dcdc` | `hardware_flag()` |
| `power_values` | `HARDWARE_power_values` | `hardware_i16_array()` |
| `power_default` | `HARDWARE_power_default` | `hardware_int()` |

---

## 8. 缺字段时的行为

| 情况 | 行为 | 后果 |
|---|---|---|
| 引脚字段缺失 | 返回 `UNDEF_PIN(-1)` | 对应功能被跳过（如无天线切换） |
| 数组字段缺失 | 返回 `nullptr` | `POWER_OUTPUT_VALUES_COUNT` 为 0，该频段被判定不支持 |
| 布尔字段缺失 | false | 保守（如 `radio_dcdc` 默认关闭） |
| 整份 JSON 缺失 | `options_init()` 失败 | 进入 `hardwareUndefined`，只开 WiFi |

第三行是 `isSupportedRFRate()` 的判据基础（见 `common_cpp.md`）。

---

## 9. 读码路径

1. 本文件 25 行起，按分组扫一遍枚举；
2. `lib/OPTIONS/options.cpp`——看 JSON 键如何映射到枚举；
3. `tx_main.cpp` 的 `setupTarget()`——看引脚如何被消费；
4. `lib/POWERMGNT/POWERMGNT.cpp`——看 `power_values` 如何变成寄存器值。
