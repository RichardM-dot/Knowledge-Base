# init_packages.md —— 九个 __init__.py 包标记（合并条目）

> 对应源文件（合并条目，覆盖 9 个强相关小文件，各 7~9 行，均已加 B级导读块）：
> - `references/firmware/DesktopCompanion/host/src/__init__.py`（7 行）
> - `references/firmware/DesktopCompanion/host/src/config/__init__.py`（9 行）
> - `references/firmware/DesktopCompanion/host/src/scheduler/__init__.py`（9 行）
> - `references/firmware/DesktopCompanion/host/src/llm/__init__.py`（7 行）
> - `references/firmware/DesktopCompanion/host/src/memory/__init__.py`（8 行）
> - `references/firmware/DesktopCompanion/host/src/skill/__init__.py`（9 行）
> - `references/firmware/DesktopCompanion/host/src/skill/builtin/__init__.py`（9 行）
> - `references/firmware/DesktopCompanion/host/src/channel/__init__.py`（7 行）
> - `references/firmware/DesktopCompanion/host/src/cli/__init__.py`（7 行）
> 注释级别：B级（每个文件一块导读注释；原文件全为空）
> 关联文档：[00-总览与架构](../../../references/firmware/DesktopCompanion/Docs/00-总览与架构.md)
> 关联条目：各子包条目见同目录其他 md

---

## 1. 为什么要给空文件写注释

九个 `__init__.py` 原本全是 0 字节空文件——打开任何一个都学不到东西。按 B级规范给每个包标记加了一块 `#` 导读注释：成员清单 + 数据流位置。由于原文件为空，"剥离注释后与原文件一致"自动成立，注释是纯增量。

## 2. 九个包一句话总表

| 文件 | 包角色 | 导读块要点 |
|---|---|---|
| `src/__init__.py` | Host 包根 | 本身无代码；main.py 用 sys.path.insert 把本目录提升为导入根，各子包以顶层名互导；运行后生成 data/ |
| `config/__init__.py` | 配置包 | loader.py + config.yaml + llm.env（敏感项不入库）；main 启动第一步消费 |
| `scheduler/__init__.py` | 调度核心 | event 报文 / loop 事件泵 / pipeline 管道 / state 状态机 |
| `llm/__init__.py` | 大模型通信 | client.py 流式客户端 + prompt.py 提示词；对上只暴露 LLMClient |
| `memory/__init__.py` | 记忆系统 | stm 滑窗 / ltm 长期笔记 / manager 门面；数据在 data/ 下 |
| `skill/__init__.py` | 技能体系 | base 基类 / registry 注册表 / builtin 四技能 |
| `skill/builtin/__init__.py` | 开箱技能 | get_time / save_memory / search_memory / run_shell；注意 search_memory 未注册 |
| `channel/__init__.py` | 通信抽象 | OutputChannel 接口 + CLI 替身；为 MQTT 预留，主流程未调用 |
| `cli/__init__.py` | 终端界面 | interface.py：流式 Live 面板、斜杠命令、y/n 确认 |

## 3. 导入机制说明

工程**不使用包相对导入**做跨包引用。main.py:23-24：

```python
# 确保 src 在 path 中
sys.path.insert(0, str(Path(__file__).parent))
```

把 `host/src/` 塞到 sys.path 首位后，`config`、`scheduler`、`llm`、`memory`、`skill`、`channel`、`cli` 都成了顶层可导入名——这就是九个 `__init__.py` 存在的全部理由：**让目录成为可导入的包**。`memory` 与 `skill` 内部用了包相对导入（`from .ltm import ...`、`from .base import ...`），那是包内引用，与跨包顶层导入并存。

## 4. 陷阱与观察

1. **双导入形式并存**：包内相对导入 + 包间顶层导入混用；把 src 改成规范安装包（pip install -e）时，顶层导入会失效，需要统一改为相对导入或命名空间包。
2. **skill/builtin 的导读点名了"未注册技能"**：search_memory 实现完整但 main.py 未注册（见 [skill_registry.md](skill_registry.md)）——空文件的导读块承担了"避坑提示"职责。
3. **`src/__init__.py` 是唯一有行为注释的包根**：data/ 目录的位置契约（运行时生成 stm.md/ltm.md）写在这里，因为它是"一切相对路径的锚"（配合 loader.get_path）。

## 5. 一个导读块原文示例（skill/builtin/__init__.py 全文）

```python
# ============================================================
# skill.builtin 子包（B级导读）：内置开箱技能
# 成员：get_time.py（查时间，无参数）
#       save_memory.py（写 LTM，注入 MemoryManager）
#       search_memory.py（搜 LTM，注入 MemoryManager）
#       run_shell.py（执行终端命令，requires_confirmation=True）
# 位置：main.py 逐一实例化并 register 进 SkillRegistry；
#       注意 search_memory 实现了但 main.py 未注册（LLM 暂不可见）。
# ============================================================
```

九个文件的导读块结构统一为三段式：**成员 → 位置（数据流中的角色）→ 提示**。`#` 全注释意味着文件导入行为不变（空 `__init__.py` 与注释版语义完全一致）。

## 6. 导入体系速查

| 导入形式 | 例子 | 出现文件 |
|---|---|---|
| 顶层跨包 | `from scheduler.loop import CompanionScheduler` | main.py、pipeline.py 等 |
| 包内相对 | `from .ltm import LongTermMemory` | memory/manager.py |
| 包内相对 | `from .base import BaseSkill` | skill/registry.py |
| sys.path 补丁 | `sys.path.insert(0, str(Path(__file__).parent))` | main.py:23-24（根由） |

## 7. 自测两问

1. 删掉某个 `__init__.py` 的注释会出问题吗？（答：不会——注释不参与导入语义，剥离后与原空文件一致）
2. 为什么 channel/__init__.py 要写"主流程尚未调用"？（答：防止读者误以为 CliOutputChannel 已在生产链路上，见 channel_cli_channel.md）

## 8. 九文件行数与注释密度表

| 文件 | 总行 | 注释行 | 代码行 |
|---|---|---|---|
| src/__init__.py | 7 | 7 | 0 |
| config/__init__.py | 9 | 9 | 0 |
| scheduler/__init__.py | 9 | 9 | 0 |
| llm/__init__.py | 7 | 7 | 0 |
| memory/__init__.py | 8 | 8 | 0 |
| skill/__init__.py | 9 | 9 | 0 |
| skill/builtin/__init__.py | 9 | 9 | 0 |
| channel/__init__.py | 7 | 7 | 0 |
| cli/__init__.py | 7 | 7 | 0 |

九个文件 100% 是注释——这是空文件深注的极限形态，也是"注释零逻辑风险"的极端案例：strip 校验天然通过（剥离后 = 原空文件）。

## 9. 包结构与数据流总对照

| 包 | 上游 | 下游 | KB 条目 |
|---|---|---|---|
| config | 磁盘两个文件 | main → 所有模块 | [config_loader.md](config_loader.md) |
| scheduler | main emit | llm/memory/skill/cli 回调 | [scheduler_loop.md](scheduler_loop.md) 等 |
| llm | pipeline | OpenAI 兼容端点 | [llm_client.md](llm_client.md) |
| memory | pipeline/技能 | stm.md、ltm.md | [memory_manager.md](memory_manager.md) |
| skill | main 注册 | memory/shell 副作用 | [skill_registry.md](skill_registry.md) |
| channel | （未接线） | （未来设备） | [channel_base.md](channel_base.md) |
| cli | pipeline | 用户终端 | [cli_interface.md](cli_interface.md) |
