# helpers.h —— 通用小工具

对应源文件：`src/include/helpers.h`（共 60 行）

> 工程：ExpressLRS ｜ 层级：B 级（文件头导读）
> 相关文档：[00-总览与双链路架构](../../../references/firmware/ExpressLRS/Docs/00-总览与双链路架构.md)

---

## 1. 文件职责

三个几乎每个文件都会用到的小工具：

1. `ARRAY_SIZE(a)` —— 编译期数组长度；
2. `UNUSED(x)` —— 消警；
3. `NullStream` —— 丢弃所有写入的 Stream 实现。

---

## 2. 宏

```c
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif
```

| 宏 | 行号 | 用途 | 注意 |
|---|---|---|---|
| `ARRAY_SIZE` | 21 | `devicesRegister(ui_devices, ARRAY_SIZE(ui_devices))` | 只对真数组有效，指针会算错 |
| `UNUSED` | 24 | 消除 `-Wunused-parameter` | 不改变代码生成 |

`ARRAY_SIZE` 的经典陷阱：函数参数写成 `device_t arr[]` 时它退化成指针，
`sizeof` 得到 4/8，结果恒为 1。ELRS 里所有调用点都传真数组，安全。

---

## 3. `NullStream`（27 行）

```c
class NullStream : public Stream
{
  public:
    int available() override
    {
        return 0;
    }
    ...
    size_t write(uint8_t u_Data) override
    {
        UNUSED(u_Data);
        return 0x01;
    }

    size_t write(const uint8_t *buffer, size_t size) override
    {
        UNUSED(buffer);
        return size;
    }
};
```

| 方法 | 返回 | 语义 |
|---|---|---|
| `available()` | 0 | 永远没有数据 |
| `peek()` | -1 | 无数据 |
| `read()` | -1 | 无数据 |
| `flush()` | — | 空操作 |
| `write(uint8_t)` | 1 | 假装成功 |
| `write(buf,size)` | size | 假装全部写入 |

**为什么重要**：`TxUSB`、`BackpackOrLogStrm` 这两个指针在代码里被无条件解引用
（`TxUSB->write(...)`）。在没有物理串口的硬件上指向 `NullStream`，就不需要
在每个调用点判空。代价是调用方无法感知数据被丢弃。

---

## 4. 使用场景

| 场景 | 文件:行 | 用的哪个 |
|---|---|---|
| TX 无 USB（内置模块） | `tx_main.cpp` `setupSerial()` | `NullStream` |
| TX 无背包 | `tx_main.cpp` 同上 | `NullStream` |
| RX 无日志 | `rx_main.cpp` `setupSerial()` | `NullStream` |
| 设备表长度 | 两侧 `setup()` | `ARRAY_SIZE` |
| 调试参数未用 | 各处 | `UNUSED` |

---

## 5. 设计要点与观察

1. **零分配**：`NullStream` 是空类，`new NullStream()` 只占一个字节（加 vtable 指针）。
2. **必须 override**：`Stream` 的纯虚函数全部实现，否则编译失败——这保证了
   接口完整性。
3. **`write` 返回 size 而非 0**：很多库用返回值判断“是否需要重试”，返回 0 会
   导致死循环；返回 size 模拟“成功”。

### 已知问题（只记录）

| 位置 | 现象 |
|---|---|
| 21 行 | `ARRAY_SIZE` 对指针参数失效，无编译期保护 |
| 49 行 | 单字节 `write` 返回 1，与批量版语义一致 |

---

## 6. 互链

- 使用：[tx_main.md](tx_main.md)、[rx_main.md](rx_main.md)
- 平台差异：[05-收发机硬件抽象与目标配置](../../../references/firmware/ExpressLRS/Docs/05-收发机硬件抽象与目标配置.md)
