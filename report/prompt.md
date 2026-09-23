# Lab1 提示词汇总

## 环境说明

- AI 编程工具：Cline（VS Code 插件）
- 底层模型：DeepSeek-v4-flash
- Harness：Cline 在 VS Code 的 WSL 远程窗口中运行，通过 API Key 接入 DeepSeek，可读取项目文件、执行命令、生成代码。
- 工作目录：`/home/sxz/lab1`

---

## 练习1 提示词

```
请阅读当前项目中的 kern/init/entry.S 文件，解释以下两条指令的作用和目的：
1. la sp, bootstacktop
2. tail kern_init
请结合 RISC-V 汇编和操作系统启动流程说明，不要只翻译字面意思。
```

**迭代过程：**

1. 向 Cline 提问后，Cline 读取 `entry.S`、`kernel.ld`、反汇编文件，给出伪指令展开、栈地址、寄存器保留等分析。
2. 本人核对反汇编，确认 `la` 展开为 `auipc+addi`，`tail` 松弛为 `j kern_init`。
3. 整理为练习1答案，保存为 `会话记录/lab1_exercise1.md`。

---

## 练习2 提示词

```
我正在做 lab1 练习2：用 GDB 跟踪 QEMU 从加电到跳转 0x80200000 的过程。以下是我的观察结果：

1. GDB 连上后停在 0x1000，反汇编显示：
   auipc t0,0x0
   addi a2,t0,40
   csrr a0,mhartid
   ld a1,32(t0)
   ld t0,24(t0)
   jr t0
2. 在 0x80200000 设断点后继续运行，命中 kern_entry。
   此时寄存器：a0=0（hartid），a1=0x87e00000（DTB），sp=0x80046eb0，pc=0x80200000。
3. 单步执行 la sp, bootstacktop 后，sp=0x80203000。
4. 再单步执行 tail kern_init 后，pc=0x8020000a <kern_init>。

请帮我：
1. 解释 0x1000 处这几条指令各自完成了什么功能。
2. 解释为什么 OpenSBI 跳转到内核时 sp 是无效的，以及 entry.S 为什么必须自己设栈。
```

**迭代过程：**

1. 本人手动操作 GDB，记录上述 4 条观察结果。
2. 将观察结果发给 Cline，请求解释 `0x1000` 复位向量与 `sp` 无效原因。
3. Cline 给出完整分析，整理为 `会话记录/lab1_exercise2.md`。
4. 补充说明：Makefile 中 `-device loader` 改为 `-kernel`、GDB 命令序列，由本人与 Cline 讨论后完成。

---

## 报告生成提示词

```
请根据当前项目 /home/sxz/lab1 的代码，以及 会话记录/ 目录下的两份会话记录，生成一份 lab1 实验报告初稿，保存为 lab1_report.md。
要求：整体逻辑线、核心模块理解、知识点对照、未覆盖知识点、练习解答，以文本说明为主，言简意赅。
```

**迭代过程：**

1. Cline 生成报告初稿，包含代码事实、反汇编数据、GDB 观察结果。
2. 本人审阅后精简，去掉冗余分析，保留核心结论。
3. 按老师模板调整结构，补充实验环境、AI 工具、测试与验证等章节。
