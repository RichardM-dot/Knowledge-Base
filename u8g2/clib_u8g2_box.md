# u8g2_box.c —— 矩形图元（Box/Frame/RBox/RFrame）

> 对应源文件：`csrc/u8g2_box.c`（原 215 行，注释后 233 行；行号以注释后版本为准）
> 注释分级：**B 级（文件头导读块）**，文件头原有简版导读保留，本次补充扩展块；strip_equiv_check PASS（18 行纯注释新增，0 删除）
>
> 姊妹条目：[clib_u8g2_font.md](clib_u8g2_font.md) | [clib_u8g2_circle.md](clib_u8g2_circle.md) | [clib_u8g2_line.md](clib_u8g2_line.md) | [clib_u8g2_bitmap.md](clib_u8g2_bitmap.md)
> 深度文章：[Docs/11-图元绘制算法.md](../../../references/graphics/u8g2/Docs/11-图元绘制算法.md)

## 1. 一句话定位

四个矩形图元全部由更底层的图元**组合**而成（自身无算法内核）：
DrawBox=横线填充；DrawFrame=4 条线拼边；DrawRBox/DrawRFrame=
4 段 1/4 圆弧（复用 circle.c 的 Disc/Circle 四象限掩码）+ 直线段补边。

## 2. 逐函数表

| 函数 | 行号 | 可见性 | 职责 | 组成 | 关键点 |
|---|---|---|---|---|---|
| `u8g2_DrawBox` | 65 | 公共 | 实心矩形 | h 次 `DrawHVLine(w)` | 库中最快大面积路径；w=0/h=0 不支持 |
| `u8g2_DrawFrame` | 84 | 公共 | 空心矩形 | 2 HLine + 2 VLine | `h>=2`/`h>0` 两级防御，避角点重复 |
| `u8g2_DrawRBox` | 111 | 公共 | 实心圆角矩形 | 4×1/4 DrawDisc + 2 窄条 + 1 大块 | 圆心 `-1` 处理半开区间 |
| `u8g2_DrawRFrame` | 170 | 公共 | 空心圆角矩形 | 4×1/4 DrawCircle + 4 直线段 | 同上 |

## 3. 关键摘录

### 3.1 DrawBox 的全部实现（:65~77）

```c
void u8g2_DrawBox(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h)
{
#ifdef U8G2_WITH_INTERSECTION
  if ( u8g2_IsIntersection(u8g2, x, y, x+w, y+h) == 0 ) 
    return;
#endif /* U8G2_WITH_INTERSECTION */
  while( h != 0 )
  { 
    u8g2_DrawHVLine(u8g2, x, y, w, 0);
    y++;    
    h--;
  }
}
```

### 3.2 DrawRBox 的圆心计算（:121~134）

```c
  xl = x;
  xl += r;
  yu = y;
  yu += r;
 
  xr = x;
  xr += w;
  xr -= r;
  xr -= 1;
  
  yl = y;
  yl += h;
  yl -= r; 
  yl -= 1;

  u8g2_DrawDisc(u8g2, xl, yu, r, U8G2_DRAW_UPPER_LEFT);
  u8g2_DrawDisc(u8g2, xr, yu, r, U8G2_DRAW_UPPER_RIGHT);
```

右/下圆心的 `-1`：矩形右边界像素在 x+w−1（半开区间 [x, x+w)），
四段弧都以"圆心在内"的方式落点，不减 1 会在角落重叠/错位。

### 3.3 补心矩形（:141~166）

```c
    ww = w;
    ww -= r;
    ww -= r;
    xl++;
    yu++;
    
    if ( ww >= 3 )
    {
      ww -= 2;
      u8g2_DrawBox(u8g2, xl, y, ww, r+1);
      u8g2_DrawBox(u8g2, xl, yl, ww, r+1);
    }
```

`ww = w−2r`，`>=3` 再内缩 2 像素避开圆弧；**r 过大时 ww 为负、补心自动省略，
但四段弧会交叠出畸形**——`r <= min(w,h)/2` 是调用者责任，本文件无守卫。

## 4. 已知坑与建议

| # | 坑 | 建议 |
|---|---|---|
| 1 | w=0 或 h=0（原注释声明不支持） | 调用前判参 |
| 2 | r 超限出怪图 | 保证 r ≤ min(w,h)/2 |
| 3 | RBox 内部调 DrawBox，后者又剪裁一次 | 双重剪裁无害但有小开销；极端性能场景手写拼装 |
| 4 | 选中态反显常误用 DrawBox+改色 | 配 `u8g2_SetDrawColor` 做"反色填充+文字"更稳 |
