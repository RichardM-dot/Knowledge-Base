# u8g2_bitmap.c —— 位图绘制（Bitmap/XBM/XBMP）

> 对应源文件：`csrc/u8g2_bitmap.c`（原 275 行，注释后 292 行；行号以注释后版本为准）
> 注释分级：**B 级（文件头导读块）**，strip_equiv_check PASS（17 行纯注释新增，0 删除）
>
> 姊妹条目：[clib_u8g2_font.md](clib_u8g2_font.md) | [clib_u8g2_circle.md](clib_u8g2_circle.md) | [clib_u8g2_line.md](clib_u8g2_line.md) | [clib_u8g2_box.md](clib_u8g2_box.md)
> 深度文章：[Docs/11-图元绘制算法.md](../../../references/graphics/u8g2/Docs/11-图元绘制算法.md)

## 1. 一句话定位

三种未压缩行位图格式 × "整幅/单行"两层 API。与字体（RLE 压缩）不同，
位图是逐位展开直接画；核心优化是 HXBM/HXBMP 的**同色游程合并**——
先数连续同色位，再一次 `DrawHVLine` 画整段。

## 2. 逐函数表

| 函数 | 行号 | 可见性 | 职责 | 关键点 |
|---|---|---|---|---|
| `u8g2_SetBitmapMode` | 56 | 公共 | 透明模式开关 | 写 `bitmap_transparency` 标志 |
| `u8g2_DrawHorizontalBitmap` | 67 | 公共 | 单行 MSB-first | mask 从 0x80 起；逐位 DrawHVLine(…,1,0) |
| `u8g2_DrawBitmap` | 103 | 公共 | 整幅 MSB-first | 参数 cnt=每行字节数（u8glib 兼容） |
| `u8g2_DrawHXBM` | 124 | 公共 | 单行 LSB-first + 游程合并 | run_length 统计后整段画 |
| `u8g2_DrawXBM` | 184 | 公共 | 整幅 LSB-first | 每行 (w+7)/8 字节自动算（:169~172） |
| `u8g2_DrawHXBMP` | 209 | 公共 | 单行 LSB-first（PROGMEM） | 读位经 `u8x8_pgm_read`（:222） |
| `u8g2_DrawXBMP` | 272 | 公共 | 整幅 LSB-first（PROGMEM） | AVR 用这个 |

## 3. 关键摘录：游程合并（DrawHXBM :154~177）

```c
    u8g2_uint_t run_length = 0;
    // Determine the run length of consecutive bits with the same color
    while (len > 0 && (current_bit == 0 ? (((*b) & mask) == 0) : (((*b) & mask) != 0 )  ))
    {
        run_length++;
        x++;
        mask <<= 1;
        if (mask == 0)
        {
            mask = 1;
            b++;
        }
        len--;
    }
    if (current_bit)
    {
        u8g2->draw_color = color;
        u8g2_DrawHVLine(u8g2, x - run_length, y, run_length, 0);
    }
    else if (u8g2->bitmap_transparency == 0)
    {
        u8g2->draw_color = ncolor;
        u8g2_DrawHVLine(u8g2, x - run_length, y, run_length, 0);
    }
```

要点：

1. 数游程时 `x` 已经前移，落屏要**回退 `x - run_length`**——最易读错的一步；
2. `#ifdef OLD` 分支（:138~152）保留了旧的逐位版本，可对照性能差异；
3. 透明模式：1 位画前景色 color；0 位仅当 `bitmap_transparency==0`
   时画反色 ncolor；否则跳过（保背景）。

## 4. 格式对照与选型

| 格式 | 位序 | 每行字节 | 数据位置 | 典型来源 |
|---|---|---|---|---|
| Bitmap | MSB-first | 调用者给 cnt | RAM | u8glib 老生态、手写图标 |
| XBM | LSB-first | (w+7)/8 自动 | RAM | X11 导出、image2cpp 默认 |
| XBMP | LSB-first | (w+7)/8 自动 | AVR PROGMEM | 常量图标放 Flash |

## 5. 已知坑

1. **格式混用即镜像**：XBM 数据喂 DrawBitmap 会左右镜像 + 行错位；
2. **非 AVR 上 XBMP 也安全**（pgm_read 是宏空转），但 AVR 上 XBM 必炸
   （直接解引用 Flash 地址）；
3. 画出的位图每行按 8 对齐存取，但 `len` 传的是**像素宽** w 而非字节数
   （与 DrawBitmap 的 cnt 语义相反，读参时注意）。
