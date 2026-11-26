# Lab5 功能模块二：创建并加载第一个用户进程

**负责人：** 2412896-石欣哲

## 1. 模块整体逻辑线

本模块把系统从“只有内核线程”推进到“第一个用户程序在用户态执行”，启动链为：

```text
proc_init
  -> alloc_proc(idleproc)
  -> kernel_thread(init_main)
       -> init_main: kernel_thread(user_main) + do_wait
            -> user_main: KERNEL_EXECVE(exit) / KERNEL_EXECVE2(TEST, ...)
                 -> kernel_execve: 构造用户态 TrapFrame
                      -> do_execve: 回收旧 mm，调用 load_icode
                           -> load_icode: 解析 ELF、建页表、装代码/数据/栈
                                -> 跳 __trapret，sret 进入用户态
```

关键点是 `kernel_execve()` 不直接返回 C 调用者，而是把栈切换到刚构造好的 TrapFrame，再跳 `__trapret`，复用异常返回路径完成第一次从 S 态到 U 态的切换。

## 2. 核心函数

### 2.1 `alloc_proc()`

分配并清零 PCB。除 Lab4 的字段外，Lab5 还要初始化 `exit_code`、`wait_state`、`cptr/optr/yptr`：

```c
proc->state = PROC_UNINIT;
proc->pid = -1;
proc->runs = 0;
proc->kstack = 0;
proc->need_resched = 0;
proc->parent = NULL;
proc->mm = NULL;
memset(&proc->context, 0, sizeof(struct context));
proc->tf = NULL;
proc->pgdir = boot_pgdir_pa;
proc->flags = 0;
memset(proc->name, 0, sizeof(proc->name));
proc->exit_code = 0;
proc->wait_state = 0;
proc->cptr = proc->optr = proc->yptr = NULL;
list_init(&(proc->list_link));
list_init(&(proc->hash_link));
```

### 2.2 `proc_init()`

初始化 `proc_list` 和 PID 哈希表，把当前执行现场包装成 PID 0 的 `idleproc`，再通过 `kernel_thread(init_main, NULL, 0)` 创建 `initproc`：

```c
list_init(&proc_list);
for (i = 0; i < HASH_LIST_SIZE; i++) {
    list_init(hash_list + i);
}
idleproc = alloc_proc();
idleproc->pid = 0;
idleproc->state = PROC_RUNNABLE;
idleproc->kstack = (uintptr_t)bootstack;
idleproc->need_resched = 1;
set_proc_name(idleproc, "idle");
current = idleproc;

int pid = kernel_thread(init_main, NULL, 0);
initproc = find_proc(pid);
set_proc_name(initproc, "init");
```

### 2.3 `kernel_thread()`

把函数入口和参数整理成首次运行所需的 TrapFrame：

```c
tf.gpr.s0 = (uintptr_t)fn;      // kernel_thread_entry 中 jalr s0
tf.gpr.s1 = (uintptr_t)arg;     // kernel_thread_entry 中 move a0, s1
tf.epc = (uintptr_t)kernel_thread_entry;
tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
return do_fork(clone_flags, 0, &tf);
```

`SSTATUS_SPP` 保证首次返回进入 S 态；`SSTATUS_SPIE` 保留中断使能；清除 `SSTATUS_SIE` 避免构造期间被中断。

### 2.4 `init_main()`

创建 `user_main`，等待所有子进程退出，并检查内存是否全部回收：

```c
int pid = kernel_thread(user_main, NULL, 0);
if (pid <= 0) {
    panic("create user_main failed.\n");
}

int ret;
while ((ret = do_wait(0, NULL)) >= 0) {
    schedule();
}

cprintf("all user-mode processes have quit.\n");
assert(nr_free_pages_store == nr_free_pages());
assert(kernel_allocated_store == kallocated());
cprintf("init check memory pass.\n");
```

罗茜确认 `do_wait()` 成功回收子进程返回 0，失败返回负错误码，因此 `>= 0` 的循环可以同时兼容 0 和正 PID 两种成功返回。

### 2.5 `user_main()` 与 `kernel_execve()`

`user_main` 根据构建时是否定义 `TEST` 选择用户程序：

```c
#ifdef TEST
    KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    KERNEL_EXECVE(exit);
#endif
```

`KERNEL_EXECVE` 使用链接器生成的 `_binary_obj___user_<name>_out_start/size` 符号，把嵌入内核镜像的 ELF 传给 `kernel_execve`。

`kernel_execve()` 的关键是构造 TrapFrame 后跳 `__trapret`：

```c
struct trapframe *tf = current->tf;
uintptr_t trapframe_addr = (uintptr_t)tf;

memset(tf, 0, sizeof(struct trapframe));
tf->gpr.a0 = 0;
tf->gpr.sp = USTACKTOP;
tf->status = (read_csr(sstatus) & ~SSTATUS_SPP & ~SSTATUS_SIE) | SSTATUS_SPIE;

ret = do_execve(name, len, binary, size);
if (ret != 0) {
    return ret;
}

asm volatile(
    "mv sp, %0\n"
    "j __trapret\n"
    :
    : "r"(trapframe_addr));
```

### 2.6 `do_execve()`

先回收旧地址空间，再装入新程序：

```c
struct mm_struct *mm = current->mm;
if (mm != NULL) {
    write_csr(satp, 0x8000000000000000 | (boot_pgdir_pa >> RISCV_PGSHIFT));
    flush_tlb();
    current->pgdir = boot_pgdir_pa;
    current->mm = NULL;

    exit_mmap(mm);
    put_pgdir(mm);
    mm_destroy(mm);
}

int ret = load_icode(binary, size);
if (ret != 0) {
    return ret;
}
set_proc_name(current, name);
```

### 2.7 `load_icode()`

按 ELF 程序头建立用户地址空间：

1. `mm_create()` + `setup_pgdir()`，新页表先复制内核映射；
2. 校验 `ELF_MAGIC`、`e_phoff`、`e_phnum`、`e_phentsize`；
3. 对每个 `ELF_PT_LOAD` 段：
   - 把 `p_flags` 映射为 `VM_READ/VM_WRITE/VM_EXEC` 和 `PTE_R/PTE_W/PTE_X`；
   - `mm_map()` 建立 VMA；
   - 按 `p_memsz` 逐页 `pgdir_alloc_page()` 并清零，覆盖 `.bss`；
   - 再按 `p_filesz` 把 ELF 文件内容拷入对应页；
4. 建立用户栈：`[USTACKTOP - USTACKSIZE, USTACKTOP)`，逐页 `alloc_page()` + `page_insert()`，权限 `PTE_U | PTE_W | PTE_R`；
5. 设置 `current->mm`、`current->pgdir = PADDR(mm->pgdir)`，写 Sv39 `satp` 并 `flush_tlb()`；
6. 设置 TrapFrame：

```c
struct trapframe *tf = current->tf;
memset(tf, 0, sizeof(struct trapframe));
tf->gpr.sp = USTACKTOP;
tf->epc = elf->e_entry;
tf->status = (read_csr(sstatus) & ~SSTATUS_SPP) | SSTATUS_SPIE;
```

失败路径按 `exit_mmap -> put_pgdir -> mm_destroy` 顺序回收，避免半成品地址空间。

## 3. 用户程序预加载说明（Challenge）

**用户程序是在构建内核镜像时被预先加载进内存的。**

Makefile 先把每个用户程序编译、链接成 ELF（`obj/__user_<name>.out`），再用链接器的 `--format=binary` 把它作为一段二进制数据链接进内核：

```make
$(kernel): $(KOBJS) $(USER_BINS)
	$(LD) $(LDFLAGS) -T tools/kernel.ld -o $@ $(KOBJS) \
	      --format=binary $(USER_BINS) --format=default
```

链接器自动生成：

```text
_binary_obj___user_<name>_out_start
_binary_obj___user_<name>_out_size
```

`KERNEL_EXECVE` 宏直接引用这两个符号，把 ELF 的起止地址传给 `kernel_execve`。因此 QEMU 把 `bin/ucore.img` 加载到 `0x80200000` 时，用户 ELF 已经作为内核镜像的一部分位于内存中；`exec` 不需要读磁盘，只需要解析这段内存并建立用户页表。

**与常规操作系统的区别：**

- 常规 OS 的可执行文件存放在文件系统中，`exec` 时通过文件系统读取 ELF，按需装入内存；
- ucore Lab5 还没有文件系统，无法从磁盘读取用户程序，所以用 `-b binary` 把用户 ELF 直接链接进内核镜像；
- 优点是加载路径简单、不需要文件系统和块设备驱动；缺点是要修改用户程序就必须重新链接内核，内核镜像也会变大。

这正是 Lab5 在“没有文件系统”前提下的简化方案；后续实现文件系统后，可以替换为从文件读取并装入。

## 4. 测试与验证

### 4.1 GCC 15.1.0 + QEMU 4.1.1（石欣哲环境）

```text
riscv64-unknown-elf-gcc 15.1.0
qemu-system-riscv64 4.1.1
```

- `make`：通过；只有 `user/testbss.c` 的 `-Warray-bounds` 预期告警（测试程序故意越界写）。
- `make grade`：`116/130`，其中：
  - `hello`、`testbss`、`pgdir`、`badsegment`、`softint`、`yield`、`badarg`、`exit`、`spin`、`forktest` 的 result/output 均 OK；
  - `divzero` 稳定缺少 `value is -1.`：GCC 15 把 `1 / zero` 优化成 `0`，而 RISC-V 硬件 `div` 除零应返回 `-1`；这是用户程序 UB + 编译器优化的差异，不是内核装载/系统调用逻辑错误；
  - `faultread` / `faultreadkernel` 偶发缺少首行 `kernel_execve`，但手动单独运行均能打印该行，属于评分脚本抓取 QEMU 首行输出的时序问题；同一轮中两个测试交替出现，不是稳定功能失败。
- `cowtest`：通过，输出：

```text
kernel_execve: pid = 2, name = "cowtest".
cowtest pass.
all user-mode processes have quit.
init check memory pass.
```

### 4.2 GCC 10.2.0 + QEMU 4.1.1（罗茜最终验收环境）

罗茜在 `lab5-integration` 的 `1e619bc` 上执行：

```text
make grade: 130/130
cowtest: pass
```

按《分工》的风险约定，最终验收以 GCC 10.2.0 + QEMU 4.1.1 的 130/130 为准；GCC 15 的 `divzero` 差异作为兼容性记录保留，不修改测试程序。

## 5. AI 生成代码的 bug 与修复

1. **`free_area` 声明冲突**：Lab5 的 `default_pmm.h` 声明了 `extern free_area_t free_area;`，第一版沿用了 Lab2 的 `static free_area_t free_area;`，注入评分时编译报 `static declaration follows non-static declaration`。改为非 static 全局定义后通过。
2. **`kva2page` 指针转换**：GCC 15 把 `kva2page(kva)` 的整数参数当成错误，补 `(void *)kva` 后通过。
3. **`kernel_execve` 前置声明**：`KERNEL_EXECVE` 宏在 `user_main` 中展开时 `kernel_execve` 尚未定义，编译器报隐式声明；在文件顶部补 static 前置声明后解决。
4. **`init_main` 等待语义**：最初按标准 ucore 写 `while (do_wait(...) == 0)`，与罗茜确认 `do_wait` 成功返回 0、失败返回负值后，改为 `>= 0`，同时兼容 0 和正 PID。
