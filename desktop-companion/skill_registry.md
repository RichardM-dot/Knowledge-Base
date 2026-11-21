# skill_registry.md —— SkillRegistry 技能注册表

> 对应源文件：`references/firmware/DesktopCompanion/host/src/skill/registry.py`（共 62 行，含中文深注）
> 注释级别：A级（逐函数深注）
> 关联文档：[05-技能系统与通道](../../../references/firmware/DesktopCompanion/Docs/05-技能系统与通道.md)
> 关联条目：[skill_base.md](skill_base.md)、[scheduler_pipeline.md](scheduler_pipeline.md)、[host_main.md](host_main.md)

---

## 1. 文件职责

LLM 可用技能的"电话簿"：注册期收集全部 BaseSkill 实例；运行期按 LLM 点名的 function name 查找技能，并把技能 schema 汇出给 LLM。dict 即注册表，键 = 技能 name（约定全局唯一，重复注册是静默替换）。

数据流位置：main.py 启动 register → pipeline 每轮取 get_function_schemas 塞进 tools → LLM 返回 tool_calls 时 pipeline 用 get() 派发。

## 2. 逐函数表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "技能注册表" |
| 3-12 | 导读块 | 职责/数据流位置/同名覆盖约定 |
| 19-24 | 类头注释 | dict 插入序 = 列表序（3.7+ 保证） |
| 25-31 | `__init__` | 空 dict |
| 33-38 | `register` | 键冲突 = 后注册顶替 |
| 40-45 | `get` | 未注册返回 None（pipeline 造失败结果） |
| 47-50 | `get_function_schemas` | 全部 schema 汇出，每轮调用 |
| 54-61 | `list_skills` | 三字段展示清单（/skills 命令、启动横幅） |

## 3. 关键摘录

### 3.1 注册与查派（registry.py:29-42）

```python
    # ------------------------------------------------------------
    # register() — 启动期注册一个技能实例
    # @note   main.py 在 pipeline 启动前完成全部注册，运行期不再
    #         动态增删；键冲突 = 后注册者顶替先注册者
    # ------------------------------------------------------------
    def register(self, skill: BaseSkill) -> None:
        self._skills[skill.name] = skill

    # ------------------------------------------------------------
    # get() — 按 LLM 点名的函数名查技能；未注册返回 None，
    #         pipeline 会据此生成「未知技能」失败结果回喂
    # ------------------------------------------------------------
    def get(self, name: str) -> Optional[BaseSkill]:
        return self._skills.get(name)
```

`get` 返回 None 而非抛异常——派发方（pipeline.py:271-275）据此生成"未知技能: xxx"失败 SkillResult 回喂，LLM 下一轮自行改口。

### 3.2 schema 汇出（registry.py:47-50）

```python
    def get_function_schemas(self) -> list[dict]:
        return [s.get_schema() for s in self._skills.values()]
```

每轮对话调用一次（pipeline.py:97），整体塞进 chat 请求的 `tools` 参数。schema 内容由各技能的 `get_schema()`（[skill_base.md](skill_base.md)）生成。

### 3.3 注册现状（main.py:71-78）

| 注册顺序 | 技能 | 说明 |
|---|---|---|
| 1 | GetTimeSkill | 无依赖 |
| 2 | SaveMemorySkill(memory) | 注入 MemoryManager |
| 3 | RunShellSkill | requires_confirmation=True |
| — | SearchMemorySkill | **实现了但未注册**（阶段 1 已自动检索，功能重叠） |

## 4. 陷阱与观察

1. **同名静默覆盖**：register 无重复检查。实验方法：注册两个 `name="get_time"` 的实例，LLM 会调到后者——技能名冲突没有任何告警。
2. **运行期不可增删**：约定 pipeline 启动前完成注册；若要动态注册（如设备上线后出现新技能），需要加锁或改为快照式汇出。
3. **schema 汇出无缓存**：每轮重建 list——3 个技能的开销可忽略；技能多了可缓存（技能属性不可变的前提下）。
4. **list_skills 截断**：description 展示截 60 字（interface.py:150），完整描述只在 schema 里给 LLM。

## 5. registry × pipeline 交互逐行追踪

```
main.py:71       skills = SkillRegistry()
main.py:72-78    三次 register（get_time / save_memory(memory) / run_shell）
pipeline.py:97   ctx.skill_schemas = self.skills.get_function_schemas()
pipeline.py:102  response = await self._stream_llm(messages, ctx.skill_schemas, state)
                 → client.py:73-75 kwargs["tools"] = tools
client.py:108-125 LLM 返回 tool_calls 残片 → 拼装 ToolCall
pipeline.py:271  skill = self.skills.get(tc.function_name)
pipeline.py:291  result = await skill.execute(**tc.arguments)
main.py:90       len(skills.list_skills())   ← 启动横幅
interface.py:147 self.skills.list_skills()   ← /skills 表格
```

一条 schema 从注册到 LLM 可见要经过 4 个文件 5 次传递；任何一环丢字段（如 description 空）都会直接降低模型路由准确率。

## 6. 自测三问

1. `get()` 返回 None 时 pipeline 怎么办？（答：构造 SkillResult(False, error="未知技能: ...") 回喂，LLM 下轮改口）
2. 两个技能同名注册，第二个 register 会报错吗？（答：不会，dict 赋值静默覆盖）
3. get_function_schemas 的顺序有保证吗？（答：有——dict 保持插入序（Python 3.7+），顺序 = main.py 注册序）

## 7. schema 传递质量核对表

schema 是 LLM 路由的唯一依据，逐字段核对三个已注册技能的 description 质量：

| 技能 | description 关键词 | 路由效果评估 |
|---|---|---|
| get_time | "获取当前的日期和时间。当需要知道现在是什么时候时使用。" | 场景式触发语，明确 |
| save_memory | "将重要的信息记录到长期记忆中。……不要记录琐碎的闲聊。" | 功能 + 边界双声明，防滥用 |
| run_shell | "在用户电脑的终端中执行一条命令。……每次调用都需要用户的明确许可。" | 功能 + 风险预告 |

规律：每个 description 都是"功能句 + 时机句（+ 边界句）"三段式——新增技能时照此模板写，路由最稳。

## 8. 新增技能清单（施工顺序）

1. 在 `skill/builtin/` 建 `xxx.py`，继承 BaseSkill 填四类属性 + 实现 async execute；
2. `main.py` import 并 `skills.register(XxxSkill(...))`（有依赖则构造器注入）；
3. 无需改 registry/pipeline/llm——schema 自动汇出、派发自动生效；
4. 用 `/skills` 命令验证注册，再对 LLM 说一句触发话术验证路由。
