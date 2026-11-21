# sfud.c —— 设备管理 + 读写擦主逻辑

> 对应源文件：`references/filesystems/SFUD/sfud/src/sfud.c`（共 1332 行，含 A 级中文深注）
> 上游：armink/SFUD v1.1.0（`SFUD_SW_VERSION`，sfud_def.h:100）
>
> **互链：** [src_sfud_sfdp.md](src_sfud_sfdp.md)（SFDP 解析） · [port_sfud_port.md](port_sfud_port.md)（移植层） · [inc_sfud_def.md](inc_sfud_def.md)（结构与错误码） · Docs：[00-总览与架构](../../../references/filesystems/SFUD/docs/00-总览与架构.md) · [03-读写擦除全流程](../../../references/filesystems/SFUD/docs/03-读写擦除全流程.md) · [04-设备管理与多Flash](../../../references/filesystems/SFUD/docs/04-设备管理与多Flash.md)

---

## 1. 文件职责

SFUD 的"主控"：持有设备注册表 `flash_table[]`（sfud.c:67），实现初始化识别链（`sfud_init → sfud_device_init → hardware_init`）和读写擦三大引擎，并把每条操作翻译成 SPI 命令帧。对上暴露 `sfud.h` 的全部 API，对下只依赖 `sfud_spi` 函数指针与 `sfud_sfdp.c` 的两个探针函数。

## 2. 逐函数表（24 个函数）

| 函数 | 行号 | 可见性 | 一句话职责 |
|---|---|---|---|
| `sfud_device_init` | :126 | 公开 | 单设备初始化 = hardware_init + software_init，置 init_ok |
| `sfud_init` | :157 | 公开 | 遍历注册表逐个 device_init，绑定 index |
| `sfud_get_device` | :192 | 公开 | 按下标取设备，越界返回 NULL |
| `sfud_get_device_num` | :207 | 公开 | 设备总数（编译期常量） |
| `sfud_get_device_table` | :218 | 公开 | 注册表只读视图 |
| `qspi_set_read_cmd_format` | :238 | static | 组装 QSPI 快读格式（含 4B 地址指令迁移） |
| `sfud_qspi_fast_read_enable` | :285 | 公开 | 按线宽选最快快读指令（查扩展表） |
| `hardware_init` | :347 | static | 识别链中枢：ID→SFDP/内置表→复位→解保护→4B 模式 |
| `software_init` | :503 | static | 预留钩子，恒成功 |
| `sfud_read` | :531 | 公开 | 读引擎：越界检查→等忙→组帧→一次事务读回 |
| `sfud_chip_erase` | :606 | 公开 | 整片擦 0xC7（AT45DB 走 4 字节专有序列） |
| `sfud_erase` | :676 | 公开 | 扇区擦循环：选擦除器→WREN→擦→等忙→对齐推进 |
| `page256_or_1_byte_write` | :795 | static | 页编程引擎（写粒度 1 或 256） |
| `aai_write` | :900 | static | AAI 连续写引擎（SST 系） |
| `sfud_write` | :1005 | 公开 | 写入口：按 write_mode 分发 |
| `sfud_erase_write` | :1039 | 公开 | 先擦后写组合（非原子） |
| `reset` | :1057 | static | 0x66+0x99 两步软复位 |
| `read_jedec_id` | :1096 | static | 0x9F 读 3 字节身份证 |
| `set_write_enabled` | :1134 | static | WREN/WRDI + 回读校验 WEL |
| `set_4_byte_address_mode` | :1183 | static | 0xB7/0xE9 进出 4 字节地址模式 |
| `sfud_read_status` | :1228 | 公开 | 0x05 读状态寄存器 1 字节 |
| `wait_busy` | :1244 | static | 轮询 WIP 位直到空闲或超时 |
| `make_address_byte_array` | :1276 | static | 地址大端拆字节（3/4 字节） |
| `sfud_write_status` | :1307 | 公开 | 0x50/0x06 前导 + 0x01 写状态 |

## 3. 关键摘录与精读

### 3.1 设备注册表（sfud.c:62-77）

```c
/* user configured flash device information table */
static sfud_flash flash_table[] = SFUD_FLASH_DEVICE_TABLE;
```

由 `sfud_cfg.h` 的设备表宏展开，**非 const**（初始化回填 chip/sfdp/init_ok）。数组长度 = 设备数。

### 3.2 识别链的判定条件（hardware_init，sfud.c:370-373）

```c
    /* 四个关键参数全为 0 = 用户没有手填芯片参数 → 进入自动识别（读 ID + SFDP/内置表） */
    if (flash->chip.capacity == 0 || flash->chip.write_mode == 0 || flash->chip.erase_gran == 0
            || flash->chip.erase_gran_cmd == 0) {
```

"用户手填优先"就靠这个判断实现：设备表里 chip 四参数留 0 → 走自动识别；填了 → 跳过识别（但复位/解保护/4B 模式照做）。

### 3.3 SFDP→chip 参数翻译（sfud.c:378-409）

```c
            flash->chip.erase_gran = flash->sfdp.eraser[0].size;
            flash->chip.erase_gran_cmd = flash->sfdp.eraser[0].cmd;
            for (i = 1; i < SFUD_SFDP_ERASE_TYPE_MAX_NUM; i++) {
                if (flash->sfdp.eraser[i].size != 0 && flash->chip.erase_gran > flash->sfdp.eraser[i].size) {
```

擦除粒度取 SFDP 4 种擦除器里最小的（SFDP 侧已升序，此处再兜底扫描）。写粒度按 SFDP V1.0 的 1 字节/整页两档映射到 `SFUD_WM_BYTE`/`SFUD_WM_PAGE_256B`。

### 3.4 页编程切页算法（page256_or_1_byte_write，sfud.c:834-850）

```c
        if (addr % write_gran != 0) {
            if (size > write_gran - (addr % write_gran)) {
                data_size = write_gran - (addr % write_gran);   /* 先补齐当前页剩余空间 */
            } else {
                data_size = size;
            }
        }
```

每轮"补齐本页尾 → 整页 256B → 尾页余量"三段式。物理约束：页编程跨页回绕覆盖页首，必须软件切页。

### 3.5 wait_busy 与重试宏（sfud.c:1251-1261）

```c
    while (true) {
        result = sfud_read_status(flash, &status);
        if (result == SFUD_SUCCESS && ((status & SFUD_STATUS_REGISTER_BUSY)) == 0) {
            break;   /* WIP=0：芯片空闲，操作完成 */
        }
        /* retry counts */
        SFUD_RETRY_PROCESS(flash->retry.delay, retry_times, result);
    }
```

`SFUD_RETRY_PROCESS`（sfud_def.h:94）体内含 `break`，只能出现在循环内。超时是"次数上限"不是时间上限：10MHz、无 delay、times=10000 ≈ 16ms 上限——远小于整片擦 25s，移植层必须给大 times。

## 4. 已知缺口与隐式约定（只记录，不改码）

| # | 位置 | 现象 |
|---|---|---|
| 1 | sfud.c:801 `static cmd_data[261]` | 多设备并发写互踩共享命令缓冲 |
| 2 | sfud.c:1005 `sfud_write` DUAL_BUFFER 分支 | TODO 未实现，静默返回成功 |
| 3 | sfud.c:170-173 `sfud_init` | all_flash_result 只保留最后一次错误 |
| 4 | sfud.c:1307 `sfud_write_status` | 发完 0x01 不等忙，靠后续操作兜底 |
| 5 | sfud_def.h:77 `SFUD_ASSERT` | 失败 = while(1) 死循环，量产须关 DEBUG_MODE |
| 6 | sfud.c:1073 reset() | 第二条命令用 cmd_data[1] 单独发（0x66/0x99 必须分帧） |
