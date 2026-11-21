# DeskHop 源码笔记：mouse.c

对应源文件：`/Users/theo-richard/workspace/references/firmware/DeskHop/src/mouse.c`（共 518 行）

> 鼠标体验的核心：相对位移 → 虚拟绝对光标 → 越界切换 → 跨机/跨虚拟桌面。
> 另含加速度折线、Y 坐标比例映射、macOS workaround。

相关文档：[05-HID报告解析与转发](../../../references/firmware/DeskHop/Docs/05-HID报告解析与转发.md)、
[00-总览与双板架构](../../../references/firmware/DeskHop/Docs/00-总览与双板架构.md)

## 1. 函数总表

| 函数 | 行号 | 职责 |
|------|------|------|
| `get_jump_threshold` | L63-L72 | 本地屏切换无阈值，跨机用配置值 |
| `is_screen_switch_needed` | L84-L101 | 越界判定（含 threshold 余量） |
| `move_and_keep_on_screen` | L105-L122 | 夹紧到 [0,32767] |
| `calculate_mouse_acceleration_factor` | L124-L175 | 7 点折线插值，1.0~4.0 |
| `update_mouse_position` | L182-L207 | 位移 → 绝对坐标 + 方向 |
| `output_mouse_report` | L210-L219 | 本地队列或 UART |
| `scale_y_coordinate` | L224-L246 | 两屏高度不同时 Y 映射 |
| `switch_to_another_pc` | L260-L275 | 跨机切换动作序列 |
| `switch_virtual_desktop_macos` | L277-L308 | macOS 贴边 + 相对位移 |
| `switch_virtual_desktop` | L310-L347 | 本机多屏切换分派 |
| `do_screen_switch` | L349-L373 | 切换决策树 |
| `extract_value` | L376-L389 | 单字段提取 + ID 校验 |
| `extract_report_values` | L391-L415 | 鼠标五元组 |
| `create_mouse_report` | L417-L435 | 组 8B 绝对/相对报文 |
| `process_mouse_report` | L441-L475 | 鼠标主入口 |
| `process_mouse_queue_task` | L477-L510 | core0 出队上报 |
| `queue_mouse_report` | L512-L518 | 入队（未连接丢弃） |

## 2. 坐标系

```c
#define MAX_SCREEN_COORD 32767
#define MIN_SCREEN_COORD 0
```

（`src/include/screen.h`）

虚拟光标 `state->pointer_x/pointer_y` 是 `int16_t`，范围 0..32767。
真实鼠标的相对位移只用于更新它；对电脑发出的永远是绝对坐标（除非相对模式）。

`move_and_keep_on_screen()` 把结果夹在边界内：

```c
int32_t move_and_keep_on_screen(int position, int offset) {
    /* Lowest we can go is 0 */
    if (position + offset < MIN_SCREEN_COORD)
        return MIN_SCREEN_COORD;

    /* Highest we can go is MAX_SCREEN_COORD */
    else if (position + offset > MAX_SCREEN_COORD)
        return MAX_SCREEN_COORD;

    /* We're still on screen, all good */
    return position + offset;
}
```

（L105-L116）

## 3. 越界判定

```c
enum screen_pos_e is_screen_switch_needed(output_t *output, int position, int offset) {
    enum screen_pos_e direction = (offset < 0) ? LEFT : RIGHT;

    /* No position offset implies no switch needed. */
    if (offset == 0)
        return NONE;

    /* Local switches (virtual desktop changes) have no gap, only cross-output jumps use threshold */
    uint16_t threshold = get_jump_threshold(output, direction);

    if (position + offset < MIN_SCREEN_COORD - threshold)
        return LEFT;

    if (position + offset > MAX_SCREEN_COORD + threshold)
        return RIGHT;

    return NONE;
}
```

（L84-L101）

- `threshold` 来自 `config.jump_threshold`，默认 0；调大后需要“用力撞边”才切。
- 本地虚拟桌面切换不设阈值（`get_jump_threshold()` 返回 0），否则屏内切换会发涩。
- 判定用“位置 + 偏移”，不是当前位置；因此光标在边缘时继续推才会触发。

## 4. 加速度

```c
float calculate_mouse_acceleration_factor(int32_t offset_x, int32_t offset_y) {
    const struct curve {
        int value;
        float factor;
    } acceleration[ACCEL_POINTS] = {
                   // 4 |                                        *
        {2, 1},    //   |                                  *
        {5, 1.1},  // 3 |
        {15, 1.4}, //   |                       *
        {30, 1.9}, // 2 |                *
        {45, 2.6}, //   |        *
        {60, 3.4}, // 1 |  *
        {70, 4.0}, //    -------------------------------------------
    };             //        10    20    30    40    50    60    70
```

（L124-L140）

- 合位移 = `sqrtf(dx²+dy²)`，只看幅度不看方向；
- ≤2 → 1.0；≥70 → 4.0；中间线性插值；
- `enable_acceleration=0` 时恒 1.0；
- 慢速微调 1:1，快速甩动最高 4 倍。

`update_mouse_position()` 再乘用户速度档：

```c
    float acceleration_factor = calculate_mouse_acceleration_factor(values->move_x, values->move_y);
    int offset_x = round(values->move_x * acceleration_factor * (current->speed_x >> reduce_speed));
    int offset_y = round(values->move_y * acceleration_factor * (current->speed_y >> reduce_speed));
```

（L191-L193）

`mouse_zoom` 时 `reduce_speed=MOUSE_ZOOM_SCALING_FACTOR(2)`，速度 ÷4。

## 5. 切换决策树

```c
void do_screen_switch(device_t *state, int direction) {
    output_t *output = &state->config.output[state->active_output];

    /* No switching allowed if explicitly disabled or in gaming mode */
    if (state->switch_lock || state->gaming_mode)
        return;

    /* We want to jump in the direction of the other computer */
    if (output->pos != direction) {
        if (output->screen_index == 1) { /* We are at the border -> switch outputs */
            /* No switching allowed if mouse button is held. Should only apply to the border! */
            if (state->mouse_buttons)
                return;

            switch_to_another_pc(state, output, 1 - state->active_output, direction);
        }
        /* If here, this output has multiple desktops and we are not on the main one */
        else
            switch_virtual_desktop(state, output, output->screen_index - 1, direction);
    }

    /* We want to jump away from the other computer, only possible if there is another screen to jump to */
    else if (output->screen_index < output->screen_count)
        switch_virtual_desktop(state, output, output->screen_index + 1, direction);
}
```

（L349-L373）

决策表：

| 条件 | 动作 |
|------|------|
| `switch_lock` 或 `gaming_mode` | 不切 |
| 朝对侧方向 + 在主屏（index=1） | 跨机切换（按住鼠标键则不切） |
| 朝对侧方向 + 在副屏 | 退回上一个虚拟桌面 |
| 背离对侧 + 还有副屏 | 前进到下一个虚拟桌面 |
| 背离对侧 + 已在最后一个 | 不切 |

“按住鼠标键不跨机”是为了防止拖拽文件时误切换。

## 6. 跨机动作序列

```c
void switch_to_another_pc(
    device_t *state, output_t *output, int output_to, int direction) {
    uint8_t *mouse_park_pos = &state->config.output[state->active_output].mouse_park_pos;

    int16_t mouse_y = (*mouse_park_pos == 0) ? MIN_SCREEN_COORD : /* Top */
                      (*mouse_park_pos == 1) ? MAX_SCREEN_COORD : /* Bottom */
                                               state->pointer_y;  /* Previous */

    mouse_report_t hidden_pointer = {.y = mouse_y, .x = MAX_SCREEN_COORD};   /* 先把光标停到边缘停泊位 */

    output_mouse_report(&hidden_pointer, state);
    set_active_output(state, output_to);   /* 真正的切换点：此后键鼠数据流向另一台电脑 */
    state->pointer_x = (direction == LEFT) ? MAX_SCREEN_COORD : MIN_SCREEN_COORD;
    state->pointer_y = scale_y_coordinate(output->number, 1 - output->number, state);
}
```

（L260-L275）

1. 先把光标“藏”到旧屏边缘（`mouse_park_pos` 决定 Y）；
2. `set_active_output()` 切换并通知对端；
3. 新光标从对侧边缘进入，Y 按屏幕比例缩放。

`mouse_park_pos`：0=顶部、1=底部、其他=保持当前 Y。

## 7. Y 比例映射

`scale_y_coordinate()` 根据两个 output 的 `border.top/bottom` 计算：

- 高度相同 → 原样返回；
- 小屏 → 大屏：`top + size_to * y / 32767`；
- 大屏 → 小屏：先夹到 `[top,bottom]`，再 `(y-top)*32767/size_from`。

用于“两块屏幕高度不同，光标跨过去仍保持相同相对高度”。

## 8. macOS workaround

macOS 不认“绝对坐标跳到另一屏”，`switch_virtual_desktop_macos()`：

1. 发绝对坐标，把光标贴到屏幕边缘中间（Y=16383）；
2. 发 5 次相对位移（±10 像素），把光标“推”到下一屏；
3. 相对报文 `buttons=0`，避免拖拽状态被重复按下。

（L277-L308）

Windows 的 workaround 不同：`relative_mouse = (new_index > 1)`，切到副屏后退回相对模式。

## 9. 鼠标主入口

```c
void process_mouse_report(uint8_t *raw_report, int len, uint8_t itf, hid_interface_t *iface) {
    mouse_values_t values = {0};
    device_t *state = &global_state;

    /* Interpret the mouse HID report, extract and save values we need. */
    extract_report_values(raw_report, len, state, &values, iface);

    /* If nothing changed, don't send a report. ... */
    if (values.move_x == 0 && values.move_y == 0 &&
        values.wheel == 0 && values.pan == 0 &&
        values.buttons == state->mouse_buttons) {
        return;
    }

    /* Calculate and update mouse pointer movement. */
    enum screen_pos_e switch_direction = update_mouse_position(state, &values);

    /* Create the report for the output PC based on the updated values */
    mouse_report_t report = create_mouse_report(state, &values);

    /* Move the mouse, depending where the output is supposed to go */
    output_mouse_report(&report, state);

    /* We use the mouse to switch outputs, if switch_direction is LEFT or RIGHT */
    if (switch_direction != NONE)
        do_screen_switch(state, switch_direction);
}
```

（L441-L475）

顺序：提取 → 丢弃全零 → 更新虚拟坐标 → 组报文 → 发出 → 切换。
切换在“发出”之后：确保旧电脑先收到边缘位置，再切输出。

## 10. 坑点

1. **坐标是 `int16_t`，但逻辑范围 0..32767**：负值会溢出成 32768+，`move_and_keep_on_screen()` 已夹紧。
2. **`jump_threshold` 太大会“切不动”**：用户以为坏了，其实是需要更用力撞边。
3. **按住鼠标键不跨机**：拖拽文件时无法切屏，这是有意的；游戏模式也会锁死。
4. **macOS 的 5 次相对位移是经验值**：不同 macOS 版本/鼠标加速设置可能不够，表现为“偶尔切不过去”。
5. **Windows 多桌面依赖 `relative_mouse`**：切到副屏后必须退回相对模式，否则绝对坐标被 Windows 忽略。
6. **`switch_to_another_pc()` 先发隐藏光标再切换**：若顺序反了，旧电脑会看到光标“跳一下”。
7. **`queue_mouse_report()` 不检查活跃输出**：屏保依赖这一点给非活跃电脑发活动；
   普通输入路径由 `output_mouse_report()` 先判断方向，不会误发。
