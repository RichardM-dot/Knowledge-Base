# scheduler_pipeline.md —— Pipeline 一轮对话五阶段管道

> 对应源文件：`references/firmware/DesktopCompanion/host/src/scheduler/pipeline.py`（共 333 行，含中文深注）
> 注释级别：A级（逐函数深注）
> 关联文档：[02-调度器与事件循环](../../../references/firmware/DesktopCompanion/Docs/02-调度器与事件循环.md)、[04-记忆系统](../../../references/firmware/DesktopCompanion/Docs/04-记忆系统.md)
> 关联条目：[llm_client.md](llm_client.md)、[memory_manager.md](memory_manager.md)、[skill_registry.md](skill_registry.md)、[cli_interface.md](cli_interface.md)、[scheduler_event.md](scheduler_event.md)

---

## 1. 文件职责

一轮对话的完整生命周期都在这里：**①上下文组装 → ②流式 LLM → ③工具循环（最多 5 轮）→ ④写 STM → ⑤周期记忆整理 → 复位 IDLE**。同时守住全工程最核心的同步契约：`process()` 的 finally 必定 `set_result(event._done)` 放行主循环。

## 2. 逐函数表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "处理管道：上下文 → LLM+工具循环(流式) → 输出" |
| 3-12 | 导读块 | 六阶段总览 / _done 契约 / 状态机独占 |
| 24-40 | `PipelineContext.__init__` | 轮内工作台：7 个字段的产物注释 |
| 42-56 | `Pipeline.__init__` | 四依赖注入；max_tool_loops 硬编码 5 |
| 67-77 | `process` | try/except/finally 三层兜底 + 放行契约 |
| 86-139 | `_do_process` | 五阶段主流程（见 §3.2） |
| 151-175 | `_stream_llm` | 单次流式调用 + on_text 迁移 THINKING |
| 186-234 | `_consolidate_memory` | 周期整理：严格标准 + tools=None |
| 244-256 | `_build_messages` | [system, user] 两元素数组；局部导入防循环依赖 |
| 266-295 | `_execute_with_confirmation` | 批量执行 + 确认闸门 + 失败占位 |
| 304-333 | `_append_tool_results` | OpenAI 协议回喂（assistant.tool_calls + tool 消息成对） |

## 3. 关键摘录

### 3.1 同步契约守门（pipeline.py:67-77）

```python
    async def process(self, event: Event, state: StateMachine) -> None:
        try:
            await self._do_process(event, state)
        except Exception as e:
            # 单轮失败不影响调度器存活：错误只进 CLI 面板
            self.display.show_error(f"管道异常: {e}")
            traceback.print_exc()
        finally:
            # 同步契约的兑现点：只在未来未完成时放行，避免重复 set_result
            if event._done and not event._done.done():
                event._done.set_result(None)
```

要点：`except Exception` 不捕 `CancelledError`（BaseException 子类，正确向上传播）；`finally` 无条件执行 → cancel 路径也放行；`done()` 判断防 `InvalidStateError`。

### 3.2 五阶段主流程（pipeline.py:86-139 摘段）

```python
        # ===== 阶段 1：上下文组装 =====
        state.transition(State.THINKING)
        ctx.personality = self.memory.get_personality()
        ctx.relevant_memories = self.memory.retrieve_relevant(event.content)
        ctx.recent_context = self.memory.get_recent()
        ctx.skill_schemas = self.skills.get_function_schemas()
        ctx.messages = self._build_messages(ctx)  # system prompt + 本句用户输入
        self.display.show_context(ctx, self.memory.interaction_count)
```

```python
        tool_loops = 0
        while response.has_tool_calls and tool_loops < self.max_tool_loops:
            state.transition(State.ACTING)

            tool_results = await self._execute_with_confirmation(
                response.tool_calls, state
            )
            ctx.messages = self._append_tool_results(
                ctx.messages, response, tool_results
            )
            response = await self._stream_llm(ctx.messages, ctx.skill_schemas, state)
            tool_loops += 1
```

```python
        self.memory.append_to_stm("user", event.content)
        if response.content:
            self.memory.append_to_stm("assistant", response.content)
```

量化：`t_start`（:88）到 `show_elapsed`（:138）覆盖全程（含人工确认耗时）；阶段 4 的 user 行无条件写入（轮次计数依赖），assistant 行仅纯文本回复写入。

### 3.3 记忆整理的"宁缺毋滥"（pipeline.py:186-234 摘段）

```python
        recent = self.memory.get_recent_raw(
            count=self.memory.consolidation_interval * 2
        )
```

```python
        # tools=None：整理请求绝不允许再触发技能，避免递归整理
        try:
            resp = await self.llm.chat_stream(
                messages=[{"role": "user", "content": prompt}],
                tools=None,
                on_text=None,
            )
            content = (resp.content or "").strip()
            # 模型按约定回复「无」表示没有值得记的；空回复同样视为无
            if content and content != "无":
```

- `count = 6 × 2 = 12` 条：用户+小伴各一条，恰好 6 轮完整对话；
- 判定标准写死在 prompt（pipeline.py:194-203）：偏好/计划/指令/背景四类，寒暄明确排除；
- "回复一个字: 无" → 精确字符串比较，不写入。

### 3.4 回喂协议成对追加（pipeline.py:304-333 摘段）

```python
        for tc, r in zip(response.tool_calls, tool_results):
            assistant_msg = {
                "role": "assistant",
                "tool_calls": [{
                    "id": tc.id,
                    "type": "function",
                    "function": {
                        "name": tc.function_name,
                        # ensure_ascii=False：中文参数原样进 JSON
                        "arguments": json.dumps(tc.arguments, ensure_ascii=False)
                    }
                }]
            }
            if response.reasoning_content:
                assistant_msg["reasoning_content"] = response.reasoning_content
            new_msgs.append(assistant_msg)
            new_msgs.append({
                "role": "tool",
                "tool_call_id": tc.id,
                "content": r.format_for_llm()
            })
```

zip 配对成立的前提：`_execute_with_confirmation` 对未知技能（:272-275）与用户拒绝（:286-289）也产出占位失败结果，两列表恒等长同序。`reasoning_content` 回传是 DeepSeek 类模型续接思路的兼容处理。

## 4. 陷阱与观察

1. **max_tool_loops 硬编码**（:56）：config.yaml 的 `scheduler.max_tool_loops` 未接线（见 [config_loader.md](config_loader.md)）。
2. **system prompt 本轮不重建**（:241-242 注释）：工具循环只在数组尾部追加——本轮内记忆不变化，保证一致性。
3. **无输入预算控制**：工具循环 5 轮 × 2000 字输出上限，最坏 messages 体积可量化估算：system(约 1~2KB) + 每轮 assistant 声明 + tool 结果(≤2000 字符)。
4. **整理请求发生在用户等待期**：阶段 5 与回复同轮，第 6/12/18… 轮响应时间多一次 LLM 往返。
5. **`_do_process` 无锁**：单消费者模型下不会并发进入；若未来支持并行对话轮，PipelineContext 与状态机都要改造。
