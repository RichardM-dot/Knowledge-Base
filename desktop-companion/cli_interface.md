# cli_interface.md —— CLIInterface 渲染与命令

> 对应源文件：`references/firmware/DesktopCompanion/host/src/cli/interface.py`（共 163 行，含 B级导读块）
> 注释级别：B级（文件头导读块，方法级细节见下表与摘录）
> 关联文档：[05-技能系统与通道](../../../references/firmware/DesktopCompanion/Docs/05-技能系统与通道.md)、[02-调度器与事件循环](../../../references/firmware/DesktopCompanion/Docs/02-调度器与事件循环.md)
> 关联条目：[scheduler_pipeline.md](scheduler_pipeline.md)、[skill_registry.md](skill_registry.md)、[host_main.md](host_main.md)

---

## 1. 文件职责

用户的眼睛和耳朵，双角色：**①渲染**——pipeline 在各阶段回调 show_* 系列（流式块进 Live 面板、技能结果、耗时、错误）；**②交互**——五个斜杠命令 + 技能执行前的 y/n 确认（线程池等输入，不卡事件循环）。

pipeline.display 就是本类实例；main.py 的输入循环用 `is_command()` 把斜杠命令拦在这里，普通文字才进事件总线。

## 2. 逐方法表

| 行号 | 方法 | 职责 | 调用方 |
|---|---|---|---|
| 1 | 模块 docstring | "CLI 交互界面 — 流式输出保留面板边框" | — |
| 3-27 | `#` B级导读块 | 双角色/数据流位置/流式实现/方法清单 | — |
| 33-38 | `__init__` | Console + skills 引用 + `_live`/`_stream_buf` 槽 | main.py:80 |
| 42-54 | `_start_stream` | 建 Live Panel（15fps） | show_stream_chunk 首次 |
| 56-65 | `show_stream_chunk` | 文本增量进面板 | `_stream_llm` on_text |
| 67-71 | `_stop_stream` | 收起 Live 保留画面 | 多处（见下） |
| 73-74 | `show_stream_end` | 纯文本回复收尾 | `_stream_llm` |
| 78-87 | `show_context` | 灰色调试行：性格/轮次/记忆字符数/技能数 | pipeline 阶段 1 |
| 89-97 | `show_tool_calls` | 青色 `[LLM→] name(args)` | `_stream_llm` |
| 100-105 | `show_skill_result` | 绿/红 `[技能]` 行，data 截 100 字 | 技能执行后/整理 |
| 107-108 | `show_elapsed` | `(1.2s)` 耗时行 | pipeline 收尾 |
| 111-124 | `request_confirmation` | 黄色警告面板 + y/n | `_execute_with_confirmation` |
| 128-155 | `_handle_command` | /exit /clear /status /memory /skills | main 拦截 |
| 158-160 | `show_error` | 红色 `[错误]` 行 | `process()` 兜底 |
| 162-163 | `is_command` | `startswith("/")` | main 每句输入 |

## 3. 关键摘录

### 3.1 流式 Live 面板（interface.py:56-65）

```python
    def show_stream_chunk(self, text: str):
        """流式输出 — 追加到 Live 面板"""
        if self._live is None:
            self._start_stream()
        self._stream_buf += text
        self._live.update(
            Panel(self._stream_buf,
                  title="[bold green]小伴[/]",
                  border_style="green")
        )
```

流式渲染的机制：整段回复累积在 `_stream_buf`，每个 chunk 到达就重画 Panel；`refresh_per_second=15` 限刷新频率。Live 生命周期结束时 `transient=False` 保留最终画面（面板边框不消失，这就是文件标题的含义）。

### 3.2 y/n 确认（interface.py:111-124）

```python
    async def request_confirmation(self, skill_name: str, prompt: str) -> bool:
        self._stop_stream()
        self.console.print()
        self.console.print(
            Panel(
                f"[bold yellow]⚠ 确认操作[/]\n"
                f"技能: {skill_name}{prompt}",
                border_style="yellow"
            )
        )
        response = await asyncio.to_thread(
            input, "  [y/n] > "
        )
        return response.strip().lower() == "y"
```

先 `_stop_stream()` 收起 Live（避免面板与确认框抢屏）；`to_thread(input)` 让等待期间事件循环照常运转——未来 INTERRUPT 打断时这是必要条件。`strip().lower() == "y"` 严格单字符判定：**任何非 y 输入都视为拒绝**（宁可误拒）。

### 3.3 命令表（interface.py:128-155）

| 命令 | 行号 | 行为 |
|---|---|---|
| `/exit` | 132-135 | 抛 `SystemExit(0)`，main 捕获后退出 |
| `/clear` | 136-137 | 清屏 |
| `/status` | 138-139 | 固定打印"运行中"（无真实状态查询） |
| `/memory` | 140-141 | 空壳：提示去看 data/ 目录 |
| `/skills` | 142-153 | rich Table 列技能（名称/描述截 60 字/确认标记） |
| 其他 | 154-156 | 红字未知命令 + 可用清单 |

## 4. 陷阱与观察

1. **两层截断**：展示层 100 字（:102 `str(result.data)[:100]`），run_shell 技能层 2000 字——人看 100 字、LLM 看 2000 字。
2. **`/status` 无信息量**：固定文案，未接 state_machine.current；接入后可显示实时状态。
3. **Live 与 print 的混用纪律**：所有非流式 print 前都必须 `_stop_stream()`（show_tool_calls/show_skill_result/show_elapsed/show_error/request_confirmation 都遵守）——漏一处就会面板错位。
4. **`is_command` 只看首字符 `/`**：用户输入以 / 开头的普通文本（如路径 "/etc/hosts"）会被当命令处理——已知边界行为。

## 5. Live 面板生命周期

```
_start_stream（:42）  Live 创建：15fps 刷新 + transient=False（保留画面）
show_stream_chunk     每个 delta 到达 → _stream_buf 追加 → Panel 重画
_stop_stream（:67）   Live.stop() → _live=None → 画面定格
```

触发 `_stop_stream` 的五个时机（互斥场景）：show_tool_calls（有技能调用）、show_elapsed（收尾）、show_error（异常）、request_confirmation（确认前）、_handle_command("/exit")。纪律：**任何新 print 都必须先收 Live**，否则面板与普通行交错错位（方法表里每个消费方都已遵守）。

## 6. 渲染格式速查

| 前缀 | 颜色 | 内容 | 示例 |
|---|---|---|---|
| `[上下文]` | 灰 dim | 性格/轮次/记忆字符数/STC 字符数/技能数 | `[上下文] 性格=小伴, 轮次=6, 记忆=86字符, ...` |
| `[LLM→]` | 青 cyan | tool_call 摘要 | `[LLM→] tool_call: run_shell(command='ls')` |
| `[技能]` | 绿/红 | 结果前 100 字或错误 | `[技能] get_time → 2026年05月03日...` |
| `(...)` | 灰 dim | 全程耗时 | `(3.4s)` |
| `[错误]` | 红 | 管道异常消息 | `[错误] 管道异常: ...` |
| `[MQTT→]` | 灰 dim | （来自 channel 替身） | 预留格式 |

## 7. 自测三问

1. 为什么 show_stream_chunk 里要懒创建 Live？（答：首块到达才建，避免空面板闪烁；_live is None 即建）
2. 确认等待期间事件循环在干什么？（答：to_thread 让出，调度器可继续收事件——虽然当前主循环被 await done 挡住）
3. /exit 为什么用 SystemExit 而不是返回值？（答：抛异常穿透 _handle_command 的普通返回路径，main.py:111-112 专门捕获它作为退出信号）
