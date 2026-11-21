# deferred.h —— 延迟执行接口

对应源文件：`src/include/deferred.h`（共 28 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[01-启动流程与状态机](../../../references/firmware/ExpressLRS/Docs/01-启动流程与状态机.md)

---

## 1. 文件职责

声明“延迟执行队列”的三个入口。实现只有 3 个槽位，位于 `rxtx_common.cpp`。

---

## 2. 接口

| 函数 | 行号 | 说明 |
|---|---|---|
| `deferExecutionMicros(us, f)` | 23 | 微秒级延迟投递 |
| `executeDeferredFunction(now)` | 24 | 主循环每轮调用 |
| `deferExecutionMillis(ms, f)` | 26 | 毫秒封装，内部乘 1000 |

```c
static inline void deferExecutionMillis(unsigned long ms, std::function<void()> f)
{
    deferExecutionMicros(ms * 1000, f);
}
```

`static inline` 让每个 TU 都能内联展开，避免一次函数调用。

---

## 3. 使用约定

1. `deferExecutionMicros` 可在 ISR 调用（只写静态数组）；
2. `executeDeferredFunction` 只能在主循环调用（会执行回调）；
3. 回调里不要做阻塞操作（会拖慢整个 loop）；
4. 队列满时任务被丢弃（见实现里的 `DBGLN("No more deferred function slots available!")`）。

---

## 4. 实际用例

| 用例 | 延迟 | 原因 |
|---|---|---|
| LBT 拦截后补发 `TXdoneCallback` | `TOA` | 模拟正常发射完成的时序 |
| RX 进 WiFi 前等 ACK | 500ms | 让 StubbornSender 收尾 |
| 清上电计数 | 2000ms | 确认没连上才算一次“插拔” |
| 串口重配 | 100ms | 等当前帧发完 |

---

## 5. 设计要点与观察

1. **极简**：只有 3 个函数，没有取消接口（投递后不可撤销）。
2. **微秒为基**：毫秒版是薄封装，避免两套实现。
3. **std::function 开销**：捕获较多的 lambda 会堆分配，短 lambda 走 SBO。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 26 行 | `ms * 1000` 可能溢出（`unsigned long` 在 ESP8266 是 32 位，最大约 4294 秒） |
| 23 行 | 无返回值，投递失败无法感知 |

---

## 6. 互链

- 实现：[rxtx_common_cpp.md](rxtx_common_cpp.md)
- 使用：[tx_main.md](tx_main.md)、[rx_main.md](rx_main.md)

---

## 7. 槽位耗尽的真实场景推演

3 个槽位看起来很少，但把所有调用点列出来后会发现它够用：

| 并发场景 | 占用槽位 | 说明 |
|---|---|---|
| LBT 拦截发射 + RX 进 WiFi | 2 | 前者在 ISR 尾部投递，后者在 loop 投递 |
| LBT + 串口重配 + 清上电计数 | 3 | 恰好用满 |
| 四件事同时发生 | 1 个被丢 | 只打日志，不影响主流程 |

关键原因：**每个槽位在到期执行后立刻释放**，而绝大多数延迟是
TOA（几百微秒）或 500ms/2000ms 的一次性动作，很少叠加。

如果要扩容，最小改动是把 `maxDeferredFunctions` 改成 4 或 5，
并同步扩充 `deferred[]` 的初始化列表（实现文件 40 行）。

---

## 8. 与 RTOS 队列的对比

| 维度 | 本方案 | FreeRTOS 定时器队列 |
|---|---|---|
| 内存 | 3 × (4+4+std::function) 约 100 B | 队列 + 定时器服务任务栈，几 KB |
| 精度 | 依赖 loop 轮询间隔 | 依赖 tick（通常 1ms） |
| 取消 | 不支持 | `xTimerDelete` |
| 上下文 | 全部在 loop 执行 | 独立任务 |
| 适用 | 少量、一次性、不要求精度 | 大量、周期、要求精度 |

ELRS 选前者是因为它没有 RTOS，且延迟动作少、精度要求低（TOA 级别已经由
射频中断保证）。

---

## 9. 读码路径

1. 先看本文件（28 行）了解三个入口；
2. 跳到 `rxtx_common.cpp` 93 行看投递逻辑；
3. 回到 `tx_main.cpp` 的 `SendRCdataToRF()` LBT 分支，看“为什么需要延迟”；
4. 最后看 `rx_main.cpp` 的 `DataUlReceiveComplete()`，看 500ms 延迟如何
   解决“先 ACK 再切 WiFi”的竞态。

---

## 10. 实测建议

| 目的 | 方法 |
|---|---|
| 确认任务被执行 | 在回调里翻转一个空闲 GPIO，用示波器看延迟 |
| 确认没有丢任务 | 打开 `DEBUG_LOG`，搜索 “No more deferred function slots” |
| 测量 loop 延迟 | 在 `executeDeferredFunction()` 前后各翻转一次 GPIO |
