# src_mavlink.md —— mavlink.ino MAVLink 协议通信

> **对应源文件：`references/firmware/flix-hakimi-pio/src/mavlink.ino`（共 410 行）**
> MAVLink v1 over Wi-Fi UDP：遥测两档频率 + 10 类接收消息。坐标系 FLU↔FRD 转换只在此文件。
> 延伸阅读：[src_wifi.md](src_wifi.md)（UDP 承载）、[src_parameters.md](src_parameters.md)（PARAM_* 后端）、Docs《06-地面站链路》§2

## 常量与全局

| 行号 | 名称 | 值 | 说明 |
|---|---|---|---|
| 23-27 | SYSTEM_ID / RATE_SLOW / RATE_FAST | 1 / 1 / 10 | 系统 ID 与两档频率 |
| 32 | mavlinkConnected | false | 收到任何包即 true（开始发快速消息） |
| 33 | mavlinkPrintBuffer | String | CLI 输出暂存（4 KB 封顶） |

## 逐函数表

| 行号 | 函数 | 职责 |
|---|---|---|
| 36-39 | `processMavlink` | loop 每圈：send + receive（非阻塞） |
| 42-98 | `sendMavlink` | 1 Hz 心跳/落地状态；10 Hz 四元数/RC/电机/IMU（连接后） |
| 103-108 | `sendMessage` | pack → to_send_buffer → sendWiFi 三步之第②③步 |
| 111-124 | `receiveMavlink` | 收 UDP → 逐字节 mavlink_parse_char → handleMavlink |
| 127-380 | `handleMavlink` | 10 类消息分发（见下表） |
| 384-390 | `mavlinkPrint` | CLI 输出追加（>4096 B 不再追加） |
| 394-408 | `sendMavlinkPrint` | 按 70 B 分片发 SERIAL_CONTROL（MULTI 标志） |

## 接收消息表（handleMavlink）

| 消息 | 行号 | 行为与防线 |
|---|---|---|
| MANUAL_CONTROL | 133-151 | /1000 归一化写 control*；**NaN 整帧拒**；刷新 mavlinkStickTime（禁手势）+ controlTime |
| PARAM_REQUEST_LIST | 154-165 | 逐个回 PARAM_VALUE（带总数/序号） |
| PARAM_REQUEST_READ | 168-188 | 按名或序号；param_id 16B 可能无 '\0' → strlcpy；按序号读恒拷 16 B 的越界读已修（179-182） |
| PARAM_SET | 191-207 | parameterCheck + setParameter；拒绝时回显实际值（防界面谎报） |
| MISSION_REQUEST_LIST | 210-218 | 回 0 条任务（让 QGC 满意） |
| SERIAL_CONTROL | 221-236 | count==0 丢弃（原实现会把未初始化 data 当命令）；doCommand(echo=true) |
| SET_ATTITUDE_TARGET | 239-282 | **必须已解锁**；NaN 拒（thrust=+Inf 会让缓降永不到 0）；刷新 controlTime；type_mask → invalidate |
| SET_ACTUATOR_CONTROL_TARGET | 285-313 | 必须已解锁；NaN 拒（下游无清洗）；三级目标全置无效 → 控制链跳过；motors constrain |
| LOG_REQUEST_DATA | 316-339 | 按 ofs/count 发；**单次最多 20 行**（原版一次 1000 包 ≈70 KB 卡死控制环） |
| COMMAND_LONG | 342-379 | 版本请求 / ARM_DISARM（油门 ≤0.05）/ DO_SET_MODE；恒回 COMMAND_ACK |

## 关键摘录一：自动解锁漏洞的封堵

```c
// mavlink.ino:246-250
		// 安全门：必须**先解锁**才接受外部姿态/推力目标。原实现按报文内容自动解锁
		// （armed = thrust > 0），而 DO_SET_MODE(AUTO) 本身不检查解锁 —— 于是
		// "连上热点发两条报文"就能让四个电机转起来，绕过解锁手势与油门低位检查。
		// 注意停机方向仍然放行（推力 0 → 上锁停桨），那是既定的"停机指令"语义。
		if (!armed) return;
```

## 关键摘录二：controlTime 必须刷新的理由

```c
// mavlink.ino:260-263
		// ★ 这条报文是"外部控制在飞"的证据，必须和 MANUAL_CONTROL 一样刷新 controlTime：
		// 否则纯地面站 AUTO 飞行时失控保护对"地面站断流"完全失明（controlTime 停在
		// 遥控丢失前的旧值或恒为 0），GCS 崩溃/断连后飞机会带着最后的油门闭环自稳
		// 一直飞到电池耗尽 —— brownout 已被 disableBrownOut() 关掉，连重启停桨都不会发生。
		controlTime = t;
```

## 关键摘录三：NaN 防线（三层各拒一种注法）

- MANUAL_CONTROL：`!(valid(m.x) && ... )` 整帧拒——controlThrottle=NaN 会让 `thrustTarget<0.1` 恒假，姿态环保护整段失效（139-141）；
- SET_ATTITUDE_TARGET：thrust=+Inf 让 descend 的 `thrustTarget −= dt/10` 永远减不到 0（Inf−有限=Inf），停桨分支不可达（253-255）；
- SET_ACTUATOR_CONTROL_TARGET：constrain 是宏对 NaN 原样放行，`round(NaN)` 未定义行为（298-303）。

## 坐标系转换（FLU→FRD）

```c
// mavlink.ino:76-77
		mavlink_msg_attitude_quaternion_pack(..., time, attitude.w, attitude.x, -attitude.y, -attitude.z,
			rates.x, -rates.y, -rates.z, zeroQuat); // convert to frd
```

规则：MAVLink 用 FRD（前右下），固件 FLU（前左上），**y、z 取负**即完成转换；接收侧 SET_ATTITUDE_TARGET 反向同样取负（268-274）。

## 陷阱

- 未连接时只发心跳（64）——地面站靠心跳发现飞控。
- m.data 是 70 字节定长数组、可能无 '\0'，m.count 才是有效长度（226-229）。
- SERIAL_CONTROL 路径的命令**不受网页白名单限制**，但受 commandBlockedWhileArmed（在 doCommand 里统一拦）。

## 关键摘录四：sendMavlink 的两档节流

```c
// mavlink.ino:52-64（节选）
	static Rate slow(MAVLINK_RATE_SLOW), fast(MAVLINK_RATE_FAST); // 两档限频器
	if (slow) {
		mavlink_msg_heartbeat_pack(...);   // 心跳：保活 + 解锁/模式标志
		sendMessage(&msg);
		if (!mavlinkConnected) return; // send only heartbeat until connected
		mavlink_msg_extended_sys_state_pack(...);  // 落地状态
		sendMessage(&msg);
	}
	if (fast && mavlinkConnected) { ...四元数/RC/电机/IMU 四条... }
```

Rate 对象做 static 局部变量——限频状态跨调用保留。心跳必须周期发：地面站靠"多久没心跳"判掉线。未连接时快速消息全部不发（省带宽 + 防广播风暴）。

## 心跳里的模式标志（57-61）

```c
		(armed ? MAV_MODE_FLAG_SAFETY_ARMED : 0) |
		((mode == STAB) ? MAV_MODE_FLAG_STABILIZE_ENABLED : 0) |
		((mode == AUTO) ? MAV_MODE_FLAG_AUTO_ENABLED : MAV_MODE_FLAG_MANUAL_INPUT_ENABLED),
		mode, MAV_STATE_STANDBY);
```

custom_mode 直接填固件 mode（0~3，与 RAW/ACRO/STAB/AUTO 常量对应）；DO_SET_MODE 也按同一映射接受（366-371）——双向一致，地面站模式显示不会漂。

## 与网页控制台的通道隔离

| 输出 | 走哪 | 上限 |
|---|---|---|
| print() 的内容 | mavlinkPrintBuffer → SERIAL_CONTROL 分片 | 4096 B 封顶（超了静默丢弃） |
| 遥测 | 直接 sendMessage | 无缓冲（Rate 节流） |
| 收到的 CLI 命令 | doCommand(echo=true) | doCommand 重入保护 + commandBlockedWhileArmed |

所以 `log dump` 经 MAVLink shell 只能看到前 4 KB——长输出必须走 USB 串口（384-390 的注释明说这是权衡过的行为："这段输出没有转发到 MAVLink 终端"比 OOM 可控）。
