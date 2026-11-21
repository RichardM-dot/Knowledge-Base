# src_webui.md —— webui.ino 网页控制界面

> **对应源文件：`references/firmware/flix-hakimi-pio/src/webui.ino`（共 845 行）**
> 手机控制台：状态卡 / 解锁 / 定高四档 / 虚拟摇杆 / 自检与控制台。9 条 HTTP 路由全 GET。
> 延伸阅读：[src_wifi.md](src_wifi.md)、[src_cli.md](src_cli.md)（/cmd 走 doCommand）、[src_althold.md](src_althold.md)（/hold 后端）、Docs《06-地面站链路》§3

## 结构地图

| 行号 | 块 | 内容 |
|---|---|---|
| 66-92 | 基础 | webServer(80)、static 前置声明、extern 飞行状态 |
| 94-118 | 控制台镜像 | WEBLOG_SIZE=4096 环形缓冲、webLogMuted、webLogAppend |
| 120-157 | 延迟命令队列 | webPendingCmd(1 格 64B)、webQueueCommand、白名单 {ca,sc,laser}、webCmdAllowed |
| 159-207 | 虚拟摇杆 | webSticksOn/Active/LastRx、webAxis* 缓存、webSticksInControl、handleWebSticks、applyWebAxes |
| 216-551 | 页面本体 | WEBUI_PAGE PROGMEM 原始字符串（HTML/CSS/JS，状态卡/定高按钮/摇杆/控制台 + 页面 JS 逻辑） |
| 553-584 | 僵死客户端防护 | webClientStalled（0 超时 select）+ webSend/webSendP 统一出口 |
| 586-697 | 状态与定高路由 | handleWebRoot/jsonNum/handleWebStatus/handleWebLog/handleWebHold |
| 699-789 | 摇杆与解锁路由 | handleWebAxes/webBusyGuard/handleWebArm/handleWebDisarm/handleWebCmd |
| 791-832 | 初始化与主循环 | setupWebUI（9 条路由表）、loopWebUI（handleClient + 执行排队命令） |
| 834-845 | WIFI_ENABLED=0 空实现 | 保证关闭 WiFi 仍可链接 |

## 关键摘录一：僵死客户端防护（工程上最隐蔽的一个坑）

```c
// webui.ino:556-573
// ★ 僵死客户端防护：NetworkClient::write() 内部是「10 次 × select(1 s)」的重试循环
// （framework 常量写死：WIFI_CLIENT_MAX_WRITE_RETRY=10、WIFI_CLIENT_SELECT_TIMEOUT_US=1e6）。
// 手机切后台/锁屏后浏览器仍会以 ~1 Hz 轮询 /status，几秒就把 TCP 发送缓冲灌满 —— 那之后
// 每一次响应都会把主循环卡住长达 10 秒（悬停中 10 秒无姿态更新必炸）。
static bool webClientStalled() {
	NetworkClient &c = webServer.client();
	if (!c.connected()) return false;
	int s = c.fd();
	if (s < 0) return false;
	fd_set set;
	FD_ZERO(&set);
	FD_SET(s, &set);
	struct timeval tv = {0, 0};         // 0 超时：纯探测，绝不等待
	return select(s + 1, NULL, &set, NULL, &tv) >= 0 && !FD_ISSET(s, &set);
}
```

所有响应走 webSend/webSendP：探测到写不进 → 立刻断开该客户端、放弃本次响应。

## 关键摘录二：/axes 的安全设计

```c
// webui.ino:700-722（骨架）
	if (!webSticksOn) { webSend(200, "text/plain", "web sticks off"); return; }
	// 必须带全四个轴参数：只要求"至少一个"的话，一条 ?t=0 也能刷新 controlTime 并接管油门
	if (!(webServer.hasArg("r") && hasArg("p") && hasArg("y") && hasArg("t"))) { 400; return; }
	// NaN 拒绝 + constrain 限幅（URL 是手输/脚本可构造的）
	if (hasArg("t")) { v = arg("t").toFloat(); if (valid(v)) webAxisThrottle = constrain(v, 0, 1); }
	webAxesActive = true;
	webAxesLastRx = t;   // 真正写控制量在 applyWebAxes()（loop 里 readRC 之后）
```

`applyWebAxes`（193-207）：webSticksOn && webAxesActive 且 0.5 s 内有数据才回写控制量并刷新 controlTime——网页断开 1 s 后固件失控保护自动接管（不需要另写看门狗）。

## 关键摘录三：/hold 回"实际生效目标"

```c
// webui.ino:646-649（注释）
// 为什么回"实际生效目标"而不是回显请求值：定高目标会被 AH_TGTMAX / AH_HMAX 夹取，
// 前端必须拿夹取后的值来判断"再点一次是不是同一个档位"，否则会退化成"重复进入定高"。
```

cm 参数严格校验（660-669）：toFloat 走 atof，"abc" 得 0 而 0 意味着"退出定高"——非法输入必须 400，不能悄悄关定高。

## 页面 JS 的状态机要点（216-551）

| 机制 | 防的失败模式 |
|---|---|
| /sticks 显式开关 + 打开页面只同步 | 多设备交替写 /axes（0 值盖真实杆量 → 摇摆） |
| localStorage webrc_sender 标记 | 刷新页面自动恢复发送；旁观设备静默 |
| axesSynced：油门未对齐不发 | 一开开关就下发"油门收到底" |
| visibilitychange 主动松杆停发 | 后台定时器节流到 1 Hz 造成杆量抖动 |
| 定高期间左摇杆锁定（页面置灰） | 定高语义：上下锁定 |
| 左摇杆只控油门（偏航取消，固件自保持） | 顺带消除"左下角=上锁手势"隐患 |
| disarm 二次 confirm + 地面起飞 confirm | 误触停桨 / 误触起飞 |

## 陷阱

- /status 的 JSON 里 NaN 要过 jsonNum 退化成 0——裸 nan 是非法 JSON，前端 parse 抛错后 .catch 静默吞掉，状态卡会冻结在旧值（591-596）。
- /cmd 白名单只有 ca/sc/laser；执行经单格队列，busy 时 409。
- /arm 两道门：webBusyGuard（校准期间 409）+ 油门 ≤0.05。
- 页面在 PROGMEM：webSendP 直接从 Flash 发，不拷进 RAM（586-589）。

## 路由表与安全门（setupWebUI 794-814 的完整对照）

| 路由 | handler | 安全门 | 副作用 |
|---|---|---|---|
| / | handleWebRoot | 无（只读） | 回整页（未登记路径也回它） |
| /status | handleWebStatus | 无（只读） | 一行 JSON（900 B 缓冲） |
| /log?since=N | handleWebLog | 无（只读） | 增量控制台文本 |
| /hold?cm= | handleWebHold | cm 纯数字、12~350 | altHoldEngage/Disengage |
| /axes?r&p&y&t | handleWebAxes | webSticksOn + 四参数全 + valid + constrain | 写 webAxis* 缓存 |
| /arm | handleWebArm | webBusyGuard 409 + 油门 ≤0.05 | armed=true |
| /disarm | handleWebDisarm | 无（上锁永远允许） | armed=false + controlThrottle=0 |
| /cmd?c= | handleWebCmd | 白名单 + commandBlockedWhileArmed + 队列 | 排队执行 CLI 命令 |
| /sticks?on= | handleWebSticks | 无（开关本身即安全机制） | webSticksOn/webAxesActive |

## 控制台镜像的环形缓冲数学（94-118）

4096 字节环形缓冲 + uint32_t 累计写入数 webLogTotal：`/log?since=N` 用 `webLogTotal − since` 算增量长度，从 `(webLogHead − count) mod 4096` 开始拷贝——客户端断线重连后用 since 续读，不重不漏（环形覆盖的部分自然缺失）。motd 打印时 webLogMuted=true 的原因：约 1.3 KB 的命令表会把"开机自检输出"的开头挤出 4 KB 缓冲，而后者才是用户要看的东西（cli.ino:367-378 配合）。

## 页面 JS 与固件的 4 个握手点（读 WEBUI_PAGE 时先找这几处）

| JS 函数 | 打的接口 | 握手语义 |
|---|---|---|
| refresh()（500 ms 轮询） | /status | 状态卡 + holdOn/curTarget（按钮配色） |
| refreshLog()（700 ms 轮询） | /log?since= | 控制台增量，>8000 字符裁到尾部 4000 |
| sendAxes()（50 ms 定时器） | /axes | 每 20 帧检查响应不是 "ok" → 服务端已关遥控，本页停发 |
| doToggle/页面加载 fetch('/sticks') | /sticks | 显式开关 + 发送端标记恢复 |

 cmdBusy 600 ms 防连点（371-377）；confirm 只拦两个动作：DISARM 与地面自动起飞（378-381、384-401）。
