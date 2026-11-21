# Display.cpp —— 墨水屏驱动实现（SSD1681 寄存器级）

> 对应源文件：`src/Display.cpp`（共 530 行，含中文深注；注释前 510 行）
> 级别：B级（文件头导读扩展）
> 关联 Docs：[01-库框架与生命周期](../../../references/firmware/Watchy/Docs/01-库框架与生命周期.md)、
> [00-总览与硬件架构](../../../references/firmware/Watchy/Docs/00-总览与硬件架构.md)
> 关联 KB：[display_h.md](display_h.md)

---

## 1. 文件定位

Jean-Marc Zingg 的 GxEPD2（SSD1681/GDEH0154D67 面板）被 Watchy 魔改后的实现：
屏幕刷新时序、显存搬运、电源控制。上承 `GxEPD2_BW<WatchyDisplay,...>`
（Adafruit_GFX 绘图 API），下接 SPI。

## 2. Watchy 魔改点（相对原版 GxEPD2）

| 魔改 | 位置（注释后行） | 收益 |
|---|---|---|
| `busyCallback` → light sleep | L47-51 | 屏幕 BUSY 0.5~2.6s 期间 CPU 睡而非忙等 |
| `asyncPowerOn()`/`_PowerOnAsync()` | L69-75 / L388-399 | 升压 100ms 与绘制重叠 |
| `reduceBoosterTime` | L452-461（_InitDisplay 内） | booster 相位压缩，全刷省 ~200ms |
| `initWatchy()` | L64-67 | 全刷标志走 RTC 慢存 displayFullInit |
| `setDarkBorder()` | L77-84 | 屏外圈黑边框开关（0x3C BorderWaveform） |
| `displayFullInit` RTC 慢存 | L45 | 跨深睡记住"已做过首次全刷" |

## 3. 主要函数表（注释后行）

| 函数 | 行 | 职责 |
|---|---|---|
| `busyCallback` | L47 | BUSY 期间 `esp_light_sleep_start()`（GPIO 唤醒） |
| `initWatchy` | L64 | `init(0, displayFullInit, 2, true)` Watchy 默认参 |
| `asyncPowerOn` | L69 | 非休眠态才发异步上电 |
| `setDarkBorder` | L77 | 0x3C 命令写边框波形 |
| `clearScreen` | L86 | 写白显存→刷→再写（三步清屏） |
| `writeScreenBuffer(Again)` | L93/101 | 0x26 前帧 / 0x24 当前帧整屏写 |
| `_writeScreenBuffer` | L107 | 5000 字节（200×200/8）连续 SPI 写 |
| `writeImage(_Again/_ForFullRefresh)` | L118-131 | 写窗口位图 |
| `_writeImage` | L134 | 裁剪到屏界→设窗→逐行 SPI 搬运 |
| `writeImagePart(_Again)` | L185/191 | 源位图子窗口版 |
| `drawImage(_Part)` | L280/287 | 写→刷→写 Again（双缓冲一致性） |
| `refresh(bool)` | L320 | 部分刷走窗口；全刷走 _Init_Full+_Update_Full |
| `refresh(x,y,w,h)` | L331 | 窗口对齐 8bit 后 _Update_Part |
| `powerOff`/`hibernate` | L351/356 | 关电源 / 0x10 控制器深睡 |
| `_setPartialRamArea` | L367 | 0x44/0x45 设窗 + 0x4E/0x4F 指针定位 |
| `_PowerOnAsync/_PowerOn/_PowerOff` | L388/401/419 | 0x22/0x20 命令对 |
| `_InitDisplay` | L438 | 复位后寄存器初始化（含 booster） |
| `_reset` | L472 | 硬复位（含 pulldown 模式特判） |
| `_Init_Full/_Init_Part` | L489/496 | 进入全刷/部分刷模式 |
| `_Update_Full/_Update_Part` | L503/514 | 0xF4 全刷 / 0xFC 部分刷 + 等 BUSY |
| `_transferCommand` | L525 | DC 拉低发命令、拉高发数据 |

## 4. 关键时序摘录

### 4.1 部分刷新命令对（L514-524）

```cpp
void WatchyDisplay::_Update_Part()
{
  _startTransfer();
  _transferCommand(0x22);
  //_transfer(0xcc); // skip temperature load (-5ms)
  _transfer(0xfc);
  _transferCommand(0x20);
  _endTransfer();
  _waitWhileBusy("_Update_Part", partial_refresh_time);
}
```

0x22 = 更新控制，参数 0xFC 定义部分刷波形序列；0x20 = 执行。
被注释的 0xCC 是"跳过温度加载再省 5ms"的实验残留。每部分刷 ~500ms
（Display.h 的 `partial_refresh_time`），全刷 0xF4 → ~2600ms。

### 4.2 BUSY 期间 light sleep（L47-51）

```cpp
void WatchyDisplay::busyCallback(const void *) {
  gpio_wakeup_enable((gpio_num_t)DISPLAY_BUSY, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
}
```

BUSY 是低有效：拉低期间 CPU light sleep，GPIO 低电平唤醒源到点即醒。
这是 Watchy 续航的隐藏功臣——每天 1440 次 tick 的等待时间全部睡掉。

### 4.3 异步上电（L388-399）

```cpp
void WatchyDisplay::_PowerOnAsync()
{
  if (_power_is_on)
    return;
  _startTransfer();
  _transferCommand(0x22);
  _transfer(0xf8);
  _transferCommand(0x20);
  _endTransfer();
  waitingPowerOn = true;
  _power_is_on = true;
}
```

发完上电命令**不等 BUSY** 直接返回；真正需要屏就绪的 _PowerOn 里
若 `waitingPowerOn` 为真补一次等待（L401 起）。`showWatchFace` 正是
借此把 100ms 上电与用户 drawWatchFace 重叠（Docs/01 §1）。

## 5. 显存模型

SSD1681 有两个 RAM 区：0x24（当前帧）与 0x26（前一帧）。差分刷新
（部分刷）靠"前后帧对比"生成波形：

| 写入函数 | 目标 | 用途 |
|---|---|---|
| `writeScreenBuffer` | 0x26+0x24 | 初始清屏 |
| `writeImage` | 0x24 | 部分刷写当前帧 |
| `writeImageForFullRefresh` | 0x26+0x24 | 全刷双写保证一致 |
| `writeImageAgain` | 0x24 | 部分刷后补写（drawImage 尾部调用） |

一帧 = 200×200/8 = 5000 字节；`_writeImage` 的双重循环里
`idx = ... (h-1-(i+dy))*wb` 处理 mirror_y 翻转，`pgm_read_byte` 处理
AVR/ESP 闪存取指。

## 6. 陷阱与记录

- **`refresh(x,y,w,h)` 只对齐宽度**：w1 按字节对齐、x1 取 8 倍数；
  非对齐窗口的左边可能带邻居列。
- **`_PowerOff` 会顺带清 `_using_partial_mode`**（L419 起）：全刷后模式复位，
  混用两种刷新时注意模式切换成本（_Init_Full/_Init_Part 都含 _InitDisplay）。
- **hibernate 后必须 _reset 才能用**：`_InitDisplay` 开头判 `_hibernating`
  走 `_reset()`（L440 判断，_reset 实现在 L472）——deepSleep 醒来后的第一次刷新会自动硬复位。
- **0xCC 注释**（跳温度加载 -5ms）保留未启用：温度相关波形更保守。
- 屏 3.3V 供电与数据线（文件头英文注释强调）。
- 更深读法：对照 SSD1681 数据手册看 0x22 参数位含义。
