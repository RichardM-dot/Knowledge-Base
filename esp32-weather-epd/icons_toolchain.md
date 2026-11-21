# icons_toolchain —— 图标流水线（B级）

对应源文件（合并条目，三个强耦合小文件）：
- `icons/png_to_header.py`（共 106 行，已注释 B 级）
- `icons/final_generate_icons_h.py`（共 105 行，已注释 B 级）
- `icons/svg_to_headers.sh`（共 79 行，**未加源码注释**：校验脚本对非 .py 文件按 C 规则剥离注释，`#` 注释无法通过 strip_equiv_check，故只在 KB 覆盖）
- 产物：`platformio/include/icons/icons.h` 与 `icons_NxN.h`、`icons/NxN/*.h`（约 1000 个图标 × 9 档尺寸）

相关文档：[Docs/04-UI布局与绘制管线](../../../references/firmware/esp32-weather-epd/Docs/04-UI布局与绘制管线.md)
互链：[display_utils.md](display_utils.md) · [fonts_toolchain.md](fonts_toolchain.md)

## 0. 流水线全景

```mermaid
flowchart TD
    A["icons/svg/*.svg<br/>(weather-icons / Google / 自绘)"] -->|"svg_to_headers.sh<br/>mkdir png/NxN"| B1["inkscape -w N -h N<br/>白底 PNG (svg_to_headers.sh:55)"]
    B1 --> B2["png/NxN/*.png"]
    B2 -->|"png_to_header.py -i -o<br/>(svg_to_headers.sh:65)"| C["icons/NxN/xxx_NxN.h<br/>PROGMEM 字节数组"]
    C -->|"final_generate_icons_h.py<br/>扫描汇总 (icons 目录内运行)"| D["icons/icons.h<br/>枚举 + constexpr getBitmap"]
    D --> E["编译进固件<br/>display_utils.cpp getBitmap 调用"]
```

用法：`cd icons && ./svg_to_headers.sh 196`（尺寸 16/24/32/48/64/96/128/160/196）。png/ 或 icons/NxN/ 已存在则跳过（幂等），重做先删目录。

## 1. png_to_header.py 逐段表

| 段 | 行号 | 职责 |
|---|---|---|
| 参数解析 | 28-48 | `-i 输入.png -o 输出.h`，缺一退出 |
| 图像加载 | 61-65 | `convert('L')` 灰度化，`getdata()` 拍平像素表 |
| 位图打包 | 81-104 | 核心循环，见下 |
| 文件尾 | 105-107 | 末字节左移补齐 + `};` |

打包循环（png_to_header.py:81-104）要点：

```python
for i in range(n):
    line_width += 1
    if (pixels[i] > THRESHOLD):
        tmp_bite |= 1
    if (line_width == width):
        tmp_bite <<= BITS_PER_BITE - bit_cnt
        line_width = 0
        bit_cnt = 8
```

- **1bit 黑白**：灰度 >127 记 1（黑），MSB 优先逐位填进 `tmp_bite`；
- **行末补齐**：一行走完把残余位左移到高位（GFX 位图按整行整字节对齐）；
- **输出版式**：每行 12 字节（`BITES_PER_LINE=12`，变量名把 BYTE 拼成 BITE，原码如此）；
- 产物：`const unsigned char <文件名>[] PROGMEM = { 0x.., ... };`

## 1.1 svg_to_headers.sh 的第 3 步（汇总，sh 文件实录）

摘录 `icons/svg_to_headers.sh:59-76`（生成头文件目录后汇总索引）：

```bash
if [ ! -e "$HEADER_PATH" ]; then
  mkdir $HEADER_PATH
  for f in $PNG_FILES
  do
    echo "Generating header for $f..."
    out="${HEADER_PATH}/$(basename $f .png | tr -s -c [:alnum:] _)${1}x${1}.h"
    python3 png_to_header.py -i $f -o $out
  done

  echo "Generating include statements..."
  echo "#ifndef __ICONS_${1}x${1}_H__" > $HEADER
  echo "#define __ICONS_${1}x${1}_H__" >> $HEADER
  for f in ${HEADER_PATH}/*.h
  do
      echo "#include \"${1}x${1}/$(basename $f)\"" >> $HEADER
  done
  echo "#endif" >> $HEADER
fi
```

`tr -s -c [:alnum:] _` 把 PNG 文件名清洗成合法 C 标识符基底（`11.25deg` → `11_25deg`），与 display_utils.cpp 引用的符号名一致。

## 2. final_generate_icons_h.py 逐函数表

| 函数 | 行号 | 职责 |
|---|---|---|
| `generate_icons_h` | 30 | 唯一函数，全流程 |
| 尺寸扫描 | 45-58 | 两轮 `os.listdir`：收集尺寸列表 + 从第一个尺寸目录收集图标名 |
| 产物拼接 | 61-98 | include 列表 → `typedef enum icon_name` → `constexpr getBitmap(icon_name_t, size_t)` 两级 switch |
| 写盘 | 102-104 | 写 `icons/icons.h`（注意相对路径，必须在 icons/ 目录内运行） |

生成物形态（节选）：

```cpp
constexpr const unsigned char* getBitmap(icon_name_t icon, size_t size)
{
  switch (icon) {
  case wi_day_sunny:
    switch (size) {
    case 16: return wi_day_sunny_16x16;
    ...
```

**省 flash 机制**：`constexpr` + 未被引用的分支不会链接进固件（链接器按目标文件粒度 + 编译器常量折叠裁剪），9 档尺寸 × 上千图标的组合最终只留实际用到的。

## 3. 深读要点 / 坑点

- **总像素必须被 8 整除**（svg_to_headers.sh:32-33 原注释）：N×N 需 N 为偶数且行字节对齐；脚本现有 9 个尺寸都满足。
- **命名清洗**：头文件名由 PNG 名转义（`tr -s -c [:alnum:] _`），`wind_direction_meteorological_11_25deg` 这类带点的名字会变成下划线——display_utils.cpp 里的符号名与此一致。
- **rotate_90_svg/ 与 rotate_wind_direction_svg/**：生成 360 张风向 SVG 的辅助脚本目录（Rust/Python 混合），产物已入库，一般无需重跑。
- **icons/Makefile**：`make` 一键跑全部尺寸的 svg_to_headers.sh。

## 4. 数据流角色

SVG 源 →（本流水线）→ C 数组 → `display_utils.cpp getConditionsBitmap<>()` 选择 → `renderer.cpp drawInvertedBitmap` 上屏。图标映射的四维判据见 [display_utils.md](display_utils.md) 第 2 节。
