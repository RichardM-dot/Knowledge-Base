# u8g2_d_setup.c —— 屏幕装配总表（生成代码）

> 对应源文件：`csrc/u8g2_d_setup.c`（共 10019 行）
> 注释分级：**B 级（文件头导读块）**，strip_equiv_check PASS
> 代码性质：由 `tools/codebuild` 自动生成，**不要手工编辑**
> 上游：单色屏图形库 u8g2（olikraus）
>
> 姊妹条目：[u8g2_d_memory.md](u8g2_d_memory.md) | [u8g2_setup.md](u8g2_setup.md) |
> [u8g2_h.md](u8g2_h.md)
> 深度文章：[Docs/03-显示控制器与通信.md](../../../references/graphics/u8g2/Docs/03-显示控制器与通信.md)

---

## 1. 一句话定位

**1104 个 `u8g2_Setup_xxx()` 装配函数**，覆盖 84 个控制器族、368 个型号
（368 × 3 种内存模式 = 1104）。用户写下的第一行 u8g2 代码几乎总是从这里挑一个
函数。文件本身没有任何算法——全部是同模板的复制，**读法不是通读，而是
"按命名规则定位一个型号，跟一遍三步模板"**。

## 2. 命名规则拆解

```text
u8g2_Setup_<controller>_<comm>_<wxh>_<variant>_<mode>
             │            │      │       │        └─ 1 / 2 / f（内存模式）
             │            │      │       └─ 变体：noname / vcomh0 / alt0 / winstar…
             │            │      └─ 分辨率：128x64 / 64x48 / 256x32…
             │            └─ 通信：i2c / hw_i2c / sw_i2c / spi / p（并口）/ s（串行）
             └─ 控制器族：ssd1306 / sh1106 / st7920 / uc1701 / max7219…
```

实例：

```text
u8g2_Setup_ssd1306_i2c_128x64_noname_f
     │        │      │    │      │    └─ 全缓冲
     │        │      │    │      └─ 无特殊变体
     │        │      │    └─ 128x64 像素
     │        │      └─ I2C 接口
     │        └─ SSD1306 控制器
     └─ u8g2 装配函数前缀
```

**模式后缀的三个值**对应 `u8g2_d_memory.c` 的三个分配函数：

| 后缀 | `tile_buf_height` | 128x64 RAM | 语义 |
|---|---|---|---|
| `_1` | 1 | 128 B | 单页 |
| `_2` | 2 | 256 B | 双页 |
| `_f` | tile_height | 1024 B | 全缓冲 |

## 3. 三步模板

每个函数体完全一致，以 `u8g2_Setup_ssd1306_i2c_128x64_noname_f`（L387~L394，
逐字摘录）为例：

```c
void u8g2_Setup_ssd1306_i2c_128x64_noname_f(u8g2_t *u8g2, const u8g2_cb_t *rotation, u8x8_msg_cb byte_cb, u8x8_msg_cb gpio_and_delay_cb)
{
  uint8_t tile_buf_height;
  uint8_t *buf;
  u8g2_SetupDisplay(u8g2, u8x8_d_ssd1306_128x64_noname, u8x8_cad_ssd13xx_fast_i2c, byte_cb, gpio_and_delay_cb);
  buf = u8g2_m_16_8_f(&tile_buf_height);
  u8g2_SetupBuffer(u8g2, buf, tile_buf_height, u8g2_ll_hvline_vertical_top_lsb, rotation);
}
```

三行分别决定：

| 行 | 决定 | 落到哪 |
|---|---|---|
| `u8g2_SetupDisplay(...)` | display_cb + cad_cb + 用户的 byte/gpio | `u8x8_Setup` → `u8x8_t` 四回调 |
| `u8g2_m_16_8_f(...)` | 缓冲大小 + `tile_buf_height` | `u8g2_d_memory.c` 的静态数组 |
| `u8g2_SetupBuffer(...)` | `ll_hvline` 位序 + 旋转 cb + 窗口 | `u8g2_setup.c:99` |

**四个参数里的 `byte_cb` 与 `gpio_and_delay_cb` 是用户移植点**，其余全部写死。

## 4. 典型型号对照表（已核对源码）

| Setup 函数 | 行号 | display_cb | cad_cb | ll_hvline |
|---|---|---|---|---|
| `ssd1306_128x64_noname_f` | L311 | `u8x8_d_ssd1306_128x64_noname` | `cad_001` | `vertical_top_lsb` |
| `ssd1306_i2c_128x64_noname_f` | L387 | 同上 | `cad_ssd13xx_fast_i2c` | `vertical_top_lsb` |
| `sh1106_i2c_128x64_noname_f` | L755 | `u8x8_d_sh1106_128x64_noname` | `cad_ssd13xx_fast_i2c` | `vertical_top_lsb` |
| `st7920_p_256x32_f` | L3447 | `u8x8_d_st7920_256x32` | `cad_001` | `horizontal_right_lsb` |
| `st7920_s_128x64_f` | L3923 | `u8x8_d_st7920_128x64` | `cad_st7920_spi` | `horizontal_right_lsb` |
| `st7565_ea_dogm128_f` | L5723 | `u8x8_d_st7565_ea_dogm128` | `cad_001` | `vertical_top_lsb` |
| `max7219_64x8_f` | L9571 | `u8x8_d_max7219_64x8` | `cad_empty` | `horizontal_right_lsb` |
| `ssd1606_172x72_f` | L9191 | `u8x8_d_ssd1606_172x72` | `cad_011` | `vertical_top_lsb` |

**同一个控制器换通信方式，只换 cad 与 display_cb 的组合，ll_hvline 通常不变。**
这是"控制器差异"与"总线差异"正交设计的直接体现。

## 5. 通信后缀统计

| 后缀 | 出现次数（含 3 种模式） | 含义 | 典型 cad |
|---|---|---|---|
| `_i2c_` | 366 | 软件/硬件 I2C | `ssd13xx_i2c` / `fast_i2c` / `uc16xx_i2c` |
| `_p_` | 18 | 并口（8080/6800） | `cad_001` |
| `_s_` | 18 | 串行（SPI） | `cad_st7920_spi` 等 |
| 无后缀 | 其余 | 由用户自选 byte_cb | 多为 `cad_001` |

注意 `ssd1306_128x64_noname_f`（无 `i2c`/`spi`）并不是"没有接口"，而是
**把接口选择完全交给用户的 `byte_cb`**：传 `u8x8_byte_4wire_sw_spi` 就是 SPI 屏，
传 `u8x8_byte_sw_i2c` 就是 I2C 屏（但此时 cad 是 `cad_001`，I2C 需要
`cad_ssd13xx_i2c`，所以这种混搭通常不对）。**带 `_i2c_` 的版本才是 I2C 的正确答案。**

## 6. 如何快速定位

```bash
# 找 128x64 的 SSD1306 SPI 全缓冲
grep -n "^void u8g2_Setup_ssd1306_.*128x64.*_f(" csrc/u8g2_d_setup.c

# 找所有 64x48 的型号
grep -n "^void u8g2_Setup_.*64x48" csrc/u8g2_d_setup.c

# 找某个控制器支持哪些通信方式
grep -oE "^void u8g2_Setup_sh1106_[a-z0-9]+" csrc/u8g2_d_setup.c | sort -u
```

声明在 `csrc/u8g2.h` 的"生成代码声明区 1"（L560~L1900），需要包含头文件才能调用。

## 7. 设计要点

1. **用"函数名"代替"配置结构体"**：选型在编译期完成，运行时零分支、零字符串。
   代价是 1104 个符号 + 一个巨大的头文件。
2. **生成代码保证一致性**：手写 1104 个函数必然出现漏字段，生成器保证每个
   Setup 都调同样的三步。
3. **模式与型号正交**：`_1/_2/_f` 与控制器无关，任何型号都有三种 RAM 档位。
4. **`rotation` 是参数不是编译期常量**：同一份 Setup 可在运行期换 R0~R3。

## 8. 已知坑（读码发现，未改代码）

1. **无 `_i2c_` 后缀 ≠ 通用**。它默认配 `cad_001`（DC 引脚协议），直接接 I2C 屏
   不会工作。
2. **`_f` 不是"快"**，是"full"。有些新手以为 `_f` 是 fast，选了它反而多占 8 倍 RAM。
3. **`noname` 表示"无品牌通用初始化序列"**，不是"没有名字"；带品牌名的变体
   （`vcomh0`、`alt0`、`winstar`）只改一两条初始化命令。
4. **同一控制器可能有多份 display_info**（如 SH1106 的 `default_x_offset=2`），
   选错型号会整体偏移 2 列。
5. **`u8g2_Setup_null` 不在本文件**，在 `u8g2_setup.c:595`。

## 9. 互链

- 缓冲从哪来：[u8g2_d_memory.md](u8g2_d_memory.md)
- 装配细节：[u8g2_setup.md](u8g2_setup.md)
- 声明位置：[u8g2_h.md](u8g2_h.md)
- 通信层全景：[Docs/03-显示控制器与通信.md](../../../references/graphics/u8g2/Docs/03-显示控制器与通信.md)
