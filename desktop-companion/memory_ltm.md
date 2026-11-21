# memory_ltm.md —— LongTermMemory 长期笔记

> 对应源文件：`references/firmware/DesktopCompanion/host/src/memory/ltm.py`（共 101 行，含中文深注）
> 注释级别：A级（逐函数深注）
> 关联文档：[04-记忆系统](../../../references/firmware/DesktopCompanion/Docs/04-记忆系统.md)
> 关联条目：[memory_manager.md](memory_manager.md)、[memory_stm.md](memory_stm.md)、[llm_prompt.md](llm_prompt.md)、[skill_registry.md](skill_registry.md)

---

## 1. 文件职责

桌宠的"人生笔记"（`host/src/data/ltm.md`）：维护固定段落结构的 Markdown——`## 性格设定` / `## 重要记忆`（### 关于用户 / ### 互动历史）。三个能力：解析人设段、关键词检索全部条目行、追加互动历史。

写入方：save_memory 技能（显式记住）、pipeline 记忆整理（周期提炼）、用户手编辑。检索是朴素关键词 substring 匹配——数据量小（几百行内）时够用，是刻意的简单。

## 2. 逐函数表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "长期记忆 — 基于 Markdown 文件" |
| 3-13 | 导读块 | 段落结构/三能力/写入方/检索策略 |
| 19-24 | 类头注释 | 无缓存：每次操作重读文件 |
| 25-30 | `__init__` | 持有 path |
| 35-40 | `_ensure_file` | 初始化四段骨架（即 schema） |
| 42-43 | `_read` | 全文读取 |
| 45-75 | `get_personality` | 段落状态机 + 全/半角冒号兼容 |
| 77-94 | `search` | 关键词 substring + 尾部兜底 |
| 96-101 | `append` | 追加模式写尾部 |

## 3. 关键摘录

### 3.1 骨架即 schema（ltm.py:35-40）

```python
    def _ensure_file(self):
        if not self.path.exists():
            self.path.write_text(
                "## 性格设定\n\n## 重要记忆\n\n### 关于用户\n\n### 互动历史\n",
                encoding="utf-8"
            )
```

`get_personality` 靠 `## 性格设定` 标题定位段落、`search` 靠 `- ` 前缀筛条目——标题就是表结构，删标题等于删表。

### 3.2 人设解析（ltm.py:45-75 摘段）

```python
        for line in text.split("\n"):
            line = line.strip()
            if line.startswith("## 性格设定"):
                in_section = True
                continue
            if in_section:
                if line.startswith("## "):
                    break  # 撞到下一个二级标题 = 人设段结束
                # 条目行形如「- 名称：值」；同时兼容全角/半角冒号，
                # 因为该段既可能是程序写入也可能是用户手写
                if line.startswith("- ") and "：" in line:
                    content = line[2:]
                    if "：" in content:
                        key, val = content.split("：", 1)  # 只按第一个冒号切，值里可含冒号
                        result[key.strip()] = val.strip()
```

两个细节：`split("：", 1)` 只切第一个冒号（值含冒号不被切碎）；全角/半角冒号两个分支完全重复——兼容输入法差异。逐行状态机：遇 `## 性格设定` 开段、下一个 `## ` 闭段。

### 3.3 关键词检索（ltm.py:77-94）

```python
    def search(self, query: str, limit: int = 10) -> str:
        """简易关键词搜索相关记忆"""
        # 只检索「- 」条目行：标题与空行天然被排除
        text = self._read()
        lines = [l.strip() for l in text.split("\n") if l.strip() and l.strip().startswith("- ")]
        if not query:
            # 返回最近一些记忆
            return "\n".join(lines[-limit:])

        # 按空格切词做 substring 匹配（不分级、不打分、不分词）；
        # lower() 只对 ASCII 生效，中文关键词按原文匹配
        keywords = query.lower().split()
        matched = [l for l in lines if any(kw in l.lower() for kw in keywords)]
        if not matched:
            # 兜底策略：查不到也回最近的记忆，让模型至少有点上下文，
            # 而不是拿到空串导致 prompt 缺一段
            return "\n".join(lines[-limit:])
        return "\n".join(matched[:limit])
```

策略量化：

| 特性 | 实现 | 后果 |
|---|---|---|
| 算法 | `kw in line.lower()` 子串，O(行数×词数) | 无分词无权重 |
| 命中排序 | 文件序（旧→新）取前 10 | 非相关度、非最新优先 |
| 兜底 | 空查询/无命中 → 尾部 10 行 | prompt 记忆段永不为空 |

### 3.4 追加（ltm.py:96-101）

```python
    def append(self, entry: str) -> None:
        """追加一条到互动历史"""
        # 追加模式写文件：不整读全文件，大文件下比 STM 的整写便宜；
        # \n 前缀保证与上一条目行断行
        with open(self.path, "a", encoding="utf-8") as f:
            f.write(f"\n- {entry}")
```

追加模式 O(1) 写（不整读），与 STM 的整写形成对比——长期文件会无限增长，追加是唯一经济的选择。无删除 API：删错误记忆只能手编文件。

## 4. 陷阱与观察

1. **条目行 `- ` 前缀同时被 search 与人设解析使用**：手编时保持 `- 键：值` 或 `- 内容` 格式。
2. **save_memory 写入落点**：`ltm.append` 永远写到**文件末尾**（互动历史段之后）——即使语义上属于"关于用户"。段落语义靠条目前缀（时间戳）维系，不是靠位置。
3. **千行级增长的演进方向**：向量检索/语义分块；当前关键词匹配对中文整词命中率一般（无分词）。
4. **无缓存的重读**：用户手改文件立刻生效（好），代价是每次操作 O(文件大小) 读盘。
