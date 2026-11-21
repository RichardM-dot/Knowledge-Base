# memory_stm.md —— ShortTermMemory 滑动窗口

> 对应源文件：`references/firmware/DesktopCompanion/host/src/memory/stm.py`（共 79 行，含中文深注）
> 注释级别：A级（逐函数深注）
> 关联文档：[04-记忆系统](../../../references/firmware/DesktopCompanion/Docs/04-记忆系统.md)
> 关联条目：[memory_manager.md](memory_manager.md)、[memory_ltm.md](memory_ltm.md)、[llm_prompt.md](llm_prompt.md)

---

## 1. 文件职责

近期对话的"草稿本"：把最近 `max_entries`（config.yaml 为 50）条对话存进 `host/src/data/stm.md`，每条一行，格式固定 `- [HH:MM] 角色: 内容`。实现策略是**每次 append 整读整写全文件**（O(n) 重写）——50 条量级开销可忽略，换来实现极简与人类可直读可手改。

容量语义：纯滑动窗口，不做摘要。重要的东西要么 save_memory 显式写 LTM，要么等周期整理提炼，否则在窗口里被自然淘汰（唯一"遗忘"机制）。

## 2. 逐函数表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "短期记忆 — 基于 Markdown 文件的滑动窗口" |
| 3-15 | 导读块 | 行格式/整写策略/容量语义/被读方 |
| 20-24 | 类头注释 | path + max_entries |
| 25-28 | `__init__` | 构造即 `_ensure_file` |
| 34-36 | `_ensure_file` | 只写标题行 `## 最近对话` |
| 44-48 | `_read_lines` | `- [` 前缀契约的解析入口 |
| 54-57 | `get_recent` | 尾部 count 条，不足全返 |
| 59-79 | `append` | 行格式化 → 裁剪 → 整文件重写 |

## 3. 关键摘录

### 3.1 行格式契约（stm.py:38-48）

```python
    def _read_lines(self) -> list[str]:
        text = self.path.read_text(encoding="utf-8")
        # 过滤掉标题/空行，只留对话行；strip 保证跨平台行尾一致
        return [l.strip() for l in text.split("\n")
                if l.strip().startswith("- [")]
```

`- [` 前缀是全系统格式锚点：本文件读它、manager 轮次回推靠它（数 `"用户:"`）。手编 stm.md 丢了前缀 = 该行从记忆中消失。

### 3.2 追加与裁剪（stm.py:59-79）

```python
    def append(self, role: str, content: str) -> None:
        """追加一条对话，超过上限则裁剪"""
        # 时间戳只到分钟：窗口内存活期短，分钟粒度足够模型推断时序
        now = datetime.now().strftime("%H:%M")
        # 角色名映射成中文「用户/小伴」，与 prompt 里的人设口吻一致；
        # 未知角色原样透传，保持扩展性
        label = {"user": "用户", "assistant": "小伴"}.get(role, role)
        entry = f"- [{now}] {label}: {content}"

        lines = self._read_lines()
        lines.append(entry)

        # 滑动窗口裁剪
        if len(lines) > self.max_entries:
            lines = lines[-self.max_entries:]  # 保留最新 max_entries 条

        # 整文件重写：先读后写，无锁——单进程单事件循环下不会竞争
        self.path.write_text(
            "## 最近对话\n" + "\n".join(lines) + "\n",
            encoding="utf-8"
        )
```

**遗忘策略的三行实现**：`lines[-max_entries:]` 保留尾部、丢弃头部。量化：n ≤ 50 行，单次 append 磁盘写约几 KB；一天 200 轮 = 400 次 append，写流量 MB 级，无感。

角色名映射表：

| role | label | 来源 |
|---|---|---|
| `user` | `用户` | pipeline 阶段 4（:126） |
| `assistant` | `小伴` | pipeline 阶段 4（:128） |
| 其他 | 原样透传 | 预留扩展位 |

## 4. 陷阱与观察

1. **整文件重写非原子**：进程在 write_text 中途被 kill 可能留下截断文件；下次 `_read_lines` 读到残行仍以 `- [` 判定，影响有限但无恢复机制。
2. **get_recent 默认 20**：pipeline 阶段 1 用默认值注入 prompt；整理时显式传 `count=interval*2`（12）。两条路径的取数宽度不同，改默认值要想清楚两条链路。
3. **时间戳无日期**：跨天后窗口里只有 `[HH:MM]`，模型无法知道对话发生在哪天——长期运行的已知局限。
4. **`get_recent` 的 count 语义**：不足 count 全量返回（`[-count:]` 切片特性），无报错路径。

## 5. stm.md 全文样例（3 条对话后）

```markdown
## 最近对话
- [10:10] 用户: 早安
- [10:10] 小伴: 早呀 (๑•̀ㅂ•́)و✧ 今天想做点什么？
- [10:11] 用户: 帮我记一下周三要交报告
- [10:11] 小伴: 好，咱记下了 (๑•̀ㅁ•́๑)✧
- [10:12] 用户: 今天几号？
- [10:12] 小伴: 2026年05月03日 (星期日) 哦
```

标题行 `## 最近对话` 不参与统计（`- [` 前缀过滤）；六行 = 3 轮 = 3 次用户计数（manager.py:34 回推逻辑）。文件尾恒有 `\n`（write_text 模板末尾拼接）。

## 6. append 一次的完整链路

```
pipeline.py:126 append_to_stm("user", ...)
→ manager.py:73 stm.append(role, content)
→ manager.py:74-75 计数 +1（仅 user）
→ stm.py:68-69 读旧行 → 追加新行
→ stm.py:71-73 超 50 裁最老
→ stm.py:75-79 整文件重写
```

链路上 STM 与 manager 各做一件事：文件归 stm，计数归 manager——职责切分清晰，但轮次契约（`- [`、`用户:` 两个文本锚点）横跨两文件，改动格式要同步检查 manager.py:33-34。

## 7. 自测三问

1. 窗口满 50 条后最老的一对去哪了？（答：被 lines[-50:] 切掉，无摘要无迁移——遗忘即删除）
2. 手编 stm.md 加一行 `- [12:00] 访客: 你好` 会怎样？（答：进上下文、不影响计数——"用户:"不含"访客:"匹配；但"用户:"字样出现在内容里会误计）
3. 为什么不逐行追加而要整写？（答：50 条量级整写最简单且保证裁剪；lucidity > 性能）
