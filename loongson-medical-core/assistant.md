# assistant.c —— 语音病历助手主程序（A级逐函数深读）

对应源文件：`embedded-application/assistant.c`（原 314 行，注释后共 407 行）

> 互链：[Docs/00-总览与架构](../../../references/firmware/loongson-medical-core/Docs/00-总览与架构.md) ·
> [Docs/01-启动与初始化](../../../references/firmware/loongson-medical-core/Docs/01-启动与初始化.md) ·
> [Docs/02-核心子系统精读](../../../references/firmware/loongson-medical-core/Docs/02-核心子系统精读.md) ·
> [Docs/03-数据流与接口协议](../../../references/firmware/loongson-medical-core/Docs/03-数据流与接口协议.md)

## 1. 文件定位

龙芯(LoongArch) 端侧的**全离线语音病历助手**，单文件 C 程序，`gcc -o assistant assistant.c` 直接编译、零第三方依赖。它编排一条五段流水线：

```
arecord 录音(裸PCM) → 补WAV头 → sense-voice-main 识别 → clean_text 清洗
→ llama-cli 生成 → extract_record 提取 → save_txt_record 落盘
```

与 GUI（medical_platform.py）的耦合方式：GUI 通过 Vte PTY 拉起本程序并展示 stdout；本程序靠 `getchar()` 收 GUI 喂的回车、靠 `pidof` 清杀旧实例。

## 2. 逐函数总表

| 函数 | 行号 | 签名要点 | 职责 | 关键陷阱 |
|---|---|---|---|---|
| `struct wav_header` | L44 | 14 字段 / 44 字节 | RIFF 头内存布局 | 字段顺序不可调换 |
| `fix_wav_file` | L69 | (raw_path, wav_path) | 裸 PCM 补头成 WAV | `wav_f` 未判空（已知隐患） |
| `clean_text` | L109 | (input, output) | 去 `[]`/`<>` 区间与控制字符 | 必须 `(unsigned char)` 强转 |
| `strrstr` | L131 | (haystack, needle) | 从后往前找子串 | needle 必须非空 |
| `extract_record` | L151 | (raw, out, out_size) → 1/0 | 抓最后一对病历标记 | 标记倒序返回 0 |
| `safe_filter` | L177 | (src, dst, dst_size) | 删 `"` `\` `$` `` ` `` | — |
| `save_txt_record` | L199 | (record_raw, patient_input) | 渲染 ASCII 病历并落盘 | strtok 原地改写，先拷贝 |
| `write_prompt_file` | L264 | (patient_input) | 写 ChatML prompt | 打开失败静默用旧文件 |
| `main` | L296 | 无参，永不返回 | 主循环编排 | pidof 清场须排除自身 |

## 3. 关键代码精读

### 3.1 fix_wav_file 的两个魔法数字（L78、L85）

```c
// assistant.c L78-L85（节选）
    header.chunk_size = pcm_size + 36;  /* RIFF 规定 = 数据长 + 除本字段外的头 36 字节 */
    // ...
    header.sample_rate = 16000;         /* arecord -r 16000 */
    header.byte_rate = 32000;           /* 16000 采样/秒 x 2 字节/采样 */
```

- `36 = 44 - 4 - 4`：`chunk_size` 不把自己（4 字节）和 `riff`（4 字节）算进去；
- `byte_rate = sample_rate × block_align = 16000 × 2 = 32000`，10 秒录音 = 320 000 字节 ≈ 313 KB。

### 3.2 clean_text 的中文保命强转（L113-L118）

```c
// assistant.c L113-L118
    int i = 0, j = 0, in_b = 0, in_t = 0;   /* in_b: 在[]内  in_t: 在<>内 */
    while (input[i]) {
        if (input[i] == '[') in_b = 1;
        else if (input[i] == ']') in_b = 0;
        else if (input[i] == '<') in_t = 1;
        else if (input[i] == '>') in_t = 0;
        else if (!in_b && !in_t && (unsigned char)input[i] > 31) output[j++] = input[i];  /* 只保留区间外的可打印字符 */
        i++;
    }
```

`char` 在 Linux/LoongArch 默认有符号，UTF-8 中文首字节 0xC2~0xF4 转成 int 是负数，不加强转 `> 31` 会把中文全滤掉。

### 3.3 strrstr：一行的算法核心（L131-L139）

```c
// assistant.c L132-L137
    char *result = NULL;
    char *p = (char *)haystack;
    while ((p = strstr(p, needle)) != NULL) {
        result = p;
        p++;   /* 只前移 1 字节继续找：允许重叠匹配；needle 非空故不会死循环 */
    }
    return result;
```

存在价值：模型会复读 prompt 里的示例标记，**只有最后一对** `[病历开始]/[病历结束]` 是本次生成的病历。复杂度 O(n×m)（m=needle 长），n≤8KB 无所谓。

### 3.4 extract_record 的防御链（L154-L162）

```c
// assistant.c L154-L162
    if (!start || !end || end <= start) return 0;   /* end<=start: 标记不配对（如只复读了一次） */
    start += strlen("[病历开始]");
    while (*start == '\n' || *start == '\r') start++;   /* 跳过标记后紧随的空行 */
    int len = end - start;
    if (len >= out_size) len = out_size - 1;   /* 截断保护，给 '\0' 留 1 字节 */
    strncpy(out, start, len);
    out[len] = '\0';
    int k = strlen(out) - 1;
    while (k > 0 && (out[k] == '\n' || out[k] == '\r' || out[k] == ' ')) out[k--] = '\0';  /* 右去空白，病历更整洁 */
```

四步：配对校验 → 跳空行 → 截断钳位 → 右去空白。4096 字节缓冲约容 1300+ 汉字。

### 3.5 save_txt_record 的 strtok 拷贝防坑（L227-L233）

```c
// assistant.c L227-L233
    char buf[4096];
    strncpy(buf, record_raw, sizeof(buf)-1);   /* strtok 会原地改写缓冲，record_raw 是 const，必须先拷贝 */
    buf[sizeof(buf)-1] = '\0';
    char *line = strtok(buf, "\n");
    while (line) {
        fprintf(f, "|   %s\n", line);   /* 逐行补左边框；超宽不自动折行，打印前需人工检查 */
        line = strtok(NULL, "\n");
    }
```

文件名（L203）：`bingli_%04d%02d%02d_%02d%02d%02d.txt` = 病历编号 = 秒级时间戳，硬编码 `/home/richard/`。

### 3.6 write_prompt_file 的 ChatML 协议（L268-L281）

```c
// assistant.c L268-L276（system 段）
    fprintf(f, "<|im_start|>system\n");   /* 以下为 ChatML 的 system 段 */
    fprintf(f, "你是一个门诊病历生成助手。用第三人称医学术语，禁止使用\"您建议可能考虑\"。\n");  /* 负面词约束：压制口语化建议词 */
    fprintf(f, "输出格式：\n");
    fprintf(f, "[病历开始]\n");           /* one-shot 示例骨架，模型照此输出 */
    fprintf(f, "主诉：...\n");
    fprintf(f, "现病史：...\n");
    fprintf(f, "初步诊断：...\n");
    fprintf(f, "处理意见：...\n");
    fprintf(f, "[病历结束]<|im_end|>\n");
```

文件以 `\<\|im_start\|\>assistant\n` 收尾（L281），prompt 是"说到一半的对话"，模型续写即病历。

### 3.7 main 的启动三件事与主循环（L306-L341）

```c
// assistant.c L306-L308
    setbuf(stdout, NULL);  /* 关闭缓冲，确保 GUI 实时看到输出 */

    setenv("LD_LIBRARY_PATH", "/home/richard/sense_voice_demo/lib", 1);  /* SenseVoice 私有动态库路径，popen 子进程继承 */
```

```c
// assistant.c L328-L341
    while (1) {
        printf("\n>>> 请说话描述症状... (按回车发送) <<<\n");

        pid_t pid = fork();
        if (pid == 0) {
            /* 子进程：参数与 fix_wav_file 的头字段一一对应；
             * plughw:1,0 = 第 2 块声卡的插混设备（USB 麦克风常见序号） */
            execlp("arecord", "arecord", "-D", "plughw:1,0", "-r", "16000", "-c", "1", "-f", "S16_LE", "-t", "raw", raw_file, NULL);
            exit(0);   /* 只有 execlp 失败才会走到这里，正常情况被父进程杀掉 */
        }

        while (getchar() != '\n');   /* 阻塞等回车 = "说完了吗"的停止信号 */
        kill(pid, SIGKILL);          /* 强停录音：RAW 文件留成"录音到哪算哪" */
        waitpid(pid, NULL, 0);       /* 回收子进程，防僵尸 */
```

交互模型"先录音后收停"：fork 后麦克风立即在录，`getchar()` 被 GUI `feed_child(b"\n")` 或终端回车唤醒，SIGKILL 强停后 RAW 文件保留已录数据。

## 4. 与外部世界的全部接口

| 接口 | 方向 | 载体 |
|---|---|---|
| arecord 命令行（L335） | 出 | 参数必须与 wav_header 字段一致 |
| sense-voice-main stdout（L353-L355） | 入 | 只收含 `[` 的行（L355） |
| /tmp/prompt.txt（L265） | 出 | ChatML，llama-cli `-f` 读 |
| llama-cli stdout（L381-L392） | 入 | 边打印边累积到 8KB 缓冲（L391 有守卫） |
| stdout 文本 | 出 | `[系统]`/`[你]:` 前缀是 GUI 的展示协议 |

## 5. 已知问题（读码记录，未改代码）

1. `fix_wav_file` 中 `wav_f` 未判空（L91），fopen 失败 → fwrite(NULL) 段错误；
2. 病历/模型/库路径全部硬编码在 `/home/richard/`，换机部署需改 6+ 处；
3. `safe_filter` 只处理 4 类元字符，若未来把 `%s` 拼回命令串仍有注入面；
4. `strrstr` 对空 needle 会死循环（当前调用点恒非空，安全）。
