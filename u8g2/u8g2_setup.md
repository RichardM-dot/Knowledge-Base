# u8g2_setup.c —— 中央装配与旋转回调

> 对应源文件：`csrc/u8g2_setup.c`（共 602 行）
> 注释分级：**A 级（逐函数中文深注，22 个函数 + 6 张回调表全覆盖）**，strip_equiv_check PASS
> 上游：单色屏图形库 u8g2（olikraus）
>
> 姊妹条目：[u8g2_h.md](u8g2_h.md) | [u8g2_buffer.md](u8g2_buffer.md) |
> [u8g2_d_setup.md](u8g2_d_setup.md)
> 深度文章：[Docs/02-绘制管线精读.md](../../../references/graphics/u8g2/Docs/02-绘制管线精读.md)

---

## 1. 一句话定位

所有 `u8g2_Setup_xxx_1/2/f()` 的**公共终点**：1104 个生成装配函数只负责"选驱动 +
选缓冲"，真正的字段装配、窗口计算、旋转回调全部落在本文件的
`u8g2_SetupBuffer`（L99）与 6 张 `u8g2_cb_t` 常量表（L577~L583）。

一句话概括本文件：**它把"一块裸内存 + 一组回调"变成一台可绘制的虚拟显示器。**

## 2. 逐函数速查表

### 2.1 装配与配置（L58~L168）

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `u8g2_SetMaxClipWindow` | L58 | 裁剪窗口恢复"无限大" | `~0` 作哨兵，末尾调 `update_page_win` |
| `u8g2_SetClipWindow` | L75 | 设置用户裁剪窗口 | 只存值，交集推迟到 `update_page_win` |
| `u8g2_SetupBuffer` | L99 | **中央初始化** | 装 8 个字段 + 算初始窗口 |
| `u8g2_SetDisplayRotation` | L146 | 运行期换旋转 | 只换 `cb` + 重算窗口，缓冲不搬 |
| `u8g2_SendF` | L162 | 直发控制器命令 | 绕过缓冲，走 `u8x8_cad_vsendf` |

### 2.2 尺寸与窗口（L188~L393）

| 函数 | 行号 | 职责 | 关键点 |
|---|---|---|---|
| `u8g2_update_dimension_common` | L188 | 旋转无关的尺寸计算 | 算 `pixel_buf_*` / `buf_y0/y1` / `width/height` |
| `u8g2_apply_clip_window` | L244 | user ∩ clip | 交叠标志决定整页是否绘制 |
| `u8g2_update_dimension_r0` | L274 | R0 尺寸 | 直接复用 common |
| `u8g2_update_page_win_r0` | L284 | R0 窗口 | x 全宽，y = buf 区间 |
| `u8g2_update_dimension_r1` | L302 | R1 尺寸 | **宽高互换** |
| `u8g2_update_page_win_r1` | L316 | R1 窗口 | 页边界映射到 x 轴 |
| `u8g2_update_dimension_r2` | L332 | R2 尺寸 | 同 R0 |
| `u8g2_update_page_win_r2` | L343 | R2 窗口 | y 轴镜像，带下溢保护 |
| `u8g2_update_dimension_r3` | L364 | R3 尺寸 | 宽高互换（同 R1） |
| `u8g2_update_page_win_r3` | L378 | R3 窗口 | x 轴镜像 |

### 2.3 绘制变形（L407~L566）

| 函数 | 行号 | 变换 | 关键点 |
|---|---|---|---|
| `u8g2_draw_l90_r0` | L407 | 恒等 | 一行透传 |
| `u8g2_draw_l90_mirrorr_r0` | L420 | 水平镜像 | 水平线减 `len`，垂直线减 1 |
| `u8g2_draw_mirror_vertical_r0` | L440 | 垂直镜像 | 垂直线减 `len`，水平线减 1 |
| `u8g2_draw_l90_r1` | L463 | 顺时针 90° | `dir++`，回绕时补 `len` |
| `u8g2_draw_l90_r2` | L493 | 180° | 双轴翻转 + 方向补偿 |
| `u8g2_draw_l90_r3` | L542 | 270° | 与 R1 对称 |

### 2.4 回调表与空设备（L577~L600）

| 常量 | 行号 | 内容 |
|---|---|---|
| `u8g2_cb_r0` | L577 | common/R0 窗口/恒等绘制 |
| `u8g2_cb_r1` | L578 | 宽高互换 + R1 窗口 + R1 绘制 |
| `u8g2_cb_r2` | L579 | common + R2 窗口 + R2 绘制 |
| `u8g2_cb_r3` | L580 | 宽高互换 + R3 窗口 + R3 绘制 |
| `u8g2_cb_mirror` | L582 | R0 窗口 + 水平镜像绘制 |
| `u8g2_cb_mirror_vertical` | L583 | R0 窗口 + 垂直镜像绘制 |
| `u8g2_Setup_null` | L595 | 空设备：8 字节缓冲 + 空回调 |

## 3. 关键摘录

### 3.1 `u8g2_SetupBuffer` 的字段装配（L99~L133）

```c
void u8g2_SetupBuffer(u8g2_t *u8g2, uint8_t *buf, uint8_t tile_buf_height, u8g2_draw_ll_hvline_cb ll_hvline_cb, const u8g2_cb_t *u8g2_cb)
{
  u8g2->font = NULL;			// 字体必须等用户 u8g2_SetFont() 再给，画字前未设置是常见 crash 点
  //u8g2->kerning = NULL;
  //u8g2->get_kerning_cb = u8g2_GetNullKerning;

  //u8g2->ll_hvline = u8g2_ll_hvline_vertical_top_lsb;
  u8g2->ll_hvline = ll_hvline_cb;	// 由 setup 宏按控制器显存位序选：竖排(top_lsb) 或 横排(right_lsb)

  u8g2->tile_buf_ptr = buf;		// 帧缓冲首地址（来自 u8g2_d_memory.c 的静态数组）
  u8g2->tile_buf_height = tile_buf_height;	// 缓冲 tile 行数：决定 _1/_2/_f 三种内存模式的本质

  u8g2->tile_curr_row = 0;		// 页指针归零（全缓冲后一直是 0）

  u8g2->font_decode.is_transparent = 0; /* issue 443 */	// 字体默认"实心"模式：背景像素也要画
  u8g2->bitmap_transparency = 0;	// 位图同理，默认把 0 位当黑画出来

  u8g2->font_height_mode = 0; /* issue 2046 */	// 字高参考模式默认 TEXT
  u8g2->draw_color = 1;			// 默认画"白"(点亮像素)，0 为擦除，可被 SetDrawColor 改
  u8g2->is_auto_page_clear = 1;		// FirstPage/NextPage 默认自动清缓冲

  u8g2->cb = u8g2_cb;			// 装 R0..R3/mirror 旋转回调组
  u8g2->cb->update_dimension(u8g2);	// 由缓冲高度+页行号推 pixel/buf 尺寸
```

**装配顺序不能换**：`update_dimension` 依赖 `tile_buf_height` 与 `tile_curr_row`，
`update_page_win` 又依赖 `update_dimension` 算出的 `buf_y0/y1`。

### 3.2 8 位坐标模式的宽度封顶（L220~L228）

```c
#ifdef U8G2_16BIT
  u8g2->width = display_info->pixel_width;	// 16 位模式：直接用驱动表里的真实像素宽高
  u8g2->height = display_info->pixel_height;	// （允许 128x32 这类"tile 高 4 行、像素高 32"的精确值）
#else
  u8g2->width = 240;			// 8 位模式给宽度设上限 240（坐标只有 0..255，留出余量）
  if ( display_info->pixel_width <= 240 )
    u8g2->width = display_info->pixel_width;
  u8g2->height = display_info->pixel_height;
#endif
```

240 这个数字的来历：`u8g2_uint_t` 是 `uint8_t`，坐标 0~255，留 16 个余量给
"右开区间 + 裁剪位移"，所以封顶 240。**超过 240 的屏在 AVR 上必须开
`U8G2_16BIT`**，否则右侧被裁。

### 3.3 裁剪交集（L244~L264）

```c
static void u8g2_apply_clip_window(u8g2_t *u8g2)
{
  /* check aganst the current user_??? window */
  if ( u8g2_IsIntersection(u8g2, u8g2->clip_x0, u8g2->clip_y0, u8g2->clip_x1, u8g2->clip_y1) == 0 )
  {
    u8g2->is_page_clip_window_intersection = 0;	// clip 窗口与当前页完全不相交：本页不用画
  }
  else
  {
    u8g2->is_page_clip_window_intersection = 1;	// 有交叠，允许绘制（但只画交集区域）

    if ( u8g2->user_x0 < u8g2->clip_x0 )
      u8g2->user_x0 = u8g2->clip_x0;		// 四组收缩：user 窗口被 clip 窗口裁小
```

**标志置 0 的分支不做任何收缩**：窗口已经无关紧要，所有绘制会被
`u8g2_DrawHVLine` 第一行短路掉。

### 3.4 旋转回调表（L577~L583）

```c
const u8g2_cb_t u8g2_cb_r0 = { u8g2_update_dimension_r0, u8g2_update_page_win_r0, u8g2_draw_l90_r0 };
const u8g2_cb_t u8g2_cb_r1 = { u8g2_update_dimension_r1, u8g2_update_page_win_r1, u8g2_draw_l90_r1 };
const u8g2_cb_t u8g2_cb_r2 = { u8g2_update_dimension_r2, u8g2_update_page_win_r2, u8g2_draw_l90_r2 };
const u8g2_cb_t u8g2_cb_r3 = { u8g2_update_dimension_r3, u8g2_update_page_win_r3, u8g2_draw_l90_r3 };

const u8g2_cb_t u8g2_cb_mirror = { u8g2_update_dimension_r0, u8g2_update_page_win_r0, u8g2_draw_l90_mirrorr_r0 };
const u8g2_cb_t u8g2_cb_mirror_vertical = { u8g2_update_dimension_r0, u8g2_update_page_win_r0, u8g2_draw_mirror_vertical_r0 };
```

6 张表 = 6 种显示方向，**全部编译期常量，运行时只是一次指针赋值**。这是 u8g2
"旋转零成本"的全部秘密。

### 3.5 空设备（L595~L600）

```c
void u8g2_Setup_null(u8g2_t *u8g2, const u8g2_cb_t *rotation, u8x8_msg_cb byte_cb, u8x8_msg_cb gpio_and_delay_cb)
{
  static uint8_t buf[8];	// 仅 1 个 tile（8 字节）——最小可能的"屏"
  u8g2_SetupDisplay(u8g2, u8x8_d_null_cb, u8x8_cad_empty, byte_cb, gpio_and_delay_cb);
  u8g2_SetupBuffer(u8g2, buf, 1, u8g2_ll_hvline_vertical_top_lsb, rotation);
}
```

用途：CI 里跑图形逻辑、无屏环境做单元测试。`buf[8]` 意味着任何绘制都会写越界
——所以必须配 `u8x8_d_null_cb`（它吞掉所有消息，不会真的读缓冲）。
**不要用 `Setup_null` 做"预览"**：它不产生任何可见输出。

## 4. R1/R2/R3 的坐标变换速查

以 `width=128, height=64` 为例（用户坐标 → 物理屏幕坐标）：

| 旋转 | 变换 | 宽高关系 |
|---|---|---|
| R0 | `(x, y)` 不变 | 128x64 |
| R1 | `(height-1-y, x)` | 用户看到 64x128 |
| R2 | `(width-x, height-y)`（再按 dir 补 1） | 128x64 |
| R3 | `(y, width-1-x)` | 用户看到 64x128 |
| MIRROR | `(width-x-len, y)` | 128x64 |
| MIRROR_VERTICAL | `(x, height-y-len)` | 128x64 |

**宽高互换发生在 `update_dimension_r1/r3`（L302/L364），不在 `draw_l90` 里**。
两处必须成对修改，这是自研旋转回调最容易漏的点。

## 5. 设计要点

1. **装配与选型分离**：`u8g2_d_setup.c` 只做"选谁"，本文件做"怎么装"。
2. **窗口是派生状态**：`user_x0..y1` 从不被用户直接写，只由
   `update_page_win` 从 `buf_y0/y1` 与 `clip_*` 算出。
3. **`~0` 哨兵**：`SetMaxClipWindow` 用类型最大值表示"无上界"，比写死屏宽更通用
   （换屏不用改代码）。
4. **`SendF` 是逃生舱**：控制器私有命令不污染 API，直接用 `u8x8_cad_vsendf` 的
   格式串发。

## 6. 已知坑（读码发现，未改代码）

1. **`u8g2_SetupBuffer` 之后 `font == NULL`**，画字会崩。所有示例都在 `Setup`
   之后立刻 `SetFont`，不是风格问题而是必需。
2. **`SetDisplayRotation` 不重画**。切旋转后旧缓冲内容在新坐标系下是错的，
   必须 `ClearBuffer` + 重画。
3. **R2 的 `user_y0` 有下溢保护**（L351-L352），R3 的 `user_x0` 同理（L383-L384）；
   自己写窗口计算时若省掉这两个 `if`，非 8 倍数高度的屏会得到巨大无符号值。
4. **`SetClipWindow` 的 x1/y1 是开区间**，传 `(0,0,128,64)` 才是整屏，
   传 `(0,0,127,63)` 会少画最后一行一列。
5. **`u8g2_Setup_null` 的缓冲只有 8 字节**，且 `tile_buf_height=1`；若改成 `_f`
   语义会立刻越界。

## 7. 互链

- 字段定义：[u8g2_h.md](u8g2_h.md)
- 谁调用 `SetupBuffer`：[u8g2_d_setup.md](u8g2_d_setup.md)
- 缓冲分配：[u8g2_d_memory.md](u8g2_d_memory.md)
- 窗口如何参与裁剪：[Docs/02-绘制管线精读.md](../../../references/graphics/u8g2/Docs/02-绘制管线精读.md)
