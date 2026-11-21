# DeskHop 源码笔记：usb_descriptors.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/usb_descriptors.c`（共 296 行）

> USB 设备侧描述符与设备类回调：普通/配置两套 VID:PID、两套配置描述符、
> HID 报表描述符、字符串描述符、鼠标报告出口。

相关文档：[03-USB HID 设备侧](../../../references/firmware/DeskHop/Docs/03-USB HID 设备侧.md)

## 1. 对象/函数总表

| 对象 | 行号 | 说明 |
|------|------|------|
| `desc_device_config` | L45 | 配置模式 `2e8a:107c` |
| `desc_device` | L48 | 普通模式 `1209:c000` |
| `tud_descriptor_device_cb` | L52-L57 | 按 `config_mode_active` 选设备描述符 |
| `desc_hid_report[]` | L65-L69 | 接口 0：键盘+绝对鼠标+多媒体+系统 |
| `desc_hid_report_relmouse[]` | L71 | 接口 1：相对鼠标 |
| `desc_hid_report_vendor[]` | L73 | 接口 2：vendor 12B |
| `tud_hid_descriptor_report_cb` | L79-L92 | 按接口返回报表描述符 |
| `tud_mouse_report` | L94-L105 | 绝对/相对鼠标报告统一出口 |
| `string_desc_arr[]` | L115-L126 | 字符串表 |
| `tud_descriptor_string_cb` | L143-L184 | ASCII → UTF-16 |
| `desc_configuration[]` | L221-L246 | 普通模式 2 接口 |
| `desc_configuration_config[]` | L248-L287 | 配置模式 4 接口 |
| `tud_descriptor_configuration_cb` | L289-L296 | 按模式返回配置 |

## 2. 设备描述符

```c
                                        // https://github.com/raspberrypi/usb-pid
tusb_desc_device_t const desc_device_config = DEVICE_DESCRIPTOR(0x2e8a, 0x107c);

                                        // https://pid.codes/1209/C000/
tusb_desc_device_t const desc_device = DEVICE_DESCRIPTOR(0x1209, 0xc000);

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void) {
    if (global_state.config_mode_active)
        return (uint8_t const *)&desc_device_config;
    else
        return (uint8_t const *)&desc_device;
}
```

（L44-L57）

| 模式 | VID:PID | 主机看到 |
|------|---------|----------|
| 普通 | `1209:c000` | DeskHop Switch（键鼠） |
| 配置 | `2e8a:107c` | Raspberry Pi 配置设备（HID+MSC） |

`DEVICE_DESCRIPTOR` 宏（`usb_descriptors.h` L39-L53）设置：
`bcdUSB=0x0200`、`bDeviceClass=0`（复合设备）、`bMaxPacketSize0=64`、
`bcdDevice=0x0100`、制造商/产品/序列号字符串索引 1/2/3、1 个配置。

## 3. HID 报表描述符

```c
uint8_t const desc_hid_report[] = {TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
                                   TUD_HID_REPORT_DESC_ABS_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),
                                   TUD_HID_REPORT_DESC_CONSUMER_CTRL(HID_REPORT_ID(REPORT_ID_CONSUMER)),
                                   TUD_HID_REPORT_DESC_SYSTEM_CONTROL(HID_REPORT_ID(REPORT_ID_SYSTEM))
                                   };

uint8_t const desc_hid_report_relmouse[] = {TUD_HID_REPORT_DESC_MOUSEHELP(HID_REPORT_ID(REPORT_ID_RELMOUSE))};

uint8_t const desc_hid_report_vendor[] = {TUD_HID_REPORT_DESC_VENDOR_CTRL(HID_REPORT_ID(REPORT_ID_VENDOR))};
```

（L65-L73）

Report ID 分配：

| ID | 接口 | 语义 | 报表长 |
|----|------|------|--------|
| 0 | 0 | boot 协议无 ID | 8B 键/3B 鼠 |
| 1 | 0 | 键盘 | 8B |
| 2 | 0 | 绝对鼠标 | 8B |
| 3 | 0 | 多媒体 | 4B |
| 4 | 0 | 系统键 | 1B |
| 5 | 1 | 相对鼠标 | 8B |
| 6 | 2 | vendor | 12B |
| 7 | — | 数位板（预留） | — |

接口选择：

```c
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    if (global_state.config_mode_active)
        if (instance == ITF_NUM_HID_VENDOR)
            return desc_hid_report_vendor;

    switch(instance) {
        case ITF_NUM_HID:
            return desc_hid_report;
        case ITF_NUM_HID_REL_M:
            return desc_hid_report_relmouse;
        default:
            return desc_hid_report;
    }
}
```

（L79-L92）

普通模式下 vendor 接口不存在，但回调仍可被调用（主机可能按旧缓存请求），
返回默认报表描述符是保守选择。

## 4. 鼠标报告出口

```c
bool tud_mouse_report(uint8_t mode, uint8_t buttons, int16_t x, int16_t y, int8_t wheel, int8_t pan) {
    mouse_report_t report = {.buttons = buttons, .wheel = wheel, .x = x, .y = y, .mode = mode, .pan = pan};
    uint8_t instance = ITF_NUM_HID;
    uint8_t report_id = REPORT_ID_MOUSE;

    if (mode == RELATIVE) {
        instance = ITF_NUM_HID_REL_M;
        report_id = REPORT_ID_RELMOUSE;
    }

    return tud_hid_n_report(instance, report_id, &report, sizeof(report));
}
```

（L94-L105）

- `ABSOLUTE` → 接口 0 / ID 2；
- `RELATIVE` → 接口 1 / ID 5；
- 两者都用 8 字节 `mouse_report_t` 布局；
- 绝对/相对描述符由同一模板生成，只换 `ABS_OR_REL` 与逻辑最小值。

## 5. 配置描述符

普通模式（2 接口）：

```c
uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(ITF_NUM_HID,
                       STRID_PRODUCT,
                       HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(desc_hid_report),
                       EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE,
                       1),
```

（L221-L232）

配置模式（4 接口）额外：

| 接口 | 用途 | 端点 | 报表长 |
|------|------|------|--------|
| 2 | vendor | `0x83` IN | 12B |
| 3 | MSC | `0x04` OUT / `0x84` IN | 512B 块 |

端点地址：

| 宏 | 值 |
|----|----|
| `EPNUM_HID` | 0x81 |
| `EPNUM_HID_REL_M` | 0x82 |
| `EPNUM_HID_VENDOR` | 0x83 |
| `EPNUM_MSC_OUT` | 0x04 |
| `EPNUM_MSC_IN` | 0x84 |

## 6. 字符串描述符

```c
char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    "Hrvoje Cavrak",            // 1: Manufacturer
    "DeskHop Switch",           // 2: Product
    "0",                        // 3: Serials, should use chip ID
    "DeskHop Helper",           // 4: Mouse Helper Interface
    "DeskHop Config",           // 5: Vendor Interface
    "DeskHop Disk",             // 6: Disk Interface
```

（L113-L120，节选）

序列号索引 3 特殊处理：用 `pico_get_unique_board_id_string()` 读芯片唯一 ID，
而不是字符串表里的 "0"。这样两台电脑/两块板能区分设备实例。

`tud_descriptor_string_cb()` 把 ASCII 逐字符扩成 UTF-16LE，最多 31 字符，
首字节写长度+类型。

## 7. 坑点

1. **配置模式切描述符必须重新枚举**：`tud_descriptor_*_cb()` 只在枚举时调用；
   运行中改 `config_mode_active` 不会生效。
2. **端点地址不能冲突**：`0x81/0x82/0x83` 都是 IN，`0x04` OUT、`0x84` IN；
   改接口数量时同步改 `ITF_NUM_TOTAL` 与 `CONFIG_TOTAL_LEN`。
3. **`desc_hid_report` 的四段必须与 Report ID 一一对应**：漏一段主机会把后续
   字节当错误描述符，可能导致整个接口不工作。
4. **`tud_hid_descriptor_report_cb()` 的 default 返回接口 0 描述符**：
   对未知接口是一种容错，但也可能让主机误解析；新增接口要显式加 case。
5. **字符串描述符上限 31 字符**：`chr_count > 31` 截断；`_desc_str[32]` 刚好容纳。
6. **`DEVICE_DESCRIPTOR` 是宏不是函数**：`desc_device`/`desc_device_config` 是
   两个独立 const 结构体，不能运行时改字段。
