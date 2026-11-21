# OLED：SSD1306 驱动与绘图（B 级）

> 对应源文件：`source/Core/Drivers/OLED.cpp`（共 1001 行）、`source/Core/Drivers/OLED.hpp`（共 169 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[04-用户界面与按键状态机](../../../references/firmware/IronOS/Docs/04-用户界面与按键状态机.md)

## 职责

单色 OLED 的驱动 + 绘图原语。所有界面最终都落到 192 字节（96×16）或 512 字节
（128×32）的 framebuffer，再整块推到面板。

## 缓冲布局

```cpp
alignas(uint32_t) uint8_t OLED::screenBuffer[16 + (OLED_WIDTH * (OLED_HEIGHT / 8)) + 10]; // The data buffer
alignas(uint32_t) uint8_t OLED::secondFrameBuffer[16 + (OLED_WIDTH * (OLED_HEIGHT / 8)) + 10];
```

（`source/Core/Drivers/OLED.cpp:27-28`）

| 区域 | 大小 | 用途 |
| --- | --- | --- |
| 头部 | 16 字节 | 预留给 I2C 命令（地址/控制字节） |
| 像素 | W×H/8 | 1 bit/像素，纵向 8 像素一字节 |
| 尾部 | 10 字节 | 填充，保证 DMA/连续写安全 |

`FRAMEBUFFER_START = 17`（`OLED.hpp`）是像素区的实际起点。

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `refresh` | 163/178 | 整块刷屏（两个 `#ifdef` 版本） |
| `initialize` | 198 | 面板上电序列 |
| `drawChar` | 253 | 单字符绘制（含字体选择） |
| `transitionSecondaryFramebuffer` | 415 | 左右滑动转场 |
| `transitionScrollDown` | 505 | 上下滚动转场 |
| `setRotation` | 625 | 左右手模式（含坐标翻转） |
| `setBrightness` | 696 | 对比度 |
| `print` | 721 | 字符串（支持 `\x01` 大字体前缀） |
| `printNumber` | 801 | 数字（可去前导零） |
| `drawArea` | 855 | 位图块 |

## 刷屏

```cpp
void OLED::refresh() {
```

（`source/Core/Drivers/OLED.cpp:163`）

一次 refresh 的传输量：

```
96x16:  (192 + 17) 字节 × 9 bit ≈ 1881 bit → 400 kHz 下 ≈ 4.7 ms
128x32: (512 + 17) 字节 × 9 bit ≈ 4761 bit → 400 kHz 下 ≈ 11.9 ms
```

GUI 每帧 40 ms，所以 96×16 有 8 倍余量，128×32 约 3 倍。

## 字体与编码

- `FontStyle::SMALL`：8×16（128×32 面板）或 6×8（96×16）；
- `FontStyle::LARGE`：12×16；
- 字符串前缀 `\x01` 表示"必须用大字"（CJK 无法压进小字体）。

`drawChar` 的 `soft_x_limit` 参数是"软换行边界"，防止文字画出屏幕右侧。

## 转场

`transitionSecondaryFramebuffer(forward, viewEnterTime)` 把两块 framebuffer
按时间做像素插值；`viewEnterTime` 是模式切换的时刻，用来算动画进度。
`easeInOutTiming()` 做缓入缓出，避免线性动画的机械感。

## 陷阱

- **framebuffer 无锁**：只能由 GUI 任务访问。
- **`drawChar` 只在 y==0 时处理换行**（`\n` 语义），在 y>0 时调用需要自己算坐标。
- **`setRotation` 会重排 strip 指针**，转屏后所有坐标语义随之改变。
- **`refresh()` 是阻塞 I2C**，软件 I2C 下约 5~12 ms，期间 GUI 任务不能做别的。

## 坐标系与页结构

SSD1306 的显存按"页"组织：每页 8 行像素，一列一个字节。

```
y=0..7    → page 0，bit0 在上
y=8..15   → page 1
y=16..23  → page 2（仅 128x32）
y=24..31  → page 3（仅 128x32）
```

`drawChar` 把字模的每一列写成纵向字节，所以字体高度必须是 8 的倍数
（6×8、8×16、12×16 都满足）。

## 字体与内存

| 字体 | 字模大小 | 用途 |
| --- | --- | --- |
| 6×8 | 6 字节/字符 | 96×16 的小字 |
| 8×16 | 16 字节/字符 | 128×32 的小字 |
| 12×16 | 24 字节/字符 | 主数值/标题 |

字模存在 `Font.h` 里（生成物），按字符编码索引；CJK 走双字节编码，
所以 `print()` 需要先判断首字节是否 >0xF0。

## 刷屏的两种实现

`refresh()` 有两个 `#ifdef` 版本（163/178 行）：一个用于 SPI/并行接口，
一个用于 I2C。Pinecil 走 I2C 版本，把 17 字节命令头 + 像素区一次 `Mem_Write` 发出。

## 相关

- [i2cbb2.md](i2cbb2.md)：底层总线
- [gui_thread.md](gui_thread.md)：调用者
- [settings_gui.md](settings_gui.md)：菜单绘制
