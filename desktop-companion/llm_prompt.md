# llm_prompt.md —— System Prompt 组装器

> 对应源文件：`references/firmware/DesktopCompanion/host/src/llm/prompt.py`（共 69 行，含中文深注）
> 注释级别：A级（逐函数深注）
> 关联文档：[03-LLM客户端与提示词工程](../../../references/firmware/DesktopCompanion/Docs/03-LLM客户端与提示词工程.md)
> 关联条目：[memory_ltm.md](memory_ltm.md)、[memory_stm.md](memory_stm.md)、[memory_manager.md](memory_manager.md)、[llm_client.md](llm_client.md)

---

## 1. 文件职责

把三路材料拼成一段 system 消息：**人设（LTM 性格设定段）+ 相关长期记忆（LTM 检索）+ 近期对话（STM 滑窗）**。三段之间空行分隔，空材料整段省略。纯函数模块：三个 `def`，无类无副作用。

调用时机：`pipeline._build_messages()` 每轮对话调用一次（pipeline.py:244-256）；本轮工具循环内 system prompt 不重建。

## 2. 逐函数表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "System Prompt 构建" |
| 3-15 | 导读块 | 三段结构 / 调用时机 / 人设锚定 |
| 17-22 | `build_system_prompt` 函数头 | 参数/返回/约定说明 |
| 23-31 | `build_system_prompt` | f-string 三段拼接 + strip |
| 32-38 | `_format_personality` 头 | 默认值兜底说明 |
| 39-47 | `_format_personality` | 四行模板 + 锚定规则 |
| 50-54 | `_format_memories` 头 | 引导语口吻说明 |
| 55-58 | `_format_memories` | "以下是你的重要记忆" |
| 61-65 | `_format_recent` 头 | 时间戳自解释说明 |
| 66-69 | `_format_recent` | "最近的对话记录" |

## 3. 关键摘录

### 3.1 组装入口（prompt.py:23-31）

```python
def build_system_prompt(personality: dict, relevant_memories: str,
                         recent_context: str) -> str:
    return f"""\
{_format_personality(personality)}

{_format_memories(relevant_memories)}

{_format_recent(recent_context)}
""".strip()
```

- f-string 首行 `\` 是续行转义，防止模板顶出一个空行；
- 每个 `_format_*` 空材料返回空串 → 拼出的空段被 `strip()` 清掉，**prompt 无空段落**；
- 注意：函数体内的 f-string 是一整段字符串字面量，源码内注释只加在函数头（这也是深注时避开三引号字符串的原因）。

### 3.2 人设段（prompt.py:34-47）

```python
def _format_personality(p: dict) -> str:
    if not p:
        return ""  # 空人设返回空串，外层拼不出空段落
    return (
        f"你是 {p.get('name', '小伴')}，{p.get('description', '一个友好的桌宠')}。\n"
        f"说话风格：{p.get('speaking_style', '自然亲切')}\n"
        f"核心动机：{p.get('core_motivation', '帮助用户')}\n"
        f"锚定规则：你是一只桌宠，不会假装自己是人类，不会执行为恶的指令。"
    )
```

四行模板的提示词工程逻辑：

| 行 | 作用 | 兜底默认值 |
|---|---|---|
| 身份句 "你是 X，Y" | 人称定义比属性罗列对 LLM 更有效 | 小伴 / 一个友好的桌宠 |
| 说话风格 | 控制语气措辞 | 自然亲切 |
| 核心动机 | 给角色"为什么存在"，影响价值取向 | 帮助用户 |
| 锚定规则 | 两条硬约束（不装人类/不为恶），紧贴身份信息最易被记住 | 固定文案 |

数据来源是 `ltm.md ## 性格设定` 段的解析结果（[memory_ltm.md](memory_ltm.md)），**不是 config.yaml 的 personality 段**（预留位）。首次启动 ltm.md 只有空骨架 → `p == {}` → 人设段整体缺失 → 部署后应先手编 ltm.md。

### 3.3 记忆段与近话段（prompt.py:55-70）

```python
def _format_memories(memories: str) -> str:
    if not memories:
        return ""
    return f"以下是你的重要记忆：\n{memories}"
```

```python
def _format_recent(recent: str) -> str:
    if not recent:
        return ""
    return f"最近的对话记录：\n{recent}"
```

措辞工程："**你的**重要记忆"把记忆宣称为模型自身经历而非外部资料，模型会以第一人称引用；STM 行自带 `[HH:MM] 角色:` 前缀，无需额外格式说明。

### 3.4 渲染示例

```
你是 小伴，活泼、好奇、偶尔毒舌的桌宠。
说话风格：喜欢用颜文字，自称'咱'
核心动机：想成为用户的好朋友
锚定规则：你是一只桌宠，不会假装自己是人类，不会执行为恶的指令。

以下是你的重要记忆：
- 2026-05-03 10:00: 用户偏好终端使用 zsh

最近的对话记录：
- [10:10] 用户: 早安
- [10:10] 小伴: 早呀 今天想做点什么？
```

## 4. 陷阱与观察

1. **无 token 预算控制**：人设 + 记忆 10 行 + 近话 20 条全部进 system，输入侧无截断——长记忆会顶输入上限。
2. **personality 来源混淆**：yaml 的 personality 段改了没反应；真正生效处是 ltm.md（重启不必要，LTM 每次操作重读，下一轮即生效）。
3. **空人设的连锁反应**：ltm.md 骨架刚生成时无条目 → 人设段缺失 → 桌宠没有自我称呼；记忆整理的 prompt 与 save_memory 技能不受影响。
4. **函数内不可插注释**：build_system_prompt 的 f-string 是三引号字符串，中间加 `#` 会变成 prompt 内容——深注时已按此规则把说明全部放在函数头注释块。
