# src_wifi.md —— wifi.ino Wi-Fi 接入点与 UDP

> **对应源文件：`references/firmware/flix-hakimi-pio/src/wifi.ino`（共 99 行）**
> SoftAP（SSID 带 MAC 后缀）+ UDP 14550 收发 MAVLink。整体在 `#if WIFI_ENABLED` 内。
> 延伸阅读：[src_mavlink.md](src_mavlink.md)（负载内容）、[src_webui.md](src_webui.md)（HTTP 侧）、Docs《06-地面站链路》§2.1

## 宏与全局

| 行号 | 名称 | 值 | 说明 |
|---|---|---|---|
| 29-32 | WIFI_PASSWORD / UDP_PORT / REMOTE_PORT / REMOTE_ADDR | DILEdile / 14550 / 14550 / 255.255.255.255 | AP 密码与 UDP 端点 |
| 34 | `udp` | WiFiUDP | MAVLink 承载 |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 37-58 | `setupWiFi` | 两步起 AP：临时名读 MAC → 拼 SSID 重起；udp.begin(14550) |
| 62-74 | `sendWiFi` | 定向回传（记住的地面站 IP）或广播 |
| 79-82 | `receiveWiFi` | parsePacket + read |
| 85-97 | `printWiFiInfo` | CLI `wifi`：MAC/SSID/客户端数/IP/远端/连接状态 |

## 关键摘录一：两步起 AP（本版本特色）

```c
// wifi.ino:37-57（节选）
	WiFi.softAP("哈基米", WIFI_PASSWORD);          // 第一步：临时名起 AP 才能拿到 MAC
	String mac = WiFi.softAPmacAddress();          // "AA:BB:CC:E5:F6:78"（17 字符）
	WiFi.softAPdisconnect(true);                   // 关闭临时 AP
	String macSuffix = mac.substring(6, 14);       // 取 "E5:F6:78" 里的后 5 字符... 实际是 8 字符 "E5:F6:78" 的前段
	String ssid = "哈基米-" + macSuffix;
	WiFi.softAP(ssid.c_str(), WIFI_PASSWORD);      // 正式 SSID：多架飞机同场可认机
	udp.begin(WIFI_UDP_PORT);
```

substring(6,14) 取下标 [6,14)（MAC 第 7~14 字符，即后两字节的十六进制文本 "E5:F6"）。

## 关键摘录二：定向回传的建立

```c
// wifi.ino:69-73
	udp.beginPacket(udp.remoteIP() ? udp.remoteIP() : WIFI_UDP_REMOTE_ADDR, WIFI_UDP_REMOTE_PORT);
	// 已收到过地面站包 → 定向回传其 IP；否则广播
	udp.write(buf, len);
	udp.endPacket();
```

默认广播 255.255.255.255；收到任一地面站包后 udp.remoteIP() 变为其 IP——所以**地面站必须"先发一条再收"**。

## 陷阱

- String 传 %s 必须 .c_str()（42-43）；toString() 返回临时 String，指针只在当前语句有效（92-93）。
- sendWiFi 的就绪判断：`softAPIP()==0.0.0.0 且 status!=WL_CONNECTED` 才丢弃（65）。
- WiFi 关闭（WIFI_ENABLED=0）时整个文件不编译——mavlinkStickTime 定义在 control.ino 正是为了这种情况下手势判据仍可链接（control.ino:199-202）。

## 关键摘录三：printWiFiInfo（CLI `wifi` 命令的后端）

```c
// wifi.ino:85-97（节选）
void printWiFiInfo() {
	print("MAC: %s\n", WiFi.softAPmacAddress().c_str());
	print("SSID: %s\n", WiFi.softAPSSID().c_str());
	print("Password: %s\n", WIFI_PASSWORD);
	print("Clients: %d\n", WiFi.softAPgetStationNum()); // 已连接的地面站数量
	print("IP: %s\n", WiFi.softAPIP().toString().c_str()); // 本机 IP（默认 192.168.4.1）
	print("Remote IP: %s\n", udp.remoteIP().toString().c_str()); // 最近通信的地面站 IP
	print("MAVLink connected: %d\n", mavlinkConnected);
}
```

排障读法：`Clients: 0` = 手机/地面站没连上热点；`Clients: 1` 但 `Remote IP: 0.0.0.0` = 连上了但还没发过 UDP 包（地面站必须先发一条）；`MAVLink connected: 0` = 飞控没收到过任何包。

## 一次 UDP 收发的完整生命周期

```text
发送（sendWiFi，mavlink.ino 每条消息调用）：
  beginPacket(目标, 14550) → write(buf, len) → endPacket()
  目标 = remoteIP()（有地面站历史）或 255.255.255.255（广播）
接收（receiveWiFi，processMavlink 每圈调用）：
  parsePacket() 返回负载长度（0 = 无数据）→ read(buf, len) 拷出
  收到任何数据 ⇒ mavlinkConnected = true（mavlink.ino:114，开始发 10 Hz 快速消息）
```

## 与 webui 的分工

| 通道 | 端口 | 内容 | 阻塞性 |
|---|---|---|---|
| UDP 14550 | MAVLink 遥测/控制 | 无连接、可能丢包 | loopWebUI/handleClient 非阻塞 |
| HTTP 80 | 网页控制台 | 面向人的操作 | 见 webui 的僵死客户端防护 |

两路都挂在同一个 SoftAP 上；AP 最大客户端数由 ESP32 默认值决定（一般 4 台），`wifi` 命令的 Clients 字段实时可见。
