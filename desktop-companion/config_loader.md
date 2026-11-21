# config_loader.md —— Config 与 load_config 双源配置

> 对应源文件：`references/firmware/DesktopCompanion/host/src/config/loader.py`（共 85 行，含 B级导读块）
> 注释级别：B级（文件头导读块，函数级细节见下表与摘录）
> 关联文档：[01-启动与配置加载](../../../references/firmware/DesktopCompanion/Docs/01-启动与配置加载.md)
> 关联条目：[host_main.md](host_main.md)、[llm_client.md](llm_client.md)、[memory_manager.md](memory_manager.md)

---

## 1. 文件职责

双源配置的读取与包装：`config.yaml`（结构参数，同目录）+ `llm.env`（密钥类敏感项，同目录、不入库）。一次读入包成 `Config` 对象，下游模块只从对象取值，不直接碰文件。

## 2. 逐成员表

| 行号 | 成员 | 说明 |
|---|---|---|
| 1 | 模块 docstring | "配置加载器" |
| 3-25 | `#` B级导读块 | 双源职责/数据流位置/函数清单/已知现状 |
| 26 | `_CONFIG_DIR` | `Path(__file__).parent`，yaml/env 相对定位锚点 |
| 29-32 | `Config.__init__` | 存 `_data`（yaml dict）+ `_env`（dotenv dict） |
| 34-56 | 六个段 property | scheduler/llm/memory/personality/output/cli，缺段返回 `{}` |
| 58-69 | 三个 env property | llm_api_key / llm_base_url(默认官方端点) / llm_model(默认 gpt-4o) |
| 70-73 | `get_path` | `parent.parent / key` → host/src/<key> |
| 75-85 | `load_config` | safe_load yaml + dotenv_values + 包 Config |

## 3. 关键摘录

### 3.1 入口（loader.py:75-85）

```python
def load_config() -> Config:
    """加载 YAML 和 .env 配置"""
    yaml_path = _CONFIG_DIR / "config.yaml"
    env_path = _CONFIG_DIR / "llm.env"

    with open(yaml_path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    env_data = dotenv_values(env_path)

    return Config(data, env_data)
```

- `yaml.safe_load` 拒绝任意对象标签（`!!python/object:`），防配置文件成为代码执行入口；
- **失败模式不对称**：yaml 缺失 → FileNotFoundError 启动即崩；llm.env 缺失 → `dotenv_values` 返回空 dict，三个 env property 落默认值，直到第一次请求才 401（懒失败，排查易被误导）。

### 3.2 路径锚点（loader.py:70-73）

```python
    def get_path(self, key: str) -> Path:
        """获取相对于 host/src/ 的数据文件路径"""
        return Path(__file__).parent.parent / key
```

锚点是 loader.py 自己的位置（parent.parent = host/src/），与 CWD 无关——任何目录启动 main.py 都能定位 data/。

## 4. config.yaml 字段与接线状态总表

| 字段 | 值 | 消费者 | 状态 |
|---|---|---|---|
| `scheduler.max_tool_loops` | 5 | 无 | **预留**（真值硬编码在 pipeline.py:56） |
| `llm.temperature` | 0.7 | main.py:60 → LLMClient | 已接线 |
| `llm.max_tokens` | 2048 | main.py:61 → LLMClient | 已接线 |
| `memory.stm_max_entries` | 50 | main.py:67 → ShortTermMemory | 已接线 |
| `memory.ltm_search_limit` | 10 | 无 | **预留**（ltm.search 函数默认值） |
| `memory.consolidation_interval` | 6 | main.py:68 → MemoryManager | 已接线 |
| `personality.*` | 小伴等 | 无 | **预留**（人设实际来自 ltm.md） |
| `cli.log_level` | INFO | 无 | **预留**（无日志系统） |

## 5. 陷阱与观察

1. **改预留字段无效**：max_tool_loops / personality / log_level 三处是"配置先行"的预留位（见 [host_main.md](host_main.md) 与 [scheduler_pipeline.md](scheduler_pipeline.md)）。
2. **人设真身在 ltm.md**：yaml personality 段 ≠ 生效人设；生效路径 memory.get_personality → ltm.md `## 性格设定`（见 [memory_ltm.md](memory_ltm.md)）。
3. **`llm.env` 三键缺失行为**：KEY 缺 → 请求期 401；BASE_URL 缺 → 默认 api.openai.com（国内网络可能连不通）；MODEL 缺 → gpt-4o（部分中转站不识别）。
4. **六 property 双层兜底**：段级 `.get(段名, {})` + 调用侧 `.get(键, 默认)`——删整段不报错，删文件才报错。

## 6. 失败模式矩阵

| 场景 | 行为 | 用户感知 |
|---|---|---|
| config.yaml 文件缺失 | `open()` 抛 FileNotFoundError | 启动即崩，traceback 指向 loader.py:80 |
| config.yaml 某段缺失 | property 返回 `{}` | 下游落默认值，静默 |
| config.yaml 某键缺失 | 下游 `.get(键, 默认)` 兜底 | 静默 |
| llm.env 文件缺失 | dotenv_values 返回空 dict | 静默（懒失败） |
| llm.env 键缺失 | property 返回默认值 | 第一次请求时 401/连不上 |
| LLM_API_KEY 粘贴带换行 | 请求 401 | 难排查——检查 env 文件行尾 |

## 7. 自测三问

1. 为什么用 safe_load？（答：拒绝 `!!python/object` 等标签，防配置文件执行任意代码）
2. get_path("data") 的锚点是什么？（答：loader.py 自身位置 parent.parent = host/src/，与 CWD 无关）
3. 首次部署最小配置动作是什么？（答：仅创建 llm.env 三个键；config.yaml 随仓库自带）

## 8. 与 freertos/嵌入式配置的对照（迁移学习）

| 维度 | 本工程 Config | FreeRTOSConfig.h（嵌入式） |
|---|---|---|
| 载体 | YAML + dotenv 运行时读 | 编译期宏 |
| 缺失行为 | property 兜底 `{}` | `#ifndef` 条件默认值 |
| 敏感项 | env 文件不入库 | 无此概念（全编译进固件） |
| 共通点 | 都是"未配置→安全默认值"模式 | — |

共同的设计哲学：**配置缺失永远不是错误，落到保守默认值**——只有载体文件整体缺失才硬失败（yaml FileNotFoundError / 头文件缺失）。

*（EOF 补充：对照小节为跨工程联想，不影响本文件结论）*
