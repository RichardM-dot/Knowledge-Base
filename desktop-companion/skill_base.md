# skill_base.md —— BaseSkill 与 SkillResult

> 对应源文件：`references/firmware/DesktopCompanion/host/src/skill/base.py`（共 80 行，含中文深注）
> 注释级别：A级（逐类逐函数深注）
> 关联文档：[05-技能系统与通道](../../../references/firmware/DesktopCompanion/Docs/05-技能系统与通道.md)
> 关联条目：[skill_registry.md](skill_registry.md)、[scheduler_pipeline.md](scheduler_pipeline.md)、[llm_client.md](llm_client.md)

---

## 1. 文件职责

技能体系的两个地基：

1. `SkillResult`——技能执行的统一返回值，`format_for_llm()` 决定模型能看到什么；
2. `BaseSkill`——所有技能的抽象基类：子类只填四个类属性 + 实现 `execute()`，即可被注册、被 LLM 调用。

**核心契约**：`execute` 必须是 async 且**永不抛异常**——用 `SkillResult(success=False)` 表达失败，让错误信息可回喂给 LLM（builtin 四技能全部遵守：超时/空参数/异常都转成失败结果）。

## 2. 逐类逐函数表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "技能基类" |
| 3-12 | 导读块 | 两个地基/子类约定/异常契约 |
| 20-38 | `SkillResult` | success/data/error + format_for_llm |
| 40-46 | 类头注释 | 子类约定：name 蛇形唯一、description 即说明书 |
| 47-50 | 四个类属性 | name/description/parameters/requires_confirmation |
| 57-68 | `get_schema` | OpenAI function schema 生成 |
| 71-73 | `format_confirmation_prompt` | 确认弹窗正文（run_shell 重写） |
| 78-80 | `execute`（抽象） | async + SkillResult 返回 |

## 3. 关键摘录

### 3.1 SkillResult 三态（skill_base.py:22-37）

```python
    success: bool
    data: Any = None  # 成功时的产物（字符串/对象均可，展示时 str()）
    error: Optional[str] = None  # 失败原因，会原文回喂给 LLM

    # ------------------------------------------------------------
    # format_for_llm() — 决定回喂给 LLM 的结果文本
    # @return 成功 → data 字符串(空 data 说「成功」)；失败 → 「错误: …」
    # @note   pipeline._append_tool_results 把它的返回值放进
    #         role=tool 消息，是模型感知执行结果的唯一通道。
    # ------------------------------------------------------------
    def format_for_llm(self) -> str:
        if self.success:
            # data 为空统一说「成功」：给 LLM 一句确定性的反馈，
            # 避免它把空串理解成执行异常
            return str(self.data) if self.data else "成功"
        return f"错误: {self.error}"
```

三态映射：

| success | data | 输出 | 消费方 |
|---|---|---|---|
| True | 有 | `str(data)` | role=tool 消息（模型）+ 面板绿色行截 100 字 |
| True | None | `"成功"` | 同上 |
| False | — | `"错误: ..."` | 面板红色行 |

### 3.2 类属性 = schema 原料（skill_base.py:47-50）

```python
    name: str = ""
    description: str = ""
    parameters: dict = {}  # JSON Schema 的 properties/required 部分
    requires_confirmation: bool = False  # True 则执行前必须人工 y/n 放行
```

- `description` 是提示词工程的一部分——LLM 靠它判断"何时调用"；写法参考：功能 + 触发场景 + 边界（"不要记录琐碎的闲聊"）；
- `parameters` 是 JSON Schema 片段（properties/required），嵌进 schema 的 function.parameters。

### 3.3 schema 生成（skill_base.py:57-65）

```python
    def get_schema(self) -> dict:
        return {
            "type": "function",
            "function": {
                "name": self.name,
                "description": self.description,
                "parameters": self.parameters,
            }
        }
```

结构固定为 OpenAI function-calling 的 `tools[]` 元素；registry 的 `get_function_schemas` 逐技能调用汇出。

### 3.4 抽象方法（skill_base.py:78-80）

```python
    @abstractmethod
    async def execute(self, **kwargs) -> SkillResult:
        ...
```

`**kwargs` 即 LLM 填的 arguments——参数名必须与 parameters 的 properties 对齐，多传被 kwargs 吞掉、少传靠技能内 `kwargs.get` 兜底。

## 4. builtin 四技能对照（未注释文件，此表为准）

| 技能 | 特有配置 | execute 要点 | 行号（builtin 文件内） |
|---|---|---|---|
| get_time | 无参数 schema | datetime.now + 中文星期映射 | get_time.py:16-23 |
| save_memory | 注入 memory；required=["content"] | 空内容失败；`f"{now}: {content}"` 写 LTM | save_memory.py:28-36 |
| run_shell | requires_confirmation=True；重写确认文案 | subprocess.run(shell=True, timeout=30)，输出截 2000 字 | run_shell.py:24-43 |
| search_memory | required=["query"] | 转发 memory.retrieve_relevant | search_memory.py:26-31 |

安全三件套（run_shell）：人工确认闸门 + 30s 超时 + 输出截断 2000 字符；`shell=True` 意味着命令拥有用户全部 shell 权限，确认环节是唯一防线。

## 5. 陷阱与观察

1. **类属性 `parameters: dict = {}` 共享**：四个类属性是类级共享的可变默认值——若子类在实例里原地修改 `self.parameters` 会污染父类。builtin 都只读，无实际触发。
2. **format_confirmation_prompt 默认实现打印整个 kwargs 字典**：参数多时可读性差；run_shell 重写为两行专用文案是正解样板。
3. **execute 签名无 schema 校验**：arguments 与 parameters 的对齐完全靠 LLM 自觉 + 技能内兜底；不匹配的后果是失败 SkillResult 回喂（自愈）。
4. **SkillResult 是 dataclass**：`data: Any` 可以装任意对象，但 format_for_llm 会 `str()`——建议技能只装字符串（builtin 均如此）。
