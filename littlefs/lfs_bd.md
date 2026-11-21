# lfs_bd —— 块设备驱动层（rambd / filebd / emubd）

对应源文件：`bd/lfs_rambd.c`（共 131 行）+ `bd/lfs_rambd.h`（99 行）+
`bd/lfs_filebd.c`（178 行）+ `bd/lfs_filebd.h`（95 行）+
`bd/lfs_emubd.c`（758 行）+ `bd/lfs_emubd.h`（263 行）；
三对文件强耦合于同一 `lfs_config` 回调形态，故合为一条。

三个模拟盘 = 移植的三级台阶：rambd 学接口、filebd 做镜像、
emubd 做掉电/坏块测试。深度解析见 `Docs/07-移植与实测指南.md`。

## 1. rambd：最小参考实现

| 函数 | rambd.c 行号 | 职责 |
| --- | --- | --- |
| `lfs_rambd_create` | 23 | 分配/接管 erase_size*erase_count 缓冲并清零 |
| `lfs_rambd_destroy` | 57 | 释放（静态缓冲不释放） |
| `lfs_rambd_read` | 68 | 地址换算 + memcpy（4 条契约断言） |
| `lfs_rambd_prog` | 88 | 同上（不校验已擦除——RAM 无 1→0 语义） |
| `lfs_rambd_erase` | 108 | no-op |
| `lfs_rambd_sync` | 123 | no-op |

**地址公式**：`offset = block*erase_size + off`。
移植真实驱动时照抄它的 4 条 LFS_ASSERT（块号/对齐/越界）。

## 2. filebd：POSIX 文件镜像

| 函数 | filebd.c 行号 | 职责 |
| --- | --- | --- |
| `lfs_filebd_create` | 29 | open(O_RDWR|O_CREAT) 镜像文件 |
| `lfs_filebd_destroy` | 63 | close |
| `lfs_filebd_read` | 76 | 先 memset 0（短读可复现）再 lseek+read |
| `lfs_filebd_prog` | 112 | lseek+write |
| `lfs_filebd_erase` | 145 | no-op |
| `lfs_filebd_sync` | 160 | fsync（Win: FlushFileBuffers） |

错误处理模式值得照抄：`err = -errno` 把 POSIX 错误翻成 littlefs 负值约定。

## 3. emubd：官方测试体系的地基

### 3.1 块管理：引用计数 + COW（emubd.c 行 32-92）

| 函数 | 行号 | 职责 |
| --- | --- | --- |
| `lfs_emubd_incblock` | 51 | 引用 +1 |
| `lfs_emubd_decblock` | 58 | 引用 -1，归零释放 |
| `lfs_emubd_mutblock` | 67 | rc==1 原地改；rc>1 先复制；无块分配 |

这让 `lfs_emubd_copy`（行 727）用 O(块数) 指针克隆整个盘（快照）。

### 3.2 四回调（行 97-541）

| 函数 | 行号 | 关键行为 |
| --- | --- | --- |
| `lfs_emubd_create` | 116 | 块指针数组 + 可选磁盘镜像 + 擦除值预填 |
| `lfs_emubd_destroy` | 208 | 引用计数释放 |
| `lfs_emubd_powerloss` | 235 | OOO 模式回滚第一笔写→调 cb→恢复 |
| `lfs_emubd_read` | 306 | 坏块 READERROR / 磨损统计 / nanosleep |
| `lfs_emubd_prog` | 356 | 坏块判定→**断言擦除态**→写入→镜像→掉电注入 |
| `lfs_emubd_erase` | 450 | OOO 保存第一笔写→磨损 +1→擦除值回填 |
| `lfs_emubd_sync` | 546 | 清 OOO 状态（sync 边界外不许乱序） |

### 3.3 测试扩展 API（行 546-738）

| 函数 | 行号 | 用途 |
| --- | --- | --- |
| `lfs_emubd_crc_/crc/bdcrc` | 565/590/599 | 块/全盘校验和（状态断言） |
| `lfs_emubd_readed/proged/erased` | 619/626/633 | IO 量统计（性能分析） |
| `lfs_emubd_set*` | 640-656 | 手工改计数（测试控制） |
| `lfs_emubd_wear/setwear` | 664/685 | 逐块磨损（均衡验证） |
| `lfs_emubd_powercycles/setpowercycles` | 706/715 | 掉电倒计时 |
| `lfs_emubd_copy` | 727 | COW 快照（分支宇宙） |

### 3.4 关键配置（lfs_emubd.h）

| 字段 | 行号 | 含义 |
| --- | --- | --- |
| `erase_value` | 105 | 擦除值（-1 不模拟；NAND 可设 0） |
| `erase_cycles` | 109 | 磨损阈值，超过即坏块 |
| `badblock_behavior` | 112 | 5 种坏块行为（含静默 PROGNOOP） |
| `power_cycles/behavior/cb` | 117-124 | 掉电注入三件套 |
| `track_branches` | 131 | 掉电分支跟踪（重内存） |
| `disk_path` | 135 | 镜像文件（可视化） |
| `*_sleep` | 139-147 | 人为延迟 |

## 4. 掉电实验模板（emubd 精髓）

```c
// power_cycles=1：第一次写就掉电，重挂载校验，循环加深
struct lfs_emubd_config bdcfg = {
    .read_size=16, .prog_size=16, .erase_size=4096, .erase_count=128,
    .erase_value=-1,
    .power_cycles=1,                       // 每次 prog/erase 后掉电
    .powerloss_behavior=LFS_EMUBD_POWERLOSS_NOOP,
    .powerloss_cb=my_powerloss_exit,       // longjmp/exit
};
```

掉电语义的关键：**prog/erase 之间允许乱序完成，sync 边界必须收敛**
（`lfs_emubd_sync` 清 OOO 状态）——这正是真实 flash 控制器的行为模型。

## 5. 互链

- 上游消费者：[lfs_fs_ops.md](lfs_fs_ops.md)（lfs_bd_* 缓存层调 cfg 回调）
- 接口定义：[lfs_h.md](lfs_h.md) `struct lfs_config`
- 深度：`Docs/07-移植与实测指南.md`（跑通步骤+掉电实验）、`Docs/00-总览与设计哲学.md`
