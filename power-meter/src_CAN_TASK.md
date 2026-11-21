# src_CAN_TASK —— CAN 数据协议声明与发送任务实现

> 对应源文件：`src/CAN_TASK.h`（共 50 行，注释范围内，B 级文件头导读块，strip_equiv 校验 PASS）
> 关联实现：`src/CAN_TASK.c`（共 121 行，不在本次注释清单内、已有完整注释；因与头文件强耦合，行为解读并入本条目）
> 工程路径：`/Users/theo-richard/workspace/references/firmware/Power-Meter`

**互链 Docs：** [03-主循环与CAN任务](../../../references/firmware/Power-Meter/Docs/03-主循环与CAN任务.md)（逐行精读）、[05-中断系统与数据流总图](../../../references/firmware/Power-Meter/Docs/05-中断系统与数据流总图.md)、KB：[src_can](src_can.md)

---

## 1. 文件职责与位置

`CAN_TASK.h` 是 CAN 通道的**协议说明书**：文件头（:5-12）定义了 0x605 帧的数据布局，并声明三个函数。实现全部在 `CAN_TASK.c`：打包发送（:22-49）、滤波器初始化与启动（:62-103）、Bus-Off 软件恢复（:113-121）。数据流位置：int16 四元组 → 8 字节 CAN 帧的出口。

---

## 2. 数据协议（CAN_TASK.h:5-12）

```text
CAN 标准帧，标识符 0x605，DLC = 8
数据区依次为 4 个 int16 大端数（高字节在前）：
  [0..1] 分流电压（mV）
  [2..3] 总线电压（mV）
  [4..5] 电流（mA）
  [6..7] 功率（0.01 W）
```

| 字节 | 内容 | 单位 | 上限（int16） |
|---|---|---|---|
| 0..1 | 分流电压 | mV | ±32.767 V |
| 2..3 | 总线电压 | mV | ±32.767 V |
| 4..5 | 电流 | mA | ±32.767 A |
| 6..7 | 功率 | 0.01 W | ±327.67 W |

溢出即回绕（原工程既有行为）；VOFA 通道 float 不受影响。

---

## 3. 逐函数表（CAN_TASK.c 实现）

| 函数 | 行号 | 职责 | 调用者 |
|---|---|---|---|
| `CAN_send` | 22-49 | 帧头配置 + 大端打包 + `HAL_CAN_AddTxMessage` 入邮箱 | main.c:98（每 2 ms） |
| `CAN_Filter_Init` | 62-103 | DeInit/Init 重启 → 全通滤波器 → `HAL_CAN_Start` → 使能 RX FIFO0 与 TX 邮箱空中断 | main.c:172（一次） |
| `HAL_CAN_ErrorCallback` | 113-121 | ESR Bus-Off 位检查 → Stop/Start 软件恢复 | CAN 错误中断（HAL 弱函数重写） |
| `CAN_Send_INA226` | 仅声明 | 原工程未实现的预留接口 | 无（无人调用） |

---

## 4. 关键代码摘录

### 4.1 打包发送（CAN_TASK.c:30-48）

```c
    /* 帧头配置：标准帧、数据帧、DLC = 8 */
    tx.DLC   = 0x08;          /* 数据长度 8 字节 */
    tx.IDE   = CAN_ID_STD;    /* 标准标识符（11 位） */
    tx.RTR   = CAN_RTR_DATA;  /* 数据帧（非远程帧） */
    tx.StdId = stdid;         /* 标准帧 ID */
    tx.ExtId = 0x000;         /* 扩展帧 ID 不使用 */

    /* 4 个 int16 拆成 8 字节，高字节在前（大端） */
    Data[0] = ((num1) >> 8);
    Data[1] = (num1);
    Data[2] = ((num2) >> 8);
    Data[3] = (num2);
    Data[4] = ((num3) >> 8);
    Data[5] = (num3);
    Data[6] = ((num4) >> 8);
    Data[7] = (num4);

    HAL_CAN_AddTxMessage(&hcan, &tx, Data, &mailbox);
```

大端打包：`Data[偶数]` 取高 8 位（`>>8`），`Data[奇数]` 低 8 位（赋值截断）。STM32 内存小端，此处手动拆字节以匹配协议的大端约定。

### 4.2 全通滤波器（CAN_TASK.c:70-79）

```c
    sFilterConfig.FilterBank           = 0;                    /* 使用滤波器组 0 */
    sFilterConfig.FilterMode           = CAN_FILTERMODE_IDMASK;/* ID 掩码模式 */
    sFilterConfig.FilterScale          = CAN_FILTERSCALE_32BIT;/* 32 位宽度 */
    sFilterConfig.FilterIdHigh         = 0x0000;               /* 过滤 ID 高 16 位 */
    sFilterConfig.FilterIdLow          = 0x0000;               /* 过滤 ID 低 16 位 */
    sFilterConfig.FilterMaskIdHigh     = 0x0000;               /* 掩码高 16 位：全不关心 */
    sFilterConfig.FilterMaskIdLow      = 0x0000;               /* 掩码低 16 位：全不关心 */
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;         /* 匹配报文入 FIFO0 */
```

掩码模式收帧规则：`(帧ID & Mask) == (FilterId & Mask)`。Mask=0 ⇒ 任何 ID 都匹配 ⇒ 全部接收（调试期标准姿势；产品化应收窄，如 `Id=0x600, Mask=0x700` 只收 0x6xx 段）。

### 4.3 Bus-Off 软件恢复（CAN_TASK.c:113-121）

```c
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    /* 检查错误状态寄存器中的 Bus-Off 标志 */
    if (hcan->Instance->ESR & CAN_ESR_BOFF)
    {
        HAL_CAN_Stop(hcan);
        HAL_CAN_Start(hcan);
    }
}
```

硬件层 `AutoBusOff=ENABLE`（can.c:66）已能自动恢复；这里的 Stop→Start 是原工程保留的额外保险，两层机制并存。

---

## 5. 时序量化

| 项 | 数值 | 计算 |
|---|---|---|
| 发送频率 | 500 帧/s | 2 ms 周期 |
| 帧长 | ~111~135 bit | 8 字节标准帧含位填充 |
| 出线耗时 | ~0.13 ms | @1 Mbps，硬件并行 |
| 总线负载 | ~6.5% | 500 × ~130 bit ÷ 1 Mbps |
| `CAN_send` CPU 耗时 | ~10 µs | 12 个寄存器写入 |

---

## 6. 已知问题（记录在案，不改码）

| # | 问题 | 位置 |
|---|---|---|
| 1 | `CAN_send` 形参 `_hcan` 未使用，实现固定用全局 `hcan`（当前调用恰好都传 `&hcan`，行为一致） | CAN_TASK.c:48 |
| 2 | `CAN_Send_INA226()` 只有声明没有实现 | CAN_TASK.h:39-40 |
| 3 | RX FIFO0 中断使能但 `HAL_CAN_RxFifo0MsgPendingCallback` 未实现，收到的报文被丢 | CAN_TASK.c:93 侧 |
| 4 | TX 邮箱空中断使能但无业务（每秒 ~500 次空进） | CAN_TASK.c:99 侧 |
| 5 | `CAN_Filter_Init` 先 DeInit 再 Init，重复配置（无害保险动作） | CAN_TASK.c:65-66 |

---

## 7. 修改指引

| 想改 | 位置 |
|---|---|
| 发送 ID / 数据顺序 | main.c:98（ID）、CAN_TASK.c:37-44（打包顺序） |
| 收窄接收过滤 | CAN_TASK.c:73-76（FilterId/FilterMask） |
| 实现 CAN 接收 | 在 CAN_TASK.c 补 `HAL_CAN_RxFifo0MsgPendingCallback` |
| 修多实例传参 | CAN_TASK.c:48 改用形参 `_hcan`（改前全工程核对调用点） |
