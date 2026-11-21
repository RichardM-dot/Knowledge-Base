# KB: ef_utils.c —— CRC32 校验工具（A级深注）

对应源文件：`easyflash/src/ef_utils.c`（原始 99 行，注释深注后 134 行；A级逐函数中文深注，strip_equiv PASS）

## 0. 定位与读法

全库最小的源文件，却握着掉电安全的钥匙：一张 256 项 CRC32 查表 + 一个流式计算函数 `ef_calc_crc32`。ENV 节点校验、legacy 区校验、IAP 固件包校验全部经由它。

## 1. 逐函数表

| 函数 | 行号(注后) | 职责 | 复杂度 | 依赖 |
| --- | --- | --- | --- | --- |
| `ef_calc_crc32` | L121（表 L35-80） | 流式计算 CRC-32/ISO-HDLC | O(size)，每字节 1 查表+1 移位 | 无（仅 const 表） |

## 2. 关键摘录（逐字，注后行号）

### 2.1 核心循环（注后 L125-133）

```c
    p = (const uint8_t *)buf;
    crc = crc ^ ~0U; /* 前置条件化：crc=0 -> 寄存器初值 0xFFFFFFFF */

    while (size--) {
        /* 查表核心：低 8 位异或当前字节选出表项，寄存器右移 8 位吞掉该字节 */
        crc = crc32_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ ~0U; /* 后置条件化：得到最终校验值 */
```

三个设计点：

1. **反射多项式 0xEDB88320**：与 zlib/以太网同款，表项是编译期常量（.rodata，1KB 占 Flash 不占 RAM）；
2. **流式接口**：首次 `crc=0`，续算传上次返回值——`ef_env.c` 的 `read_env` 用 32B 栈缓冲分段算整条 KV 的 CRC（ef_env.c L569-578 的 for 循环，实测 569-578），靠的就是这个接口；
3. **双重 `^ ~0U`**：标准 CRC-32 的初值/余数条件化，保证空串返回 0、前导 0 字节也参与混淆。

### 2.2 查表法原理速记

逐位法每字节要 8 轮"异或+移位"；查表法把"寄存器低 8 位 ⊕ 输入字节"作为索引，直接取 8 轮运算的预计算结果再异或高位——**空间换时间**的教科书案例。

## 3. 与其他文件的耦合关系

| 调用方 | 用途 | 覆盖的数据 |
| --- | --- | --- |
| `ef_env.c read_env`（L539-595） | KV 节点 CRC 校验 | name_len 起至 value 末尾（含对齐填充） |
| `ef_env.c create_env_blob`（L1353-1363） | 写前算 CRC | 四段累加：头字段+key+填充+value |
| `ef_env_legacy*.c calc_env_crc` | 区级 CRC | 结束地址游标+保存计数+数据段 |
| `ef_env.c update_env_cache`（L372） | name-CRC16（取高 16 位） | key 名字，做缓存索引 |
| `ef_iap.c` 生态 | 固件包校验（用户代码调用） | 任意 buffer |

## 4. 陷阱

1. **多态调用参数 `crc=0` 而不是 `0xFFFFFFFF`**：函数内部做条件化，外部再传 0xFFFFFFFF 会双重异或出错——这是最容易踩的坑；
2. 大小端无关：按字节处理，两端架构结果一致；
3. 无临界区需求（纯函数），可安全用于 ISR。

## 5. 深入：CRC 覆盖范围的完整地图

| 调用点（原始/注后行号） | 覆盖数据 | 失效后果 |
| --- | --- | --- |
| ef_env.c `create_env_blob`（L1353-1363） | 头字段(name_len 起) + key + 对齐填充(0xFF) + value | 写前算好存入头，读时复核 |
| ef_env.c `read_env`（L566-580） | 同上（32B 分段流式） | CRC 不符→crc_is_ok=false→节点不可见 |
| ef_env.c `update_env_cache`（L372） | key 名（取 CRC32 高 16 位作索引） | 碰撞靠回读名字复核兜底 |
| ef_env_legacy.c `calc_env_crc`（注后 L961） | 游标(4B)+[保存计数]+数据段 | 上电仲裁/重建的唯一判据 |
| ef_env_legacy_wl.c `calc_env_crc`（注后 L1018） | 游标(4B)+详情区 | 同上（PFS 计数不参与） |

对齐填充字节参与 CRC 是个精妙细节：填充内容是确定的 0xFF，写入方按同一规则补算（create_env_blob L1356-1362 的 `while (align_remain--)` 循环逐字节喂给 CRC），任何一处填充被位翻转都能被发现。

## 6. 与 FlashDB 的对照

FlashDB 的 `fdb_calc_crc32` 是本函数的直接后代（签名、查表结构、双重 `^~0U` 完全一致），差异只在调用面：KVDB 把 CRC 扩展到"status+name+value 全节点"。读完本函数即可无缝阅读 FlashDB 校验代码。

## 7. 自检测试向量

本实现是标准 CRC-32/ISO-HDLC（alias CRC-32/ADCCP），可用公开校验值验证移植正确性：

| 输入 | 期望输出 |
| --- | --- |
| `"123456789"`（9 字节，首调 crc=0） | `0xCBF43926` |
| 空数据（size=0，crc=0） | `0x00000000` |
| 单字节 `0x00`（crc=0） | `0xD202EF8D` |

单元测试三行即可覆盖核心路径；若与 zlib 结果不一致，先查是否误传了初值 0xFFFFFFFF。

## 8. 实现极简主义解读

全文件只有"1 表 + 1 函数"：没有上下文结构体、没有动态内存、没有硬件依赖。这种"无状态工具函数"风格让它在 ISR/任务/任意模式下都可重入，也是 armink 系列库（EasyLogger/FlashDB）公共底座的统一风格——值得在自研库里复用。

## 9. 逐行复杂度账本

```c
while (size--) {                                        // 每字节一次
    crc = crc32_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8); // 1 次异或取索引 + 1 次查表 + 1 次移位异或
}
```

- 时间：O(n)，n 为字节数——1KB 数据约 1024 次查表，72MHz Cortex-M3 上约 30~50 µs；
- 空间：表 1KB（.rodata）+ 3 个局部变量；无堆、无锁；
- 分支：循环体零分支（流水线友好），这也是查表法快过逐位法的另一半原因。

## 10. 互链

- 掉电安全应用：Docs/[04-掉电安全与恢复](../../../references/filesystems/EasyFlash/Docs/04-掉电安全与恢复.md)
- 缓存索引用法：[ef_env.c.md](ef_env.c.md) 的缓存表一节
