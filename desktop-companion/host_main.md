# host_main.md —— main.py 总装入口

> 对应源文件：`references/firmware/DesktopCompanion/host/src/main.py`（共 142 行，含中文深注）
> 注释级别：A级（逐函数深注）
> 关联文档：[01-启动与配置加载](../../../references/firmware/DesktopCompanion/Docs/01-启动与配置加载.md)、[00-总览与架构](../../../references/firmware/DesktopCompanion/Docs/00-总览与架构.md)
> 关联条目：[config_loader.md](config_loader.md)、[scheduler_loop.md](scheduler_loop.md)、[scheduler_pipeline.md](scheduler_pipeline.md)

---

## 1. 文件职责

DesktopCompanion Host 的**总装车间 + 交互主循环**。把配置、LLM、记忆、技能、CLI、调度器六个模块装配成一体，然后进入「读输入 → 发事件 → 等处理完成」的循环。进程只有一个 asyncio 事件循环；阻塞式 `input()` 通过 `asyncio.to_thread` 丢进线程池。

启动顺序（main.py:47-88）：

```
load_config → get_path(data) → LLMClient → MemoryManager
→ SkillRegistry(注册3技能) → CLIInterface → Pipeline → CompanionScheduler
→ create_task(scheduler.run) → CLI 主循环
```

## 2. 逐函数/逐段表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "DesktopCompanion Host — 入口" |
| 3-14 | `#` 导读块 | 职责/启动顺序/线程模型/同步协议/关联文件 |
| 19-38 | imports | asyncio/sys/Path + rich + 六个模块 + scheduler 四件 |
| 23-24 | `sys.path.insert(0, ...)` | 把 `host/src/` 提升为导入根，后续全是顶层导入 |
| 46-47 | `async def main()` | 函数头注释 + 协程定义 |
| 48-49 | Console 横幅 | 启动提示 |
| 52-53 | 配置加载 | yaml 结构参数 + llm.env 密钥；data 目录定位 |
| 56-62 | LLMClient 构造 | env 三元组 + yaml llm 段（temperature/max_tokens） |
| 64-69 | MemoryManager 构造 | ltm.md/stm.md 路径 + stm_max=50 + 整理周期 |
| 71-78 | 技能注册 | get_time / save_memory(注入 memory) / run_shell |
| 80-83 | CLI + Pipeline + Scheduler | 四依赖注入管道 |
| 88 | `create_task(scheduler.run())` | 调度器作为后台常驻协程 |
| 89-91 | 启动横幅 | 模型名 / 已注册技能数 / 退出提示 |
| 94-130 | CLI 主循环 | to_thread 读输入 → 命令拦截 → Event + done Future → emit → await |
| 132-138 | finally | cancel 调度器 + 吞 CancelledError |
| 141-142 | `__main__` | `asyncio.run(main())` 唯一入口 |

## 3. 关键摘录

### 3.1 双协程装配（main.py:80-88）

```python
    cli = CLIInterface(skills)
    # 四个依赖一次性注入管道：LLM 生成、记忆取材、技能执行、CLI 渲染
    pipeline = Pipeline(llm, memory, skills, cli)
    scheduler = CompanionScheduler(pipeline)

    # 3. 启动调度器
    # 调度器作为后台常驻协程：主循环只管 emit() 投事件，
    # 真正的「取事件 → 跑管道」都发生在那个协程里
    scheduler_task = asyncio.create_task(scheduler.run())
```

装配次序即依赖次序：每个模块的构造参数都来自前一个的产物（详见 01 篇第 1 节的依赖表）。

### 3.2 done Future 同步协议（main.py:119-130）

```python
            done = asyncio.get_event_loop().create_future()
            event = Event(
                source=EventSource.CLI,
                content=user_input,
            )
            # _done 是 scheduler/event.py 里约定的内部字段(repr=False)，
            # 调度器不解释它，只有 pipeline 的 finally 会写它
            event._done = done
            # 非阻塞投递：进入 CompanionScheduler.event_queue（asyncio.Queue）
            scheduler.emit(event)
            # 阻塞点：整条管道（含技能执行、用户确认、记忆整理）跑完才放行
            await done
```

**这是全工程最核心的握手协议**：主循环把 future 挂到事件上投进队列，`Pipeline.process` 的 finally 块（pipeline.py:76-77）负责 `set_result`。任何路径（正常完成/异常/cancel）都会放行，主循环不会挂死。

### 3.3 命令拦截（main.py:108-113）

```python
            if cli.is_command(user_input):
                try:
                    cli._handle_command(user_input)
                except SystemExit:
                    break
                continue
```

斜杠命令不进事件队列——`/exit` 抛 `SystemExit` 被 catch 后 break；其余命令就地执行。

## 4. 陷阱与观察

1. **`asyncio.get_event_loop()`（119 行）**：协程内取 loop，Python 3.12+ 建议 `get_running_loop()`；当前行为正确（记录现状）。
2. **主循环串行等待**：`await done` 意味着同一时刻用户只能问一句；连发多句会排队（NORMAL 优先级）。
3. **search_memory 未注册**：78 行只注册了三个技能，`SearchMemorySkill` 实现完整但 LLM 不可见（见 [skill_registry.md](skill_registry.md)）。
4. **`config.scheduler.max_tool_loops` 未消费**：yaml 字段是预留位，真值在 pipeline.py:56。
5. **input 依赖线程池**：`asyncio.to_thread(input)` 若在无线程池环境（嵌入式 Python）会退化，桌面端无碍。

## 5. 装配依赖表（记忆锚点）

```
Config ──────────┬─→ LLMClient(env三元组 + llm段)
                 ├─→ MemoryManager(data路径 + memory段)
data目录 ────────┘        │
                          ↓
                  SaveMemorySkill(memory) → SkillRegistry → CLIInterface(skills)
                                                                  ↓
                          Pipeline(llm, memory, skills, cli) → CompanionScheduler
```

依赖链没有环：memory 最先装配（被技能依赖），pipeline 汇聚四件套，scheduler 最后包住 pipeline。任何模块想加构造参数，顺着这条链从 config 找起。

## 6. 自测三问

1. 主循环为什么用 to_thread 而不是 async input？（答：标准库 input 没有异步版；阻塞读必须挪线程池）
2. `cli._handle_command` 是私有方法却跨文件调用，问题大吗？（答：同进程内部约定，注释已明示；规范做法是改成公开方法）
3. finally 里为什么 await scheduler_task？（答：等 cancel 真正生效再退出，避免 asyncio 报"Task was destroyed but it is pending"）
