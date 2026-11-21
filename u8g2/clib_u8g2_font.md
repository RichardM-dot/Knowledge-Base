# u8g2_font.c —— 字体渲染引擎

> 对应源文件：`csrc/u8g2_font.c`（原 1630 行，注释后 2001 行；行号以注释后版本为准）
> 注释分级：**A 级（逐函数中文深注）**，strip_equiv_check PASS（371 行纯注释新增，0 删除）
> 上游：单色屏图形库 u8g2（olikraus），本文件是 C 端字体解码的唯一实现
>
> 姊妹条目：[clib_u8g2_circle.md](clib_u8g2_circle.md) | [clib_u8g2_line.md](clib_u8g2_line.md) | [clib_u8g2_bitmap.md](clib_u8g2_bitmap.md) | [clib_u8g2_box.md](clib_u8g2_box.md)
> 深度文章：[Docs/10-字体引擎精读.md](../../../references/graphics/u8g2/Docs/10-字体引擎精读.md)

---

## 1. 一句话定位

把"bdfconv 编译出的 RLE 压缩字形数组"解码成水平线段画到缓冲区：**寻址（找字形）
→ 位流解码（还原像素）→ 落屏（DrawHVLine）**，外加字符串排版（步进、参考点、
kerning、宽度测量）。无 malloc、无浮点，Flash 字体 + 23 字节 RAM 缓存即可工作。

## 2. 逐函数速查表

### L1/L2：字节访问与字体头（:117~243）

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `u8g2_font_get_byte` | 130 | 读 1 字节 | 经 `u8x8_pgm_read`，AVR 走 LPM |
| `u8g2_font_get_word` | 144 | 读 16 位字 | **大端**手工拼装，与 MCU 字节序无关 |
| `u8g2_read_font_info` | 165 | 解析 23 字节头 → `font_info` | SetFont 时调用一次 |
| `u8g2_GetFontSize` | 214 | 计算字体总字节数 | 顺链表走，仅调试/wiki 用 |

### L3：位流解码（:319~930）

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `u8g2_font_decode_get_unsigned_bits` | 319 | 无对齐取 cnt 位 | LSB-first，跨字节拼接，两次 pgm 读 |
| `u8g2_font_decode_get_signed_bits` | 375 | 偏移二进制还原有符号数 | 值 = 原码 − 2^(cnt−1) |
| `u8g2_add_vector_y/x` | 402/420 | 旋转坐标映射 | 纯加减实现 4 方向，需 U8G2_WITH_FONT_ROTATION |
| `u8g2_font_decode_len` | 509 | 画一段游程 | rem=glyph_width−lx 折行；透明/实心分叉点 |
| `u8g2_font_2x_decode_len` | 615 | 2x 游程 | 每段画 2 条线（2x2 大像素），无旋转 |
| `u8g2_font_setup_decode` | 717 | 初始化解码状态机 | 解 width/height，缓存 fg/bg 色 |
| `u8g2_font_decode_glyph` | 764 | 解码并绘制一个字形 | 头→剪裁→RLE 主循环，返回 dx |
| `u8g2_font_2x_decode_glyph` | 873 | 2x 版 | 剪裁框按 2w×2h |
| `u8g2_font_get_glyph_data` | 952 | glyph 寻址 | 'A'/'a' 分段链 + unicode 4B/项索引表 |
| `u8g2_font_draw_glyph` | 1051 | 寻址+解码入口 | NULL → 返回 0（静默跳过） |
| `u8g2_font_2x_draw_glyph` | 1069 | 2x 入口 | |

### L4：公共 API（:1090~2001）

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `u8g2_IsGlyph` | 1090 | 查字形是否存在 | 只寻址不解码 |
| `u8g2_GetGlyphWidth` | 1106 | 查步进 dx | **副作用**更新 glyph_x_offset/glyph_width |
| `u8g2_SetFontMode` | 1135 | 透明/实心模式 | 只写 1 个标志，分叉在 decode_len |
| `u8g2_DrawGlyph` | 1149 | 画单字形 | 先叠加 font_calc_vref 参考点 |
| `u8g2_DrawGlyphX2` | 1176 | 2x 单字形 | vref 也乘 2 |
| `u8g2_draw_string` | 1192 | 字符串主循环（static） | next_cb 取码点，0xffff 结束/0xfffe 跳过 |
| `u8g2_draw_string_2x` | 1247 | 2x 字符串 | 无旋转 |
| `u8g2_DrawStr / DrawStrX2` | 1275/1284 | ASCII 入口 | 挂 ascii_next 回调 |
| `u8g2_DrawUTF8 / DrawUTF8X2` | 1307/1316 | UTF-8 入口 | 挂 utf8_next 回调 |
| `u8g2_DrawExtendedUTF8` | 1333 | kerning + RTL | to_left=1 从右往左排 |
| `u8g2_DrawExtUTF8` | 1387 | 扁平字距表版 | [左码][右码][字距] 三元组，0 结尾 |
| `u8g2_UpdateRefHeight` | 1455 | 重算参考上/下沿 | TEXT/XTEXT/ALL 三档 |
| `u8g2_SetFontRefHeightText/ExtendedText/All` | 1483/1492/1501 | 三档模式 | 立即触发 UpdateRefHeight |
| `u8g2_font_calc_vref_font/bottom/top/center` | 1514/1531/1548/1570 | vref 回调 | 函数指针策略 |
| `u8g2_SetFontPosBaseline/Bottom/Top/Center` | 1522/1539/1560/1583 | 参考点模式 | y=基线/底/顶/居中 |
| `u8g2_SetFont` | 1597 | 换字体 | 同字体短路；不重置 Pos 模式（issue 195） |
| `u8g2_is_all_valid / u8g2_IsAllValidUTF8` | 1621/1644 | 覆盖范围预检 | 首个缺失即返回 0 |
| `u8g2_string_width` | 1662 | 串宽公共底层 | Σdx + 末字符三步修正 |
| `u8g2_GetXOffsetGlyph/UTF8` | 1724/1734 | 查首字符 X 偏移 | 复用 GetGlyphWidth 副作用 |
| `u8g2_GetGlyphHorizontalProperties` | 1757 | 一次取 w/ox/dx | u8g 兼容层用 |
| `u8g2_GetStrX` | 1775 | 首字符 X 偏移 | 只看 *s，不解 UTF-8 |
| `u8g2_GetStrWidth / GetUTF8Width` | 1887/1914 | 测宽 | 居中排版必备 |
| `u8g2_SetFontDirection` | 1927 | 书写方向 0~3 | 需 U8G2_WITH_FONT_ROTATION |
| `u8g2_DrawHB` | 1981 | HarfBuzz 回放 | 4B/行表驱动，需透明模式 |

（另有 `u8g2_calculate_exact_string_width` 位于 `#ifdef OBSOLETE` 内，不参与编译。）

## 3. 关键摘录

### 3.1 字体头尺寸（:51）

```c
#define U8G2_FONT_DATA_STRUCT_SIZE 23
```

23 字节字段布局见文件头注释（:68~119），或 KB 上方 Docs 链接中的表格。

### 3.2 无对齐取位的跨字节拼接（:333~344）

```c
  // 本次取值越过字节边界：拼接下一字节的低位补齐高位
  if ( bit_pos_plus_cnt >= 8 )
  {
    uint8_t s = 8;
    s -= bit_pos;
    f->decode_ptr++;
    val |= u8x8_pgm_read( f->decode_ptr ) << (s);
    bit_pos_plus_cnt -= 8;
  }
  val &= (1U<<cnt)-1;
```

### 3.3 RLE 主循环（:845~859）

```c
    for(;;)
    {
      a = u8g2_font_decode_get_unsigned_bits(decode, u8g2->font_info.bits_per_0);
      b = u8g2_font_decode_get_unsigned_bits(decode, u8g2->font_info.bits_per_1);
      do
      {
	u8g2_font_decode_len(u8g2, a, 0);
	u8g2_font_decode_len(u8g2, b, 1);
      } while( u8g2_font_decode_get_unsigned_bits(decode, 1) != 0 );

      if ( decode->y >= h )
	break;
    }
```

继续位=1 表示**同一对 (a,b) 再画一遍**（对重复行的压缩），不是读新值。

### 3.4 串宽的末字符修正（:1700~1712）

```c
    w -= dx;
    w += u8g2->font_decode.glyph_width;  /* the real pixel width of the glyph, sideeffect of GetGlyphWidth */
    /* issue #46: we have to add the x offset also */
    w += u8g2->glyph_x_offset;	/* this value is set as a side effect of u8g2_GetGlyphWidth() */
```

前置守卫：末字形 glyph_width==0（空格）不修正（issue #16）；
首字符 x_offset>0 时补回（issue #1561）。

## 4. 与其他模块的关系

| 模块 | 关系 |
|---|---|
| `u8g2.h` 的 `u8g2_font_info_t/decode_t`（:231~290） | 本文件的运行时数据结构 |
| `u8x8utf8.c` 的 `u8x8_ascii_next/utf8_next` | next_cb 状态机，字节→码点 |
| `u8g2_hvline.c` 的 `u8g2_DrawHVLine` | 所有像素的最终出口 |
| `u8g2_intersection.c` | decode_glyph 的字形级剪裁 |
| `u8g2_kerning.c` | DrawExtendedUTF8 的字距来源 |
| `tools/font/bdfconv` | 生成字体数组的 PC 端编译器 |

## 5. 已知坑（读码发现，未改代码）

1. `u8g2_GetGlyphWidth` 的副作用设计（更新 3 个状态）——调用即改全局，
   复用前要清楚这点；
2. 缺字形静默返回 dx=0，无任何错误上报；
3. `t` 模式字体官方只保证透明模式正确，实心会花背景；
4. `DrawHB` 的 delta 是 int8_t，字形步进上限 127 像素。
