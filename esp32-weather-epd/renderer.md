# renderer.cpp —— 墨水屏渲染主引擎（A级）

对应源文件：`platformio/src/renderer.cpp`（共 1990 行）；声明头 `platformio/include/renderer.h`（109 行）
相关文档：[Docs/03-墨水屏驱动与刷新时序](../../../references/firmware/esp32-weather-epd/Docs/03-墨水屏驱动与刷新时序.md) · [Docs/04-UI布局与绘制管线](../../../references/firmware/esp32-weather-epd/Docs/04-UI布局与绘制管线.md)
互链：[main.md](main.md) · [display_utils.md](display_utils.md) · [conversions.md](conversions.md)

## 0. 文件定位

全项目最大文件：5 种面板的 `display` 全局对象定义（编译期选一）+ 3 个文字绘制原语 + 屏生命周期管理 + 29 个绘制函数（主面板 14 小部件 / 5 日预报 / 预警 / 逐时图 / 状态栏 / 错误页）。已按 A 级逐函数深注。

## 1. 面板对象（renderer.cpp:50-90）

`#ifdef DISP_BW_V2 / DISP_3C_B / DISP_7C_F / DISP_7C_E6 / DISP_BW_V1` 五选一，模板第二参数是页缓冲行数（HEIGHT 或 HEIGHT/2、HEIGHT/4），使各面板 RAM 占用收敛 ~48KB。`extern` 声明与 `DISP_WIDTH/DISP_HEIGHT` 宏在 renderer.h:28-61。`ACCENT_COLOR` 未定义时回退 `GxEPD_BLACK`（renderer.cpp:92-94）。

## 2. 逐函数总表

### 2.1 原语与生命周期

| 函数 | 行号 | 职责 |
|---|---|---|
| `getStringWidth` | 103 | 当前字体下量宽 |
| `getStringHeight` | 117 | 量高 |
| `drawString` | 135 | 对齐绘制（LEFT/RIGHT/CENTER，先量宽再反推起点） |
| `drawMultiLnString` | 175 | 自动折行 + 末行省略号（断点=空格/连字符，keepLastChar 防死循环） |
| `initDisplay` | 282 | 供电→init(复位脉冲10ms)→SPI 重映射→setFullWindow+firstPage |
| `powerOffDisplay` | 319 | hibernate + EPD_PWR 拉低 |

### 2.2 主面板小部件（POS_* 宏定位，网格公式 `162*PosX / 204+56*PosY`）

| 函数 | 行号 | 内容 |
|---|---|---|
| `drawCurrentSunrise` | 340 | 日出时间（图标+标签+_strftime） |
| `drawCurrentWind` | 374 | 风速六单位 + 箭头/度数/CPN 罗盘文案 |
| `drawCurrentUVI` | 463 | UV 指数 + 描述（12pt→5pt→两行降级） |
| `drawCurrentAirQuality` | 525 | calc_aqi() 现算 AQI + 描述（pb 传 NULL） |
| `drawCurrentInTemp` | 606 | 室内温度（NAN→"--"） |
| `drawCurrentSunset` | 654 | 日落（与 sunrise 对称） |
| `drawCurrentHumidity` | 686 | 室外湿度 % |
| `drawCurrentPressure` | 719 | 气压 8 单位 |
| `drawCurrentVisibility` | 794 | 能见度（<1.95 保留 1 位小数；达上限显示 "> x"） |
| `drawCurrentInHumidity` | 854 | 室内湿度 |
| `drawCurrentMoonrise/Moonset` | 893 / 926 | 月出/月落 |
| `drawCurrentMoonphase` | 960 | 月相图标+名称 |
| `drawCurrentDewpoint` | 1013 | 露点（48px+24px 复合图标） |
| `drawCurrentConditions` | 1072 | 总装：196px 大图标 + 48pt 温度 + 体感 + 逐个调用上表 |

### 2.3 其余绘制

| 函数 | 行号 | 职责 |
|---|---|---|
| `drawForecast` | 1211 | 5 日预报列（82px/列；DISPLAY_DAILY_PRECIP=2 smart 模式） |
| `drawAlerts` | 1321 | 预警栏（filterAlerts 过滤 → 1 条 48px/2 条 32px） |
| `drawLocationDate` | 1442 | 右上角城市+日期（x=DISP_WIDTH-2 右对齐） |
| `modulo` | 1465 | 真模运算（负数安全），坐标轴吸附专用 |
| `kelvin_to_plot_y` | 1482 | 开尔文→图表 y 像素（三单位制编译期分支） |
| `drawOutlookGraph` | 1513 | 逐时图：双 y 轴 + 折线 + 降水点阵 + 小时图标防遮挡 |
| `drawStatusBar` | 1871 | 底部状态栏（游标右→左堆叠） |
| `drawError` | 1966 | 全屏错误页（196px 图标 + 1/2 行文案） |

## 3. 关键摘录

### 3.1 对齐绘制（renderer.cpp:135-155 函数体）

```cpp
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextColor(color);
  display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  // 对齐 = 用量到的宽度反推起点偏移；色值由 setTextColor 决定
  if (alignment == RIGHT)
  {
    x = x - w;
  }
  if (alignment == CENTER)
  {
    x = x - w / 2;
  }
  display.setCursor(x, y);
  display.print(text);
```

### 3.2 状态栏游标堆叠（renderer.cpp:1871 起，模式示例）

```cpp
  pos -= 24;
  display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 17,
                             getBatBitmap24(batPercent), 24, 24, dataColor);
  pos -= sp + 9;
```

每段 = 图标宽 + 间距 2px，文字用 `drawString(pos, ..., RIGHT)` 右对齐贴着游标。错误段 `statusStr` 非空才画（error_icon + 文案，ACCENT_COLOR）。

## 4. 深读要点

- **分页模式约定**：draw* 无状态可重入，main.cpp 的 do/while 每页整重画（见 [main.md](main.md) 3.2）。新增分区必须进同一个 do/while。
- **FONT_48pt8b_temperature**（1072 函数内 968 行附近注释）：只含 `0123456789.-°`，由 fonts 流水线 pyftsubset 切出（见 [fonts_toolchain.md](fonts_toolchain.md)）。
- **`drawInvertedBitmap`**：GxEPD2 的"反相"位图绘制（1=黑），位图来自 `icons_*.h` PROGMEM。
- **drawOutlookGraph 的三个数学点**：① 负温度吸附用 `modulo`（1465）；② 降水柱 = 步长 2 的抖动取点；③ 小时图标 y = 图标宽度范围内折线最高点（局部斜率插值 + 区间最小值，1743-1765 附近）。
- **BW_V1 分支**：640×384 面板在 drawForecast/drawCurrentConditions 内有独立坐标（如 `318 + i*64`），且小部件仅 POS 0~5 可用。

## 5. 已知问题（记录不改码）

1. `powerOffDisplay` 尾注释写的是 `// end initDisplay`（复制笔误，renderer.cpp:325）。
2. `drawOutlookGraph` 的参数 `timeInfo` 实际未被使用——函数内声明的同名局部变量 `tm *timeInfo = localtime(&ts)` 将其遮蔽（renderer.cpp:1813 附近），x 轴标签直接用 `hourly[i].dt`。
3. `drawForecast` 里 `hiStr/loStr` 后接的分隔符 "|" 在 TEMP_ORDER_LH 模式下顺序不变（视觉上仍是 Hi|Lo 位置布局由两个 drawString 的左右锚点保证——实测两种模式都对，但改动时容易误判）。

## 6. 数据流角色

见 [Docs/04](../../../references/firmware/esp32-weather-epd/Docs/04-UI布局与绘制管线.md) 第 7 节：六个 draw* 分区 + drawError，全部汇入页缓冲，经 GxEPD2 SPI 推给控制器。
