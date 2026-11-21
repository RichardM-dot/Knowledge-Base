# scheduler_state.md —— State 五态与 StateMachine

> 对应源文件：`references/firmware/DesktopCompanion/host/src/scheduler/state.py`（共 35 行，含中文深注）
> 注释级别：A级（逐类逐函数深注）
> 关联文档：[02-调度器与事件循环](../../../references/firmware/DesktopCompanion/Docs/02-调度器与事件循环.md)
> 关联条目：[scheduler_pipeline.md](scheduler_pipeline.md)、[scheduler_loop.md](scheduler_loop.md)

---

## 1. 文件职责

用五态枚举标记"管道此刻在干什么"。全工程只有 `scheduler.current_task` 持有的这一个 StateMachine 实例，pipeline 独占迁移权。它不控制流程（迁移是无条件赋值），只服务于观察（当前哪个阶段）。

## 2. 逐类表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "状态机" |
| 3-12 | 导读块 | 迁移路径图 / 迁移权归属 |
| 16-24 | `State(Enum)` | 五态，字符串值即 CLI 可读显示名 |
| 25-35 | `StateMachine` | current 单字段 + transition 无条件赋值 |

## 3. 五态语义（state.py:16-24）

```python
class State(Enum):
    # 五态字符串值即 CLI 可读的显示名（idle/thinking/...）
    IDLE = "idle"                       # 空闲：等待事件
    THINKING = "thinking"               # 思考：LLM 正在流式生成
    ACTING = "acting"                   # 行动：技能执行/记忆整理中
    WAITING_CONFIRMATION = "waiting_confirmation"  # 等待用户 y/n 确认
    RESPONDING = "responding"           # 应答：回复完成，展示耗时
```

| 态 | 进入点（pipeline.py） | 含义 |
|---|---|---|
| IDLE | 启动 / 每轮收尾（:139） | 等事件 |
| THINKING | 阶段 1 开头（:91）、on_text 首块（:157-158） | 组装 + 流式生成 |
| ACTING | 工具循环体（:108）、整理前（:133） | 技能执行/记忆整理 |
| WAITING_CONFIRMATION | 确认前（:280） | 人机确认窗口 |
| RESPONDING | 收尾（:137） | 展示耗时 |

## 4. 状态机实现（state.py:25-35）

```python
class StateMachine:
    # ------------------------------------------------------------
    # StateMachine —— 最小化状态机（只存当前态，不校验迁移合法性）
    # 设计取舍：管道代码是唯一迁移者且路径固定，加非法迁移检查
    # 收益有限，所以 transition 是无条件赋值——简单即可靠。
    # ------------------------------------------------------------
    def __init__(self):
        self.current = State.IDLE  # 进程启动即空闲

    def transition(self, to: State) -> None:
        self.current = to  # 无条件迁移：不记录历史，不触发回调
```

设计取舍（头注释已写明）：迁移点全部集中在 pipeline 一个文件，人肉可验证 → 不加校验、不加历史、不加回调。要加告警只需在 transition 里加断言。

完整迁移路径（合法序列）：

```
IDLE → THINKING → [ACTING ⇄ (WAITING_CONFIRMATION) → THINKING]×N → RESPONDING → IDLE
```

`ACTING → ACTING`（记忆整理自环）与 `ACTING → THINKING`（回喂后再问）是循环段的两种出口。

## 5. 陷阱与观察

1. **无锁**：transition 是裸赋值；安全前提是"pipeline 唯一迁移者"。若技能内部也要迁移状态，需重新审视。
2. **无历史**：调试"状态跳变史"只能靠日志；枚举字符串值（idle/thinking/...）就是为此准备的展示名。
3. **_stream_llm 的迁移守卫**（pipeline.py:157-158）：`if state.current != State.THINKING: transition(...)`——避免每个 chunk 重复迁移同一态，这是全工程唯一的"条件迁移"。

## 6. 迁移点全清单（逐行号）

| 迁移 | 位置 | 触发条件 |
|---|---|---|
| → THINKING | pipeline.py:91 | 阶段 1 组装开始 |
| THINKING → ACTING | pipeline.py:108 | 每轮工具循环体开头 |
| ACTING → WAITING_CONFIRMATION | pipeline.py:280 | 技能 requires_confirmation |
| WAITING_CONFIRMATION → ACTING | pipeline.py:285 | 确认返回（无论 y/n） |
| ACTING → THINKING | pipeline.py:157-158 | 流式回调首块且当前非 THINKING |
| ACTING → ACTING（自环） | pipeline.py:133 | 记忆整理前（若已在 ACTING 则重复赋值） |
| → RESPONDING | pipeline.py:137 | 收尾展示耗时 |
| RESPONDING → IDLE | pipeline.py:139 | 本轮结束 |

全部 8 个迁移点集中在 pipeline.py 一个文件，可用 grep 一次性核对：`grep -n "state.transition" host/src/scheduler/pipeline.py`。

## 7. 状态可见性

当前状态机只被 pipeline 写、**没有任何展示消费**（interface.py 的 show_context 打印的是性格/轮次，不含 state）——五态的价值目前是"代码可读性 + 未来状态展示的预留"。若要立即可视化，可在 `_stream_llm`/`_execute_with_confirmation` 里加一行 `self.display.show_state(state.current.value)`（记录为演进方向）。

## 8. 自测三问

1. transition 校验合法迁移吗？（答：不校验，无条件赋值——设计取舍见函数头注释）
2. 取消（CancelledError）后状态停在哪个态？（答：停在取消那一刻的态，process 的 finally 不迁移——下一轮阶段 1 会先迁 THINKING 复位，所以不会卡死）
3. 为什么枚举值用字符串不用 int？（答：字符串值可直接做 CLI/日志展示名，self-explanatory）

## 9. 与 OV-Watch 状态机的对照（迁移学习）

| 维度 | 本工程 StateMachine | OV-Watch 的界面状态机 |
|---|---|---|
| 形态 | 5 态单字段 | 界面层级栈 + 消息队列驱动 |
| 迁移控制 | pipeline 独占（代码集中） | 多任务各自投递消息 |
| 校验 | 无 | 消息驱动天然串行 |
| 共通点 | 都把"状态"当观察手段而非控制流 | — |

结论：状态机在本工程里是"灯"不是"闸"——控制流由 async/await 结构本身表达。这种"结构即控制流"的协程式设计与 RTOS 消息驱动式设计是两种正交范式，读两边代码时注意区分。

*（EOF 补充：本节为跨工程联想，行号引用以当前文件为准）*
