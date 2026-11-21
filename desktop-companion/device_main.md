# device_main.md —— ESP32 固件入口 app_main

> 对应源文件：`references/firmware/DesktopCompanion/device/src/main/main.c`（共 35 行，wc -l 计；36 行内容、文件尾无换行；UTF-8 / CRLF 行尾，含中文深注）
> 注释级别：A级（文件头导读块 + 函数头 Doxygen 块 + 行内注释）
> 关联文档：[06-设备端固件与通信协议](../../../references/firmware/DesktopCompanion/Docs/06-设备端固件与通信协议.md)
> 关联条目：[channel_base.md](channel_base.md)（未来 MQTT/串口通道的 host 侧对应物）

---

## 1. 文件职责

ESP-IDF 应用入口：`app_main()` 由 FreeRTOS 主任务调起，当前固件是"每秒打印 hello world"的最小验证程序，打通编译-烧录-monitor 串口链路。README 规划的 Host 串口协议（握手/指令/应答帧）尚未落码，本循环即被协议收发逻辑替换前的骨架。

## 2. 逐段表

| 行号 | 内容 | 说明 |
|---|---|---|
| 1-3 | 三个 include | stdio + FreeRTOS.h + task.h |
| 5-18 | `/* */` 文件导读块 | 职责/规划/构建烧录说明 |
| 20-21 | `void app_main(void) {` | IDF 应用入口 |
| 22-28 | `/** */` Doxygen 函数头 | brief/param/return/note |
| 29-35 | 主循环体 | printf + vTaskDelay |
| 36 | `}` | 文件结束（无尾换行） |

## 3. 关键摘录

### 3.1 主循环（main.c:29-35）

```c
    while (1) {
        // stdout 默认走 UART0 控制台，idf.py monitor 直接可见（IDF 默认 115200 波特）
        printf("hello world\n");
        // pdMS_TO_TICKS：毫秒→tick 换算，IDF 默认 tick 率 100Hz，1000ms=100 tick；
        // vTaskDelay 阻塞本任务并让出 CPU，喂给更低优先级任务与空闲任务
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
```

- `while (1)` 永不退出：若 `app_main` 返回，FreeRTOS 主任务会删除自身（IDF 允许），这里选择常驻为将来协议层留骨架；
- `printf` → stdout → 默认控制台 UART0（115200 8N1）——当前唯一的"输出协议"就是日志行；
- `pdMS_TO_TICKS(1000)`：IDF 默认 `configTICK_RATE_HZ=100`（每 tick 10ms），1000ms = **100 tick**；实际延时 ∈ [1000ms, 1010ms)（tick 粒度截断）。

### 3.2 与裸机的本质区别

`app_main` 不是 main()：ESP-IDF 先启动 ROM bootloader → second stage bootloader → FreeRTOS 调度器，再由**主任务**调起 `app_main`。因此这里可以直接调用 `vTaskDelay` 这类阻塞 RTOS API（裸机 main 里阻塞会让整个系统停摆）。

## 4. 构建与烧录

```cmake
# device/src/main/CMakeLists.txt
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ".")
```

```
cd device/src
idf.py set-target esp32
idf.py build flash monitor   # monitor 退出键 Ctrl+]（Ctrl+C 会触发复位）
```

工程级 CMake 要求 `cmake_minimum_required(VERSION 3.16)` + 按固定顺序 `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` → `project(src)`。

## 5. 规划中的协议层（未实现，详见 06 篇）

| 规划项 | 内容 |
|---|---|
| 物理层 | UART 115200 8N1，与控制台共用 |
| 帧格式 | 文本 JSON 行 + 换行定界：`{"id":"a1b2","cmd":"ping","arg":null}\n` |
| 应答 | 同 id 回填：`{"id":"a1b2","ok":true,"data":"pong"}\n` |
| 握手 | Host 发 ping，3s 超时重发共 3 次 |
| 日志兼容 | 非 JSON 行降级为日志处理（保住现有 printf 行为） |

## 6. 陷阱与观察

1. **CRLF + 无尾换行**：全文件 CRLF 行尾且文件末尾无换行——编辑保持原样（混入 LF 不影响编译但污染 diff）。
2. **UTF-8 中文注释**：本文件已含中文深注；确保编辑器 UTF-8 保存（GCC 对 UTF-8 注释无碍）。
3. **tick 率假设**：`pdMS_TO_TICKS` 的换算依赖 `configTICK_RATE_HZ`；调 tick 率时数值自动正确——用宏而非裸数字的意义所在。
4. **热循环 printf 开销**：115200 波特 ≈ 11.5KB/s，一行 12 字节约 1ms；将来高频打印换 `ESP_LOGI` + 级别过滤。
5. **无看门狗喂狗需求**：vTaskDelay 阻塞期间 IDLE 任务运行，任务看门狗（若启用）不会饿死——纯忙等 while 才会触发。

## 7. main.c 全文件逐行速查（36 行，wc -l=35 因无尾换行）

| 行 | 内容（缩略） | 类别 |
|---|---|---|
| 1-3 | 三个 include | 代码 |
| 4 | 空行 | — |
| 5-18 | 文件导读块 | 注释 |
| 19 | 空行 | — |
| 20-21 | app_main 定义 | 代码 |
| 22-28 | Doxygen 函数头 | 注释 |
| 29 | while (1) { | 代码 |
| 30 | UART0 console 说明 | 注释 |
| 31 | printf hello world | 代码 |
| 32-33 | tick 换算与让出 CPU | 注释 |
| 34 | vTaskDelay(100 tick) | 代码 |
| 35-36 | 两个收尾大括号 | 代码 |

代码 11 行 / 注释 25 行——骨架期的注释密度是有意为之：为协议层接班留下的施工说明。

## 8. FreeRTOS 启动时序（app_main 之前发生了什么）

```
上电 → ROM bootloader（判断启动模式）
     → second stage bootloader（加载分区表/应用镜像）
     → FreeRTOS 调度器启动（创建 main task，优先级 1，可配）
     → 组件初始化（GPIO/UART/文件系统等 per CONFIG）
     → app_main() 进入 ← 本文件从这里开始
```

app_main 的栈来自 main task（IDF 默认约 3.5KB 可配 CONFIG_ESP_MAIN_TASK_STACK_SIZE），当前 while 循环内的 printf 栈耗远低于上限。

## 9. 自测三问

1. app_main 返回会发生什么？（答：主任务删除自身，调度器继续跑——本工程选择永不返回）
2. pdMS_TO_TICKS(1000) 等于多少 tick？（答：IDF 默认 100Hz 下等于 100 tick，即 10ms/tick）
3. monitor 里怎么退出而不复位芯片？（答：Ctrl+]；Ctrl+C 是发中断信号，可能触发复位）
