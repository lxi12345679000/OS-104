## 功能模块三：进程调度与切换

**负责人：** 2412634-罗茜

### 模块功能与接口

本模块负责把已经创建并登记为 `PROC_RUNNABLE` 的内核线程真正交给 CPU 执行。涉及四个接口：

```c
void cpu_idle(void);
void schedule(void);
void proc_run(struct proc_struct *proc);
void switch_to(struct context *from, struct context *to);
```

四者的职责严格分开：

- `cpu_idle()` 是 `idleproc` 的执行主体，只在 `need_resched` 被置位时进入调度器。
- `schedule()` 只负责选择下一个可运行线程，不直接保存或恢复寄存器。
- `proc_run()` 负责把调度结果落到系统状态上：关中断、更新 `current`、切换 Sv39 页表、刷新 TLB，再调用上下文切换。
- `switch_to()` 只完成 `struct context` 中的寄存器保存与恢复，不参与调度策略。

### `cpu_idle()` 与 `schedule()`

`proc_init()` 把当前执行现场包装成 `idleproc` 时设置了 `need_resched = 1`。内核进入 `cpu_idle()` 后检测到该标记，调用 `schedule()`，从而把 CPU 交给 `initproc`。

`schedule()` 使用 FIFO 选择策略：

1. 先将当前线程的 `need_resched` 清零。
2. 当前线程不是 `idleproc` 时，从当前线程的后继节点开始扫描，实现轮转。
3. 当前线程是 `idleproc` 时，从 `proc_list` 链表头开始扫描，让所有就绪线程得到机会。
4. 跳过头节点哨兵，只选择 `state == PROC_RUNNABLE` 的进程控制块。
5. 扫描一整圈仍找不到其他就绪线程时，回退到 `idleproc`。
6. 只有目标与当前线程不同才调用 `proc_run()`。

整个扫描和状态更新位于 `local_intr_save/restore` 保护区中，避免选择 `next` 的过程中被时钟中断重新进入调度器。

关键代码：

```c
void schedule(void) {
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;

    local_intr_save(intr_flag);
    {
        current->need_resched = 0;
        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last;
        do {
            le = list_next(le);
            if (le != &proc_list) {
                next = le2proc(le, list_link);
                if (next->state == PROC_RUNNABLE) {
                    break;
                }
            }
        } while (le != last);

        if (next == NULL || next->state != PROC_RUNNABLE) {
            next = idleproc;
        }

        next->runs++;
        if (next != current) {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}
```

### `proc_run()` 与 Sv39 页表切换

`proc_run()` 先确认目标状态是 `PROC_RUNNABLE`，随后在关中断临界区中完成：

```text
保存旧 current
-> current = next
-> write_csr(satp, Sv39_MODE | next->pgdir >> 12)
-> flush_tlb()
-> switch_to(&prev->context, &next->context)
```

`pgdir` 保存页表根节点的物理地址，而 `satp.PPN` 需要的是该地址右移 `RISCV_PGSHIFT` 后的物理页号。模式字段使用 Sv39，即 `8 << 60`，与 `pmm.c::enable_paging()` 的编码一致。

本实验所有内核线程共享内核页表，因此切换前后代码仍能继续执行；但实现仍按进程页表接口更新 `satp`，为后续用户进程保留正确语义。不能使用 `libs/riscv.h` 中遗留的 `lsatp()`，因为它按 Sv32 编码，无法正确表示 RV64 的 Sv39 `satp`。

关键代码：

```c
void proc_run(struct proc_struct *proc) {
    if (proc == current) {
        return;
    }

    assert(proc->state == PROC_RUNNABLE);
    bool intr_flag;
    struct proc_struct *prev = current;
    struct proc_struct *next = proc;

    local_intr_save(intr_flag);
    {
        current = next;
        write_csr(satp,
                  0x8000000000000000 | (next->pgdir >> RISCV_PGSHIFT));
        flush_tlb();
        switch_to(&(prev->context), &(next->context));
    }
    local_intr_restore(intr_flag);
}
```

### `switch_to()` 的寄存器布局

`struct context` 保存线程切换所需的被调用者保存寄存器，顺序为：

```text
ra, sp, s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11
```

RV64 下每个寄存器占 8 字节，因此对应偏移依次为：

```text
0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104
```

函数参数遵循 RISC-V 调用约定：`a0 = &from->context`，`a1 = &to->context`。先使用 `sd` 保存旧现场，再从 `a1` 指向的新上下文恢复同样的字段，最后通过 `ret` 跳到新线程的 `context.ra`。

对于首次运行的线程，`copy_thread()` 已把 `context.ra` 设置为 `forkret`，把 `context.sp` 设置为该线程栈顶的 TrapFrame，因此切换完成后会沿以下路径进入线程函数：

```text
switch_to -> forkret -> forkrets -> __trapret
          -> kernel_thread_entry -> fn(arg)
```

### Challenge 1：`local_intr_save/restore` 如何开关中断

`local_intr_save(intr_flag)` 展开后调用 `__intr_save()`：

1. 读取当前 `sstatus.SIE`。
2. 如果中断原本开启，则调用 `intr_disable()`，并把 `true` 写入 `intr_flag`。
3. 如果中断原本关闭，则不做任何操作，并把 `false` 写入 `intr_flag`。

`local_intr_restore(intr_flag)` 根据保存值决定是否恢复：

1. `intr_flag == true` 表示进入临界区前中断开启，退出时调用 `intr_enable()`。
2. `intr_flag == false` 表示进入前中断已经关闭，退出时保持关闭。

这种设计不是无条件关中断后再无条件开中断，而是保存并恢复调用前的局部状态，因而支持嵌套临界区。若外层已经关闭中断，内层恢复时不会错误地提前开启。

它保护的是单处理器上的“检查条件—更新状态—执行切换”操作，例如：

- `schedule()` 中选择 `next`、更新 `need_resched` 和调用 `proc_run()`；
- `proc_run()` 中更新 `current`、写入 `satp` 和执行 `switch_to()`。

它不等价于多核自旋锁，也不保证多条 CPU 之间的互斥；多核并发仍需要额外的原子操作或锁机制。

### 实现与调试过程

1. 先核对 `proc.h` 的 `struct context` 字段顺序，而不是直接沿用 x86 版本或多级页表实验中的偏移。
2. 发现 `libs/riscv.h::lsatp()` 使用 Sv32 模式，确定不能用于 RV64。
3. 参考 `pmm.c::enable_paging()`，使用 Sv39 模式位与页表物理页号组合写入 `satp`。
4. 对 `schedule()` 的链表扫描使用环形 `do-while`，并单独处理链表头哨兵和历史 `next` 非就绪的情况。
5. 用 `riscv64-unknown-elf-objdump -d obj/kern/process/switch.o` 核对实际汇编偏移，确认保存与恢复顺序一致。

### 当前验证结果

环境：

```text
Ubuntu 22.04.5
riscv64-unknown-elf-gcc 10.2.0
qemu-system-riscv64 4.1.1
```

罗茜正式分支已完成：

```text
proc.c / sched.c / switch.S 编译通过
switch_to 反汇编偏移检查通过
```

罗茜正式分支完整链接目前停在：

```text
kern/init/init.c:29: undefined reference to `proc_init'
```

这是功能模块二尚未实现的预期依赖，不是本模块的编译错误。

为避免把贺佳怡或其他人的实现混入罗茜提交，另外建立了一份不提交的 `/tmp` 临时集成副本。该副本只做两件事：

1. 补入一版临时的 function2，用于让进程创建链可运行；
2. 补入 Lab3 已验证的 Trap 入口和 `forkrets`。

在 `riscv64-unknown-elf-gcc 10.2.0`、`qemu-system-riscv64 4.1.1` 下执行完整评分，结果为：

```text
check PMM:              OK
check Process Create:   OK
check Schedule:         OK
Total Score: 30/30
```

`Test Schedule` 的正则检查通过，说明测试输出中已经出现两个测试线程实际运行的证据，以及规定的 `initproc` 三行输出。临时副本中的 function2 和 Trap 修复不属于罗茜分支，不会提交。

另外，当前 `lab4` 基线的 `kern/trap/trapentry.S` 仍有 Lab3 的 `RESTORE_ALL`/`trap()` 占位：

```text
#lab3:填写你在lab3中实现的代码
```

这会使 `__trapret` 无法正确恢复完整 TrapFrame，在首次 `sret` 或时钟中断后阻塞集成验收。该文件属于石欣哲的基线集成范围，罗茜不直接修改；合并前需要把 Lab3 已验证实现补入基线。临时副本验证已经证明：补齐该基线后，本模块的调度与上下文切换可以通过全部 Lab4 检查。

