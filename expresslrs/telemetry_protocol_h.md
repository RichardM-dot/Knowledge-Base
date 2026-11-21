# telemetry_protocol.h —— 遥测/数据分片常量

对应源文件：`src/include/telemetry_protocol.h`（共 34 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[02-OTA 空中协议精读](../../../references/firmware/ExpressLRS/Docs/02-OTA 空中协议精读.md)

---

## 1. 文件职责

定义“一包 OTA 能装多少业务数据”以及 Stubborn 协议的分片上限。
这些宏被 OTA 打包、StubbornSender/Receiver、CRSF/MSP 转发共用，
改一个就影响空口兼容性。

---

## 2. 全部宏

| 宏 | 行号 | 值 | 推导 |
|---|---|---|---|
| `ELRS4_DATA_DL_SHIFT` | 22 | 1 | 下行序号右移量 |
| `ELRS4_DATA_DL_BYTES_PER_CALL` | 23 | 5 | 8 字节包减 type/crc/索引 |
| `ELRS4_DATA_DL_MAX_PACKAGES` | 24 | 127 | `255 >> 1` |
| `ELRS8_DATA_DL_BYTES_PER_CALL` | 25 | 10 | 13 字节包减开销 |
| `ELRS8_DATA_DL_SHIFT` | 26 | 3 | 下行序号右移量 |
| `ELRS8_DATA_DL_MAX_PACKAGES` | 27 | 31 | `255 >> 3` |
| `ELRS4_DATA_UL_BYTES_PER_CALL` | 29 | 5 | 上行同 5 |
| `ELRS8_DATA_UL_BYTES_PER_CALL` | 30 | 10 | 上行同 10 |
| `ELRS_DATA_UL_BUFFER` | 31 | 65 | 重组缓冲 |
| `ELRS_MSP_MAX_PACKAGES` | 32 | 14 | `65/5 + 1` |
| `AP_MAX_BUF_LEN` | 34 | 64 | AirPort FIFO 深度 |

---

## 3. 推导过程

### 3.1 载荷为什么是 5 或 10

OTA4 包 8 字节的分配：

| 字段 | 字节 |
|---|---|
| 类型 + CRC 高位 | 1 |
| `packageIndex` + `stubbornAck` | 1 |
| 载荷 | 5 |
| CRC 低位 | 1 |

OTA8 包 13 字节：

| 字段 | 字节 |
|---|---|
| 类型 + ack + 索引 | 1 |
| 载荷 | 10 |
| CRC16 | 2 |

### 3.2 包数上限为什么是 127/31

`packageIndex` 的位宽不同：

| 包型 | 索引位宽 | 原始上限 | SHIFT | 实际上限 |
|---|---|---|---|---|
| OTA4 | 7 bit | 127 | 1 | 127 |
| OTA8 | 5 bit | 31 | 3 | 31 |

`ELRS4_DATA_DL_MAX_PACKAGES = 255 >> 1 = 127` 与 7bit 一致；
`ELRS8 = 255 >> 3 = 31` 与 5bit 一致。用 `255 >> shift` 的写法是为了
和“8bit 字段”的直觉对齐。

### 3.3 上行缓冲为什么是 65

```c
#define ELRS_DATA_UL_BUFFER 65
#define ELRS_MSP_MAX_PACKAGES ((ELRS_DATA_UL_BUFFER / ELRS4_DATA_UL_BYTES_PER_CALL)+1)
```

65 / 5 = 13，+1 = 14 包。65 这个数不是整数倍，多出的 1 字节用于存放
“MSP 类型字节”（第一个字节是 opcode，后面才是 payload）。

---

## 4. 使用位置

| 宏 | 使用处 |
|---|---|
| `ELRS4/8_DATA_DL_BYTES_PER_CALL` | `OTA.h` 的 `data_dl.payload` 数组 |
| `ELRS4/8_DATA_DL_MAX_PACKAGES` | `DataDlReceiver.setMaxPackageIndex()` |
| `ELRS4/8_DATA_UL_BYTES_PER_CALL` | `OTA.h` 的 `data_ul.payload` 数组 |
| `ELRS_DATA_UL_BUFFER` | `rx_main.cpp` 的 `DataUlBuffer` |
| `ELRS_MSP_MAX_PACKAGES` | `DataUlReceiver.setMaxPackageIndex()` |
| `AP_MAX_BUF_LEN` | `tx_main.cpp` 的 `apInputBuffer/apOutputBuffer` |

---

## 5. 设计要点与观察

1. **一切从包长推导**：没有魔法数字，每个常量都能从 8/13 字节算出来。
2. **上下行分开定义**：虽然当前值相同，但保留了独立调整的可能。
3. **`SHIFT` 与位宽绑定**：改包结构必须同步改 SHIFT，否则序号回绕。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 25 行 | 注释 `some of this is consumed by LinkStats buuuut....` 说明 10 字节里有一部分被 LinkStats 占用 |
| 32 行 | `ELRS_MSP_MAX_PACKAGES` 只在 ELRS4 载荷下正确，ELRS8 时会偏大 |

---

## 6. 互链

- 包结构：[02-OTA 空中协议精读](../../../references/firmware/ExpressLRS/Docs/02-OTA 空中协议精读.md)
- 使用：[tx_main.md](tx_main.md)、[rx_main.md](rx_main.md)
