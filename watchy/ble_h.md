# BLE.h —— BLE OTA 服务端声明

> 对应源文件：`src/BLE.h`（共 58 行，含中文深注；注释前 44 行）
> 级别：B级（文件头导读扩展）
> 关联 Docs：[05-Wi-Fi与低功耗实测指南](../../../references/firmware/Watchy/Docs/05-Wi-Fi与低功耗实测指南.md)
> 关联 KB：[ble_cpp.md](ble_cpp.md)

---

## 1. 文件定位

`class BLE` 的声明：BLE OTA 固件升级服务端的对外接口只有 3 个方法。
头文件引入 BLEDevice/BLEServer/BLE2902（ESP32 Arduino BLE 栈）与
esp_ota_ops（分区写入）。

## 2. 类成员表（注释后 L22-42 附近）

| 成员 | 类型 | 作用 |
|---|---|---|
| `local_name` | String | 广播名（保留，实际 begin 参数直传 init） |
| `pServer` | BLEServer* | GATT 服务器 |
| `pESPOTAService` | BLEService* | OTA 门牌服务（cd77498e-…） |
| `pESPOTAIdCharacteristic` | BLECharacteristic* | 只读 ID 特征 |
| `pService` | BLEService* | 数据服务（86b12865-…） |
| `pVersionCharacteristic` | BLECharacteristic* | 只读 HW/SW 版本 5 字节 |
| `pOtaCharacteristic` | BLECharacteristic* | 固件数据通道（WRITE+NOTIFY） |
| `pWatchFaceNameCharacteristic` | BLECharacteristic* | 只读表盘名 |

成员均带类内初始化（NULL），构造/析构为空壳。

## 3. 方法（与 BLE.cpp 对应）

| 方法 | 签名 | 语义 |
|---|---|---|
| `begin` | bool begin(const char *localName) | 建服务并广播；默认参数 "Watchy BLE OTA"（头文件未写默认值，实现在 .cpp 给了默认参——两处不一致的写法） |
| `updateStatus` | int | OTA 状态：-1 未开始 / 0 连接 / 1 传输中 / 2 就绪 / 4 断开 |
| `howManyBytes` | int | 已收字节数（进度条） |

## 4. 使用范式（若自行启用 OTA）

```cpp
BLE BT;
BT.begin("Watchy BLE OTA");
while (true) {
  int st = BT.updateStatus();
  if (st == 2) { esp_restart(); }          // 收完换区重启
  if (st == 4) { break; }                  // 断开退出
  // BT.howManyBytes() 画进度
  delay(100);
}
```

（对照被注释的 `Watchy::updateFWBegin()`，Watchy.cpp:1223 起——状态机
0/1/2/4 分别刷"已连接/下载中/完成重启/断开"四屏。）

## 5. 陷阱

- `begin` 的默认参数写在 .cpp 而不是 .h：**在只包含 BLE.h 的翻译单元里
  调 `BT.begin()` 不带参会编译报错**（默认参必须在声明处）——使用时显式传名；
- 想省 flash：不用 BLE OTA 可在库裁剪 BLE.cpp（1KB 级收益，有限）；
- 实现细节与状态机在 [ble_cpp.md](ble_cpp.md) §5。
