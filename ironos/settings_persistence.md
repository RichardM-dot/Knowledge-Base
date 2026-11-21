# Settings：设置持久化（B 级）

> 对应源文件：`source/Core/Src/Settings.cpp`（共 362 行）、`source/Core/Inc/Settings.h`（共 225 行）
> 工程：`references/firmware/IronOS` ｜ 关联文档：[05-设置系统与持久化](../../../references/firmware/IronOS/Docs/05-设置系统与持久化.md)

## 职责

全部用户设置的单一事实来源：57 项 `uint16_t`，固定存放在 flash 最后一页，
带版本号、长度、逐项范围校验。

## 数据结构

```cpp
typedef struct {
  uint16_t versionMarker;
  uint16_t length; // Length of valid bytes following
  uint16_t settingsValues[SettingsOptionsLength];
  // used to make this nicely "good enough" aligned to 32 bytes to make driver code trivial
  uint32_t padding;

} systemSettingsType;
```

（`source/Core/Inc/Settings.h:113-120`）

`sizeof = 2+2+57*2+4 = 122` 字节（61 个半字），可被 `fmc_halfword_program` 整除。

## 函数表

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `saveSettings` | 82 | memcmp 后按需擦写整页 |
| `loadSettings` | 102 | 读 flash + sanitise |
| `sanitiseSettings` | 109 | 版本/长度/范围三重校验 |
| `resetSettings` | 142 | 全 0xFF 后重建默认值 |
| `setSettingValue` | 148 | 写入前夹到 [min,max] |
| `getSettingValue` | 162 | 无边界数组读 |
| `nextSettingValue` | 166 | 递增 + 回绕 |
| `isLastSettingValue` | 180 | 温度项按 C/F 判上限 |
| `prevSettingValue` | 209 | 递减 + 回绕 |
| `lookupHallEffectThreshold` | 223 | 档位 → 阈值（近似指数） |
| `lookupVoltageLevel` | 260 | 电池串数 → 欠压截止 |
| `lookupTipName` | 271 | 烙铁头型号 → 译名 |
| `getUserSelectedTipResistance` | 317 | 型号 → 电阻（0=自动） |

## sanitise 的三道校验

```cpp
  if (systemSettings.versionMarker != SETTINGSVERSION) {
    memset((void *)&systemSettings, 0xFF, sizeof(systemSettings));
    systemSettings.versionMarker = SETTINGSVERSION;
    dirty                        = true;
  }
```

（`source/Core/Src/Settings.cpp:113-117`）

1. 版本不匹配 → 整块 0xFF（等价于擦除态），后续范围校验填默认值；
2. `padding != 0xFFFFFFFF` → 修正（保证回滚固件时语义一致）；
3. `length < SettingsOptionsLength` → 新增项填 0xFFFF 并更新长度（向前兼容）。

最后逐项检查 min/max，越界回默认值。

## 保存策略

```cpp
  systemSettingsType settings;
  flash_read_buffer((uint8_t *)&settings, sizeof(systemSettingsType));
  if (memcmp((void *)&settings, (void *)&systemSettings, sizeof(systemSettingsType))) {
    flash_save_buffer((uint8_t *)&systemSettings, sizeof(systemSettingsType));
  }
```

（`source/Core/Src/Settings.cpp:85-88`）

内容相同则不写，显著降低擦写次数。调用点只在"退出菜单/调温超时/校准完成/告警计数"
等低频事件，不在每次按键时写。

## 存储地址

```c
#define SETTINGS_START_PAGE (0x08000000 + (127 * 1024))
```

（`source/Core/BSP/Pinecil/configuration.h:176`）

即 `0x0801FC00`，128 KB flash 的最后一页（1 KB）。

## 陷阱

- **新增设置项只能追加**，插队会让所有后续项错位。
- **`SETTINGSVERSION` 改一次等于让所有设备恢复默认**，非破坏性变更不要动它。
- **`getSettingValue` 不检查下标**，传非法枚举会读到相邻字段。
- **`lookupVoltageLevel` 返回 0.1 V 单位**（96 = 9.6 V）。

## 相关

- [settings_gui.md](settings_gui.md)：设置项如何被编辑
- [settings_menu.md](settings_menu.md)：菜单交互
- [main_startup.md](main_startup.md)：上电加载
