# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | 比麻雀更小的麻雀（最小可执行内核） |
| **姓名** | [贺佳怡] |
| **学号** | [2412105] |
| **完成日期** | 2026-10-07 |

---

## 一、实验目的

1. 理解 RISC-V 下操作系统从固件到内核的启动流程；
2. 掌握 ucore 的入口执行路径（`entry.S` → `kern_init`）；
3. 学习用 VS Code + Claude Code 辅助搭建和调试 OS 实验环境。

---

## 二、实验环境

| 项目 | 内容 |
|------|------|
| 宿主系统 | Windows + WSL2 (Ubuntu) |
| 交叉编译器 | riscv64-unknown-elf-gcc 13.2.0 |
| 模拟器 | QEMU 8.2.2 (qemu-system-riscv64) |
| 编辑器 | VS Code + WSL 远程连接 |
| AI 工具 | Claude Code（VS Code 插件），底层模型 DeepSeek-V4-Pro |
| 代码路径 | ~/ucore |

---

## 三、实验整体逻辑分析

### 2.1 本章节的逻辑主线

本章围绕 **"操作系统是如何启动的"** 展开，核心问题：

> 从上电到 ucore 内核跑起来，控制权是怎么一步步交到内核手里的？

RISC-V 平台上，启动并非"上电直接跑内核"，而是先由 M-mode 固件 OpenSBI 完成硬件初始化，再切换到 S-mode，把控制权交给内核。ucore 需要在约定的入口地址开始执行，逐步建立能运行 C 代码的环境。

### 2.2 功能的逐步实现

1. **固件阶段（OpenSBI）**：完成硬件初始化，是启动链路的起点。
2. **控制权交接**：OpenSBI 跳到内核入口 `0x80200000`，这是从固件到内核的关键一跳。
3. **内核入口 `entry.S`**：建立内核栈，之后才能运行 C 代码。
4. **`kern_init()`**：内核 C 主入口，负责后续初始化。
5. **console / SBI 输出**：能打印，才证明内核真的跑起来了。

---

## 四、实验内容与实现

### 功能模块：QEMU 启动与 ucore 入口

**涉及文件/函数：**

```c
// kern/init/entry.S   内核入口，建立栈后跳转 kern_init
// kern/init/init.c    void kern_init(void);
// libs/sbi.c          void sbi_console_putchar(int ch);
// kern/driver/console.c
```

**功能说明：**

让 ucore 在 QEMU 中从 OpenSBI 正确接手控制权并执行：QEMU 上电 → OpenSBI 初始化 → 跳到 `0x80200000` → ucore 执行 → console 通过 SBI 输出到串口。


#### 实现迭代过程

本模块的实现经历了 2 次迭代。

##### 第一次迭代

**问题：**

- `make qemu` 只显示 OpenSBI 日志后卡住；
- 关键信息：`Domain0 Next Address : 0x0000000000000000`；
- 原因：Makefile 用 `-device loader,file=$(UCOREIMG),addr=0x80200000` 加载 ucore，OpenSBI v1.3 不会据此设置下一跳地址。

**修改：**

将 Makefile 中 qemu 规则的加载方式由 `-device loader` 改为 `-kernel $(UCOREIMG)`。

```makefile
qemu: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(QEMU) \
		-machine virt \
		-nographic \
		-bios default \
		-kernel $(UCOREIMG)
```

##### 第二次迭代

**最终结果：**

- ✅ 编译通过（`make` 生成 `bin/kernel`、`bin/ucore.img`）
- ✅ `make qemu` 正常启动
- ✅ 输出 `(THU.CST) os is loading ...`

**关键改进点：**

- 用 `-kernel` 替代 `-device loader`，QEMU 会把内核入口地址传给 OpenSBI，`Domain0 Next Address` 变为 `0x80200000`，交接正常。



## 五、测试与验证

**测试截图：**

![测试结果截图](./images/test_result.png)

关键输出：

```
Domain0 Next Address      : 0x0000000080200000
Domain0 Next Mode         : S-mode
...
(THU.CST) os is loading ...
```

`Domain0 Next Address` 为 `0x80200000`，且内核打印成功，说明 ucore 已正确启动。

---

## 六、实验总结与收获

### 对操作系统的理解

1. 重要知识点与 OS 原理对应：

| 实验知识点 | OS 原理对应 | 说明 |
|------------|-------------|------|
| OpenSBI 启动 | 固件 / BIOS / Bootloader | 角色类似，实验中由 OpenSBI 承担 |
| `0x80200000` 入口 | 内核加载地址 | 由链接脚本 `tools/kernel.ld` 决定 |
| `entry.S` 建立栈 | 内核栈初始化 | 与 x86 boot 阶段类似，RISC-V 用 `sp` |
| `kern_init` | 内核主函数 | 类似 Linux `start_kernel` |
| console 输出 | 内核打印 / tty | 通过 SBI 调 UART |

