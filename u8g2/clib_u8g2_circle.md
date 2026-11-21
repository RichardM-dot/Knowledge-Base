# u8g2_circle.c —— 圆/圆盘/椭圆图元

> 对应源文件：`csrc/u8g2_circle.c`（原 479 行，注释后 497 行；行号以注释后版本为准）
> 注释分级：**B 级（文件头导读块）**，strip_equiv_check PASS（18 行纯注释新增，0 删除）
>
> 姊妹条目：[clib_u8g2_font.md](clib_u8g2_font.md) | [clib_u8g2_line.md](clib_u8g2_line.md) | [clib_u8g2_bitmap.md](clib_u8g2_bitmap.md) | [clib_u8g2_box.md](clib_u8g2_box.md)
> 深度文章：[Docs/11-图元绘制算法.md](../../../references/graphics/u8g2/Docs/11-图元绘制算法.md)

## 1. 一句话定位

四个"圆类"图元的整数算法实现：误差递推内核算 1/8 弧，section 回调按四象限
位掩码（`U8G2_DRAW_UPPER_* / LOWER_*`，u8g2.h:1895~1899）镜像展开。
无浮点、无乘除（椭圆仅初始化有 4 次乘法）。

## 2. 逐函数表

| 函数 | 行号 | 可见性 | 职责 | 关键点 |
|---|---|---|---|---|
| `u8g2_draw_circle_section` | 61 | static | 圆周对称点落屏 | 每象限 2 点（(x,y) 与 (y,x) 交换），最多 8 像素 |
| `u8g2_draw_circle` | 92 | static | Bresenham 圆内核 | f=1−rad 起步；`while(x<y)` 只算 1/8 弧 |
| `u8g2_DrawCircle` | 127 | 公共 | 圆周入口 | 先 IsIntersection 包围盒剪裁 |
| `u8g2_draw_disc_section` | 147 | static | 圆盘填充段 | 每象限 2 条 VLine：`DrawVLine(x0±x, y0−y, y+1)` |
| `u8g2_draw_disc` | 178 | static | 圆盘内核 | 与 draw_circle 逐行相同的递推，仅 section 不同 |
| `u8g2_DrawDisc` | 213 | 公共 | 实心圆入口 | 同一剪裁模式 |
| `u8g2_draw_ellipse_section` | 235 | static | 椭圆周点 | 每象限 1 点（无交换对称） |
| `u8g2_draw_ellipse` | 262 | static | 中点椭圆 | 双区段：|斜率|<1 沿 y、>1 沿 x |
| `u8g2_DrawEllipse` | 351 | 公共 | 椭圆入口 | 剪裁框 x0±rx / y0±ry |
| `u8g2_draw_filled_ellipse_section` | 368 | static | 椭圆填充段 | 每象限 1 条 VLine |
| `u8g2_draw_filled_ellipse` | 395 | static | 实心椭圆内核 | 同 draw_ellipse 递推 |
| `u8g2_DrawFilledEllipse` | 484 | 公共 | 实心椭圆入口 | |

## 3. 关键摘录

### 3.1 误差递推内核（u8g2_draw_circle，:100~124）

```c
    f = 1;
    f -= rad;
    ddF_x = 1;
    ddF_y = 0;
    ddF_y -= rad;
    ddF_y *= 2;
    x = 0;
    y = rad;

    u8g2_draw_circle_section(u8g2, x, y, x0, y0, option);
    
    while ( x < y )
    {
      if (f >= 0) 
      {
        y--;
        ddF_y += 2;
        f += ddF_y;
      }
      x++;
      ddF_x += 2;
      f += ddF_x;

      u8g2_draw_circle_section(u8g2, x, y, x0, y0, option);    
    }
```

- `f` 是隐函数 F(x,y)=x²+y²−r² 的增量形式；`ddF_x/ddF_y` 是其二阶差分，
  使每步只需加减；
- `f >= 0`（而非 >0）是中点法的取点约定，保证象限衔接处不缺像素。

### 3.2 椭圆双区段的边界变量（:271~295）

```c
  rxrx2 = rx;  rxrx2 *= rx;  rxrx2 *= 2;   /* 2rx² */
  ryry2 = ry;  ryry2 *= ry;  ryry2 *= 2;   /* 2ry² */
  ...
  stopx = ryry2;  stopx *= rx;   stopy = 0;
  while( stopx >= stopy ) { ... }          /* 区段 1：斜率 |dy/dx|<1 */
```

区段 2（:313 起）从 (0, ry) 反向同理，`stopx <= stopy` 终止。
出处：Foley《Computer Graphics》p.90（源码 :230-233 注释亦标注）。

## 4. 消费者与陷阱

| 项 | 说明 |
|---|---|
| `u8g2_box.c` 的 DrawRBox/DrawRFrame | 传单象限掩码复用 Disc/Circle 画圆角 |
| `f>=0` 边界 | 与"中点在圆内"判定对应，改动会破坏象限衔接 |
| 半开区间 | 剪裁框用 x0+rad+1，与像素半开区间 [x, x+w) 一致 |
| 性能 | 大半径实心圆用 VLine 数量大；静态图形建议预生成 XBM |
