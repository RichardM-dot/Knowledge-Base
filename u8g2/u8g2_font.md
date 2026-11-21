# u8g2_font.c —— 字体数据格式与 API 契约

> 对应源文件：`csrc/u8g2_font.c`（共 2001 行）
> 注释分级：本条目覆盖**字体数据格式 + 公共 API 契约**（上半场视角）；
> 逐函数渲染实现见姊妹条目 [clib_u8g2_font.md](clib_u8g2_font.md)（A 级深注）
> 上游：单色屏图形库 u8g2（olikraus）
>
> 姊妹条目：[u8g2_h.md](u8g2_h.md) | [u8g2_buffer.md](u8g2_buffer.md)
> 深度文章：[Docs/10-字体引擎精读.md](../../../references/graphics/u8g2/Docs/10-字体引擎精读.md)、
> [Docs/02-绘制管线精读.md](../../../references/graphics/u8g2/Docs/02-绘制管线精读.md)

---

## 1. 一句话定位

**把 bdfconv 编译出的 RLE 位图字体数组，解码成一条条水平线画进帧缓冲。**
本文件同时承担三件事：字体头解析（`u8g2_read_font_info`）、字形寻址
（`u8g2_font_get_glyph_data`）、RLE 解码与绘制（`u8g2_font_decode_glyph`），
外加字符串排版与度量 API（`u8g2_DrawStr` / `u8g2_GetUTF8Width` 等）。

本条目只讲**数据格式与调用契约**；位流拼接、游程折行等实现细节见
[clib_u8g2_font.md](clib_u8g2_font.md)。

## 2. 字体文件的 23 字节头

结构体定义在 `csrc/u8g2.h` L265~L290，逐字段对应字体数组的前 23 字节：

| 偏移 | 字段 | 类型 | 含义 |
|---|---|---|---|
| 0 | `glyph_cnt` | u8 | 字形总数（0xFF 表示超过 255，靠 start_pos 兜底） |
| 1 | `bbx_mode` | u8 | 高度参考模式：0=text / 1=xtext / 2=all |
| 2 | `bits_per_0` | u8 | 背景游程的位宽 |
| 3 | `bits_per_1` | u8 | 前景游程的位宽 |
| 4 | `bits_per_char_width` | u8 | 字形宽度字段的位宽 |
| 5 | `bits_per_char_height` | u8 | 字形高度字段的位宽 |
| 6 | `bits_per_char_x` | u8 | x 偏移字段的位宽（有符号） |
| 7 | `bits_per_char_y` | u8 | y 偏移字段的位宽（有符号） |
| 8 | `bits_per_delta_x` | u8 | 步进宽字段的位宽（有符号） |
| 9 | `max_char_width` | s8 | 最大字形宽 |
| 10 | `max_char_height` | s8 | 最大字形高（不是 ascent） |
| 11 | `x_offset` | s8 | 全局 x 偏移 |
| 12 | `y_offset` | s8 | 全局 y 偏移 |
| 13 | `ascent_A` | s8 | 大写 A 的上高（TEXT 模式） |
| 14 | `descent_g` | s8 | 小写 g 的下探（通常为负） |
| 15 | `ascent_para` | s8 | 括号类上高（ALL 模式） |
| 16 | `descent_para` | s8 | 括号类下探 |
| 17 | `start_pos_upper_A` | u16 | 大写 A 段的起始偏移 |
| 19 | `start_pos_lower_a` | u16 | 小写 a 段的起始偏移 |
| 21 | `start_pos_unicode` | u16 | unicode 区起始偏移（仅 `U8G2_WITH_UNICODE`） |

常量：

```c
#define U8G2_FONT_DATA_STRUCT_SIZE 23
```

（`csrc/u8g2_font.c` L51）关闭 `U8G2_WITH_UNICODE` 时结构体少 2 字节，
但**内置字体数组是按 23 字节生成的**，所以这个宏必须与字体数据匹配。

## 3. 字形记录格式

### 3.1 ASCII 段（编码 ≤ 255）

```text
[1 字节编码][1 字节本条记录总长][字形数据...]
```

寻址逻辑（`u8g2_font_get_glyph_data`，L952~L981）：

```c
    for(;;)
    {
      if ( u8x8_pgm_read( font + 1 ) == 0 )
	break;
      if ( u8x8_pgm_read( font ) == encoding )
      {
	return font+2;	/* skip encoding and glyph size */
      }
      font += u8x8_pgm_read( font + 1 );
    }
```

- **`font+1 == 0` 是段结束哨兵**，不是"长度为 0 的字形"；
- 返回的是 `font+2`，即**跳过编码与长度两字节**，指向字形数据本体；
- 遍历是**线性**的，但通过 `start_pos_upper_A` / `start_pos_lower_a`
  先跳到对应段（L960~L969），把平均比较次数从 O(glyph_cnt) 降到 O(段内字形数)。

### 3.2 Unicode 段（编码 > 255）

两级结构：先 4 字节索引项 `[2 字节段偏移][2 字节编码上界]`，再段内线性扫
（L997~L1036）：

```c
    do
    {
      font += u8g2_font_get_word(unicode_lookup_table, 0);
      e = u8g2_font_get_word(unicode_lookup_table, 2);
      unicode_lookup_table+=4;
    } while( e < encoding );
```

段内记录是 `[2 字节编码大端][1 字节长度][数据...]`，命中返回 `font+3`（L1033）。

**注意 `u8g2_font_get_word` 是手工大端拼装**，与 MCU 字节序无关——字体数据是
跨平台的二进制格式，不能直接 `*(uint16_t*)`。

### 3.3 字形数据本体

`u8g2_font_setup_decode`（L717~L736）按固定顺序读五个字段：

```c
  decode->glyph_width = u8g2_font_decode_get_unsigned_bits(decode, u8g2->font_info.bits_per_char_width);
  decode->glyph_height = u8g2_font_decode_get_unsigned_bits(decode,u8g2->font_info.bits_per_char_height);
```

随后 `decode_glyph`（L775~L777）读三个有符号字段：

```c
  x = u8g2_font_decode_get_signed_bits(decode, u8g2->font_info.bits_per_char_x);
  y = u8g2_font_decode_get_signed_bits(decode, u8g2->font_info.bits_per_char_y);
  d = u8g2_font_decode_get_signed_bits(decode, u8g2->font_info.bits_per_delta_x);
```

顺序 = **宽、高、x 偏移、y 偏移、步进 dx**，全部是**非字节对齐的位流**，
位宽由字体头决定。之后才是 RLE 位流（a 个 0、b 个 1、1 位继续标志）。

## 4. 公共 API 契约

| API | 行号 | 契约 |
|---|---|---|
| `u8g2_SetFont` | L1597 | 解析 23 字节头到 `font_info`；**同一字体指针短路返回**；不重置 Pos 模式 |
| `u8g2_SetFontMode` | L1135 | 0=实心（画背景）/ 1=透明（只画笔画） |
| `u8g2_SetFontDirection` | L1927 | 0~3，需 `U8G2_WITH_FONT_ROTATION` |
| `u8g2_SetFontPosBaseline/Bottom/Top/Center` | L1522/1539/1560/1583 | 改 `font_calc_vref` 回调 |
| `u8g2_DrawGlyph` | L1149 | 画单字形，先叠加 vref 修正 |
| `u8g2_DrawStr` | L1275 | ASCII（ISO-8859-1）字符串 |
| `u8g2_DrawUTF8` | L1307 | UTF-8 字符串，需 `U8G2_WITH_UNICODE` |
| `u8g2_GetStrWidth` | L1887 | 像素宽度（逐字符累计 dx） |
| `u8g2_GetUTF8Width` | L1914 | UTF-8 版测宽 |
| `u8g2_IsGlyph` | L1090 | 只寻址不解码，判断字符是否存在 |
| `u8g2_GetGlyphWidth` | L1106 | 返回 dx，并**副作用**写 `glyph_x_offset` / `glyph_width` |
| `u8g2_IsAllValidUTF8` | L1644 | 全串覆盖预检，首个缺失即返回 0 |

### 4.1 画字的前置条件

```c
u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);   /* 必须先设字体 */
u8g2_DrawStr(&u8g2, 0, 10, "Hello");
```

`u8g2_SetupBuffer` 把 `u8g2->font` 置 NULL（`u8g2_setup.c:101`），
**未设字体直接画字 = 解引用空指针**。这是 u8g2 新手第一大 crash 源。

### 4.2 参考点语义

`y` 的含义由 `font_calc_vref` 决定，默认是**基线**（`u8g2_SetFontPosBaseline`，
在 `SetupBuffer` 末尾被调用，`u8g2_setup.c:128`）。四档：

| 模式 | `y` 指向 | vref 回调 |
|---|---|---|
| Baseline | 基线（字母底部） | `u8g2_font_calc_vref_font` |
| Bottom | 下探最低点 | `..._bottom` |
| Top | 上高最高点 | `..._top` |
| Center | 上高与下探的中点 | `..._center` |

### 4.3 缺失字形的处理

`u8g2_font_draw_glyph`（L1051）找不到字形时返回 0，**不报错、不画占位符**。
字符串绘制会静默跳过该字符继续排——中文缺字形时表现为"字消失、间距还在"。

## 5. 字体选型与 Flash 账本

| 字体 | 大约字节数 | 覆盖 | 用途 |
|---|---|---|---|
| `u8g2_font_4x6_tf` | ~600 | ASCII | 极小屏 |
| `u8g2_font_6x10_tf` | ~900 | ASCII | 通用文本 |
| `u8g2_font_7x14B_tf` | ~1400 | ASCII 粗体 | 标题 |
| `u8g2_font_10x20_tf` | ~2000 | ASCII | 大字号 |
| `u8g2_font_wqy12_t_gb2312` | ~100 KB | GB2312 常用字 | 中文（需外挂） |
| `u8g2_font_unifont_t_symbols` | ~30 KB | Unicode 符号 | 图标 |

**字体的代价在 Flash 不在 RAM**：`font_info` 只缓存 23 字节，`font_decode` 约
12~16 字节，无论字体多大，RAM 开销固定。选型只看 Flash 预算。

自定义字体流程（`tools/font/`）：

```text
BDF/OTF → bdfconv → C 数组（含 23 字节头）→ 加 extern 声明 → u8g2_SetFont
```

`bdfconv` 的参数决定 `bits_per_*` 与是否含 unicode 段；**改参数必须重新生成整个
数组**，不能只替换部分字形。

## 6. 与其他模块的关系

| 模块 | 关系 |
|---|---|
| `u8g2.h` 的 `font_info` / `font_decode`（L265/L308） | 本文件的运行时状态 |
| `u8x8utf8.c` 的 `u8x8_ascii_next` / `utf8_next` | 字节流 → 码点 |
| `u8g2_hvline.c` 的 `u8g2_DrawHVLine` | 所有字形像素的最终出口 |
| `u8g2_intersection.c` | 字形级裁剪（`decode_glyph` 内的 `IsIntersection`） |
| `u8g2_kerning.c` | `DrawExtendedUTF8` 的字距来源 |
| `tools/font/bdfconv` | PC 端字体编译器 |

## 7. 已知坑（读码发现，未改代码）

1. **`u8g2_GetGlyphWidth` 有副作用**：更新 `glyph_x_offset` 与
   `font_decode.glyph_width`。测宽后再画字是安全的（画字会重算），
   但依赖这两个字段做布局时要清楚它们会被后续调用覆盖。
2. **ASCII 段线性查找**：`glyph_cnt` 大（如含 200 个 ASCII 字形）时，
   靠 `start_pos_upper_A/lower_a` 分段才不至于太慢；自建字体若无这两字段
   （置 0），每个字符都从头扫。
3. **`t` 模式字体只保证透明模式正确**：实心模式下背景像素可能与预期不符。
4. **`DrawHB` 的 delta 是 `int8_t`**，字形步进上限 127 像素，超宽字形会被截断。
5. **UTF-8 与 ISO-8859-1 是两套入口**：`DrawStr` 传中文会逐字节当 Latin-1 画，
   必须用 `DrawUTF8`。
6. **`U8G2_WITH_UNICODE` 关闭时，`start_pos_unicode` 字段不存在**，
   含 unicode 段的字体数组会多出 2 字节，解析偏移全错——宏与字体必须匹配。

## 8. 互链

- 逐函数渲染实现：[clib_u8g2_font.md](clib_u8g2_font.md)
- 字体结构体字段：[u8g2_h.md](u8g2_h.md)
- 绘制管线位置：[Docs/02-绘制管线精读.md](../../../references/graphics/u8g2/Docs/02-绘制管线精读.md)
- RLE 解码全流程：[Docs/10-字体引擎精读.md](../../../references/graphics/u8g2/Docs/10-字体引擎精读.md)
