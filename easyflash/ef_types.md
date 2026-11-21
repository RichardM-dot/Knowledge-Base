# KB: ef_types —— types 强类型插件（B级导读；合并条目）

对应源文件（合并条目，两文件强耦合按 STANDARD 允许合并）：
- `easyflash/plugins/types/ef_types.c`（原始 395 行，注释深注后 407 行；B级导读块，strip_equiv PASS）
- `easyflash/plugins/types/ef_types.h`（原始 84 行，注释深注后 95 行；B级导读块，strip_equiv PASS）

## 0. 定位与读法

给字符串 ENV 套"强类型外壳"的可选插件：基本类型 atoi/atol/atof 直转；数组与结构体经 cJSON/struct2json 序列化为 JSON 字符串再走 `ef_set_env/ef_get_env`。**构建在字符串 API 之上，三套 env 模式通用**。依赖 cJSON（同目录 struct2json/，C级第三方不动）。

## 1. 逐函数表（ef_types.c，注释后行号；文件未做函数级深注，仅文件头导读）

| 函数 | 行号 | 职责 | 要点 |
| --- | --- | --- | --- |
| `ef_types_init` | L65 | 注册 s2j 内存钩子 | 不调则用 libc malloc/free |
| `ef_get_bool` | L69 | 读 bool | atoi 非 0 即真 |
| `ef_get_char/short/int` | L79-89 | 读整数 | 全部转发 ef_get_long |
| `ef_get_long` | L91 | 读长整 | atol |
| `ef_get_float/double` | L101-113 | 读浮点 | atof 转发 |
| `ef_get_array`（static） | L122 | 数组读泛型 | cJSON_Parse→按 8 类枚举分发取元素 |
| `ef_get_*_array` ×8 | L178-208 | 数组读 API | 一行转发 |
| `ef_get_struct` | L220 | 结构体读 | 用户 get_cb（struct2json）反序列化；**返回 malloc 内存须调用方 free** |
| `ef_set_bool` | L232 | 写 bool | "0"/"1" |
| `ef_set_char/short/int` | L242-252 | 写整数 | 转发 ef_set_long |
| `ef_set_long` | L254 | 写长整 | snprintf 20 字节缓冲 |
| `ef_set_float/double` | L262-272 | 写浮点 | snprintf "%lf" |
| `ef_set_array`（static） | L284 | 数组写泛型 | cJSON_CreateArray→逐项创建→PrintUnformatted→set→free |
| `ef_set_*_array` ×8 | L356-386 | 数组写 API | 一行转发 |
| `ef_set_struct` | L396 | 结构体写 | set_cb 序列化→set→释放 |

## 2. 关键摘录（逐字，注后行号）

### 2.1 数组写核心（ef_set_array，L340-343）

```c
        char_value = cJSON_PrintUnformatted(array);
        if (char_value) {
            result = ef_set_env(key, char_value);
            s2jHook.free_fn(char_value);
```

序列化结果立即落 ENV，JSON 文本用完即 free——内存路径：cJSON 树 → 字符串 → ENV 字符串，两跳分配都受 s2jHook 管。

### 2.2 结构体读的内存契约（L217-218 原注释）

```c
 * @return value returned structure ENV value pointer. @note The returned value will malloc new ram.
 *         You must free the value then used finish.
```

## 3. 陷阱

1. **128B 上限传导**：插件走 `ef_get_env`（NG 模式 128B 静态缓冲 + 可打印校验），大数组/大结构体必被截断——大对象请直接 `ef_set_env_blob` 存内存镜像；
2. `ef_types.h` L34 的 `#include "struct2json\inc\s2j.h"` 用了**反斜杠路径**（Windows 习惯），类 Unix 工具链需改成正斜杠或加 include path（不改库文件，构建系统侧解决）；
3. `ef_set_array` 里 `EF_INFO("Memory full!\n", key)` 的 format 与参数不匹配（原码小瑕疵，记录不改）；
4. double 的 "%lf" 在部分编译器等价 "%f"，精度按编译器实现。

## 4. 内存与调用链

```text
ef_set_int(key, 16000)
  └─ snprintf "%ld"            [栈 21B]
  └─ ef_set_env(key, "16000")  [字符串 API 底座]

ef_set_float_array(key, arr, 3)
  └─ cJSON_CreateArray          [堆: cJSON 树]
  └─ cJSON_CreateNumber ×3      [堆]
  └─ cJSON_PrintUnformatted     [堆: "[0.5,1,2]"]
  └─ ef_set_env(key, json)      [字符串落 ENV]
  └─ s2jHook.free_fn(json)      [堆回收]
  └─ cJSON_Delete(array)        [堆回收]

ef_get_struct(key, json_to_wifi)
  └─ ef_get_env(key)            [128B 静态缓冲!!]
  └─ cJSON_Parse                [堆]
  └─ get_cb(json)               [堆: 结构体实例]
  └─ 返回实例指针               [调用方 free]
```

堆峰值 ≈ cJSON 树 + 序列化字符串双份；RAM 紧张的 MCU 建议数组元素 ≤ 32 个、结构体 ≤ 4 字段，或干脆 `ef_set_env_blob` 直存内存镜像。

## 5. 启用方法

1. 把 `plugins/types/`（含 struct2json 的 src/inc）整目录加入工程；
2. 头文件搜索路径加 `plugins/types`（顺带解决陷阱 2 的反斜杠 include）；
3. `#include <ef_types.h>`，在 `easyflash_init()` 成功后调用 `ef_types_init(NULL)`。

## 6. 与"直接存结构体"方案对比

| 维度 | types 插件（JSON 文本） | `ef_set_env_blob`（内存镜像） |
| --- | --- | --- |
| 人类可读 | 是（串口/上位机直接看） | 否（hex dump） |
| 版本兼容 | 好（缺字段可容忍，按名解析） | 差（结构体一变偏移全乱，需自加版本号） |
| RAM/ROM | +cJSON ~10KB | ~0 |
| 单值开销 | 数百字节（JSON 冗余） | 精确 sizeof |
| 适用 | 少量、可读性优先的配置 | 大结构体、高频读写 |

经验法则：人看的配置走 types 插件，机器看的大数据走 blob——两套 API 的分界线正是"谁消费这份数据"。

## 7. 互链

- 字符串 API 底座：Docs/[06-API 与实测指南](../../../references/filesystems/EasyFlash/Docs/06-API与实测指南.md) 1.6 节
- 底层实现：[ef_env.c.md](ef_env.c.md)（get/set 的 128B 缓冲即截断源头）
