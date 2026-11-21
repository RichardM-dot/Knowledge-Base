# Display.h —— 墨水屏驱动声明（WatchyDisplay）

> 对应源文件：`src/Display.h`（共 120 行，含中文深注；注释前 103 行）
> 级别：B级（文件头导读扩展）
> 关联 Docs：[00-总览与硬件架构](../../../references/firmware/Watchy/Docs/00-总览与硬件架构.md)
> 关联 KB：[display_cpp.md](display_cpp.md)

---

## 1. 文件定位

`WatchyDisplay : public GxEPD2_EPD` 的类声明：面板常量、Watchy 增强接口
（initWatchy/asyncPowerOn/setDarkBorder/busyCallback）、GxEPD2 虚函数实现清单。
原文件头保留 Jean-Marc Zingg 的版权与来源注释（GxEPD2 库、Good Display
GDEH0154D67 面板、SSD1681 控制器、"slightly modified for Watchy"）。

## 2. 面板常量（注释后 L48-58 附近）

| 常量 | 值 | 说明 |
|---|---|---|
| WIDTH / WIDTH_VISIBLE / HEIGHT | 200 | 200×200 黑白 |
| panel | GDEH0154D67 | 面板型号 |
| hasColor | false | 黑白 |
| hasPartialUpdate / hasFastPartialUpdate | true | 支持部分刷/快刷 |
| power_on_time | 100 ms | 升压 |
| power_off_time | 150 ms | 降压 |
| full_refresh_time | 2600 ms | 全刷（实测 2.51s） |
| partial_refresh_time | 500 ms | 部分刷（实测 0.457s） |

`reduceBoosterTime = true`（constexpr，L102）：压缩 booster 时序省 ~200ms。

成员 `darkBorder`（L100）：屏外圈黑边框开关，setDarkBorder 写入。

## 3. 方法分组

| 组 | 方法 | 备注 |
|---|---|---|
| Watchy 增强 | initWatchy / setDarkBorder / asyncPowerOn / _PowerOnAsync / busyCallback | 魔改新增 |
| 双缓冲写 | clearScreen / writeScreenBuffer(Again) / writeImage(ForFullRefresh/Again/Part) | 0x24/0x26 两 RAM 区 |
| 一体绘制 | drawImage / drawImagePart / drawNative | 写→刷→再写 |
| 刷新 | refresh(bool) / refresh(x,y,w,h) / powerOff / hibernate | 时序见 KB display_cpp |
| private | _writeScreenBuffer / _writeImage(Part) / _setPartialRamArea / _PowerOn/_PowerOff / _InitDisplay / _Init_Full/_Init_Part / _Update_Full/_Update_Part / _reset / _transferCommand | 命令时序层 |

## 4. 在显示栈中的位置

```
用户 drawWatchFace
   │ Adafruit_GFX API（drawString/fillCircle/print…）
GxEPD2_BW<WatchyDisplay, 200>   ← Watchy::display 单例
   │ 窗口管理、黑白缓冲
WatchyDisplay（本类）
   │ SPI 命令 + 显存搬运
SSD1681 控制器 → GDEH0154D67 面板
```

## 5. 陷阱

- `waitingPowerOn` 是 **public 可写成员**（L65）——外部可误改；
- `WIDTH_VISIBLE == WIDTH`：该面板无可视边界偏移（有的 EPD 面板边缘不可见）；
- 时序常量是**实测标注值**（注释给出 μs 级样例），改 booster 参数后这些
  常量要重测；
- 完整逐函数解读在 [display_cpp.md](display_cpp.md)。
