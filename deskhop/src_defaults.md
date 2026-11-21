# DeskHop 源码笔记：defaults.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/defaults.c`（共 84 行）

> 出厂默认配置 `default_config`。只在 Flash 配置校验失败（空白/CRC 错/版本不符）
> 时被 `load_config()` 整体采用。

相关文档：[08-实测与坑点指南](../../../references/firmware/DeskHop/Docs/08-实测与坑点指南.md)

## 1. 文件结构

| 区段 | 行号 | 内容 |
|------|------|------|
| 许可证 | L1-L10 | GPLv3 |
| 中文导读（新增） | L12-L18 | B 级导读 |
| include | L19 | `#include "main.h"` |
| 默认配置 | L33-L84 | `const config_t default_config` |

## 2. 配置字段表

| 字段 | 值 | 来源宏 | 含义 |
|------|----|--------|------|
| `magic_header` | `0xB00B1E5` | 硬编码 | 有效配置标识 |
| `version` | `CURRENT_CONFIG_VERSION`(8) | config.h | 布局版本 |
| `output[A].number` | `OUTPUT_A`(0) | — | 输出编号 |
| `output[A].speed_x/y` | 16 / 28 | `MOUSE_SPEED_A_FACTOR_*` | 鼠标速度 |
| `output[A].border` | 0 / 32767 | — | 整屏 |
| `output[A].screen_count` | 1 | — | 单显示器 |
| `output[A].screen_index` | 1 | — | 主屏 |
| `output[A].os` | `OUTPUT_A_OS`(MACOS) | user_config.h | 电脑 A 系统 |
| `output[A].pos` | `RIGHT` | — | A 在右侧 |
| `output[A].screensaver` | DISABLED / 240s / 0 / 0 | `SCREENSAVER_A_*` | 屏保 |
| `output[B].pos` | `LEFT` | — | B 在左侧 |
| `output[B].os` | `OUTPUT_B_OS`(LINUX) | user_config.h | 电脑 B 系统 |
| `enforce_ports` | `ENFORCE_PORTS`(0) | user_config.h | 不强制端口 |
| `force_kbd_boot_protocol` | `ENFORCE_KEYBOARD_BOOT_PROTOCOL`(0) | user_config.h | 键盘用 report 协议 |
| `force_mouse_boot_mode` | `false` | — | 鼠标用 report 协议 |
| `enable_acceleration` | `ENABLE_ACCELERATION`(1) | user_config.h | 开加速 |
| `hotkey_toggle` | `HOTKEY_TOGGLE`(Caps) | user_config.h | 切机键 |
| `kbd_led_as_indicator` | `KBD_LED_AS_INDICATOR`(0) | user_config.h | 不用 Caps 指示 |
| `jump_threshold` | `JUMP_THRESHOLD`(0) | user_config.h | 无越界余量 |
| `led_mode` | `LED_ACTIVE_OUTPUT` | — | 活跃板常亮 |

## 3. 关键摘录

```c
const config_t default_config = {
    .magic_header = 0xB00B1E5,
    .version = CURRENT_CONFIG_VERSION,
    .output[OUTPUT_A] =
        {
            .number = OUTPUT_A,
            .speed_x = MOUSE_SPEED_A_FACTOR_X,
            .speed_y = MOUSE_SPEED_A_FACTOR_Y,
            .border = {
                .top = 0,
                .bottom = MAX_SCREEN_COORD,
            },
            .screen_count = 1,
            .screen_index = 1,
            .os = OUTPUT_A_OS,
            .pos = RIGHT,
```

（L33-L47）

```c
    .enforce_ports = ENFORCE_PORTS,
    .force_kbd_boot_protocol = ENFORCE_KEYBOARD_BOOT_PROTOCOL,
    .force_mouse_boot_mode = false,
    .enable_acceleration = ENABLE_ACCELERATION,
    .hotkey_toggle = HOTKEY_TOGGLE,
    .kbd_led_as_indicator = KBD_LED_AS_INDICATOR,
    .jump_threshold = JUMP_THRESHOLD,
    .led_mode = LED_ACTIVE_OUTPUT,
};
```

（L76-L84）

## 4. 设计意图

1. **默认 A 在右、B 在左**：鼠标从 A 向右推越界 → 切到 B；从 B 向左推 → 切到 A。
   与 `output[A].pos=RIGHT`、`output[B].pos=LEFT` 一致。
2. **border 整屏 0..32767**：未校准时的恒等映射；用户可用
   右Shift+F12+Y 记录实际边界。
3. **屏保默认关闭**：避免用户没配置就被“鼠标自己动”吓到。
4. **`force_mouse_boot_mode=false`**：默认 report 协议，保留滚轮/横向滚轮。
5. **所有可调项来自 `user_config.h`**：改默认值只需改宏，不用碰本文件。

## 5. 与 load_config 的关系

```c
    if (magic_header_fail || checksum_fail || version_fail)
        memcpy(running_config, &default_config, sizeof(config_t));
```

（`src/utils.c` L112-L114 附近）

- 默认配置**不写 Flash**，只装载到 RAM；
- 用户第一次保存配置后，Flash 里才有有效副本；
- 升级固件若改了 `config_t` 布局，必须递增 `CURRENT_CONFIG_VERSION`，
  否则旧配置会被误判为有效，字段错位。

## 6. 坑点

1. **`CURRENT_CONFIG_VERSION` 必须与 `config_t` 布局同步**：加字段/改顺序就要 +1。
2. **默认值只在“无有效 Flash 配置”时生效**：用户改过配置后，改本文件不会影响现有设备。
3. **`output[A].os` 默认 MACOS、`output[B].os` 默认 LINUX**：这是作者自己的环境；
   你的环境若相反，多屏/锁屏快捷键会不对，需在配置页面或 `user_config.h` 改。
4. **`speed_x=16`、`speed_y=28`**：X/Y 不同比例，补偿 16:9 屏幕的像素宽高比；
   改成相同值会让斜向移动变形。
5. **`jump_threshold=0`**：光标到边缘立即切，容易误触；可调到 500~1000。
6. **`hotkey_toggle` 不能设 `HID_KEY_NONE`**：0 会出现在大量报文中，导致键盘失灵
   （`user_config.h` 有明确警告）。
