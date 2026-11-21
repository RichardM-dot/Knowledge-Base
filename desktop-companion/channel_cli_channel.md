# channel_cli_channel.md —— CliOutputChannel 终端替身

> 对应源文件：`references/firmware/DesktopCompanion/host/src/channel/cli_channel.py`（共 28 行，含 B级导读块）
> 注释级别：B级（文件头导读块）
> 关联文档：[05-技能系统与通道](../../../references/firmware/DesktopCompanion/Docs/05-技能系统与通道.md)
> 关联条目：[channel_base.md](channel_base.md)、[cli_interface.md](cli_interface.md)

---

## 1. 文件职责

`OutputChannel` 的终端替身（Debug 实现）：把"本该发往设备"的消息以灰色调试行打印到终端，协议形状（topic + payload dict）与未来真实通道保持一致，方便日后无缝换实现。

## 2. 全文结构

| 行号 | 内容 |
|---|---|
| 1 | 模块 docstring："CLI 输出通道 — Debug 模式下模拟 MQTT 发送" |
| 3-11 | `#` B级导读块：职责/数据流位置/函数清单 |
| 17-19 | `CliOutputChannel.__init__`（持有 Console） |
| 21-28 | `send` 实现（打印两行调试信息，恒返回 True） |

## 3. 关键摘录（cli_channel.py:17-28）

```python
class CliOutputChannel(OutputChannel):
    def __init__(self):
        self.console = Console()

    async def send(self, topic: str, payload: dict) -> bool:
        self.console.print(
            f"  [dim][MQTT→] {topic}[/dim]"
        )
        self.console.print(
            f"  [dim]payload: {payload}[/dim]"
        )
        return True
```

行为要点：

- `[dim]` 灰色渲染——视觉上与正式输出（面板/技能行）区分开；
- 恒返回 `True`：Debug 替身不会失败，上层逻辑无法靠返回值感知真实故障；
- 输出格式 `[MQTT→] {topic}` + `payload: {payload}`——连调试文案都按 MQTT 语义命名，替换成 paho-mqtt 实现时上层调用代码零改动。

## 4. 陷阱与观察

1. **当前无人实例化**：主流程未接线；它是给未来 `send_to_device` 技能预备的"占位实现"，现在手动 `await CliOutputChannel().send("dc/led", {"on": 1})` 可用于测试。
2. **payload 直接 str()**：dict 内含中文时 repr 显示转义形式；实现真实通道时注意 `ensure_ascii=False`（与 pipeline 回喂 JSON 同一坑，见 [scheduler_pipeline.md](scheduler_pipeline.md)）。
3. **与 cli/interface.py 的分工**：interface 是"给人看的输出"（面板/表格），本类是"给设备的输出"（协议帧）——两者别混用。

## 5. 换真实 MQTT 实现的核对清单

| 检查点 | 现状（替身） | 真实实现要做 |
|---|---|---|
| payload 序列化 | f-string 直接打印 | json.dumps(payload, ensure_ascii=False) |
| 失败传播 | 恒 True | publish 信息码/等待 ACK → bool |
| 断线处理 | 无 | reconnect + 发送前检查连接态 |
| 超时 | 无 | 内部 timeout 或 QoS=0 快速失败 |
| 与 device 侧对接 | 无 | 帧格式对齐 06 篇规划稿 |

替换前后上层代码零改动——这就是接口 + 替身模式的价值；本文件 28 行里的协议形状（topic + dict）是唯一的"契约遗产"。

## 6. 自测三问

1. 替身为什么放在 channel/ 而不是 cli/？（答：按"输出方向"分包——channel 是对外协议侧，cli 是对人 UI 侧）
2. `return True` 会不会掩盖问题？（答：Debug 期故意如此；真实实现必须换成真实成败）
3. Console 实例为什么自己建而不用 interface 的？（答：本类独立于界面生命周期，避免耦合 rich 状态）

## 7. 注释后全文件逐行速查（28 行）

| 行 | 内容（缩略） | 类别 |
|---|---|---|
| 1 | `"""CLI 输出通道 — Debug 模式下模拟 MQTT 发送"""` | docstring（原有） |
| 3-11 | `#` B级导读块（职责/数据流位置/函数清单） | 注释 |
| 13-15 | imports（rich Console + OutputChannel） | 代码 |
| 17 | `class CliOutputChannel(OutputChannel):` | 代码 |
| 18-19 | `__init__`（持有 Console） | 代码 |
| 21-28 | `send` 实现（两行灰色 print + return True） | 代码 |

代码 11 行 / 注释 9 行。实现了 ABC 唯一抽象方法，实例化即用，是"接口的最小合格实现"样板。

## 8. 与 interface.py 的展示对照

| 维度 | CliOutputChannel（本文件） | CLIInterface |
|---|---|---|
| 观众 | 设备/协议（未来） | 人 |
| 格式 | `[MQTT→] topic` + payload repr | Live 面板/表格/彩色行 |
| 调用者 | （未来）send_to_device 技能 | pipeline.display |
| 失败表达 | True 恒定 | N/A（展示无成败） |

两套输出并存且互不感知；唯一共享的只有 rich 的 Console 类（各持实例）。

## 9. 自测两问

1. send 的 print 在流式 Live 面板存活期会怎样？（答：Live 会与普通 print 抢屏——真实接线后 send_to_device 调用应先走 request_confirmation 相同的收屏纪律）
2. payload 里带中文会显示成什么？（答：dict repr 转义形式（\uXXXX）；真实实现要 ensure_ascii=False）

## 10. Debug 替身模式的三点收获

1. **替身先于真实实现**：接口定下来立刻给一个恒真替身，上层（未来的技能）可以先行开发测试，不被通信栈阻塞——依赖倒置在个人项目里的落地形态；
2. **替身的形状 = 真实实现的形状**：topic/payload/bool 三件套与 MQTT publish 同构，替换成本趋近于零；替身期间积累的调用代码不是一次性投资；
3. **替身要"诚实地区分自己"**：灰色 `[MQTT→]` 前缀 + dim 样式，让人一眼知道这是模拟输出而非真实发送——替身最怕被当成真的。

对应到更成熟的生态：这就是 test double / stub 模式在架构层的应用，与 freertos 学习中"先写空任务壳再填肉"是同一思路。
