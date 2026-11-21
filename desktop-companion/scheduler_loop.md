# scheduler_loop.md —— CompanionScheduler 事件泵

> 对应源文件：`references/firmware/DesktopCompanion/host/src/scheduler/loop.py`（共 56 行，含中文深注）
> 注释级别：A级（逐函数深注）
> 关联文档：[02-调度器与事件循环](../../../references/firmware/DesktopCompanion/Docs/02-调度器与事件循环.md)
> 关联条目：[scheduler_event.md](scheduler_event.md)、[scheduler_pipeline.md](scheduler_pipeline.md)、[scheduler_state.md](scheduler_state.md)

---

## 1. 文件职责

Host 侧的**事件泵**：独占消费一个无界 `asyncio.Queue`，把每个 `Event` 变成 `pipeline.process()` 协程任务；`Priority.INTERRUPT` 事件可打断当前任务。单消费者模型，`emit()` 是无锁生产入口。

数据流位置：`main.py` 主循环 → 本文件队列 → `pipeline.py` 消费。

## 2. 逐函数表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "调度器主循环" |
| 3-13 | 导读块 | 职责/数据流位置/并发约定（无界队列语义） |
| 20-26 | 类头注释 | 持有四件套：queue/pipeline/state_machine/current_task |
| 27-33 | `__init__` | 无界队列 + 全局唯一状态机 + 当前任务槽 |
| 35-51 | `run()` | 主循环：get 事件 → INTERRUPT cancel → create_task 派发 |
| 53-56 | `emit()` | put_nowait 非阻塞投递 |

## 3. 关键摘录

### 3.1 事件泵主体（loop.py:35-51）

```python
    async def run(self) -> None:
        """主循环：永不停歇的事件处理"""
        while True:
            # 阻塞点：队列空时挂起，不占 CPU；来事件即唤醒
            event = await self.event_queue.get()

            # INTERRUPT 优先级语义：新事件直接取消上一轮对话协程
            # （旧协程在 pipeline.process 的 finally 里仍会 set_result，
            #   所以主循环的 await done 不会永久挂起）
            if event.priority == Priority.INTERRUPT and self.current_task:
                self.current_task.cancel()

            # fire-and-forget：任务引用只存 current_task，不做 await；
            # 完成通知走 event._done Future，而不是在这里等
            self.current_task = asyncio.create_task(
                self.pipeline.process(event, self.state_machine)
            )
```

三个设计决策：

1. **无界队列**（loop.py:29 `asyncio.Queue()` 无 maxsize）：事件洪峰不丢只积压，用内存换可靠性；
2. **fire-and-forget**：不 await 任务，完成通知走 `event._done` Future（契约见 [scheduler_event.md](scheduler_event.md)）；
3. **INTERRUPT 打断**：`cancel()` 是协作式——只有管道内的 await 点会抛 `CancelledError`；被取消的旧协程仍走 finally 放行旧事件的 `_done`。

### 3.2 非阻塞投递（loop.py:53-56）

```python
    def emit(self, event: Event) -> None:
        """推入事件（非阻塞）"""
        # put_nowait：同步上下文(main 主循环)也能投递，无需 await
        self.event_queue.put_nowait(event)
```

`put_nowait` + 无界队列 = 投递永不失败也永不等待。若未来改为有界队列，这里需要 fallback 策略（丢事件或 await）。

## 4. 陷阱与观察

1. **fire-and-forget 无兜底**：任务异常不会在 `run()` 中暴露（好在 `process()` 内部全捕获）。若未来有人在 pipeline 外抛异常，会成为"静默死亡"的任务——可加 done callback 记日志。
2. **INTERRUPT 当前无人发送**：CLI 主循环只发 NORMAL（见 [host_main.md](host_main.md)）；这是为 MQTT/设备上报预留的通道。
3. **旧任务引用被覆盖**：`current_task` 只保留最近一个任务；若连续两个 INTERRUPT 事件紧挨着到达，第二个到达时 `current_task` 已指向第一个 INTERRUPT 派发的新任务——极端时序下可能取消"新任务"而不是"旧任务"。当前单消费者串行场景不会触发（事件间隔足够大）。
4. **状态机共享**：`state_machine` 是全工程唯一实例，所有轮次共用；pipeline 是唯一迁移者。

## 5. 事件构造与投递速查

主循环侧的完整事件生命周期（配合 [host_main.md](host_main.md)）：

```python
# main.py:119-128（节选，行号为注释后真实行号）
            done = asyncio.get_event_loop().create_future()
            event = Event(
                source=EventSource.CLI,
                content=user_input,
            )
            event._done = done
            scheduler.emit(event)
            await done
```

| 步骤 | 发生在 | 队列状态 |
|---|---|---|
| 构造 Event | main.py:120-123 | — |
| 挂 future | main.py:126 | — |
| put_nowait | main.py:128 → loop.py:56 | 队列 +1 |
| queue.get | loop.py:39 | 队列 -1 |
| create_task | loop.py:49-51 | 0（在跑） |
| finally set_result | pipeline.py:76-77 | 主循环放行 |

## 6. 两事件连发时序推演

假设用户在回答期间又敲了一句（NORMAL）：

```
t0: 事件A入队 → 被取走 → create_task(任务A) → 流式生成中
t1: 事件B入队（排队等待，run 阻塞在 queue.get 上？否——run 已消费 A，
    正阻塞在 await queue.get()，B 到达立即唤醒 run）
t2: run 取走 B → B.priority=NORMAL → 不取消 A → create_task(任务B)
t3: 任务A、任务B 并发存在！两轮 pipeline 同时在跑（共享同一状态机）
```

注意 t2~t3 的行为：**NORMAL 事件也会 create_task 新任务**，`run()` 没有"上一轮没跑完就排队"的守卫——所谓串行其实是靠"主循环 await done 不发下一句"维持的约定，不是队列强制。只有 `_done` 的等待者在 main 主循环，调度器层面两个任务可并发。共享状态机在并发下会互相覆盖 current 值（无锁），这是 fire-and-forget 设计的隐含边界（当前用法触发不了，记录为边界事实）。

## 7. 自测三问

1. `emit()` 在什么情况下会失败？（答：不会——put_nowait + 无界队列；只有事件循环不存在时才抛）
2. INTERRUPT 事件到达时，旧任务一定会被取消吗？（答：仅当 `current_task` 非空且确实是旧轮；引用可能已被覆盖，见 §6）
3. 为什么完成通知不用 `await current_task` 而用 `_done` Future？（答：run 需要立即回到 queue.get 继续收事件；Future 还能让**别的**协程（main 主循环）挂起等待同一完成信号）
