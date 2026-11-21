# u8g2.h —— 总契约（结构体 + API 声明）

> 对应源文件：`csrc/u8g2.h`（共 4525 行）
> 注释分级：**A 级（结构体字段逐项 + API 契约注释）**，strip_equiv_check PASS
> 上游：单色屏图形库 u8g2（olikraus）
>
> 姊妹条目：[u8g2_buffer.md](u8g2_buffer.md) | [u8g2_setup.md](u8g2_setup.md) |
> [u8g2_d_memory.md](u8g2_d_memory.md) | [u8g2_d_setup.md](u8g2_d_setup.md) |
> [u8g2_font.md](u8g2_font.md)
> 深度文章：[Docs/00-总览与架构.md](../../../references/graphics/u8g2/Docs/00-总览与架构.md)

---

## 1. 一句话定位

`u8g2.h` 是全库唯一的公共头文件：它定义**图形层上下文 `u8g2_t`**、**旋转回调组
`u8g2_cb_t`**、**字体头/解码器结构**，并声明全部 API。文件 4525 行里约 2/3 是
生成代码（1104 个 Setup 声明 + 400 多个字体 extern 数组），人工需要精读的只有
L226~L520 的结构体与宏区、L1900~L1930 的 buffer API、L1985~L2230 的绘图 API。

## 2. 文件分区地图

| 行区间 | 内容 | 读法 |
|---|---|---|
| L1~L17 | 中文导读块（本次注释新增） | 速览 |
| L88~L94 | `U8G2_16BIT` 强制规则 | 必读 |
| L226~L234 | `u8g2_uint_t` 二选一 | 必读 |
| L240~L262 | 回调 typedef（update/draw/l90/hvline/kerning） | 必读 |
| L265~L290 | `u8g2_font_info_t`：23 字节字体头 | 精读 |
| L308~L326 | `u8g2_font_decode_t`：RLE 解码现场 | 精读 |
| L330~L338 | `u8g2_kerning_t` | 略读 |
| L346~L351 | `u8g2_cb_struct`：旋转三件套 | 必读 |
| L365~L437 | `u8g2_struct`：四段式字段布局 | 精读 |
| L440~L452 | `u8g2_GetU8x8` / `SetAutoPageClear` 宏 | 必读 |
| L455~L500 | u8x8 wrapper 宏（InitDisplay/SetPowerSave…） | 速览 |
| L504~L515 | `U8G2_R0~R3` / `MIRROR` 宏 | 必读 |
| L520~L540 | setup / rotation / SendF 声明 | 速览 |
| L551~L1894 | **生成代码声明区**：d_memory（L551~L778）+ d_setup（L788~L1894） | 按需查 |
| L1900~L1935 | buffer API + 动态分配宏 | 必读 |
| L1945~L2230 | 绘图 / 字体 / 裁剪 API 声明 | 必读 |
| L2320~L4318 | 字体 extern 数组（400+ 个） | 按需查 |
| L4300~L4525 | u8g 兼容宏（`u8g_font_*` 别名） | 略读 |

## 3. `u8g2_t` 四段式字段布局（L365~L437）

```c
struct u8g2_struct
{
  u8x8_t u8x8;			// [1] 内嵌通信层：display/cad/byte/gpio 四回调 + display_info 尺寸表
  u8g2_draw_ll_hvline_cb ll_hvline;	/* low level hvline procedure */	// [2] 最底层像素写入：按控制器显存位序选 vertical_top_lsb 或 horizontal_right_lsb
  const u8g2_cb_t *cb;		/* callback drawprocedures, can be replaced for rotation */	// [2] 旋转/镜像三件套（R0..R3/mirror），SetDisplayRotation 只换这个指针
```

四段的完整字段表：

| 段 | 字段 | 行号 | 类型 | 含义 |
|---|---|---|---|---|
| [1] | `u8x8` | L367 | `u8x8_t` | 内嵌通信层，**必须是首成员** |
| [2] | `ll_hvline` | L368 | 函数指针 | 写位实现（竖排/横排） |
| [2] | `cb` | L369 | `const u8g2_cb_t *` | 旋转回调组 |
| [2] | `tile_buf_ptr` | L372 | `uint8_t *` | 帧缓冲首地址 |
| [2] | `tile_buf_height` | L373 | `uint8_t` | 缓冲 tile 行数（1/2/满屏） |
| [2] | `tile_curr_row` | L374 | `uint8_t` | 当前页起始 tile 行 |
| [3] | `pixel_buf_width` | L377 | `u8g2_uint_t` | 缓冲宽（像素） |
| [3] | `pixel_buf_height` | L378 | `u8g2_uint_t` | 缓冲高（像素） |
| [3] | `pixel_curr_row` | L379 | `u8g2_uint_t` | 当前页首行像素 y |
| [3] | `buf_y0` / `buf_y1` | L385~L386 | `u8g2_uint_t` | 本页缓冲纵向区间 |
| [3] | `width` / `height` | L389~L390 | `u8g2_uint_t` | 用户坐标系宽高（旋转后互换） |
| [3] | `user_x0~user_y1` | L395~L398 | `u8g2_uint_t` | 用户绘制窗口 = 页 ∩ clip |
| [3] | `clip_x0~clip_y1` | L402~L405 | `u8g2_uint_t` | 用户自设裁剪窗口（可编译掉） |
| [3] | `is_page_clip_window_intersection` | L420 | `uint8_t` | 0 时整页绘制短路 |
| [4] | `font` | L410 | `const uint8_t *` | 当前字体数组 |
| [4] | `font_calc_vref` | L414 | 函数指针 | 文字参考点（基线/顶/底/中） |
| [4] | `font_decode` | L415 | `u8g2_font_decode_t` | 单字形渲染现场 |
| [4] | `font_info` | L416 | `u8g2_font_info_t` | 字体头缓存（23 B） |
| [4] | `font_height_mode` | L423 | `uint8_t` | TEXT/XTEXT/ALL |
| [4] | `font_ref_ascent/descent` | L424~L425 | `int8_t` | 参考上高/下探 |
| [4] | `glyph_x_offset` | L427 | `int8_t` | GetGlyphWidth 的副作用输出 |
| — | `bitmap_transparency` | L429 | `uint8_t` | 位图透明模式 |
| — | `draw_color` | L431 | `uint8_t` | 0 擦除 / 1 点亮 / 2 XOR |
| — | `is_auto_page_clear` | L435 | `uint8_t` | 分页循环自动清屏 |

### 为什么首成员必须是 `u8x8_t`

```c
#define u8g2_GetU8x8(u8g2) ((u8x8_t *)(u8g2))
```

（L440）C 标准保证结构体首成员的地址等于结构体地址，所以这个强制转换是**零开销**
且合法的。所有 u8x8 wrapper 宏（L460 起）都靠它把 `u8g2_t*` 当 `u8x8_t*` 用。
如果改动字段顺序，全库立刻崩溃——这是本文件最硬的一条隐式契约。

## 4. `u8g2_uint_t`：一个 typedef 决定屏宽上限

```c
#ifdef U8G2_16BIT
typedef uint16_t u8g2_uint_t;	/* for pixel position only */
```

```c
typedef uint8_t u8g2_uint_t;		/* for pixel position only */
```

（L229~L234）L91~L94 还强制"32 位环境必须开 16BIT"。影响：

| 模式 | 坐标上限 | 支持最大屏 | `u8g2_t` 省/费 |
|---|---|---|---|
| 8 位（AVR 默认） | 0~255 | 240x240 左右 | 省约 15 B |
| 16 位（ARM 默认） | 0~65535 | 任意 | 多约 15 B |

`u8g2_update_dimension_common` 在 8 位模式下把 `width` 封顶 240（`u8g2_setup.c:224`），
超过 240 像素的屏**必须**开 16BIT，否则右侧会被裁掉。

## 5. 字体三结构

| 结构 | 行号 | 大小 | 作用 |
|---|---|---|---|
| `u8g2_font_info_t` | L265 | 23 B（含 unicode） | 字体文件头，`u8g2_SetFont` 时解析一次 |
| `u8g2_font_decode_t` | L308 | ~12~16 B | 单个字形的 RLE 解码游标与局部坐标 |
| `u8g2_kerning_t` | L330 | 指针集 | 字距表（多数内置字体没有） |

`font_info` 的 23 字节是三段式布局（`glyph_cnt`/`bits_per_*` 等），
详见 [u8g2_font.md](u8g2_font.md)。

## 6. 关键宏一览

| 宏 | 行号 | 展开 | 用途 |
|---|---|---|---|
| `u8g2_GetU8x8` | L440 | `(u8x8_t *)(u8g2)` | 降级到通信层 |
| `u8g2_SetAutoPageClear` | L450 | 写 `is_auto_page_clear` | 关掉分页自动清屏 |
| `u8g2_SetupDisplay` | L460 | `u8x8_Setup` | 装四回调 |
| `u8g2_InitDisplay` | L466 | `u8x8_InitDisplay` | 发初始化序列 |
| `u8g2_SetPowerSave` | L468 | `u8x8_SetPowerSave` | 开关显示 |
| `u8g2_SetContrast` | L470 | `u8x8_SetContrast` | 对比度 |
| `U8G2_R0` | L504 | `(&u8g2_cb_r0)` | 不旋转 |
| `U8G2_R1/R2/R3` | L505~L507 | 对应 cb 常量 | 90/180/270 度 |
| `U8G2_MIRROR` | L508 | `(&u8g2_cb_mirror)` | 水平镜像 |
| `U8G2_MIRROR_VERTICAL` | L509 | `(&u8g2_cb_mirror_vertical)` | 垂直镜像 |
| `u8g2_SetBufferPtr` | L1927 | 写 `tile_buf_ptr` | 动态分配时绑定缓冲 |

## 7. API 分组（人工可读区）

| 组 | 声明起点 | 实现文件 |
|---|---|---|
| 装配 / 旋转 / 直发命令 | L533 / L536 / L539 | `u8g2_setup.c` |
| 缓冲 / 分页 / 导出 | L1909~L1965 | `u8g2_buffer.c` |
| 裁剪 / 像素 / 线段 | L1985~L1998 | `u8g2_hvline.c` |
| 位图 | L2007~L2016 | `u8g2_bitmap.c` |
| 圆 / 椭圆 / 弧 / 线 / 框 / 多边形 | L2035~L2110 | `u8g2_circle.c` 等 |
| 字体 | L2130~L2200 | `u8g2_font.c` |
| UI 组件（Message/SelectionList/InputValue） | L2205~L2230 | `u8g2_message.c` 等 |

## 8. 关键摘录：字体头结构（L265~L290）

```c
struct _u8g2_font_info_t
{
  /* offset 0 */
  uint8_t glyph_cnt;			// 字形总数（0xFF = 超过 255，靠 start_pos 兜底）
  uint8_t bbx_mode;			// 高度参考模式：0=text 1=xtext 2=all（决定 SetFontRefHeight 语义）
  uint8_t bits_per_0;			// RLE：连续 0 的游程所用位宽
  uint8_t bits_per_1;			// RLE：连续 1 的游程所用位宽
```

`bits_per_0` / `bits_per_1` 是 RLE 解码的核心参数：**每个字体可以有不同的游程位宽**，
解码器必须从文件头读，不能写死。

## 9. 已知坑（读码发现，未改代码）

1. **`u8g2_t` 字段顺序不可改**：`u8g2_GetU8x8` 依赖首成员布局。
2. **`font` 未初始化就画字 = 崩溃**。`u8g2_SetupBuffer` 把 `font` 置 NULL
   （`u8g2_setup.c:101`），必须自己 `u8g2_SetFont`。
3. **8 位坐标模式的 240 像素上限**是隐式的，没有任何编译期检查——宽屏在 AVR 上
   会静默截断。
4. **`U8G2_USE_DYNAMIC_ALLOC` 与 `u8g2_SetBufferPtr` 成对出现**，只开宏不设指针
   会在第一次 `ClearBuffer` 写空指针（`u8g2.h` L101 的注释警告了这一点）。
5. **字体 extern 数组有 400+ 条**，全部编译进去会占满 flash；只引用用到的名字，
   链接器会丢弃其余。

## 10. 互链

- 缓冲实现：[u8g2_buffer.md](u8g2_buffer.md)
- 装配实现：[u8g2_setup.md](u8g2_setup.md)
- 缓冲分配生成表：[u8g2_d_memory.md](u8g2_d_memory.md)
- 屏幕装配生成表：[u8g2_d_setup.md](u8g2_d_setup.md)
- 字体格式：[u8g2_font.md](u8g2_font.md)
- 架构总览：[Docs/00-总览与架构.md](../../../references/graphics/u8g2/Docs/00-总览与架构.md)
