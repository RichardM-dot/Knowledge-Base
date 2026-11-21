# sfud_cfg.h —— 用户裁剪配置

> 对应源文件：`references/filesystems/SFUD/sfud/inc/sfud_cfg.h`（共 70 行，含 B 级导读注释块）
> 上游：armink/SFUD v1.1.0 出厂默认配置
>
> **互链：** [inc_sfud_def.md](inc_sfud_def.md)（宏的效果落在哪些结构） · [src_sfud.md](src_sfud.md)（设备表展开） · [port_sfud_port.md](port_sfud_port.md)（QSPI 配套移植项） · Docs：[00-总览与架构](../../../references/filesystems/SFUD/docs/00-总览与架构.md)（配置矩阵） · [05-实测与移植指南](../../../references/filesystems/SFUD/docs/05-实测与移植指南.md)

---

## 1. 文件职责

SFUD 唯一的"用户配置面板"：5 个功能开关 + 1 个设备表宏。每个开关决定编译进库的代码路径；`sfud.c` / `sfud_sfdp.c` 用 `#ifdef` 消费它们。B 级注释 = 文件头导读块（sfud_cfg.h:1-26），设备表多行宏内部不得插入注释（反斜杠续行）。

## 2. 逐宏表（行号为注释后实号）

| 宏 | 行号 | 默认 | 作用 | 关掉的影响 |
|---|---|---|---|---|
| `SFUD_DEBUG_MODE` | :51 | 开 | 调试日志 + SFUD_ASSERT 生效 | 省 ROM 数百 B；**断言同时失效**（未初始化调用不再被拦） |
| `SFUD_USING_SFDP` | :53 | 开 | 编入 SFDP 探测（sfud_sfdp.c 全文件） | 省 ~2KB ROM；老芯片识别只剩内置表 |
| `SFUD_USING_FAST_READ` | :55 | **关**（注释态） | sfud_read 改用 0x0B 快读 + 1 dummy 字节 | SPI 时钟慢时快读反而更慢（dummy 开销） |
| `SFUD_USING_FLASH_INFO_TABLE` | :57 | 开 | 编入 31 款芯片内置参数表 | 省 ~1KB；需配合 SFDP 或手填参数 |
| `SFUD_FLASH_DEVICE_TABLE` | :63 | 1 台占位 | 设备注册表（展开为 sfud.c 的 flash_table[]） | 未定义则 sfud.c `#error`（:56-58） |
| `SFUD_USING_QSPI` | :68 | 开 | 编入 qspi_read 挂钩 + QSPI 扩展信息表 | 省 ROM；QSPI 读不再多线提速 |

组合规则（README 2.3）：`SFUD_USING_SFDP` 与 `SFUD_USING_FLASH_INFO_TABLE` **至少开一个**；都不开则必须在设备表/设备对象里手填 chip 参数。资源占用官方基准：标准配置 RAM 0.2KB / ROM 5.5KB；最小配置 RAM 0.1KB / ROM 3.6KB。

## 3. 关键摘录

### 3.1 设备表宏（sfud_cfg.h:59-66）

```c
enum {
    SFUD_XXXX_DEVICE_INDEX = 0,
};

#define SFUD_FLASH_DEVICE_TABLE                                                \
{                                                                              \
    [SFUD_XXXX_DEVICE_INDEX] = {.name = "XXXX", .spi.name = "SPIX"},           \
}
```

要点：

- **指定初始化器** `[索引] = {...}`：只填 name 与 spi.name，其余字段全 0——"全 0"正是 `hardware_init` 判定"用户未手填芯片参数"的信号（见 [src_sfud.md](src_sfud.md) 3.2）；
- `spi.name` 是设备表与移植层 `sfud_spi_port_init` 之间的**接线号**；
- 多设备写法：枚举加下标、表加元素（README 2.3.6 的 W25Q64CV + GD25Q64B 双设备例）；
- 每个元素是一个 `sfud_flash`（约 100~130B RAM，开 SFDP/QSPI 更大），表的大小直接进 RAM。

### 3.2 出厂默认开关的矛盾点

默认全开（含 QSPI）但没有配 QSPI 移植层时，`sfud_read` 的快读分支不会被走到（`read_cmd_format.instruction` 保持 0x03），只是 ROM 多背了扩展表——最小化 ROM 时按需关。

## 4. 裁剪建议（按场景）

| 场景 | 推荐配置 |
|---|---|
| 现代芯片 + 省 ROM | 开 SFDP，关 FLASH_INFO_TABLE |
| 老芯片（如 SST25VF016B） | 开 FLASH_INFO_TABLE（+SFDP 无妨） |
| 量产 | 关 SFUD_DEBUG_MODE（assert 死循环风险） |
| 裸机单 Flash 最小 ROM | 关 SFDP+INFO_TABLE+QSPI，手填 chip 参数（README 2.3.5） |
| QSPI 提速 | 开 QSPI + 移植层 qspi_read + 初始化后调 sfud_qspi_fast_read_enable(flash, 4) |

## 5. 陷阱标注

1. 设备表宏是**多行宏**：续行符之间插注释会直接编译失败（STANDARD 红线同源）；
2. `SFUD_DEBUG_MODE` 关掉后 `SFUD_ASSERT` 变空宏——`flash->spi.wr` 未挂这类错误从"死循环可见"变成"空指针崩溃"，更难查；
3. 设备表元素数量与 `sfud_get_device_num()` 编译期绑定，运行期改不了。
