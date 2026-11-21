# u8g2_buffer.c —— 帧缓冲管理

> 对应源文件：`csrc/u8g2_buffer.c`（共 305 行）
> 注释分级：**A 级（逐函数中文深注，13 个函数全覆盖）**，strip_equiv_check PASS
> 上游：单色屏图形库 u8g2（olikraus）
>
> 姊妹条目：[u8g2_h.md](u8g2_h.md) | [u8g2_setup.md](u8g2_setup.md) |
> [u8g2_d_memory.md](u8g2_d_memory.md)
> 深度文章：[Docs/01-内存与缓冲模型.md](../../../references/graphics/u8g2/Docs/01-内存与缓冲模型.md)

---

## 1. 一句话定位

本文件是"RAM 帧缓冲"的唯一管理者：**清屏、发送、分页循环、局部更新、导出**。
绘制函数（`u8g2_DrawXxx`）只改 `tile_buf_ptr` 指向的内存，本文件负责把这块内存
搬到控制器显存。全部 13 个函数，没有一个会分配内存——缓冲由
`u8g2_d_memory.c` 的静态数组提供。

## 2. 逐函数速查表

| 函数 | 行号 | 类型 | 职责 | 关键点 |
|---|---|---|---|---|
| `u8g2_ClearBuffer` | L56 | 公开 | 整块缓冲清零 | 长度 = `tile_width × tile_buf_height × 8` |
| `u8g2_send_tile_row` | L76 | static | 发一个 tile 行 | 偏移 = `src_row × tile_width × 8` |
| `u8g2_send_buffer` | L105 | static | 逐 tile 行搬运 | `do-while`，两侧任一到底即停 |
| `u8g2_SendBuffer` | L134 | 公开 | 发送 + 刷新 | 电子纸靠末尾 REFRESH 成像 |
| `u8g2_SetBufferCurrTileRow` | L149 | 公开 | 设页起点 | 联动 `update_dimension/page_win` |
| `u8g2_FirstPage` | L164 | 公开 | 分页循环头 | 可选清屏 + 页指针归零 |
| `u8g2_NextPage` | L180 | 公开 | 分页循环推进 | 返回 1=继续 / 0=结束 |
| `u8g2_UpdateDisplayArea` | L217 | 公开 | 局部刷新 | **仅全缓冲**，页模式静默失败 |
| `u8g2_UpdateDisplay` | L250 | 公开 | 整帧发送（无刷新） | 电子纸"只改 RAM" |
| `u8g2_WriteBufferPBM` | L265 | 调试 | 导出 PBM（竖排位序） | `get_pixel_1` |
| `u8g2_WriteBufferXBM` | L275 | 调试 | 导出 XBM（竖排位序） | 可用于截图比对 |
| `u8g2_WriteBufferPBM2` | L290 | 调试 | 导出 PBM（横排位序） | `get_pixel_2` |
| `u8g2_WriteBufferXBM2` | L300 | 调试 | 导出 XBM（横排位序） | ST7920/T6963 类 |

## 3. 关键摘录

### 3.1 缓冲长度公式（L58~L62）

```c
  size_t cnt;
  cnt = u8g2_GetU8x8(u8g2)->display_info->tile_width;	// 一"tile 行"的字节数 = 宽像素/8（128 宽 = 16）
  cnt *= u8g2->tile_buf_height;				// 缓冲的 tile 行数：页模式 1/2 行，全缓冲 = 满屏行数
  cnt *= 8;						// 每 tile 竖跨 8 像素行，即 8 字节
  memset(u8g2->tile_buf_ptr, 0, cnt);			// 典型 128x64 全缓冲：16*8*8 = 1024 字节
```

`cnt` 用 `size_t` 而不是 `uint8_t`：16×8×8 = 1024 已经超过 8 位上限，
用窄类型会静默截断成 0，清屏直接失效。

### 3.2 发送循环的终止条件（L112~L122）

```c
  src_row = 0;
  src_max = u8g2->tile_buf_height;			// 缓冲高度（tile 行）
  dest_row = u8g2->tile_curr_row;			// 当前页起始行（FirstPage/NextPage 推进）
  dest_max = u8g2_GetU8x8(u8g2)->display_info->tile_height;	// 显示 RAM 总 tile 行数

  do
  {
    u8g2_send_tile_row(u8g2, src_row, dest_row);	// 缓冲第 src_row 行 → 显示第 dest_row 行
    src_row++;
    dest_row++;
  } while( src_row < src_max && dest_row < dest_max );	// 任一侧走完就停，天然处理非 8 倍数高度屏
```

**`dest_max` 这个条件是页模式正确性的关键**：128x64 屏用 `_2` 缓冲时，
`src_max=2`，最后一页 `dest_row` 从 6 开始，6→7→8（= dest_max）停止，
正好发两行。如果只判 `src_row < src_max`，当屏高不是缓冲高度整数倍时会写到
显存外。

### 3.3 `NextPage` 的顺序（L183~L197）

```c
  u8g2_send_buffer(u8g2);		// 当前页内容 → 显示 RAM（从 tile_curr_row 起共 tile_buf_height 行）
  row = u8g2->tile_curr_row;
  row += u8g2->tile_buf_height;		// 页指针前移一个缓冲高度
  if ( row >= u8g2_GetU8x8(u8g2)->display_info->tile_height )
  {
    u8x8_RefreshDisplay( u8g2_GetU8x8(u8g2) );	// 所有页发完：刷新（电子纸成像；其他屏多为空操作）
    return 0;				// 绘制循环结束
  }
  if ( u8g2->is_auto_page_clear )
  {
    u8g2_ClearBuffer(u8g2);		// 下一页从干净的缓冲开始
  }
  u8g2_SetBufferCurrTileRow(u8g2, row);	// 翻页并重算裁剪窗口——下一轮画图落在新的页窗口内
  return 1;				// 还有页没画，继续循环
```

顺序 = **发送 → 判结束 → 清屏 → 翻页**。返回 0 的分支**不清屏**（已经结束了）。

### 3.4 局部刷新的守卫（L222~L224）

```c
  /* check, whether we are in full buffer mode */
  if ( u8g2->tile_buf_height != u8g2_GetU8x8(u8g2)->display_info->tile_height )
    return; /* not in full buffer mode, do nothing */	// 页模式缓冲与屏不同高，区域坐标对不上，直接放弃
```

这是**静默失败**：页模式调用 `UpdateDisplayArea` 不会报错、不会返回错误码，
只会什么都不做。调试"局部刷新无效"时第一个要查的就是当前是不是 `_f` 模式。

### 3.5 导出函数的位序参数（L267~L268 vs L292~L293）

```c
  u8x8_capture_write_pbm_buffer(u8g2_GetBufferPtr(u8g2), u8g2_GetBufferTileWidth(u8g2), u8g2_GetBufferTileHeight(u8g2), u8x8_capture_get_pixel_1, out);
```

```c
  u8x8_capture_write_pbm_buffer(u8g2_GetBufferPtr(u8g2), u8g2_GetBufferTileWidth(u8g2), u8g2_GetBufferTileHeight(u8g2), u8x8_capture_get_pixel_2, out);
```

`get_pixel_1` = 竖排字节（LSB 在上，SSD1306 类）；`get_pixel_2` = 横排字节
（MSB 在左，ST7920 类）。选错会得到旋转 90° 或镜像的图，但**不会崩溃**——
这是纯调试接口。

## 4. 三种内存模式的行为差异

| 行为 | `_f` 全缓冲 | `_2` 双页 | `_1` 单页 |
|---|---|---|---|
| `tile_buf_height` | 满屏 | 2 | 1 |
| `ClearBuffer` 清多少 | 整屏 | 2 行 | 1 行 |
| `SendBuffer` 发多少 | 整屏 | 2 行（从 `tile_curr_row`） | 1 行 |
| `FirstPage/NextPage` | 循环 1 次 | 循环 4 次（128x64） | 循环 8 次 |
| `UpdateDisplayArea` | 可用 | 无效 | 无效 |
| 每帧绘制次数 | 1 | 4 | 8 |

## 5. 设计要点

1. **缓冲是"用户可见坐标系"的唯一实现**。`DrawXxx` 不需要知道屏幕是哪一页，
   窗口移动由 `SetBufferCurrTileRow` 触发的 `update_page_win` 完成。
2. **发送粒度为 tile 行**，不是字节也不是整屏。这让页模式与全缓冲共用同一个
   `u8g2_send_buffer`，只靠 `tile_buf_height` 区分。
3. **没有 DMA/异步支持**。`SendBuffer` 是阻塞的，一整帧期间 CPU 全在跑总线循环。
   AVR 上 1 KB I2C 帧约 13 ms，这段时间画不了任何东西。
4. **导出接口属于"可测试性设计"**。`WriteBufferPBM/XBM` 让 PC 端可以逐字节比对
   渲染结果，u8g2 官方的回归测试就是靠它。

## 6. 已知坑（读码发现，未改代码）

1. **`SendBuffer` 之后缓冲内容不变**。想看下一帧必须自己 `ClearBuffer`，
   否则新旧画面叠加（页模式下尤其明显）。
2. **`FirstPage` 的自动清屏可以被关掉**，关掉后第一页会残留上一次的内容
   （`is_auto_page_clear=0` 时）。
3. **`UpdateDisplayArea` 的 `tx/ty/tw/th` 是 tile 坐标**，不是像素：
   传 `(0,0,16,8)` 才是整屏，传 `(0,0,128,64)` 会读越界。
4. **`NextPage` 返回 0 时才发 REFRESH**。如果循环体里 `break` 提前退出，
   电子纸永远不刷新——这是"电子纸白屏"的常见原因。

## 7. 互链

- 缓冲从哪来：[u8g2_d_memory.md](u8g2_d_memory.md)
- 字段含义：[u8g2_h.md](u8g2_h.md)
- 窗口如何随翻页变化：[u8g2_setup.md](u8g2_setup.md)
- 内存模型全景：[Docs/01-内存与缓冲模型.md](../../../references/graphics/u8g2/Docs/01-内存与缓冲模型.md)
