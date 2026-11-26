# 2412896-石欣哲 Lab5 提示词与 AI 迭代记录

> 按“我提出提示词 → AI 给出初版 → 我核对/编译/运行验证 → 调整提示词再问”的顺序整理。
> 只保留能代表实现思路的轮次，不是完整聊天记录。

## 1. 进程启动链：`alloc_proc` / `proc_init` / `kernel_thread` / `init_main`

### 第 1 轮：搭起 idleproc 到 init_main 的启动链

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

## 2. 第一次进入用户态：`user_main` 与 `kernel_execve`

### 第 2 轮：构造用户态 TrapFrame 并复用 `__trapret`

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

## 3. ELF 装载：`do_execve` 与 `load_icode`

### 第 3 轮：回收旧地址空间并装入新 ELF

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

## 4. GCC 15.1.0 兼容性

### 第 4 轮：处理 `free_area` 和 `kva2page`

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

## 5. 用户程序预加载问题（Challenge）

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
