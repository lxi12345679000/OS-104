# Lab4 进程创建模块 (Function 2) Prompt 与 AI 修正记录

## 1. 四段式 Prompt 记录

**第一段（背景与目标）**：
你是操作系统开发专家。我现在在做 ucore OS 的 Lab4 实验，需要实现进程的创建与初始化（function2）。涉及的文件是 `kern/process/proc.c`。

**第二段（具体任务）**：
请帮我实现以下 6 个函数：`alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread`、`copy_mm`。要求遵循 ucore RISC-V 架构的规范。

**第三段（约束条件）**：
- `alloc_proc` 必须将 `context` 清零，且 `pgdir` 设为 `boot_pgdir_pa`。
- `proc_init` 要正确创建 `idleproc`（PID 0，使用 `bootstack`）和 `initproc`（PID 1）。
- `copy_thread` 中必须将新线程的 `context.ra` 设置为 `forkret`，且子进程的 `a0` 返回值为 0。
- `copy_mm` 在 Lab4 中只需提供空实现即可，不要引入 Lab5 的逻辑。

**第四段（预期输出）**：
请给出完整的 C 语言代码片段，并确保没有编译警告。

## 2. AI 生成代码的 Bug 与修复记录

**Bug 1：编译报错 `implicit declaration of function 'copy_thread'`**
- **原因**：AI 将 `copy_thread` 和 `copy_mm` 的定义写在了 `do_fork` 函数的后面。C 语言编译器在编译到 `do_fork` 时找不到这两个函数的声明，默认它们返回 `int`，导致与后面的 `static void` 定义冲突。
- **修复**：在 `//LAB4 function2` 区域的最上方，添加了这两个函数的静态前置声明：
  `static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);`
  `static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);`

**Bug 2（环境坑）：`make grade` 报错 `no $qemu_out`**
- **原因**：代码逻辑完美，但在 WSL 环境下，自编译的 QEMU 4.1.1 与新版的 QEMU 8.2.2 在启动参数上存在差异（`-device loader` 与 `-kernel`）。此外，`grade.sh` 后台调用 QEMU 时丢失环境变量或输出重定向失败，导致抓不到输出。
- **修复**：确认这不是代码错误。本地直接运行 `make qemu` 能够完整输出 `initproc` 的执行结果。最终通过修改 Makefile 绝对路径、还原测试脚本、以及修复 VS Code `askpass` 引起的网络凭证问题，成功将代码推送到远端。