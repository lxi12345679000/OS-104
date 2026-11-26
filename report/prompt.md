# Lab5 AI 提示词与迭代记录

石欣哲和贺佳怡提供各自整理的提示词及验证记录。罗茜的模块提示词来自原报告整理稿，联调和调试部分根据实际对话与终端输出整理。石、贺的工具名和具体模型版本在材料中未注明。

## 一、石欣哲（2412896）

> 按“我提出提示词 → AI 给出初版 → 我核对/编译/运行验证 → 调整提示词再问”的顺序整理。
> 只保留能代表实现思路的轮次，不是完整聊天记录。

### 1. 进程启动链：`alloc_proc` / `proc_init` / `kernel_thread` / `init_main`

#### 第 1 轮：搭起 idleproc 到 init_main 的启动链

**我的提示词：**

```text
〖任务说明〗
在 Lab5 的 kern/process/proc.c 中，补齐功能模块二的启动链：
- static struct proc_struct *alloc_proc(void);
- void proc_init(void);
- int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
- static int init_main(void *arg)。

约束：
- 只修改 proc.c 中我负责的 function2 区域；
- 不修改 do_fork/copy_thread/copy_mm（贺佳怡）、do_wait/do_exit（罗茜）；
- 不修改 struct proc_struct、struct context、struct trapframe、trapentry.S；
- 保留 lab5 注释，把 YOUR CODE 替换为 2412896。

输出要求：给出完整 C 代码，并说明
switch_to -> forkret -> forkrets -> __trapret -> kernel_thread_entry -> fn(arg)
这条首次运行链。

〖依赖说明〗
- kmalloc/kfree、memset、list_init/list_add、proc_list、hash_list、
  nr_process、idleproc、initproc、current 已定义；
- setup_kstack、put_kstack、get_pid、hash_proc、find_proc、wakeup_proc、
  set_proc_name 已存在；
- pmm.h 提供 boot_pgdir_pa、bootstack、KSTACKSIZE、nr_free_pages；
- kmalloc.h 提供 kallocated；sched.h 提供 schedule；
- kernel_thread_entry 在 kern/process/entry.S，forkret 在 proc.c，
  forkrets 在 trapentry.S。

〖接口清单〗
- 保持上述 4 个函数签名不变；
- 允许在文件顶部增加 init_main/user_main/alloc_proc 的 static 前置声明；
- 不新增对外全局接口。

〖行为规格〗
alloc_proc：
- kmalloc 失败返回 NULL；
- state=PROC_UNINIT，pid=-1，runs=0，kstack=0，need_resched=0，
  parent=NULL，mm=NULL，tf=NULL，flags=0，name 清零；
- context 清零，使 idleproc 的 context.ra == 0；
- pgdir=boot_pgdir_pa；exit_code=0，wait_state=0，cptr/optr/yptr=NULL；
- list_link/hash_link 初始化。

proc_init：
- 初始化 proc_list 和 hash_list；
- alloc_proc 创建 idleproc，pid=0、PROC_RUNNABLE、kstack=bootstack、
  need_resched=1、name="idle"；current=idleproc；nr_process++；
- kernel_thread(init_main, NULL, 0)，失败 panic；
- find_proc 找到 initproc，设置 name="init"。

kernel_thread：
- 栈上 TrapFrame 清零；
- s0=fn，s1=arg，epc=kernel_thread_entry；
- status 按 sstatus 设置 SPP/SPIE、清除 SIE；
- 调用 do_fork(clone_flags, 0, &tf) 并返回 PID。

init_main：
- 记录 nr_free_pages 和 kallocated 初值；
- 创建 user_main，失败 panic；
- 循环 do_wait(0, NULL) 等待所有子进程退出，成功（>=0）时 schedule()；
- 打印 all user-mode processes have quit.；
- 断言内存全部回收，打印 init check memory pass.

验证：
- make 通过；
- 后续与贺佳怡的 do_fork/copy_thread、罗茜的 do_wait 联调。
```

**AI 回答（摘要）：**

生成结果给出了四个函数的完整实现，并指出：
- `alloc_proc` 必须额外清零 Lab5 新增的 `exit_code`、`wait_state`、`cptr/optr/yptr`；
- `kernel_thread` 的 `SSTATUS_SPP` 决定首次返回进入 S 态；
- `init_main` 的等待循环要按 `do_wait` 的实际返回值写。

**我的核对与修正：**

1. `proc_init` 调用 `alloc_proc` 时，`alloc_proc` 尚未定义，编译报隐式声明；在文件顶部补 `static struct proc_struct *alloc_proc(void);`。
2. `init_main` 最初写成 `while (do_wait(...) == 0)`；罗茜确认 `do_wait` 成功回收返回 0、失败返回负值，改为 `while ((ret = do_wait(...)) >= 0) schedule();`，同时兼容 0 和正 PID。
3. `init_main` 末尾由罗茜在集成时补了 `sbi_shutdown()`，用于评分时正常结束 QEMU。

### 2. 第一次进入用户态：`user_main` 与 `kernel_execve`

#### 第 2 轮：构造用户态 TrapFrame 并复用 `__trapret`

**我的提示词：**

```text
〖任务说明〗
在 proc.c 中实现：
- static int user_main(void *arg);
- static int kernel_execve(const char *name, unsigned char *binary, size_t size)。
目标是让 user_main 通过 KERNEL_EXECVE/KERNEL_EXECVE2 选中用户程序，
kernel_execve 构造用户态 TrapFrame 后跳 __trapret，首次进入 U 态。

约束：
- 只修改 proc.c 的 function2 区域；
- 不修改 tools/user.ld、user/ 下的程序、trapentry.S；
- 保留 lab5 注释，YOUR CODE 替换为 2412896。

〖依赖说明〗
- KERNEL_EXECVE 宏已由框架给出，符号为
  _binary_obj___user_<name>_out_start/_size；
- current->tf 指向当前内核栈上的 TrapFrame；
- USTACKTOP=0x80000000 在 memlayout.h；
- SSTATUS_SPP/SPIE/SIE 在 riscv.h；
- do_execve 已由 proc.h 声明。

〖接口清单〗
- 保持 user_main、kernel_execve 的 static 签名；
- 允许在文件顶部增加 kernel_execve 前置声明。

〖行为规格〗
user_main：
- 定义 TEST 时调用 KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE)；
- 否则调用 KERNEL_EXECVE(exit)；
- execve 返回则 panic("user_main execve failed.\n")。

kernel_execve：
- tf=current->tf，保存 tf 地址；
- 清零 tf；a0=0；sp=USTACKTOP；epc=0；
- status 清 SPP/SIE、置 SPIE；
- 调用 do_execve；失败返回负值；
- 成功后 mv sp, tf; j __trapret，不返回普通调用者。

验证：
- make 通过；
- 集成后 hello 输出 Hello world!!. / I am process 2. / hello pass.
```

**AI 回答（摘要）：**

生成结果用 `__KERNEL_EXECVE` 宏连接用户程序符号，并在 `kernel_execve` 末尾用内联汇编 `mv sp, tf; j __trapret`。同时提醒：`kernel_execve` 在 `user_main` 的宏展开处被引用，必须先声明。

**我的核对与修正：**

1. 补 `static int kernel_execve(...);` 前置声明，消除 `implicit declaration` 和 `static declaration follows non-static`。
2. `do_execve` 内部会重新设置 `tf->epc/sp/status`，因此 `kernel_execve` 里的初值只作为兜底；关键是保存 `tf` 地址并在 execve 成功后跳 `__trapret`。
3. 在集成副本中 `hello` 能正常输出三行，说明第一次 U 态切换链路打通。

### 3. ELF 装载：`do_execve` 与 `load_icode`

#### 第 3 轮：回收旧地址空间并装入新 ELF

**我的提示词：**

```text
〖任务说明〗
在 proc.c 中实现：
- int do_execve(const char *name, size_t len, unsigned char *binary, size_t size);
- static int load_icode(unsigned char *binary, size_t size)。
要求解析 ELF、建立新 mm/页表、装入 PT_LOAD 段、建立用户栈，
并设置可 sret 到用户入口的 TrapFrame。

约束：
- 只修改 proc.c 的 function2 区域；
- 使用框架已有的 mm_create、setup_pgdir、mm_map、pgdir_alloc_page、
  get_page、page_insert、exit_mmap、put_pgdir、mm_destroy；
- 不修改 vmm.c、pmm.c 中贺佳怡/我的其他函数；
- 保留 lab5 注释。

〖依赖说明〗
- elfhdr/proghdr/ELF_MAGIC/ELF_PT_LOAD/ELF_PF_* 在 libs/elf.h；
- USTACKTOP/USTACKSIZE/USTACKPAGE 在 memlayout.h；
- VM_READ/VM_WRITE/VM_EXEC/VM_STACK 在 vmm.h；
- PTE_U/R/W/X 在 mmu.h；
- SATP_MODE Sv39 写法与 pmm.c::enable_paging 相同；
- setup_pgdir 会把 boot_pgdir_va 复制到新页表。

〖接口清单〗
- 保持 do_execve 的公开签名；
- load_icode 为 static；
- 允许内部错误标签 bad_cleanup_mmap/bad_elf_cleanup_pgdir 等。

〖行为规格〗
do_execve：
- 若 current->mm != NULL：先切回内核页表，exit_mmap/put_pgdir/mm_destroy，
  current->mm=NULL、current->pgdir=boot_pgdir_pa；
- load_icode；失败返回负值；成功 set_proc_name 并返回 0。

load_icode：
- current->mm 必须为 NULL；
- mm_create + setup_pgdir；
- 校验 ELF_MAGIC、e_phoff、e_phnum、e_phentsize；
- 对每个 PT_LOAD：
  - p_filesz > p_memsz 返回 -E_INVAL；
  - p_flags 映射为 VM_* 和 PTE_*；
  - mm_map；
  - 按 p_memsz 逐页 pgdir_alloc_page 并清零；
  - 按 p_filesz 把文件内容拷入页；
- 用户栈 [USTACKTOP-USTACKSIZE, USTACKTOP)，逐页 alloc_page + page_insert；
- current->mm=mm，current->pgdir=PADDR(mm->pgdir)，写 Sv39 satp，flush_tlb；
- tf 清零，sp=USTACKTOP，epc=e_entry，status 清 SPP、置 SPIE；
- 失败按 exit_mmap -> put_pgdir -> mm_destroy 回收。

验证：
- make 通过；
- hello/pgdir/testbss 能进入用户态并正常退出；
- init_main 打印 init check memory pass.
```

**AI 回答（摘要）：**

生成结果按上述流程实现，并提醒两个细节：
- `.bss` 必须按 `p_memsz` 分配并清零，不能只拷贝 `p_filesz`；
- `exec` 替换当前进程映像时，必须先切回内核页表再释放旧 mm，否则释放过程中代码仍在旧地址空间上执行。

**我的核对与修正：**

1. 在 `load_icode` 中增加 `e_phentsize == sizeof(struct proghdr)` 和 `e_phoff/e_phnum` 边界检查，避免非法 ELF 越界。
2. 页面分配按 `ROUNDDOWN(p_va, PGSIZE)` 到 `p_va + p_memsz`，先清零再按 `p_filesz` 拷贝；`get_page` 取页后按页内偏移拷贝，兼容非页对齐的段。
3. 用户栈按 `USTACKPAGE=256` 页全部预映射，权限 `PTE_U|PTE_W|PTE_R`。
4. 失败路径按 `exit_mmap -> put_pgdir -> mm_destroy` 顺序清理，避免半成品 mm。

### 4. GCC 15.1.0 兼容性

#### 第 4 轮：处理 `free_area` 和 `kva2page`

**我的提示词：**

```text
〖任务说明〗
在 GCC 15.1.0 下编译 Lab5 时出现：
- default_pmm.c: static declaration of 'free_area' follows non-static declaration；
- kmalloc.c: passing argument 1 of 'kva2page' makes pointer from integer
  without a cast。
请在不改变接口和运行语义的前提下修复。

〖依赖说明〗
- default_pmm.h 声明 extern free_area_t free_area;
- kva2page 的签名是 struct Page *kva2page(void *kva)；
- __slob_free_pages 的参数是 unsigned long kva。

〖接口清单〗
- 不修改 default_pmm.h、pmm.h 的签名；
- 只调整 default_pmm.c 的定义和 kmalloc.c 的调用处。

〖行为规格〗
- free_area 改为非 static 全局定义，与头文件 extern 匹配；
- kva2page 调用处补 (void *)kva；
- GCC 10/13/15 均可编译，运行行为不变。

验证：
- make 通过；
- make grade 能进入 QEMU。
```

**AI 回答（摘要）：**

建议把 `static free_area_t free_area;` 改为 `free_area_t free_area;`，并在 `__slob_free_pages` 中写 `kva2page((void *)kva)`。

**我的核对与修正：**

两处修改后 GCC 15 构建通过。另记录：GCC 15 会把用户程序 `divzero.c` 的 `1 / zero` 优化为 0，而 RISC-V 硬件除零返回 -1；这是用户程序 UB 与编译器优化的差异，最终验收以 GCC 10.2.0 的 130/130 为准，不改测试程序。

### 5. 用户程序预加载问题（Challenge）

**我的提示词：**

```text
请结合 Lab5 的 Makefile 和 KERNEL_EXECVE 宏，解释：
1. 用户程序是在什么时候被加载到内存中的？
2. 与常规操作系统从文件系统加载可执行文件有什么区别？
3. 为什么 ucore Lab5 要采用这种方式？
要求给出代码级依据，不要只讲概念。
```

**AI 回答（摘要）：**

用户程序在构建内核镜像时就被链接进内核：Makefile 用 `--format=binary` 把 `obj/__user_<name>.out` 链进 `bin/kernel`，生成 `_binary_obj___user_<name>_out_start/_size` 符号；QEMU 加载 `bin/ucore.img` 时，这些字节已经随内核镜像位于物理内存中。常规 OS 从文件系统读取 ELF 并按需装入；Lab5 没有文件系统，所以用这种“链接进内核”的方式简化加载。优点是无需文件系统和块设备驱动；缺点是要改用户程序就得重新链接内核，内核镜像也会变大。

**我的核对：**

在 `Makefile` 的 `$(kernel)` 规则和 `KERNEL_EXECVE` 宏中逐项确认了符号名和链接方式，报告中的说明与代码一致。

## 二、贺佳怡（2412105）

来源为提交 `bcc1ddd` 的提示词与 bug 记录。

---

### 一、练习 spec

#### 1. 功能模块三：fork 与地址空间复制

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

#### 2. COW Challenge

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

### 二、prompt 迭代记录

#### 2.1 模块三：普通 fork

##### 第 1 轮 prompt（骨架）

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

##### 第 2 轮 prompt（地址空间）

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

##### 第 3 轮 prompt（失败回滚）

```text
copy_range 任何一步失败（alloc_page 失败或 page_insert 失败）都必须回滚：
用 unmap_range(to, start, addr) 撤销子进程中已经建立的映射，
并保证失败时不会留下引用计数泄漏。
copy_mm 在 dup_mmap 返回 -E_NO_MEM 后执行 exit_mmap + put_pgdir + mm_destroy。
请分析每个失败点的资源归属。
```

**最终结果：** `badarg`、`forktest` 通过，失败路径无泄漏。

---

#### 2.2 COW Challenge

##### 第 1 轮 prompt（设计）

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

##### 第 2 轮 prompt（缺页处理）

```text
实现 do_pgfault 的 COW 分支：
- 先做 VMA 检查和读写权限检查；
- PTE 有效时只可能是 COW 写缺页；非 COW 的有效 PTE 再次写缺页返回 -E_INVAL；
- ref > 1：alloc_page + memcpy，新页 perm = (旧 PTE & 0x3FF) | PTE_W & ~PTE_COW，
  page_insert 替换；page_insert 失败时释放新页，旧映射保持不变；
- ref == 1：原地 (*ptep | PTE_W) & ~PTE_COW，并 tlb_invalidate。
```

**结果：** `cowtest` 的三代隔离通过。

##### 第 3 轮 prompt（回滚）

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

### 三、bug 与修复记录

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

### 四、测试记录

#### 4.1 环境

| 项 | 值 |
|---|---|
| 提交 | `lab5-integration` @ `1e619bc` |
| 编译器 | `riscv64-unknown-elf-gcc 13.2.0` |
| QEMU | `4.1.1` |
| 构建 | `cd code && make`（通过；仅有 `user/testbss.c` 自身的数组越界告警） |

#### 4.2 模块三相关测试

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

#### 4.3 `cowtest`

```text
kernel_execve: pid = 2, name = "cowtest".
cowtest pass.
all user-mode processes have quit.
init check memory pass.
```

#### 4.4 分配失败回滚（故障注入）

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

#### 4.5 完整 `make grade`（本机）

| 项 | 结果 |
|---|---|
| 原版 `make grade` | 0/120，全部 `no $qemu_out` |
| 原因 | 本机无 `riscv64-unknown-elf-gdb`；`gdb-multiarch 15.1` 与 QEMU 4.1.1 gdbstub 不兼容（`Truncated register 37 in remote 'g' packet`），grade 的 `-S` + gdb 流程使 qemu 一直暂停 |
| 临时适配后 | **123/130**（仅在 `/tmp` 副本跳过 gdb 断点，评分正则不变） |
| 唯一失分 | `divzero`：GCC 13.2 在 `-O2` 下把 `1/zero`（UB）优化为 0，期望是 RISC-V 硬件除零的 -1；与本模块代码无关 |
| 罗茜环境 | GCC 10.2.0 + QEMU 4.1.1，130/130，作为最终验收基线 |

#### 4.6 QEMU 8.2.2 兼容性冒烟

- `/usr/bin/qemu-system-riscv64` 为 8.2.2，但本套 ucore 使用 legacy SBI（`libs/sbi.h` 中 ext 0/1/2/8）；
- QEMU 8.2 自带 OpenSBI 1.x 已删除 legacy 扩展，内核无法输出 `(THU.CST) os is loading`；
- 结论：QEMU 8.2.2 只做兼容性记录，不作为实验运行环境，不据此修改实验语义。

## 三、罗茜（2412634）

### 模块一 spec：异常返回与系统调用

用户态发起 `ecall` 后进入内核，恢复用户栈和返回地址，把系统调用结果写回 `a0`。同时保留内核态异常返回路径。验证非法系统调用号、用户态缺页入口和返回后的执行位置。

#### 最终提示词

> 按实现整理的提示词，依据现有报告草稿。

````markdown
在 RISC-V ucore Lab5 中实现用户态系统调用链路，要求：

1. 阅读 trapentry.S，实现 SAVE_ALL/RESTORE_ALL，使用 36 个 8 字节槽位保存通用寄存器和 sstatus、sepc、stval、scause。
2. 用户态 trap 时正确切换内核栈；返回用户态前把内核栈顶写入 sscratch。
3. __alltraps 将 sp 作为 trapframe 参数传入 trap()。
4. exception_handler 只对 CAUSE_USER_ECALL 将 epc 加 4，然后调用 syscall。
5. syscall 从 current->tf 读取 a0 作为系统调用号，以 a1..a5 作为参数，查表分发并把结果写回 a0。
6. 非法调用号返回 -E_INVAL，不允许越界访问函数表。
7. 缺页异常按 do_pgfault(mm, error_code, tval) 统一转发。
8. 所有新增代码保留 lab5 注释，并用 2412634 标注负责人。
````

#### 实现迭代过程

##### 第一次迭代

**实现内容：**

- 完成用户态/内核态双向 `RESTORE_ALL`
- 完成 `__alltraps` 参数传递
- 完成系统调用表与 `sys_*` 函数
- 完成缺页异常入口

**遇到的问题：**

1. 最初接口草案将 `do_wait()` 描述为成功时返回子进程 pid，但用户库 `wait()`、`waitpid()` 和评分测试实际期望成功返回 0，失败返回负错误码。
2. 项目仍包含其他成员负责的未实现函数，完整链接无法通过，不能把链接失败错误归因于 trap 或系统调用模块。

**问题解决策略：**

- 将 `do_wait()` 成功返回值修正为 0，并同步修改接口文档。
- 对 trap、syscall、proc 中的罗茜模块分别编译目标文件，区分“本模块编译错误”和“其他模块尚未实现”的链接错误。

##### 第二次迭代

**改进内容：**

- 给关键控制流补充中文注释，并在主要实现块标注 `2412634`。
- 明确缺页失败时用户进程应被终止，而不是让整个内核崩溃。
- 明确 `do_kill()` 只设置退出请求，实际资源回收仍由 `do_exit()` 完成。

**当前结果：**

- ✅ `kern/trap/trap.c` 编译通过
- ✅ `kern/trap/trapentry.S` 汇编通过
- ✅ `kern/syscall/syscall.c` 编译通过
- ✅ `kern/process/proc.c` 编译通过
- 集成版通过完整评分。

### 模块四 spec：进程控制与回收

等待成功返回 0，参数错误或无匹配子进程返回负错误码。进程退出后进入僵尸态，等父进程回收。调度以 `need_resched` 为入口，终止请求由目标进程在退出路径处理。

#### 最终提示词

> 按实现整理的提示词，依据现有报告草稿。

````markdown
在 RISC-V ucore Lab5 中实现进程生命周期函数：

1. do_wait(pid, code_store)：pid=0 等待任意子进程，pid>0 等待指定直属子进程。
2. 写入用户退出码前必须调用 user_mem_check。
3. 只有僵尸子进程才能被回收；回收时关中断移除进程树和哈希表关系，再释放内核栈和 PCB。
4. do_exit：释放当前进程用户地址空间，保存退出码，转为 PROC_ZOMBIE，唤醒等待父进程，把自己的子进程转交给 initproc，最后调用 schedule。
5. do_yield：只设置 need_resched 并返回 0。
6. do_kill：设置目标 PF_EXITING；睡眠目标需要唤醒；不直接释放其资源。
7. 遵守接口约定，保留 lab5 注释并用 2412634 标记负责人。
````

#### 实现迭代记录

##### 第一次迭代

**遇到的问题：**

- 用户态 `wait()` 实际使用 pid 0 表示任意子进程；`waitpid(-1, ...)` 按非法 pid 返回错误。
- 非法退出码指针必须返回错误但保留僵尸子进程，方便用户程序稍后用合法地址再次等待。
- 退出进程不能直接把子进程留在已死亡父进程下，否则会形成无人回收的进程树。

**解决策略：**

- `do_wait()` 对代码中 `pid != 0` 与 `pid == 0` 两条路径分别处理。
- 在任何摘链操作前验证 `code_store`。
- `do_exit()` 将孤儿子进程转交给 `initproc`。

##### 第二次迭代

**改进内容：**

- 在 `do_exit()` 中先从当前页表切回内核页表，再释放用户页目录。
- 在等待和销毁地址空间时补充错误码及关中断说明。
- 增加中文注释明确僵尸进程、孤儿进程和睡眠唤醒的状态变化。

**当前结果：**

- ✅ 四个目标文件编译通过
- 集成版通过完整评分。

### 联调与双 GDB

| 请求或问题 | 核对与处理 | 实际结果 |
|---|---|---|
| 合入三人的分支后继续构建与评分 | 补齐 Lab4 调度和上下文切换前置，补页表打印，核对进程地址空间引用计数和释放路径 | 构建通过 |
| 程序已经退出，空闲页数检查仍失败 | 修正 ELF 装载后的 `mm_count`、页表目录释放和 SLOB 整页回收。保留内存断言反复评分 | 评分由 74/130 经 123/130 到 130/130 |
| 双 GDB 握手超时，后续报告没有寄存器 | 先核对 QEMU 进程和监听端口。旧会话占用端口后，改用每次新建的 Unix 套接字。用户 ELF 单独加载符号 | 用户端停在 `0x800104` 的 `ecall` |
| A 启动后停住，B 单步后也等待 | 根据 `-S` 和断点状态判断谁在等待。B 连接后运行，A 在每个临时断点处各继续一次 | 两个 QEMU 断点均命中。B 的 PC 返回 `0x800108`，内核正常关机 |
