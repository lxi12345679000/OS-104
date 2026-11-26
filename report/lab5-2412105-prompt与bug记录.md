# Lab5 模块三与 COW：spec / prompt / bug 记录（2412105 贺佳怡）


---

## 一、练习 spec

### 1. 功能模块三：fork 与地址空间复制

**目标：** 实现 `fork` 系统调用的内核侧逻辑，使子进程获得独立的用户地址空间。

**必须实现：**

| 函数 | 文件 | 要求 |
|---|---|---|
| `do_fork` | `kern/process/proc.c` | 分配 PCB、内核栈、地址空间、上下文，挂入进程表并唤醒 |
| `copy_mm` | `kern/process/proc.c` | 内核线程沿用内核页表；CLONE_VM 共享；普通 fork 复制 mm |
| `copy_thread` | `kern/process/proc.c` | 复制 trapframe，设置子进程返回值和首次调度入口 |
| `copy_range` | `kern/mm/pmm.c` | 复制 `[start, end)` 的页面映射，失败可回滚 |
| `dup_mmap` | `kern/mm/vmm.c` | 复制 VMA 链表并逐段调用 `copy_range` |

**验收标准：**

- `badarg` 输出 `fork ok.` / `badarg pass.`
- `forktest` 创建 32 个子进程并全部回收，输出 `forktest pass.`
- 进程退出后空闲页数恢复到测试前水平
- 失败路径不泄漏页表和物理页

### 2. COW Challenge

**目标：** 在 fork 上实现写时复制，避免无条件的物理页深拷贝。

**必须实现：**

- 使用 `PTE_COW` 软件位标记共享只读页；
- fork 时可写页在父子两侧都设为只读 + `PTE_COW`，引用计数 +1；
- 写缺页时按引用计数决定「复制」还是「原地恢复可写」；
- 复制失败时回滚：子进程映射撤销、父进程 PTE 权限恢复、引用计数回退。

**验收标准：**

- `cowtest` 通过（未写共享、首次写复制、连续写隔离、引用归零回收）；
- 分配失败回滚后，父进程 PTE 为可写且无 `PTE_COW`，引用计数回到 1；
- 与 `exec/exit` 共存，无 panic。

---

## 二、prompt 迭代记录

### 2.1 模块三：普通 fork

#### 第 1 轮 prompt（骨架）

```text
在 ucore RISC-V 上实现 fork：alloc_proc + setup_kstack + copy_mm + copy_thread，
最后分配 pid、挂入 hash 和进程树。请给出 do_fork 的完整实现。
```

**结果：** 能编译，但子进程第一次调度后行为异常。

**问题与修正：**

- `copy_thread` 里 trapframe 的位置和 `context.sp/ra` 没对齐，子进程无法从 `forkret` 正确返回；
- 提示词里没有强调 `a0 = 0`，子进程 fork 返回值不确定。

**补充 prompt：**

```text
copy_thread 请严格按下面的布局实现：
proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
*proc->tf = *tf;
proc->tf->gpr.a0 = 0;
proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;
proc->context.ra = (uintptr_t)forkret;
proc->context.sp = (uintptr_t)proc->tf;
并说明用户进程和内核线程在 sp 上的区别。
```

#### 第 2 轮 prompt（地址空间）

```text
实现 copy_mm：
1. current->mm == NULL（内核线程）时 proc->pgdir = boot_pgdir_pa；
2. CLONE_VM 时只增加 mm_count，共享 current->pgdir；
3. 普通 fork 时 mm_create + setup_pgdir + dup_mmap；
4. 失败时 exit_mmap + put_pgdir + mm_destroy。
注意 proc->pgdir 必须是物理地址，因为 proc_run 会把它写进 satp。
```

**结果：** fork 能进入用户态，但 fork 后父子页面仍然共享物理页且都可写。

**问题与修正：**

- `copy_range` 一开始只实现了 `share=false` 深拷贝，页表项权限直接照抄父进程；
- `dup_mmap` 没有保持 VMA 的 `vm_flags`，用户栈和代码段的权限混淆。

**补充 prompt：**

```text
dup_mmap 复制 VMA 时必须保留 vm_start/vm_end/vm_flags，并按原链表顺序插入；
copy_range(share=false) 为每个有效 PTE 分配新页、memcpy、去掉 PTE_COW，
用 page_insert 建立映射；get_pte 返回 NULL 或 PTE 无效的地址直接跳过（惰性分配）。
```

#### 第 3 轮 prompt（失败回滚）

```text
copy_range 任何一步失败（alloc_page 失败或 page_insert 失败）都必须回滚：
用 unmap_range(to, start, addr) 撤销子进程中已经建立的映射，
并保证失败时不会留下引用计数泄漏。
copy_mm 在 dup_mmap 返回 -E_NO_MEM 后执行 exit_mmap + put_pgdir + mm_destroy。
请分析每个失败点的资源归属。
```

**最终结果：** `badarg`、`forktest` 通过，失败路径无泄漏。

---

### 2.2 COW Challenge

#### 第 1 轮 prompt（设计）

```text
在已通过的普通 fork 基础上实现 COW。使用 PTE_COW = 0x100 标记共享只读页。
fork 时把可写页在父子两侧都改成只读 + PTE_COW，引用计数 +1；
只读页直接共享，不加 COW。请给出 copy_range 的 share=true 分支。
```

**结果：** 子进程写共享页能触发缺页，但父进程写不触发。

**问题与修正：**

- 只改了子进程的 PTE，父进程 PTE 仍是可写，父进程写入直接落到共享页；
- 修复：`page_insert` 成功后同步修改 `*src_ptep`，并 `tlb_invalidate(from, addr)`。

**补充 prompt：**

```text
copy_range(share=true) 里，page_insert 成功后必须把父进程的 PTE 也改成
(*src_ptep | PTE_COW) & ~PTE_W，并刷新父进程页表的 TLB。
只读页（perm 不含 PTE_W 且不含 PTE_COW）保持原权限直接共享。
```

#### 第 2 轮 prompt（缺页处理）

```text
实现 do_pgfault 的 COW 分支：
- 先做 VMA 检查和读写权限检查；
- PTE 有效时只可能是 COW 写缺页；非 COW 的有效 PTE 再次写缺页返回 -E_INVAL；
- ref > 1：alloc_page + memcpy，新页 perm = (旧 PTE & 0x3FF) | PTE_W & ~PTE_COW，
  page_insert 替换；page_insert 失败时释放新页，旧映射保持不变；
- ref == 1：原地 (*ptep | PTE_W) & ~PTE_COW，并 tlb_invalidate。
```

**结果：** `cowtest` 的三代隔离通过。

#### 第 3 轮 prompt（回滚）

```text
补 copy_range(share=true) 的失败回滚：
1. unmap_range(to, start, addr) 撤销子进程已建立的映射（引用计数随之回退）；
2. 遍历父进程 [start, addr)，对仍带 PTE_COW 的页面：
   若 page_ref(page) == 1，说明父进程又成为唯一使用者，
   恢复 *src_ptep = (*src_ptep | PTE_W) & ~PTE_COW 并 tlb_invalidate；
   若 ref > 1，保持 COW 状态不变。
请解释为什么必须用 ref == 1 作为恢复条件。
```

**结果：** 故障注入验证通过（见报告第 5 节）。

---

## 三、bug 与修复记录

| # | 现象 | 根因 | 修复 | 验证 |
|---|---|---|---|---|
| 1 | 子进程第一次被调度后异常，fork 返回值不对 | `copy_thread` 的 trapframe 位置 / `context.sp` / `context.ra` 不一致；`a0` 未清零 | trapframe 放 `kstack + KSTACKSIZE - sizeof(tf)`；`context.ra = forkret`、`context.sp = tf`；`a0 = 0` | `badarg` 输出 `fork ok.` |
| 2 | 子进程 `satp` 装载后地址空间错误 | `proc->pgdir` 保存了虚拟地址 | 统一保存 `PADDR(mm->pgdir)`；内核线程用 `boot_pgdir_pa` | `forktest` 通过 |
| 3 | fork 后父进程写共享页不触发缺页 | 只改子进程 PTE，父进程仍是可写 | `copy_range` 同步修改父 PTE 为只读 + COW，并 `tlb_invalidate` | `cowtest` 中父进程写 17 与子进程隔离 |
| 4 | 分配失败后父进程页面停在只读 + COW | 回滚只撤销了子进程映射，没有恢复父进程权限 | 回滚时对 `ref == 1` 的父 PTE 恢复 `PTE_W`、清 `PTE_COW` | 故障注入正向 PASS / 负对照 FAIL |
| 5 | `copy_range` 失败后子进程页表残留映射 | 失败直接返回，没有 `unmap_range` | `failed:` 标签统一 `unmap_range(to, start, addr)` | `to_mapped=0` |
| 6 | `copy_mm` 失败后 VMA / 页表泄漏 | 只返回错误码，没有销毁半成品 mm | `dup_mmap` 失败后 `exit_mmap + put_pgdir + mm_destroy` | 后续 fork 正常，空闲页恢复 |
| 7 | `ref == 1` 时原地恢复写权限后仍缺页 | 修改 PTE 后没有刷新 TLB | 原地恢复后 `tlb_invalidate(mm->pgdir, la)` | `cowtest` 连续写通过 |

> 说明：以上是模块实现和联调中实际处理的典型问题，按「现象 → 根因 → 修复 → 验证」整理，
> 供报告的 bug 记录章节使用。

---

## 四、测试记录

### 4.1 环境

| 项 | 值 |
|---|---|
| 提交 | `lab5-integration` @ `1e619bc` |
| 编译器 | `riscv64-unknown-elf-gcc 13.2.0` |
| QEMU | `4.1.1` |
| 构建 | `cd code && make`（通过；仅有 `user/testbss.c` 自身的数组越界告警） |

### 4.2 模块三相关测试

**`badarg`：**

```text
kernel_execve: pid = 2, name = "badarg".
fork ok.
badarg pass.
all user-mode processes have quit.
init check memory pass.
```

**`forktest`：**

```text
kernel_execve: pid = 2, name = "forktest".
I am child 31
...
I am child 0
forktest pass.
all user-mode processes have quit.
init check memory pass.
```

### 4.3 `cowtest`

```text
kernel_execve: pid = 2, name = "cowtest".
cowtest pass.
all user-mode processes have quit.
init check memory pass.
```

### 4.4 分配失败回滚（故障注入）

```text
[COW-RB] before page0: pte=0x20128817 W=1 COW=0 ref=1
[COW-RB] before page1: pte=0x20129417 W=1 COW=0 ref=1
[COW-RB] before page2: pte=0x20129817 W=1 COW=0 ref=1
[COW-RB] copy_range returned -4 (expect -4)
[COW-RB] after  page0: pte=0x20128817 W=1 COW=0 ref=1 to_mapped=0
[COW-RB] after  page1: pte=0x20129417 W=1 COW=0 ref=1 to_mapped=0
[COW-RB] after  page2: pte=0x20129817 W=1 COW=0 ref=1 to_mapped=0
[COW-RB] RESULT: PASS
```

负对照（关闭父 PTE 恢复逻辑）：

```text
[COW-RB] after  page0: pte=0x20128913 W=0 COW=1 ref=1 to_mapped=0
[COW-RB] after  page1: pte=0x20129513 W=0 COW=1 ref=1 to_mapped=0
[COW-RB] RESULT: FAIL
```

### 4.5 完整 `make grade`（本机）

| 项 | 结果 |
|---|---|
| 原版 `make grade` | 0/120，全部 `no $qemu_out` |
| 原因 | 本机无 `riscv64-unknown-elf-gdb`；`gdb-multiarch 15.1` 与 QEMU 4.1.1 gdbstub 不兼容（`Truncated register 37 in remote 'g' packet`），grade 的 `-S` + gdb 流程使 qemu 一直暂停 |
| 临时适配后 | **123/130**（仅在 `/tmp` 副本跳过 gdb 断点，评分正则不变） |
| 唯一失分 | `divzero`：GCC 13.2 在 `-O2` 下把 `1/zero`（UB）优化为 0，期望是 RISC-V 硬件除零的 -1；与本模块代码无关 |
| 罗茜环境 | GCC 10.2.0 + QEMU 4.1.1，130/130，作为最终验收基线 |

### 4.6 QEMU 8.2.2 兼容性冒烟

- `/usr/bin/qemu-system-riscv64` 为 8.2.2，但本套 ucore 使用 legacy SBI（`libs/sbi.h` 中 ext 0/1/2/8）；
- QEMU 8.2 自带 OpenSBI 1.x 已删除 legacy 扩展，内核无法输出 `(THU.CST) os is loading`；
- 结论：QEMU 8.2.2 只做兼容性记录，不作为实验运行环境，不据此修改实验语义。

---

## 五、交给罗茜的合并建议

1. 模块三和 COW 两节可直接放入「四、实验内容与实现」，测试记录放入「五、测试与验证」。
2. 报告里的「最终提示词」建议保留第 3 轮收敛版；前三轮放在「实现迭代过程」，体现迭代。
3. 环境差异（gdb / QEMU 8.2.2 / divzero）请在报告中如实记录，最终评分以罗茜的 GCC 10.2.0 + QEMU 4.1.1 为准。
4. 本人只写了主责部分，未改动系统调用、进程生命周期、trap、`load_icode` 等他人模块。
