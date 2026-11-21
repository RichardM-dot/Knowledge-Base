# DeskHop 源码笔记：hid_parser.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/hid_parser.c`（共 352 行）

> HID 报表描述符解析器：把字节码条目解析成“字段位偏移 + usage + 类型”，
> 再交给 `hid_report.c:extract_data()` 登记。

相关文档：[05-HID报告解析与转发](../../../references/firmware/DeskHop/Docs/05-HID报告解析与转发.md)

## 1. 函数总表

| 函数 | 行号 | 职责 |
|------|------|------|
| `get_descriptor_value` | L51-L60 | 按 0/1/2/4B 取小端值 |
| `get_or_create_report_offset` | L73-L91 | 每个 report id 的位偏移账本 |
| `get_current_offset` | L95-L101 | 读当前偏移（隐式建账） |
| `get_usage` | L109-L127 | 取第 i 个字段的 usage |
| `store_element` | L140-L162 | 全局+局部+偏移 → `report_val_t` |
| `handle_global_item` | L171-L178 | GLOBAL 条目入 `globals[16]` |
| `handle_local_item` | L190-L208 | LOCAL usage 入队/记 global_usage |
| `handle_main_input` | L221-L263 | 逐字段登记并推进偏移 |
| `handle_main_item` | L276-L295 | MAIN 分发 + 清 LOCAL |
| `parse_report_descriptor` | L321-L352 | 主循环 |

## 2. 条目格式

| 头部位段 | 宽度 | 含义 |
|----------|------|------|
| `size` | 2 | 0→0B，1→1B，2→2B，3→4B |
| `type` | 2 | 0 Main，1 Global，2 Local |
| `tag` | 4 | 语义 |

`SIZE_LOOKUP[4] = {0,1,2,4}` 把 size 编码翻成字节数。注意 3 表示 4 字节。

```c
uint32_t get_descriptor_value(uint8_t const *report, int size) {
    switch (size) {
        case SIZE_8_BIT:
            return report[0];
        case SIZE_16_BIT:
            return tu_u16(report[1], report[0]);
        case SIZE_32_BIT:
            return tu_u32(report[3], report[2], report[1], report[0]);
        default:
            return 0;
    }
}
```

（L51-L60）

用 `tu_u16/tu_u32` 而不是指针强转：避免非对齐访问与别名问题。

## 3. 偏移账本

```c
uint32_t *get_or_create_report_offset(parser_state_t *parser, uint8_t report_id) {
    for (int i = 0; i < parser->num_report_offsets; i++) {
        if (parser->report_offsets[i].report_id == report_id) {
            return &parser->report_offsets[i].offset_in_bits;
        }
    }

    if (parser->num_report_offsets < MAX_REPORTS_PER_IFACE) {
        parser->report_offsets[parser->num_report_offsets].report_id = report_id;
        parser->report_offsets[parser->num_report_offsets].offset_in_bits = 0;
        return &parser->report_offsets[parser->num_report_offsets++].offset_in_bits;
    }

    return NULL;
}
```

（L73-L91）

同一接口的不同 report id 各自从 bit 0 排布，必须分开记账。上限 24，超出返回 NULL，
`handle_main_input()` 放弃该字段（不越界）。

## 4. 字段生成

```c
void handle_main_input(parser_state_t *parser, item_t *item, hid_interface_t *iface) {
    uint32_t size  = parser->globals[RI_GLOBAL_REPORT_SIZE].val;
    uint32_t count = parser->globals[RI_GLOBAL_REPORT_COUNT].val;
    report_val_t val = {0};

    /* Swap count and size for 1-bit variables, it makes sense to process e.g. NKRO with
       size = 1 and count = 240 in one go instead of doing 240 iterations
       Don't do this if there are usages in the queue, though.
       */
    if (size == 1 && parser->usage_count <= 1) {
        size  = count;
        count = 1;
    }
```

（L221-L233）

- `Report Size × Report Count` 决定字段总位宽；
- size=1 且最多 1 个 usage → 交换，240 位 NKRO 位图一次登记；
- 若描述符为每位声明了多个 usage（修饰键），不交换，逐字段处理。

```c
    for (int i = 0; i < count; i++) {
        store_element(parser, &val, get_usage(parser, i), item->val, size, iface);

        /* Use the parsed data to populate internal device structures */
        extract_data(iface, &val);

        /* Iterate <count> times and increase offset by <size> amount, moving by <count> x <size> bits */
        *current_offset += size;
    }
```

（L239-L247）

`item->val` 的低两位是 Input 语义：bit0=Constant，bit1=Variable。
`store_element()` 把它翻译成 `item_type`/`data_type`。

## 5. usage 语义

`handle_local_item()` 分两种情况：

- 出现在 Collection 结束处（`IS_BLOCK_END`）→ 记为 `global_usage`，代表设备大类；
- 出现在 Collection 内部 → 压入 usage 队列，供下一个 Main 条目消费。

队列上限 128，满则丢弃；`get_usage()` 在 i 超界时重复最后一个 usage（HID 规范）。

处理完 Main 后游标前进并把最后一个 usage 复制到新位置：

```c
    if (parser->p_usage + parser->usage_count < parser->usages + HID_MAX_USAGES) {
        parser->p_usage += parser->usage_count;

        /* Carry the last usage of this block to the new location */
        *parser->p_usage = *(parser->p_usage - 1);
    } else {
        parser->p_usage = parser->usages + HID_MAX_USAGES - 1;
    }
```

（L255-L262）

这样下一个 Main 没声明 usage 时能继承。

## 6. 主循环

```c
void parse_report_descriptor(hid_interface_t *iface,
                            uint8_t const *report,
                            int desc_len
                            ) {
    item_t item = {0};

    /* Wipe parser_state clean */
    memset(&parser_state, 0, sizeof(parser_state_t));
    parser_state.p_usage = parser_state.usages;

    while (desc_len > 0) {
        item.hdr = *(header_t *)report++;
        item.val = get_descriptor_value(report, item.hdr.size);

        switch (item.hdr.type) {
            case RI_TYPE_MAIN:
                handle_main_item(&parser_state, &item, iface);
                break;

            case RI_TYPE_GLOBAL:
                handle_global_item(&parser_state, &item);
                break;

            case RI_TYPE_LOCAL:
                handle_local_item(&parser_state, &item);
                break;
        }
        /* Move to the next position and decrement size by header length + data length */
        report += SIZE_LOOKUP[item.hdr.size];
        desc_len -= (SIZE_LOOKUP[item.hdr.size] + 1);
    }
}
```

（L321-L352）

解析器状态是全局单例 `parser_state`（L303），约 800B，放静态区避免占栈。

## 7. 数据结构

`report_val_t`（`hid_parser.h` L97-L112）是解析结果的核心：

| 字段 | 含义 |
|------|------|
| `offset` | 字段起始位（相对 payload，不含 report id） |
| `offset_idx` | `offset >> 3`，字节偏移 |
| `size` | 字段位宽 |
| `usage_min/max` | usage 区间（NKRO 判定用） |
| `item_type` | DATA/CONSTANT |
| `data_type` | VARIABLE/ARRAY |
| `report_id` | 所属报表 |
| `usage_page`/`usage`/`global_usage` | 语义 |

## 8. 坑点

1. **`desc_len` 无下溢/剩余长度检查**：最后一条目声明超长数据时会越界读描述符缓冲。
   加固：`desc_len -= ...` 前检查 `desc_len >= 1 + data_len`。
2. **size=3 表示 4 字节**，不是 3；写新条目解析时容易错。
3. **NKRO 交换条件依赖 `usage_count<=1`**：若厂商为 240 个键各声明一个 usage，
   不交换，逐字段循环 240 次，解析变慢但结果仍正确（`get_usage` 逐个取）。
4. **`globals[tag]` 混放不同 tag**：取用时必须知道自己在取哪个 tag，
   例如 `RI_GLOBAL_REPORT_SIZE`。
5. **`parser_state` 非线程安全**：只在 core1 的 mount 回调里调用，不存在重入；
   若未来在 core0 也解析，需要加锁或改成实例化。
6. **usage 队列用尽后钳在最后一格**：极复杂描述符会丢失后续 usage 区分度，
   表现为多媒体键错乱。
