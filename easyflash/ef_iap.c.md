# KB: ef_iap.c —— IAP 在线升级辅助（B级导读；前轮已注，本轮未改）

对应源文件：`easyflash/src/ef_iap.c`（共 309 行；B级文件头导读块：职责 + 10 个对外 API 清单 + 在 OTA 数据流中的位置，strip_equiv PASS）

## 0. 定位与读法

IAP（In-Application Programming）工具箱：把固件包先下载到 Flash 备份区，校验后搬运到 App 区/Bootloader 区。只管"搬"，不管"校验"（CRC32 由调用方用 `ef_calc_crc32` 自算）。启用条件 `EF_USING_IAP`（默认关）。

## 1. 逐函数表（原始行号 = 注后行号，文件未改）

| 函数 | 行号 | 职责 | 要点 |
| --- | --- | --- | --- |
| `ef_iap_init` | L61 | 算备份区首址 | `EF_START_ADDR + ENV_AREA_SIZE + LOG_AREA_SIZE`（按启用模块累加）；纯地址运算 |
| `ef_erase_bak_app` | L84 | 清备份区 | size 由调用方给（一般=App 最大尺寸） |
| `ef_erase_spec_user_app` | L112 | 擦 App 区（自定义回调） | App 区可能不在 ef_port 管辖，允许传 app_erase |
| `ef_erase_user_app` | L140 | 擦 App 区（默认） | = spec 版 + ef_port_erase |
| `ef_erase_bl` | L152 | 擦 Bootloader | 直接 port 擦 |
| `ef_write_data_to_bak` | L181 | 分包写备份区 | **防越界裁剪**：`*cur_size+size > total_size` 时裁掉尾巴；cur_size 出入参接力=断点续传 |
| `ef_copy_spec_app_from_bak` | L215 | 备份区→App 区（自定义回调） | **128B 栈缓冲**（uint32_t buff[32]）逐块搬运；注意最后一块可能整块多搬（不裁剪，依赖 App 区可写 0xFF） |
| `ef_copy_app_from_bak` | L256 | 同上默认版 | 转发 |
| `ef_copy_bl_from_bak` | L268 | 备份区→Bootloader | 与 copy_app 同构，走 ef_port_write |
| `ef_get_bak_app_start_addr` | L305 | 查备份区首址 | 全模块共用 |

## 2. 关键摘录（逐字，原始行号）

### 2.1 备份区首址的叠加算术（L61-76）

```c
    bak_app_start_addr = EF_START_ADDR ;

#if defined(EF_USING_ENV)
    bak_app_start_addr += ENV_AREA_SIZE;
#endif

#if defined(EF_USING_LOG)
    bak_app_start_addr += LOG_AREA_SIZE;
#endif
```

与 ef_cfg.h 的布局图一致：IAP 备份区永远排在 ENV/LOG 之后。

### 2.2 分包写入的越界防护（L185-190）

```c
    /* make sure don't write excess data */
    if (*cur_size + size > total_size) {
        size = total_size - *cur_size;
    }

    result = ef_port_write(ef_get_bak_app_start_addr() + *cur_size, (uint32_t *) data, size);
```

### 2.3 128B 缓冲搬运循环（L223-232）

```c
    /* cycle copy data */
    for (cur_size = 0; cur_size < app_size; cur_size += sizeof(buff)) {
        app_cur_addr = user_app_addr + cur_size;
        bak_cur_addr = ef_get_bak_app_start_addr() + cur_size;
        ef_port_read(bak_cur_addr, buff, sizeof(buff));
        result = app_write(app_cur_addr, buff, sizeof(buff));
        if (result != EF_NO_ERR) {
            break;
        }
    }
```

## 3. 已知问题记录（不改码）

1. `ef_copy_spec_app_from_bak`/`ef_copy_bl_from_bak` 循环按 `sizeof(buff)`（128B）整块搬运，**最后一块不裁剪**：App 尺寸非 128 倍数时会多读多写尾部数据（读到备份区尾部 0xFF，写到 App 区尾部 0xFF）——NOR 上无害，但严格模式下依赖 App 区尾部可写；
2. `ef_write_data_to_bak` 裁剪后不返回"实际写入量"给调用方（只更新 cur_size）；
3. size/addr 均要求 4 字节对齐（port 层字操作），调用方保证。

## 4. 典型升级流程（配合使用）

```text
ef_erase_bak_app(128KB) → 循环 ef_write_data_to_bak(分包) → CRC 校验固件包
→ ef_erase_user_app → ef_copy_app_from_bak → 复位进 Bootloader 跳转
```

## 5. 与 demo 的对接（Ymodem 实测路径）

README 的 IAP 演示（串口+Ymodem）调用序列：

```text
终端发固件包 → Ymodem 接收循环:
    每收 1 包 → ef_write_data_to_bak(buf, len, &cur_size, fw_size)
                [cur_size 跨包接力 = 断点续传位标]
收完 → CRC32(ef_calc_crc32) 对比固件包尾校验
     → ef_erase_user_app(APP_ADDR, fw_size)
     → ef_copy_app_from_bak(APP_ADDR, fw_size)
     → NVIC_SystemReset() → Bootloader 校验后跳转
```

`cur_size` 由调用方持久化（如存一个 ENV）即可实现真断点续传——接口设计已经留好钩子。

## 6. 搬运缓冲的取舍

`uint32_t buff[32]`（128B）是栈上缓冲：加大可加速搬运（NOR 页大小 256B 可配 64 字），但栈预算增加；搬运全程不可中断（App 区正在被改写），**中断向量需在搬运前重定向到 RAM/Bootloader**（这是 IAP 通用问题，非本库职责）。

## 7. 互链

- API 表：Docs/[06-API 与实测指南](../../../references/filesystems/EasyFlash/Docs/06-API与实测指南.md) 1.4 节
- 布局依赖：[ef_cfg_h.md](ef_cfg_h.md)；CRC：[ef_utils.c.md](ef_utils.c.md)
