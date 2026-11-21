# test_main.cpp —— native 空 main

对应源文件：`src/src/test_main.cpp`（共 25 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)

---

## 1. 文件职责

当 PlatformIO 的 `[env:native]` 以“可执行程序”方式构建时，链接器需要一个
`main()`。本文件提供最小实现：打印一行提示后返回 0。

---

## 2. 全部代码

```cpp
#ifndef UNIT_TEST
#include <iostream>

using namespace std;

int main()
{
	cout << "Doing nothing just keep build all happy!" << endl;
	return 0;
}
#endif
```

| 行号 | 内容 | 说明 |
|---|---|---|
| 19 | `#ifndef UNIT_TEST` | 单元测试构建时排除 |
| 20 | `#include <iostream>` | 仅用于打印 |
| 24 | `int main()` | 空实现 |
| 22 | `cout << "Doing nothing just keep build all happy!"` | 提示语 |

---

## 3. 为什么需要它

| 构建方式 | main 来源 |
|---|---|
| `pio run -e native`（无测试） | 本文件 |
| `pio test -e native` | Unity 框架生成 |
| 固件构建 | Arduino 核心 |

PlatformIO 的 native 平台默认把 `src/` 编成可执行文件，没有 `main` 会链接失败。
用 `#ifndef UNIT_TEST` 把两条路径分开，避免符号重复。

---

## 4. 设计要点与观察

1. **纯占位**：不初始化任何东西，不跑任何逻辑。
2. **条件编译**：`UNIT_TEST` 由 `platformio.ini` 的 `-D UNIT_TEST=1` 定义。
3. **提示语的作用**：手动运行 `./program` 时能确认“确实什么都没做”，
   而不是误以为测试没跑。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 22 行 | `cout`/`endl` 在嵌入式工具链里不可用，但本文件只在 native 编译 |
| 20 行 | 使用 `<iostream>` 会显著增大 native 二进制（对本用途无所谓） |

---

## 5. 互链

- 平台伪造：[native_h.md](native_h.md)
- 构建配置：[05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)
- 同类替身：[esp8266_waveform_stubs_cpp.md](esp8266_waveform_stubs_cpp.md)

---

## 6. 构建矩阵：三种 main 的来源

| 命令 | 平台 | 定义 | main 来源 | 产物 |
|---|---|---|---|---|
| `pio run -e native` | native | 无 `UNIT_TEST` | 本文件 | 可执行文件 |
| `pio test -e native` | native | `UNIT_TEST=1` | Unity | 测试可执行 |
| `pio run -e esp32-tx` | ESP32 | `TARGET_TX=1` | Arduino 核心 | 固件 bin |

`platformio.ini` 里 `[env:native]` 的关键行：

```ini
build_flags =
	-std=c++11
	-Iinclude
	-D PROGMEM=""
	-D UNIT_TEST=1
	-D TARGET_NATIVE
```

注意 `-D UNIT_TEST=1` 是**无条件**加的，所以 `pio run -e native` 时
本文件的 `#ifndef UNIT_TEST` 为假，`main` 不会编进来——此时由 PlatformIO
的 native 模板提供 main。这看起来矛盾，实际是“两个入口二选一”：
`UNIT_TEST` 定义了就走测试框架，没定义（例如手动改 ini）就走本文件。

---

## 7. `UNIT_TEST` 宏的连锁影响

定义 `UNIT_TEST` 会同时改变多处行为：

| 文件 | 影响 |
|---|---|
| `common.h` | 跳过真实驱动头文件，改用 `SX12xxDriverCommon.h` |
| `common.h` | 不声明 `extern SX1280Driver Radio` |
| `targets.h` | 允许不定义监管域（`#if ... || defined(UNIT_TEST)`） |
| `helpers.h` | `NullStream` 仍可用（继承伪造的 `Stream`） |
| 本文件 | 排除 `main` |

所以“能不能在 native 上编译”本质上取决于这些条件编译是否覆盖完整。

---

## 8. 为什么不用 `#ifdef` 包住整个 src

有人会问：为什么不给每个源文件都加 `#ifdef UNIT_TEST`？
因为 ELRS 的目标恰恰是“**业务代码零改动可测**”——所有平台差异都被
`native.h` 的伪造层吸收。加 `#ifdef` 会让代码里到处是测试专用分支，
反而降低可测性。

---

## 9. 实测建议

| 目的 | 命令 | 预期 |
|---|---|---|
| 确认能链接 | `pio run -e native` | 输出提示语后退出 0 |
| 确认测试入口 | `pio test -e native` | 跑 Unity 用例，不打印提示语 |
| 确认宏生效 | `pio run -e native -v` | 编译命令里能看到 `-D UNIT_TEST=1` |
