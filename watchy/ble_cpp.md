# BLE.cpp —— BLE OTA 蓝牙固件升级实现

> 对应源文件：`src/BLE.cpp`（共 152 行，含中文深注；注释前 135 行）
> 级别：B级（文件头导读扩展）
> 关联 Docs：[05-Wi-Fi与低功耗实测指南](../../../references/firmware/Watchy/Docs/05-Wi-Fi与低功耗实测指南.md)
> 关联 KB：[ble_h.md](ble_h.md)

---

## 1. 文件定位

ESP32 BLE GATT 服务端：手机 App 经两个 Service（OTA 门牌 + 数据通道）
推送固件到 OTA 分区。**注意：默认固件中此路径不可达**——
`Watchy::updateFWBegin()` 整段注释停用（Watchy.cpp:1203-1308），BLE 类
从未被实例化，本文件是"保留的死资产"。

## 2. GATT 布局（注释后 L8-24 的宏 + L104-148）

| UUID 宏 | 值 | 角色 |
|---|---|---|
| SERVICE_UUID_ESPOTA | cd77498e-1ac8-… | OTA 服务门牌（App 靠它识别 Watchy） |
| CHARACTERISTIC_UUID_ID | cd77498f-… | 只读 ID 特征 |
| SERVICE_UUID_OTA | 86b12865-4b70-… | 数据服务 |
| CHARACTERISTIC_UUID_FW | 86b12866-… | 固件数据（WRITE+NOTIFY） |
| CHARACTERISTIC_UUID_HW_VERSION | 86b12867-… | 只读版本 5 字节 |
| CHARACTERISTIC_UUID_WATCHFACE_NAME | 86b12868-… | 只读表盘名 |

包长与状态宏：`FULL_PACKET 512`、`CHARPOS_UPDATE_FLAG 5`、
`STATUS_CONNECTED 0 / STATUS_UPDATING 1 / STATUS_READY 2 / STATUS_DISCONNECTED 4`。

## 3. 全局状态（注释后 L25-33）

`otaHandler`（esp_ota 句柄）、`status`（-1 初始）、`bytesReceived`、
`updateFlag`（是否已 esp_ota_begin）。均为文件级全局——BLE 回调上下文
与查询接口共享的黑板。

## 4. 回调与接口表

| 函数 | 行 | 职责 |
|---|---|---|
| `BLECustomServerCallbacks::onConnect/onDisconnect` | L35-39 | 置 status 0/4 |
| `otaCallback::onWrite` | L62 | OTA 数据接收状态机（核心） |
| `BLE::begin` | L104 | 建 Server/Service/特征并广播 |
| `BLE::updateStatus` | L150 | 读 status |
| `BLE::howManyBytes` | L152 | 读 bytesReceived（进度） |

## 5. onWrite 状态机摘录（注释后 L62-79）

```cpp
void otaCallback::onWrite(BLECharacteristic *pCharacteristic) {
  auto rxData = pCharacteristic->getValue();
  if (!updateFlag) { // If it's the first packet of OTA since bootup, begin OTA
    esp_ota_begin(esp_ota_get_next_update_partition(NULL), OTA_SIZE_UNKNOWN,
                  &otaHandler);
    updateFlag = true;
    status     = STATUS_UPDATING;
  }
  if (_p_ble != NULL) {
    if (rxData.length() > 0) {
      esp_ota_write(otaHandler, rxData.c_str(), rxData.length());
      bytesReceived = bytesReceived + rxData.length();
      if (rxData.length() != FULL_PACKET) {
        esp_ota_end(otaHandler);
        if (ESP_OK == esp_ota_set_boot_partition(
                          esp_ota_get_next_update_partition(NULL))) {
          status = STATUS_READY;
        }
      }
    }
  }
```

状态机逻辑：
1. 首包：`esp_ota_begin` 在下一个 OTA 分区开写（OTA_SIZE_UNKNOWN = 全分区）；
2. 每包 512 字节 `esp_ota_write` 累计；
3. **长度≠512 视为最后一包**：`esp_ota_end` 收尾 + `set_boot_partition`
   切换启动分区 → status=READY（主调方随后 esp_restart 换区）；
4. 每包回写 5 字节 txData 并 `notify()`——App 侧 ACK/流控。

## 6. begin() 的装配序列（L104-148）

BLEDevice::init(名字) → createServer + 连接回调 → 两个 createService →
4 个 createCharacteristic（属性见 §2）→ FW 特征挂 BLE2902 描述符与
otaCallback → start 双服务 → 广播挂 ESPOTA 服务 UUID → 写入
hardwareVersion 5 字节与表盘名。硬件版本 5 字节布局：
{HW_major, HW_minor, SW_major, SW_minor, SW_patch}（config.h 的宏）。

## 7. 陷阱与记录

- **默认不可达**：无任何调用点实例化 BLE 类（updateFWBegin 注释）；
  自定义表盘想用 OTA 需自行 `BLE BT; BT.begin("名字");` 并轮询 updateStatus。
- **`esp_ota_begin` 无错误分支**：失败（分区不足）时 updateFlag 已置 true，
  后续包继续写失败句柄——健壮性缺口，记录不改。
- **"最后一包"判定靠长度**：恰好 512 字节整倍数的固件会多等一个短包，
  App 侧协议须保证收尾发短包。
- **`_p_ble` 成员没用上**：otaCallback 存了 BLE 指针但 onWrite 未使用（死成员）。
- BLE 与 WiFi 同时开会内存紧张（经典 ESP32 限制）；OTA 时不要同时联网。
- 类声明见 [ble_h.md](ble_h.md)。
