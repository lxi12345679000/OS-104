# Lab4 进程创建与初始化模块 (Function 2) 报告

## 1. 模块整体逻辑线
本模块负责 ucore 操作系统中进程控制块（PCB）的分配、第一个内核线程 `idleproc` 和第二个内核线程 `initproc` 的创建，以及 `fork` 机制的实现。执行链如下：
`kern_init` -> `proc_init` (创建 idleproc 和 initproc) -> `kernel_thread` -> `do_fork` -> `copy_thread` -> 返回 PID 并由 `find_proc` 找到 `initproc`。

## 2. 核心函数实现说明

### 2.1 `alloc_proc(void)`
负责分配并初始化一个 PCB 结构体。
- 使用 `kmalloc` 分配内存，失败返回 `NULL`。
- 初始化各项字段：`state = PROC_UNINIT`，`pid = -1`，`runs = 0`，`need_resched = 0`，`parent = NULL`，`mm = NULL`，`tf = NULL`。
- **关键点**：使用 `memset` 将 `context` 清零，使得 `idleproc` 的 `context.ra` 为 0；将 `pgdir` 指向 `boot_pgdir_pa`，因为内核线程共享内核地址空间。

### 2.2 `proc_init(void)`
初始化进程管理机制并创建初始线程。
- 初始化 `proc_list` 链表和 `hash_list` 哈希表。
- 调用 `alloc_proc` 创建 `idleproc`，设置 PID 为 0，状态为 `PROC_RUNNABLE`，`kstack` 指向 `bootstack`，`need_resched = 1`。
- 将 `current` 指向 `idleproc`。
- 调用 `kernel_thread(init_main, "Hello world!!", 0)` 创建 `initproc`，并通过 `find_proc` 找到该线程的 PCB，设置名称为 "init"。

### 2.3 `kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)`
构建内核线程的初始 TrapFrame。
- 在栈上定义 `trapframe tf` 并清零。
- 设置 `tf.epc = kernel_thread_entry`，`tf.gpr.s0 = fn`，`tf.gpr.s1 = arg`。
- 设置 `sstatus` 的 SPP 和 SPIE 位，保证新线程第一次返回时处于内核态且中断开启。
- 调用 `do_fork` 并返回其 PID。

### 2.4 `do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)`
创建新进程的完整流程。
- 调用 `alloc_proc` 分配 PCB。
- 调用 `setup_kstack` 分配内核栈。
- 调用 `copy_mm`（Lab4 中为空实现）和 `copy_thread`。
- 关闭中断，分配 PID，将新进程加入全局链表和哈希表，增加 `nr_process`。
- 调用 `wakeup_proc` 唤醒新进程并返回 PID。
- **错误处理**：遇到分配失败时，依次清理已分配的内核栈和 PCB，防止内存泄漏。

### 2.5 `copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)`
复制并设置新线程的上下文。
- 在内核栈顶分配 TrapFrame 空间并拷贝传入的 `tf`。
- 设置 `tf->gpr.a0 = 0`，使得新线程的 fork 返回值为 0。
- 设置 `tf->gpr.sp` 为栈顶地址。
- 设置 `proc->context.ra = forkret`，`proc->context.sp = proc->tf`，确保 `switch_to` 后能正确进入中断返回路径。

### 2.6 `copy_mm(uint32_t clone_flags, struct proc_struct *proc)`
Lab4 阶段由于都是内核线程且共享内核页表，因此该函数仅保留接口，`proc->mm` 赋值为 `NULL`，不执行复杂的内存复制逻辑。

## 3. 测试验证
运行 `make qemu`，成功输出：
```text
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"