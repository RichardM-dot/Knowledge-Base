# llm_client.md —— LLMClient 流式客户端

> 对应源文件：`references/firmware/DesktopCompanion/host/src/llm/client.py`（共 148 行，含中文深注）
> 注释级别：A级（逐类逐函数深注）
> 关联文档：[03-LLM客户端与提示词工程](../../../references/firmware/DesktopCompanion/Docs/03-LLM客户端与提示词工程.md)
> 关联条目：[llm_prompt.md](llm_prompt.md)、[scheduler_pipeline.md](scheduler_pipeline.md)、[skill_base.md](skill_base.md)

---

## 1. 文件职责

与大模型通信的唯一出口：封装 `AsyncOpenAI` 的流式 chat.completions 调用；SSE 分片实时回调渲染；把散落的 tool_call 残片按协议 index 拼装成完整 ToolCall；兼容 DeepSeek 风格 `reasoning_content`。客户端**无状态**——messages 每次全量传入，不存会话历史。

兼容任意 OpenAI 协议端点（base_url/api_key/model 来自 llm.env，见 [config_loader.md](config_loader.md)）。

## 2. 逐类逐函数表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "LLM 客户端 — 兼容 OpenAI 协议，支持流式输出" |
| 3-12 | 导读块 | 职责/协议/无状态声明 |
| 20-25 | `ToolCall` | id / function_name / arguments(已解析 dict) |
| 28-40 | `LLMResponse` | content / tool_calls / reasoning_content + has_tool_calls |
| 42-47 | 类头注释 | 单例连接池复用；参数来源 |
| 48-54 | `__init__` | AsyncOpenAI 实例 + 四参数 |
| 58-148 | `chat_stream` | 请求构造 → SSE 循环 → 三类 delta 处理 → 拼装收尾 |

## 3. 关键摘录

### 3.1 请求构造（client.py:65-78）

```python
        kwargs = {
            "model": self.model,
            "messages": messages,
            "temperature": self.temperature,
            "max_tokens": self.max_tokens,
            "stream": True,
            "stream_options": {"include_usage": True},  # 让最后一个 chunk 携带 token 用量
        }
        if tools:
            # 不传 tools 字段 = 本次调用禁用 function calling（记忆整理用）
            kwargs["tools"] = tools

        # 返回的是 AsyncStream，真正发请求在此 await；SSE 分片在下面循环里逐个到
        stream = await self.client.chat.completions.create(**kwargs)
```

`tools` 缺省 `None` 时**整个字段不发送**——记忆整理请求（pipeline.py:212）靠这个禁用工具调用，杜绝递归。

### 3.2 tool_call 残片拼装（client.py:107-125）

```python
            # 工具调用 (delta 累积)
            if delta.tool_calls:
                for tc_delta in delta.tool_calls:
                    idx = tc_delta.index
                    if idx not in tc_buf:
                        tc_buf[idx] = {
                            "id": "",
                            "function_name": "",
                            "arguments": "",
                        }
                    if tc_delta.id:
                        # id 只在首个 delta 出现，直接覆盖即可
                        tc_buf[idx]["id"] = tc_delta.id
                    if tc_delta.function:
                        # name 与 arguments 都是增量片段，必须字符串拼接
                        if tc_delta.function.name:
                            tc_buf[idx]["function_name"] += tc_delta.function.name
                        if tc_delta.function.arguments:
                            tc_buf[idx]["arguments"] += tc_delta.function.arguments
```

**按 index 而非 id 建桶的原因**：流式协议下只有每个调用的第一个 delta 带 id，后续残片只有 index——以 id 为 key 会丢桶。name/arguments 用 `+=` 拼接；一个长参数 JSON 可能被拆几十个残片。

### 3.3 JSON 残缺降级（client.py:127-148 摘段）

```python
        for idx in sorted(tc_buf.keys()):
            buf = tc_buf[idx]
            try:
                args = json.loads(buf["arguments"])
            except (json.JSONDecodeError, TypeError):
                # 流被截断导致 JSON 残缺时降级为空参数：
                # 技能层会因缺参数而返回失败结果，LLM 下轮自行纠正
                args = {}
```

失败前移策略：空参数 → 技能返回失败 SkillResult → 原文回喂 → 模型自纠。整条链路无异常打断对话。

三类 delta 的处理对照：

| delta 字段 | 处理 | 行号 |
|---|---|---|
| `content` | append + on_text 回调（await，渲染反压） | 93-99 |
| `reasoning_content` | `getattr` 兜底（SDK 不映射未知字段） | 101-105 |
| `tool_calls[i]` | tc_buf 按 index 拼桶 | 107-125 |

## 4. 陷阱与观察

1. **include_usage 开了没消费**：usage-only 尾包在 :89-91 被跳过，token 计量被丢弃；要统计成本时从尾 chunk 取 `chunk.usage`。
2. **`content="".join(parts) or None`**（:145）：空文本 → None；reasoning-only 回复（content/tool_calls 双空）走"无回复"路径。
3. **on_text 反压**：回调 await，终端渲染慢会拖慢整个流式消费——设计上的背压保护，不是 bug。
4. **空 choices 包**：`chunk.choices[0] if chunk.choices else None`（:89）——include_usage 尾包 choices 为空列表，直接下标会 IndexError，此守卫必须有。
5. **sorted(tc_buf.keys())**（:130）：还原协议顺序，保证 pipeline 回喂 zip 配对不乱——改拼装逻辑必须维持该不变量。
