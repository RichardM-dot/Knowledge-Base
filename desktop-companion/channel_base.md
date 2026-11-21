# channel_base.md —— OutputChannel 输出通道抽象

> 对应源文件：`references/firmware/DesktopCompanion/host/src/channel/base.py`（共 23 行，含 B级导读块）
> 注释级别：B级（文件头导读块）
> 关联文档：[05-技能系统与通道](../../../references/firmware/DesktopCompanion/Docs/05-技能系统与通道.md)、[06-设备端固件与通信协议](../../../references/firmware/DesktopCompanion/Docs/06-设备端固件与通信协议.md)
> 关联条目：[channel_cli_channel.md](channel_cli_channel.md)、[device_main.md](device_main.md)

---

## 1. 文件职责

"向设备发消息"的抽象接口：只定义 `send(topic, payload) -> bool` 一个方法，作为 Host 对外下发指令的标准出口。README 所述 MQTT 通道尚未实现，本文件只有接口骨架；`cli_channel.py` 是它的 Debug 替身。

## 2. 全文结构

| 行号 | 内容 |
|---|---|
| 1 | 模块 docstring："输出通道抽象接口" |
| 3-13 | `#` B级导读块：职责/现状/数据流位置/函数清单 |
| 18-23 | `OutputChannel(ABC)` + 抽象 `send` |

## 3. 关键摘录（base.py:18-23）

```python
class OutputChannel(ABC):
    """设备输出通道 — send_to_device 技能通过此接口发送消息"""

    @abstractmethod
    async def send(self, topic: str, payload: dict) -> bool:
        ...
```

接口形状正是 MQTT publish 的形状：主题字符串 + 结构化负载 dict + 成败布尔。docstring 点名了未来消费者——`send_to_device` 技能（规划中），实现后 LLM 即可经它驱动设备。

## 4. 在数据流中的位置

```
LLM/技能侧产生输出 → （未来）send_to_device 技能 → OutputChannel.send → 真实通信栈（MQTT/串口）
```

当前接线状态：**主流程尚未调用**（真实输出走 `cli/interface.py` 渲染）。host 侧串口/MQTT 实现落地后，device 侧对应物见 [device_main.md](device_main.md) 的规划稿。

## 5. 陷阱与观察

1. **返回值语义未约定**：bool 是"发送成功"还是"送达确认"？MQTT QoS 不同等级答案不同——实现时应写进 docstring。
2. **topic 无命名规范**：建议 `dc/<device_id>/<action>` 式层级，避免未来多设备冲突。
3. **同步超时缺失**：接口无 timeout 参数——MQTT 断线时 send 可能长期挂起，实现类需内部自带超时。

## 6. 从接口到落地的演进推演（三步）

```
现状          OutputChannel(抽象) + CliOutputChannel(替身) + 无人调用
第一步        实现 MqttOutputChannel(paho-mqtt)：send → client.publish(topic, json.dumps(payload))
第二步        定义 send_to_device 技能（BaseSkill 子类），构造器注入 OutputChannel
第三步        main.py 注册该技能 → LLM schema 自动可见 → 设备侧（见 device_main.md）协议层对接
```

三步中 **base.py 与 cli_channel.py 一行都不用改**——抽象接口的意义就在这：替换实现 = 新增文件，不修改存量（对扩展开放、对修改关闭的最小实践）。

## 7. 自测三问

1. send 的 topic/payload 形状对应 MQTT 的什么？（答：publish 的主题名与消息体；payload dict 对应 JSON）
2. 为什么 send 是 async？（答：与技能体系（async execute）同构，网络 IO 天然异步）
3. 返回 bool 的另一半场景是什么？（答：send_to_device 技能把它转成 SkillResult(success=ret)——False 时 LLM 能感知发送失败）

## 8. 注释后全文件逐行速查（23 行）

| 行 | 内容（缩略） | 类别 |
|---|---|---|
| 1 | `"""输出通道抽象接口"""` | docstring（原文件自带） |
| 3-13 | `#` B级导读块（职责/现状/位置/清单） | 注释 |
| 15-16 | `from abc import ABC, abstractmethod` | 代码 |
| 18 | `class OutputChannel(ABC):` | 代码 |
| 19 | `"""设备输出通道 — send_to_device 技能通过此接口发送消息"""` | 类 docstring（原有） |
| 21-23 | `@abstractmethod async def send(...)` | 代码 |

代码 6 行 / 注释 11 行 / docstring 2 行。这是一份"接口即文档"的最小范本：全文件没有一个可执行语句之外的成员。

## 9. 实现类验收标准（写新通道时逐条对照）

1. send 必须是 async，且**永不抛异常**（对齐技能体系的 SkillResult 契约：失败用返回值表达）；
2. bool 返回值语义要写进实现类的 docstring（发出 ≠ 送达）；
3. payload 一律 dict，序列化放实现内部；
4. 不要在 send 里做 UI 输出——那是 cli/interface.py 的事。

## 10. 自测两问

1. 本文件能单独跑起来吗？（答：能 import 不能运行——ABC 抽象类无实现，实例化会 TypeError）
2. 为什么不用 Protocol（结构化鸭子类型）而用 ABC？（答：工程用 3.7+ 传统 ABC；Protocol 需 typing 扩展——两者皆可，现状更保守）

## 11. 接口方法签名逐 token 解读

```python
    async def send(self, topic: str, payload: dict) -> bool:
```

| token | 含义 | 深层理由 |
|---|---|---|
| `async` | 协程入口 | 网络发送天然异步；与技能 execute 的 async 契约同构，调用侧统一 await |
| `topic: str` | 主题 | MQTT 语义的一等公民；串口实现可把它复用为"目标地址/设备号" |
| `payload: dict` | 负载 | dict 是"序列化方式未定"的表达——JSON/MQTT5 properties 都吃得下 |
| `-> bool` | 成败 | 供技能层转 SkillResult；不用异常对齐全工程"失败即返回值"风格 |

四个 token 各自对应一个下游假设：改任何一个都要连同 send_to_device 技能与设备协议一起审。

## 12. 与本工程其他 ABC 的对比

| ABC | 抽象方法数 | 实现数（当前） | 演进方向 |
|---|---|---|---|
| BaseSkill | execute ×1 | 4 builtin | 增技能不改基类 |
| OutputChannel | send ×1 | 1 替身 | 换 MQTT 实现 |

两个 ABC 形态完全同构：单抽象方法 + 类属性配置。这种"窄接口"是注册表式架构能运作的前提——registry 只认接口签名，不关心实现细节。
