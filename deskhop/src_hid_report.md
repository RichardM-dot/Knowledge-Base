# DeskHop 源码笔记：hid_report.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/hid_report.c`（共 647 行）

> 描述符字段路由 + 报文位提取 + 键盘三路径。与 `hid_parser.c` 成对：
> parser 负责“读懂描述符”，report 负责“把描述符变成字段并解报文”。

相关文档：[05-HID报告解析与转发](../../../references/firmware/DeskHop/Docs/05-HID报告解析与转发.md)

## 1. 函数总表

| 函数 | 行号 | 职责 |
|------|------|------|
| `get_report_value` | L52-L89 | 按位偏移/位宽提取有符号值 |
| `handle_consumer_control_values` | L105-L118 | 多媒体字段登记 |
| `handle_system_control_values` | L130-L143 | 系统键字段登记 |
| `is_modifier_descriptor` | L153-L159 | 是否 8 位修饰键位域 |
| `is_nkro_key_field` | L172-L191 | 是否 NKRO 位图 |
| `store_modifier` | L200-L204 | 存修饰键描述 |
| `store_key_field` | L216-L221 | 标记 ARRAY 键码字节 |
| `store_nkro_block` | L231-L265 | 存 NKRO 块 |
| `handle_keyboard_descriptor_values` | L274-L303 | 键盘字段总入口 |
| `handle_buttons` | L310-L325 | 鼠标按键字段 |
| `_store` | L332-L335 | 通用保存 |
| `get_mouse_id` | L344-L346 | 取 mouse.report_id 地址 |
| `get_consumer_id` | L353-L355 | 取 consumer.report_id 地址 |
| `get_system_id` | L362-L364 | 取 system.report_id 地址 |
| `report_receivers[]` | L367-L373 | receiver 枚举 → 函数指针 |
| `extract_data` | L395-L475 | usage_map 匹配与登记 |
| `extract_bit_variable` | L490-L517 | NKRO 位图取键 |
| `_extract_kbd_boot` | L520-L530 | boot 8B 直接拷贝 |
| `_extract_kbd_other` | L542-L572 | 按偏移手工抠 |
| `_extract_kbd_nkro` | L574-L601 | NKRO 位图路径 |
| `extract_kbd_data` | L622-L647 | 三路径总入口 |

## 2. 位提取

```c
int32_t get_report_value(uint8_t *report, int len, report_val_t *val) {
    /* Calculate the bit offset within the byte */
    uint16_t offset_in_bits = val->offset % 8;

    /* Calculate the remaining bits in the first byte */
    uint16_t remaining_bits = 8 - offset_in_bits;

    /* Calculate the byte offset in the array */
    uint16_t byte_offset = val->offset >> 3;

    if (byte_offset >= len)
        return 0;

    /* Create a mask for the specified number of bits */
    uint32_t mask = (1u << val->size) - 1;

    /* Initialize the result value with the bits from the first byte */
    int32_t result = report[byte_offset] >> offset_in_bits;

    /* Move to the next byte and continue fetching bits until the desired length is reached */
    while (val->size > remaining_bits && byte_offset + 1 < len) {
        result |= report[++byte_offset] << remaining_bits;
        remaining_bits += 8;
    }

    /* Apply the mask to retain only the desired number of bits */
    result = result & mask;   /* 只保留字段声明的位宽，多余高位清零 */

    /* Special case if our result is negative.
       Check if the most significant bit of 'val' is set */
    /* 符号扩展：字段最高位为 1 即负数（鼠标向左/上移动），高 32 位补 1 */
    if (result & ((mask >> 1) + 1)) {
        /* If it is set, sign-extend 'val' by filling the higher bits with 1s */
        result |= (0xFFFFFFFFU << val->size);
    }

    return result;
}
```

（L52-L89）

算法：右移对齐首字节 → 不足时逐字节左移拼接 → mask → 符号扩展。
复杂度 O(size/8)，典型 ≤2 次循环。`byte_offset >= len` 防越界。

**边界**：`1u << val->size` 在 size=32 时 UB；当前最大 16 位。

## 3. 字段路由

`extract_data()` 的 `usage_map[]` 共 8 条：

| # | page | global_usage | usage | handler | receiver |
|---|------|--------------|-------|---------|----------|
| 1 | Button | Mouse | — | `handle_buttons` | MOUSE |
| 2 | Desktop | Mouse | X | `_store` | MOUSE |
| 3 | Desktop | Mouse | Y | `_store` | MOUSE |
| 4 | Desktop | Mouse | Wheel | `_store` | MOUSE |
| 5 | Consumer | Mouse | AC Pan | `_store` | MOUSE |
| 6 | Keyboard | Keyboard | — | keyboard handler | KEYBOARD |
| 7 | Consumer | Consumer | — | consumer handler | CONSUMER |
| 8 | Desktop | System | — | `_store` | SYSTEM |

匹配规则：

```c
    for (const usage_map_t *hay = map; hay != &map[ARRAY_SIZE(map)]; hay++) {
        /* ---> If any condition is not defined, we consider it as matched <--- */
        bool global_usages_match = (val->global_usage == hay->global_usage) || (hay->global_usage == 0);
        bool usages_match        = (val->usage == hay->usage) || (hay->usage == 0);
        bool usage_pages_match   = (val->usage_page == hay->usage_page) || (hay->usage_page == 0);

        if (global_usages_match && usages_match && usage_pages_match) {
            /* Keyboards claim their slot in the handler, where the report ID is known. */
            if (hay->get_id != NULL)
                *(hay->get_id(iface)) = val->report_id;

            hay->handler(val, hay->dst, iface);

            iface->report_handler[val->report_id] = hay->receiver_id;
        }
    }
```

（L459-L474）

任一条件为 0 即通配；命中后写 report_id、调 handler、登记 receiver。
一个字段可能命中多条，后者覆盖前者，因此表顺序有语义。

## 4. 键盘字段分拣

`handle_keyboard_descriptor_values()`：

1. `get_or_add_keyboard(iface, report_id)` 按 report id 取/建槽；
2. CONSTANT 填充跳过；
3. `is_modifier_descriptor()` → 存 `modifier`；
4. `store_key_field()` → 标记 ARRAY 键码字节；
5. `store_nkro_block()` → 存 NKRO 位图块；
6. 首次登记置 `is_found` 并 `num_keyboards++`。

`is_nkro_key_field()` 的条件：usage 区间非空、usage 数 ≥ 位数；恰好相等最理想；
多出的 usage 只在位宽 ≥32 时容忍（避免把修饰键误判）。

## 5. 三条提取路径

```c
int32_t extract_kbd_data(
    uint8_t *raw_report, int len, uint8_t itf, hid_interface_t *iface, hid_keyboard_report_t *report) {
    keyboard_t *keyboard = get_keyboard(iface, raw_report[0]);

    /* Clear the report to start fresh */
    memset(report, 0, KBD_REPORT_LENGTH);

    /* If we're in boot protocol mode, then it's easy to decide. */
    if (iface->protocol == HID_PROTOCOL_BOOT)
        return _extract_kbd_boot(raw_report, len, report);

    /* NKRO is a special case. If extraction fails, fall through to other extractors. */
    if (keyboard->is_nkro) {
        int32_t ret = _extract_kbd_nkro(raw_report, len, iface, report);
        if (ret >= 0)
            return ret;
        memset(report, 0, KBD_REPORT_LENGTH);
    }

    /* If we're getting 8 bytes of report, it's safe to assume standard modifier + reserved + keys */
    if (!iface->uses_report_id && (len == KBD_REPORT_LENGTH || len == KBD_REPORT_LENGTH + 1))
        return _extract_kbd_boot(raw_report, len, report);

    /* This is something completely different, look at the report  */
    return _extract_kbd_other(raw_report, len, iface, report);
}
```

（L622-L647）

| 路径 | 条件 | 方法 |
|------|------|------|
| boot | `protocol==BOOT` | 8B 直接拷贝，9B 跳过 ID |
| NKRO | `keyboard->is_nkro` | 位图逐位转键码，失败降级 |
| 偏移 | 其他 | 按 modifier/key_array 手工抠 |

输出统一 `hid_keyboard_report_t`（8B 6KRO），后续模块只认这一种格式。

## 6. NKRO 位图

```c
int32_t extract_bit_variable(
    const nkro_block_t *block, const uint8_t *raw_report, int report_length, uint8_t *dst, int max_keys) {
    int key_count = 0;

    for (int block_bit = 0; block_bit < block->size_bits && key_count < max_keys; block_bit++) {
        int report_bit  = block->offset_bits + block_bit;
        int byte_index  = report_bit >> 3;
        int bit_index   = report_bit & 0b111;

        /* Report is shorter than the descriptor claims, don't read past the end of it */
        if (byte_index >= report_length)
            break;

        if (raw_report[byte_index] & (1 << bit_index)) {
            dst[key_count++] = (uint8_t)(block->usage_min + block_bit);
        }
    }

    return key_count;
}
```

（L490-L512 附近）

位为 1 → 键码 = `usage_min + 块内位序号`；最多填满 6 个槽；短报文 break。

## 7. 鼠标五元组

`process_mouse_report()`（`mouse.c` L441-L475）调用 `extract_report_values()`：

- boot 协议：按 `hid_mouse_report_t` 固定布局；
- report 协议：按 `mouse.move_x/move_y/wheel/pan/buttons` 的位偏移逐个提取；
- 字段缺失回退到上一次按键状态。

全零且按键未变时丢弃，防止 QMK 复合键盘的鼠标接口发干扰帧。

## 8. 坑点

1. **consumer 字段把 bit 偏移当数组下标**：`cc_array[src->offset]`，上限 16；
   前置填充位会导致部分多媒体键丢失。
2. **NKRO 启发式误判**：连续 usage 的多媒体位域可能被当 NKRO，输出错误键码。
3. **`extract_kbd_data()` 在 len=0 时读 `raw_report[0]`**：正常路径由
   `process_keyboard_report()` 的 `length<8` 挡住。
4. **`(1u << size)` 的 size=32 UB**：当前字段 ≤16 位，未触发。
5. **一个字段命中多条 map**：后者覆盖 `report_handler`；新增规则要注意顺序。
6. **`report_receivers[]` 只有 5 项**：`report_handler` 存的是 receiver 枚举（0..4），
   不会越界；若新增 receiver 枚举，必须同步扩数组。
