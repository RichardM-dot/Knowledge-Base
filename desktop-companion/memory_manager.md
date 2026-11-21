# memory_manager.md —— MemoryManager 记忆门面

> 对应源文件：`references/firmware/DesktopCompanion/host/src/memory/manager.py`（共 85 行，含中文深注）
> 注释级别：A级（逐函数深注）
> 关联文档：[04-记忆系统](../../../references/firmware/DesktopCompanion/Docs/04-记忆系统.md)
> 关联条目：[memory_stm.md](memory_stm.md)、[memory_ltm.md](memory_ltm.md)、[scheduler_pipeline.md](scheduler_pipeline.md)、[skill_registry.md](skill_registry.md)

---

## 1. 文件职责

STM/LTM 的门面（Facade）：把 stm.py（滑窗）与 ltm.py（长期笔记）包成一个对外记忆服务。pipeline、技能、记忆整理都只认它。另维护 `interaction_count` 轮次计数器驱动周期整理；启动时从 STM 回推计数，重启不丢周期。

数据落盘：`host/src/data/stm.md`、`host/src/data/ltm.md`——文件即数据库，可手编。

## 2. 逐函数表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "记忆管理器 — 统一对外接口" |
| 3-13 | 导读块 | 门面职责/计数器/落盘格式 |
| 19-24 | 类头注释 | 持有四件套 |
| 25-34 | `__init__` | 组装 ltm/stm + 轮次回推 |
| 40-44 | `get_personality` | 转发 ltm 性格段解析 |
| 47-50 | `retrieve_relevant` | 转发 ltm.search |
| 53-55 | `get_recent` | 转发 stm.get_recent（默认 20） |
| 56-61 | `get_recent_raw` | 显式 count，记忆整理用 |
| 64-66 | `save_to_ltm` | 转发 ltm.append |
| 68-75 | `append_to_stm` | 写 STM + user 行计数 +1 |
| 77-84 | `needs_consolidation` | 取模触发 + 两个守卫 |

## 3. 关键摘录

### 3.1 重启续算（manager.py:25-34）

```python
    def __init__(self, ltm_path: Path, stm_path: Path,
                 stm_max: int = 50, consolidation_interval: int = 10):
        self.ltm = LongTermMemory(ltm_path)
        self.stm = ShortTermMemory(stm_path, stm_max)
        self.consolidation_interval = consolidation_interval
        # 从已有 STM 推算当前对话轮次
        # 重启续算：STM 是唯一持久真相，数「用户:」行数即当前轮次，
        # 使 % N 触发的整理节奏跨进程重启仍然准点
        existing = self.stm._read_lines()  # 直呼私有方法：同包门面，可接受
        self.interaction_count = len([l for l in existing if "用户:" in l])
```

计数不落盘、从 STM 文件回推——STM 是唯一持久真相。**边界行为（量化）**：窗口满载 50 条（≈25 用户行）后，回推计数只能到 ~25 并在此震荡，不再是真实历史轮次；`% 6` 的触发点随之漂移（24/30 命中 → 实际 5~6 轮一整理），行为无害但不精确。

### 3.2 计数器维护与触发（manager.py:72-84）

```python
    def append_to_stm(self, role: str, content: str) -> None:
        self.stm.append(role, content)
        if role == "user":
            self.interaction_count += 1

    def needs_consolidation(self) -> bool:
        """是否需要触发记忆整理"""
        # 周期 <=0 视为永久关闭该功能（配置开关语义）
        if self.consolidation_interval <= 0:
            return False
        # 取模触发：第 N、2N、3N… 次用户发言时恰好命中；
        # interaction_count > 0 防止首次对话(计数=0)被 0%N==0 误触
        return self.interaction_count > 0 and \
            self.interaction_count % self.consolidation_interval == 0
```

两个守卫：`<=0` 当开关用（关闭整理功能）；`>0` 防"第一轮就整理"（0 % 6 == 0 的数学陷阱）。只有 user 行计数——"一轮 = 一次用户发言"，语义稳定。

### 3.3 门面转发关系

| 门面方法 | 实际实现 | 调用方 |
|---|---|---|
| `get_personality` | ltm.get_personality | pipeline 阶段 1 → prompt 人设段 |
| `retrieve_relevant` | ltm.search | pipeline 阶段 1 + search_memory 技能 |
| `get_recent` / `get_recent_raw` | stm.get_recent | pipeline 阶段 1（默认20）/ 整理（N×2） |
| `save_to_ltm` | ltm.append | save_memory 技能 + 记忆整理 |
| `append_to_stm` | stm.append + 计数 | pipeline 阶段 4（user/assistant 各一次） |

## 4. 陷阱与观察

1. **直呼 `stm._read_lines()` 私有方法**（:33）：同包门面里可接受，但若 stm 行格式契约变化，此处是最先被打破的耦合点（依赖 `"用户:"` 文本匹配）。
2. **手编 stm.md 的坑**：把"用户:"写进小伴行正文会多算轮次；破坏 `- [` 行前缀会丢上下文（见 [memory_stm.md](memory_stm.md)）。
3. **无锁**：单事件循环下无并发读改；未来多协程并发 append 需要加锁或改队列化写入。
4. **consolidation_interval 双源**：yaml 值 6 生效，代码默认 10 只是兜底（main.py:68 传入）。

## 5. interaction_count 生命周期推演

```
启动:      从 stm.md 数「用户:」行（manager.py:33-34）
对话中:    每 user 行 +1（manager.py:74-75）
触发判定:  count % 6 == 0 且 count > 0（manager.py:79-84）
窗口满载:  count ≈ 25 震荡（50 条 ≈ 25 用户行），触发点漂移至 5~6 轮
重启:      重新回推——计数本身不落盘，STM 是唯一持久真相
```

推演（窗口未满载时）：第 6 轮触发整理 → 第 12 轮再触发 → yaml 的 6 生效；若把 consolidation_interval 改 0，needs_consolidation 恒 False——配置开关语义。

## 6. 自测三问

1. 为什么 get_recent_raw 在 manager 层显式传 count 而不透传默认？（答：整理需要凑满 interval×2 条完整对话，默认 20 与 interval 无关）
2. 直呼 stm._read_lines() 违反封装吗？（答：同包门面的务实选择，注释已声明；代价是格式契约耦合）
3. 计数器为何不落盘？（答：STM 文件本身就是计数器的持久真相，落盘反而引入双源不一致）
