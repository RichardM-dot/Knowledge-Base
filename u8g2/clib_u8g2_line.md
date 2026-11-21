# u8g2_line.c —— 直线图元（Bresenham）

> 对应源文件：`csrc/u8g2_line.c`（原 92 行，注释后 111 行；行号以注释后版本为准）
> 注释分级：**B 级（文件头导读块）**，strip_equiv_check PASS（19 行纯注释新增，0 删除）
> ⚠️ 上游笔误：本文件版权头内文件名误写为 `u8g2_box.c`（:3），未改动，特此备注
>
> 姊妹条目：[clib_u8g2_font.md](clib_u8g2_font.md) | [clib_u8g2_circle.md](clib_u8g2_circle.md) | [clib_u8g2_bitmap.md](clib_u8g2_bitmap.md) | [clib_u8g2_box.md](clib_u8g2_box.md)
> 深度文章：[Docs/11-图元绘制算法.md](../../../references/graphics/u8g2/Docs/11-图元绘制算法.md)

## 1. 一句话定位

全文件只有一个公共函数 `u8g2_DrawLine`（:58~110）：标准整数 Bresenham，
主循环沿主轴逐像素 `u8g2_DrawPixel`。无窗口剪裁（:68 原注释自述），
越界由 DrawPixel 逐点兜底。

## 2. 逐函数表

| 函数 | 行号 | 可见性 | 职责 | 关键点 |
|---|---|---|---|---|
| `u8g2_DrawLine` | 58 | 公共 | 任意直线 | swapxy 处理陡线；err=dx/2 对称起步；终点 MAX 回绕防御 |

## 3. 关键摘录（:70~97）

```c
  if ( x1 > x2 ) dx = x1-x2; else dx = x2-x1;
  if ( y1 > y2 ) dy = y1-y2; else dy = y2-y1;

  if ( dy > dx ) 
  {
    swapxy = 1;
    tmp = dx; dx =dy; dy = tmp;
    tmp = x1; x1 =y1; y1 = tmp;
    tmp = x2; x2 =y2; y2 = tmp;
  }
  if ( x1 > x2 ) 
  {
    tmp = x1; x1 =x2; x2 = tmp;
    tmp = y1; y1 =y2; y2 = tmp;
  }
  err = dx >> 1;
  if ( y2 > y1 ) ystep = 1; else ystep = -1;
  y = y1;

#ifndef  U8G2_16BIT
  if ( x2 == 255 )
    x2--;
#else
  if ( x2 == 0xffff )
    x2--;
#endif

  for( x = x1; x <= x2; x++ )
```

四个要点：

1. **swapxy**：斜率>1 时交换 x/y 轴，画点时再换回（:99~102 的 if/else）；
   一个标志替代两套循环；
2. **err = dx/2 起步**：决策以线段中点为基准，线不整体偏移半像素；
3. **x2==255/0xffff 先减 1**：`x <= x2` 遇无符号 MAX 回绕成死循环的经典防御，
   8bit/16bit 两种 u8g2_uint_t 配置都覆盖；
4. **不做剪裁**：注释原文 "no intersection check at the moment, should be
   added..."——长斜线在屏外时开销照付。

## 4. 使用建议

| 场景 | 建议 |
|---|---|
| 画水平/垂直线 | 用 `u8g2_DrawHLine/DrawVLine`（hvline.c 快速路径），不要用 DrawLine |
| 画矩形边框 | 用 `u8g2_DrawFrame`（box.c），4 条线且避角重复 |
| 大量屏外直线 | 先自行包围盒判断，或启用 U8G2_WITH_INTERSECTION 后自包一层 |
