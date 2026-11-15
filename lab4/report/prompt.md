# Lab 4 AI 提示词汇总

> 整理版：按“我提出提示词 → AI 给出初版 → 我核对/编译/运行验证 → 调整提示词再问”的顺序整理，
> 只保留能代表实现思路的轮次，不是完整聊天记录。本文档随实验进度增量维护。

## 2412896-石欣哲

### 1. Lab2 基线移植：`page_init()` 与 Best-Fit 分配器

#### 第 1 轮：提出移植需求

**我的提示词：**

```text
〖任务说明〗
在 Lab4 新框架中补齐标有 “//lab2 请补充你在lab2中的代码” 的两处位置，
把小组 Lab2 已经验证过的物理内存管理代码移植进来，使 Lab4 的 pmm_init()
能够完成物理页描述符初始化和空闲页管理。

修改位置与约束：
- 只允许修改 kern/mm/pmm.c 与 kern/mm/default_pmm.c 中的 lab2 占位区域；
- 不得修改 check_pgdir()、check_boot_pgdir()、basic_check()、best_fit_check()
  以及 tools/ 下的测试与评分脚本；
- 不得改动 Lab4 的函数签名、数据结构、宏定义和 lab4 注释；
- 保留 Lab2 原有中文注释与负责人标记，不把队友的 Lab2 工作标成自己的学号。

输出要求：
给出可直接编译的两段代码，并说明 page_init() 与 default_pmm_manager
各成员函数之间的调用关系。

〖依赖说明〗
- kern/mm/pmm.c 已包含 dtb.h，可调用 get_memory_base() / get_memory_size()；
- memlayout.h 定义了 PHYSICAL_MEMORY_OFFSET、struct Page、free_area_t、
  PageReserved / SetPageReserved / PageProperty 等；
- pmm.h 定义了 struct pmm_manager、page2pa()、pa2page()、PADDR()、KADDR()；
- default_pmm.c 已包含 pmm.h、list.h、stdio.h；
- Lab4 的 pmm.c 固定使用 &default_pmm_manager，因此实现必须导出该对象。

〖接口清单〗
- 在 pmm.c 中实现 static void page_init(void)；
- 在 default_pmm.c 中实现 default_init、default_init_memmap、
  default_alloc_pages、default_free_pages、default_nr_free_pages，
  并导出 const struct pmm_manager default_pmm_manager；
- 允许增加文件内 static 辅助函数，不改变对外接口。

〖行为规格〗
page_init：
- 前置：pmm_manager 已初始化，DTB 已解析；
- 后置：va_pa_offset 被设置为 PHYSICAL_MEMORY_OFFSET；
- 后置：根据 DTB 的 mem_base / mem_size 计算 npage，并把 Page 描述符
  数组放在内核 end 之后、按页对齐；
- 后置：所有描述符先标记为 reserved，只有描述符数组之后的完整空闲页
  才通过 init_memmap() 注册给 PMM；
- DTB 信息非法或 Page 数组越界时 panic，不允许静默继续。

default_pmm_manager：
- init：清空 free_list，nr_free = 0；
- init_memmap：把 [base, base+n) 逐页清标志、ref 置 0，首頁 property = n，
  按物理地址升序插入 free_list，nr_free += n；
- alloc_pages：在 free_list 中选择 property >= n 且 property 最小的块；
  找到后从链表摘除，剩余部分作为新块插回，nr_free -= n；
- free_pages：按地址升序插回 free_list，并分别与前后相邻空闲块合并；
- nr_free_pages：返回 nr_free；
- check：指向框架已有的 best_fit_check()。

验证：
- make 通过；
- QEMU 启动后输出 check_alloc_page() succeeded!；
- 不修改任何测试断言。
```

**AI 回答（摘要）：**

AI 给出了 `page_init()`、Best-Fit 的五个成员函数以及 `default_pmm_manager`
结构体初始化，核心逻辑与 Lab2 的 `best_fit_pmm.c` 一致，只把函数名改成
`default_*` 以匹配 Lab4 的 `pmm.c`。同时指出：Lab4 的 `default_pmm.c`
顶部保留了 `#include <best_fit_pmm.h>`，但新框架没有这个头文件，需要删除。

**我的核对与修正：**

1. 删除不存在的 `<best_fit_pmm.h>`，避免
   `fatal error: best_fit_pmm.h: No such file`。
2. `default_pmm.c` 只保留实现，不重复粘贴 Lab2 已有的 `basic_check()` /
   `best_fit_check()`，避免与框架测试函数重名。
3. `struct pmm_manager` 的 `.check` 指向框架里的 `best_fit_check()`，
   因此在结构体前增加 `static void best_fit_check(void);` 前置声明。
4. 编译通过后，我在 QEMU 中确认 `check_alloc_page() succeeded!`。

#### 第 2 轮：处理 `kmalloc.c` 的工具链报错

**我的提示词：**

```text
〖任务说明〗
把 Lab2 代码移植进 Lab4 后，kern/mm/kmalloc.c 在 GCC 15.1.0 下报错：
  passing argument 1 of 'kva2page' makes pointer from integer without a cast
请在不改变 kva2page() 既有签名、不修改 kmalloc 逻辑的前提下修复。

〖依赖说明〗
- __slob_free_pages(unsigned long kva, int order) 的参数是整数地址；
- pmm.h 中 kva2page() 的签名是 struct Page *kva2page(void *kva)。

〖接口清单〗
- 不修改 kva2page() 的声明与定义；
- 只允许调整调用处的类型转换。

〖行为规格〗
- 前置：kva 是内核虚拟地址的整数形式；
- 后置：调用 kva2page((void *)kva)，编译通过；
- 后置：释放的仍是同一个物理页，运行期行为不变。

验证：
- make 通过；
- 启动后 kmalloc_init() succeeded!。
```

**AI 回答（摘要）：**

AI 指出这是整数到指针的隐式转换，建议在调用处显式写成
`kva2page((void *)kva)`，不要为了适配调用点去改 `kva2page()` 的签名。

**我的核对与修正：**

只改了 `__slob_free_pages()` 中的一处调用，重新编译通过，
`kmalloc_init() succeeded!` 正常输出。

---

### 2. Lab3 基线移植：`interrupt_handler()` 与 `exception_handler()`

#### 第 1 轮：移植中断/异常处理

**我的提示词：**

```text
〖任务说明〗
在 Lab4 新框架的 kern/trap/trap.c 中，补齐 “//lab3 请补充你在lab3中的代码”
占位区域，把 Lab3 已经通过测试的中断/异常处理代码移植进来。
Lab4 的 trap() 已经负责按 cause 最高位分发，因此只需要提供
interrupt_handler() 和 exception_handler() 两个函数。

约束：
- 只修改 kern/trap/trap.c 的 lab3 占位区域；
- 不修改 idt_init()、print_trapframe()、print_regs()、trap() 和 trapentry.S；
- 不修改 trap.h 中的结构体布局；
- 保留 lab3 注释与原作者标记（2412105、2412634）。

输出要求：
给出两个函数的完整代码，并说明 Lab4 与 Lab3 在 trap 分发方式上的差异。

〖依赖说明〗
- clock.h 提供 clock_set_next_event() 和 extern volatile size_t ticks；
- riscv.h 提供 IRQ_S_TIMER、IRQ_S_SOFT、SIP_SSIP、
  CAUSE_ILLEGAL_INSTRUCTION、CAUSE_BREAKPOINT；
- trap.h 提供 struct trapframe 与 print_trapframe()/print_regs()；
- TICK_NUM 已在 trap.c 中定义为 100。

〖接口清单〗
- void interrupt_handler(struct trapframe *tf)；
- void exception_handler(struct trapframe *tf)；
- 两个函数必须在 trap() 之前可见。

〖行为规格〗
interrupt_handler：
- 先剥离 cause 的最高位，得到真正的中断号；
- 时钟中断：调用 clock_set_next_event()，ticks++，
  每 TICK_NUM 次调用 print_ticks()；
- supervisor 软件中断：清除 sip.SSIP 并输出提示；
- 其他中断：输出 cause 并打印完整 TrapFrame。

exception_handler：
- 非法指令 / 断点：读 16 位指令，低两位为 0x3 时长度 4，否则长度 2，
  按类型输出地址后把 tf->epc 推进实际指令长度；
- 其他异常：调用 print_trapframe(tf) 兜底；
- 不得固定给 epc 加 4。

验证：
- make 通过；
- QEMU 启动后时钟中断持续触发，输出 100 ticks；
- 不修改 Lab4 的 trap() 分发函数。
```

**AI 回答（摘要）：**

生成结果直接复用了 Lab3 的两个处理函数，并指出 Lab4 的 `trap()` 已经
内联了 Lab3 的 `trap_dispatch()` 逻辑，因此不能再重复定义
`trap_dispatch()`，否则会与框架代码重复。还提醒：Lab4 的 `trap.h`
没有声明这两个函数，必须把定义放在 `trap()` 之前，或者补函数声明。

**我的核对与修正：**

1. 只移植 `interrupt_handler()` 和 `exception_handler()`，没有移植
   `trap_dispatch()`，避免与 Lab4 已有的 `trap()` 分发逻辑冲突。
2. 两个函数放在 `trap()` 之前，利用定义即声明，避免修改 `trap.h`。
3. 保留 Lab3 的原作者标记 `2412105` / `2412634`，不把队友工作算作本人实现。
4. 编译时曾出现 `implicit declaration of function 'interrupt_handler'`，
   原因正是函数未在 `trap()` 前声明；调整定义顺序后消除。

---

### 3. Lab4 功能模块一：`page_insert()` 与 `page_remove()`

#### 第 1 轮：先实现基本映射

**我的提示词：**

```text
〖任务说明〗
在 Lab4 的 kern/mm/pmm.c 中，只修改 “//LAB4 function1 YOUR CODE” 标记处，
实现：
- int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
- void page_remove(pde_t *pgdir, uintptr_t la)。
要求把物理页映射到指定线性地址，并在删除映射时正确维护引用计数和 TLB。

约束：
- 不得修改 get_pte()、get_page()、tlb_invalidate()、check_pgdir()、
  check_boot_pgdir()、trap 相关文件、proc 相关文件和 tools/ 下脚本；
- 不得修改函数签名、struct Page、pde_t/pte_t 和 PTE_* 宏；
- 保留 “LAB4 function1” 注释，把 YOUR CODE 替换为 2412896。

〖依赖说明〗
- get_pte(pgdir, la, create)：负责定位/创建多级页表项，失败返回 NULL；
- pte_create(ppn, type)：把页号与权限位组合成页表项；
- pte2page()、page2ppn()：页表项与 struct Page 互转；
- page_ref_inc()、page_ref_dec()：维护引用计数；
- free_page()：引用计数归零时释放物理页；
- tlb_invalidate(pgdir, la)：失效指定地址的 TLB 项；
- PTE_V、PTE_R、PTE_W、PTE_U 位于 mmu.h；-E_NO_MEM 位于 error.h。

〖接口清单〗
- int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)；
- void page_remove(pde_t *pgdir, uintptr_t la)；
- 允许增加 static 辅助函数。

〖行为规格〗
page_insert：
- 用 get_pte(pgdir, la, 1) 定位或创建 PTE；
- get_pte 返回 NULL 时返回 -E_NO_MEM；
- 把 PTE 写成 pte_create(page2ppn(page), perm)，并刷新 TLB；
- 成功返回 0。

page_remove：
- 用 get_pte(pgdir, la, 0) 定位 PTE；
- 有映射时减少引用计数并在归零时释放物理页，清空 PTE 并刷新 TLB；
- 未映射时安静返回。

验证：
- make 通过；
- check_pgdir()、check_boot_pgdir() 通过。
```

**AI 回答（摘要）：**

生成结果是一个“只处理新映射”的版本：`page_insert()` 直接
`page_ref_inc(page)` 后覆盖 PTE，`page_remove()` 直接减引用并清空 PTE，
末尾都调用了 `tlb_invalidate()`。它没有区分“原地址已经映射到同一页”
和“原地址映射到其他页”。

**我运行后的结果：**

`check_pgdir()` 在下面这条断言失败：

```text
assert(page_ref(p1) == 1);
```

原因：第一次把 `p1` 映射到 `0x0` 后引用为 1；再次把 `p1` 映射到
`PGSIZE` 时，生成结果又无条件加 1；如果原 PTE 已经指向同一物理页，
引用计数就会多加。这说明第一版提示词的 Specification 漏掉了分支。

#### 第 2 轮：补充“同一页重复映射”和“替换旧映射”

**我的提示词：**

```text
〖任务说明〗
上一版 page_insert() 在 check_pgdir() 中失败：
  assert(page_ref(p1) == 1);
原因是没有区分“同一物理页重复映射同一地址”和“原地址映射到其他物理页”。
请只修改 kern/mm/pmm.c 的 //LAB4 function1 区域，补齐这两种分支。

〖依赖说明〗
- 原 PTE 有效时，pte2page(*ptep) 可以得到旧的 struct Page；
- page_ref_inc/page_ref_dec 只改引用计数，不负责释放；
- free_page() 只能在引用计数归零后调用；
- tlb_invalidate(pgdir, la) 用于失效指定地址的 TLB。

〖接口清单〗
- 保持 page_insert()、page_remove() 签名不变；
- 允许增加 static void page_remove_pte(pde_t *, uintptr_t, pte_t *)。

〖行为规格〗
page_insert：
- get_pte(pgdir, la, 1) 返回 NULL 时返回 -E_NO_MEM，不留下半完成映射；
- 新映射建立前先 page_ref_inc(page)；
- 若原 PTE 有效且指向同一物理页：只更新权限，撤销刚才的引用增量，
  不得重复增加引用；
- 若原 PTE 有效且指向其他物理页：先调用 page_remove_pte() 完整撤销旧映射；
- 最后写入 pte_create(page2ppn(page), perm)，并 tlb_invalidate()。

page_remove_pte：
- 前置：ptep 指向一个页表项；
- 若 PTE 有效：page_ref_dec(page)，归零时 free_page(page)；
- 清空 PTE，并 tlb_invalidate(pgdir, la)；
- 若 PTE 无效：什么都不做。

page_remove：
- get_pte(pgdir, la, 0)，不得为了删除而创建中间页表；
- 未映射时安静返回；
- 重复调用必须幂等。

验证：
- check_pgdir()、check_boot_pgdir() 全部通过；
- 注入测试输出 [Test PMM] PASS。
```

**AI 回答（摘要）：**

第二版给出了 `page_remove_pte()` 辅助函数，并在 `page_insert()` 中增加
“同一页 / 不同页”两条分支，最终实现如下：

```c
static void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep) {
    if (*ptep & PTE_V) {
        struct Page *page = pte2page(*ptep);
        if (page_ref_dec(page) == 0) {
            free_page(page);
        }
        *ptep = 0;
        tlb_invalidate(pgdir, la);
    }
}

int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm) {
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL) {
        return -E_NO_MEM;
    }

    page_ref_inc(page);

    if (*ptep & PTE_V) {
        struct Page *old_page = pte2page(*ptep);
        if (old_page == page) {
            page_ref_dec(page);
        } else {
            page_remove_pte(pgdir, la, ptep);
        }
    }

    *ptep = pte_create(page2ppn(page), perm);
    tlb_invalidate(pgdir, la);
    return 0;
}

void page_remove(pde_t *pgdir, uintptr_t la) {
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep != NULL) {
        page_remove_pte(pgdir, la, ptep);
    }
}
```

**我的核对与验证：**

1. 逐条对照 `check_pgdir()` 的断言，确认：
   - 首次映射后引用为 1；
   - 替换旧页后旧页引用归零；
   - 同一页重复插入同一地址时引用净增 0；
   - 重复 `page_remove()` 不重复减引用。
2. 在临时验证副本中运行，输出：
   `check_pgdir() succeeded!`、`check_boot_pgdir() succeeded!`、
   `[Test PMM] PASS`。
3. 保留 `//LAB4 function1 2412896` 标记，没有改动 `get_pte()` 和测试代码。

---

### 4. GCC 15 兼容性：`kernel_thread` 回调类型

#### 第 1 轮：定位并修复工具链差异

**我的提示词：**

```text
〖任务说明〗
在 GCC 15.1.0 下执行 Lab4 的 make grade 时，注入的 tools/test_lab4.c
报错：
  passing argument 1 of 'kernel_thread' from incompatible pointer type
  [-Wincompatible-pointer-types]
而 GCC 13 环境只把它当警告。请给出不修改测试文件、不改变
kernel_thread 语义的兼容方案。

〖依赖说明〗
- test_lab4_thread 的签名是 void test_lab4_thread(void *arg)；
- proc.h 中 kernel_thread 的签名是
  int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)；
- Lab4 Makefile 没有 -Werror，但 GCC 14+ 默认把该诊断升级为错误。

〖接口清单〗
- 保持 kernel_thread 的公开签名不变；
- 不修改 tools/test_lab4.c、tools/grade.sh；
- 只允许调整编译选项或工程内兼容层。

〖行为规格〗
- 前置：GCC 13 可编译，GCC 15 报错；
- 后置：GCC 10/13/15 都能编译；
- 后置：仍保留该诊断作为警告，便于发现真正的类型错误；
- 不改变运行期行为。

验证：
- make 通过；
- make grade 能进入 QEMU 并输出三项测试结果。
```

**AI 回答（摘要）：**

建议在 Makefile 的 CFLAGS 中加入
`-Wno-error=incompatible-pointer-types`：保留警告但不升级为错误，
既不改测试文件，也不改 `kernel_thread` 的签名。

**我的核对与修正：**

已在 `lab4/code/Makefile` 的 CFLAGS 中加入该选项，并加注释说明原因。
这样 GCC 10/13 环境不受影响，GCC 15 环境也能继续构建。

---

### 5. Lab3 Trap 入口基线补齐：`RESTORE_ALL` 与 `__alltraps`

#### 第 1 轮：补齐 Trap 返回链

**我的提示词：**

```text
〖任务说明〗
Lab4 的 kern/trap/trapentry.S 仍是 Lab3 占位：
RESTORE_ALL 为空，__alltraps 没有调用 trap()，导致首次 sret 和时钟中断
无法恢复 TrapFrame。请只修改该文件，补齐：
- RESTORE_ALL：恢复 sstatus、sepc、sbadaddr、scause，再恢复 x1、x3~x31，
  最后恢复 sp；
- __alltraps：SAVE_ALL 后执行 mv a0, sp; jal trap；
- 保留已有的 forkrets。

约束：
- 不修改 SAVE_ALL 的保存布局；
- 不修改 struct trapframe、trap()、idt_init()；
- sbadaddr 使用 0x143，兼容不认识 CSR_SBADADDR 的 GCC 10；
- 保留 lab3 标记和负责人学号。

〖依赖说明〗
- SAVE_ALL 把 x1、x3~x31 保存在 1..31*REGBYTES，
  原始 sp 在 2*REGBYTES，sstatus/sepc/sbadaddr/scause 在 32..35*REGBYTES；
- riscv.h 定义 STORE/LOAD/REGBYTES；
- __trapret 已存在，内部执行 RESTORE_ALL 和 sret；
- forkrets 已存在，负责 move sp, a0; j __trapret。

〖接口清单〗
- 保持 RESTORE_ALL、__alltraps、__trapret、forkrets 符号名不变；
- 允许在 __alltraps 前增加 .align(2)。

〖行为规格〗
- 前置：sp 指向当前 TrapFrame；
- 后置：CSR 与通用寄存器按 SAVE_ALL 的偏移恢复；
- 后置：sp 最后从 2*REGBYTES 恢复；
- 后置：trap() 返回后落入 __trapret；
- 时钟中断能反复触发并输出 100 ticks。

验证：
- make 通过；
- QEMU 中 check_pgdir/check_boot_pgdir/check_vmm 通过，并持续输出 100 ticks。
```

**AI 回答（摘要）：**

生成结果给出了 RESTORE_ALL 的 CSR/通用寄存器恢复序列和
`mv a0, sp; jal trap` 入口调用，并确认 `forkrets` 必须保留在 `__trapret` 之后。

**我的核对与修正：**

1. `sbadaddr` 使用 `0x143` 而不是 `CSR_SBADADDR`，兼容 GCC 10。
2. 恢复顺序为 CSR → x1/x3~x31 → sp，sp 最后恢复。
3. 在 `/tmp` 临时副本中只补 `proc_init()` 空桩，启动后输出
   `check_pgdir() succeeded!`、`check_boot_pgdir() succeeded!` 和持续
   `100 ticks`，证明 Trap 进入/返回链正常。

## 2412105-贺佳怡

### 1. 功能模块二：进程创建与初始化

#### 第 1 轮：实现 PCB 与线程创建链

**我的提示词：**

```text
〖任务说明〗
在 Lab4 的 kern/process/proc.c 中，只修改 “//LAB4 function2 YOUR CODE”
标记处，实现进程控制块分配、idleproc/initproc 创建、内核线程创建、
fork 路径和首次运行上下文。

需要实现：
- static struct proc_struct *alloc_proc(void);
- void proc_init(void);
- int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
- int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
- static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
- static int copy_mm(uint32_t clone_flags, struct proc_struct *proc)。

约束：
- 只改 function2 区域，把 YOUR CODE 替换为 2412105；
- 不修改 function3、pmm.c、trapentry.S、tools/；
- 不改函数签名、struct proc_struct、struct context、struct trapframe；
- 保留 lab4 注释。

输出要求：给出完整 C 代码，并说明首次运行链
switch_to -> forkret -> forkrets -> __trapret -> kernel_thread_entry -> fn(arg)。

〖依赖说明〗
- kmalloc/kfree、memset、list_init/list_add、proc_list、hash_list、
  nr_process、idleproc、initproc、current 已在框架中定义；
- setup_kstack、put_kstack、get_pid、hash_proc、find_proc、wakeup_proc、
  set_proc_name 已存在；
- pmm.h 提供 boot_pgdir_pa、KSTACKSIZE；trap.h 提供 struct trapframe；
- kernel_thread_entry 在 kern/process/entry.S，forkret 在 proc.c，
  forkrets 在 trapentry.S。

〖接口清单〗
- 保持上述 6 个函数签名不变；
- 允许在 function2 区域顶部增加两个 static 前置声明；
- 不新增对外全局接口。

〖行为规格〗
alloc_proc：
- kmalloc 失败返回 NULL；
- state=PROC_UNINIT，pid=-1，runs=0，kstack=0，need_resched=0，
  parent=NULL，mm=NULL，tf=NULL，flags=0，name 清零；
- context 用 memset 清零，使 idleproc 的 context.ra 为 0；
- pgdir=boot_pgdir_pa；list_link/hash_link 初始化。

proc_init：
- 初始化 proc_list 和 hash_list；
- 创建 idleproc：pid=0、PROC_RUNNABLE、kstack=bootstack、
  need_resched=1、name="idle"；current=idleproc；nr_process++；
- 调用 kernel_thread(init_main, "Hello world!!", 0)；
- 用 find_proc 找到 initproc，失败 panic；设置 name="init"。

kernel_thread：
- 栈上 TrapFrame 清零；
- epc=kernel_thread_entry，s0=fn，s1=arg；
- status 按 sstatus 设置 SPP/SPIE、清除 SIE；
- 调用 do_fork 并返回 PID。

do_fork：
- 检查 nr_process < MAX_PROCESS；
- alloc_proc、setup_kstack、copy_mm、copy_thread；
- 关中断分配 pid、hash_proc、加入 proc_list、nr_process++；
- wakeup_proc 后返回 PID；
- 任一步失败按顺序 put_kstack/kfree 清理，不能泄漏。

copy_thread：
- 在内核栈顶放置 TrapFrame 并复制；
- a0=0；sp=esp（esp==0 时用 tf 地址）；
- context.ra=forkret，context.sp=tf。

copy_mm：
- Lab4 中 current->mm 为 NULL，proc->mm=NULL，返回 0；
- 不引入 Lab5 的地址空间复制。

验证：
- make 通过；
- [Test Process Create] PASS；
- make qemu 输出 initproc 三行。
```

**AI 回答（摘要）：**

生成结果给出了 `alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、
`copy_thread`、`copy_mm` 的完整实现，并说明 `copy_thread` 把
`context.ra` 设置为 `forkret` 是首次运行链的关键。

**我的核对与修正：**

1. 第一版把 `copy_thread`/`copy_mm` 定义在 `do_fork` 之后，编译报
   `implicit declaration of function 'copy_thread'`；在区域顶部补了两条
   static 前置声明。
2. `alloc_proc` 中用 `memset` 清零 `context`，保证
   `idleproc->context.ra == 0`，满足测试断言。
3. `copy_mm` 保持 `proc->mm = NULL`，没有引入 Lab5 的 VMA/页目录复制。
4. 在 WSL 下 `make grade` 曾报 `no $qemu_out`，确认是本地 QEMU 版本与
   grade.sh 输出重定向差异，不是代码问题；直接 `make qemu` 能输出三行结果。

#### 第 2 轮：核对修改边界与测试断言

**我的提示词：**

```text
请核对 function2 是否只修改了 //LAB4 function2 区域，
是否覆盖了罗茜的 function3，是否修改了 pmm.c、trapentry.S 或 tools/。
同时给出 [Test Process Create] 的验证点。
```

**AI 回答（摘要）：**

核对结果为只修改了 proc.c 的 function2 区域，function3 保留；测试点包括
idleproc 的 pid/kstack/parent/mm/tf/runs/context.ra，以及 initproc 的
pid/parent/state/context.ra/sp。

**我的核对：**

在集成分支上确认 `git diff --stat` 只包含 proc.c，合并后
`[Test Process Create] PASS`。

## 2412634-罗茜

### 1. 功能模块三：调度与上下文切换

#### 第 1 轮：确定文件边界和调度链

**我的提示词：**

```text
任务：实现 Lab4 功能模块三。

允许修改：
- kern/process/proc.c 的 //LAB4 function3 区域；
- kern/schedule/sched.c；
- kern/process/switch.S。

不要修改：
- proc.c 的 function2；
- pmm.c；
- tools/test_lab4.c 和 tools/grade.sh；
- 函数签名、结构体、宏和 lab4 注释。

实现目标：
1. cpu_idle() 在 current->need_resched 为真时调用 schedule()；
2. schedule() 对 idleproc 从链表头扫描，对普通线程从后继扫描，
   只选择 PROC_RUNNABLE，找不到时回退 idleproc；
3. proc_run() 在关中断临界区中更新 current、切换 Sv39 satp、
   刷 TLB 并调用 switch_to()；
4. switch_to() 按 struct context 顺序保存和恢复 ra、sp、s0~s11。

约束：
- proc_run() 不能使用 libs/riscv.h 中遗留的 Sv32 lsatp()；
- 需要参考 pmm.c 的 enable_paging() 构造 Sv39 satp；
- 调度器和 proc_run() 的关键区必须使用 local_intr_save/restore；
- 汇编偏移必须与 proc.h 的 struct context 实际布局一致。

验证：
- 使用 GCC 10.2.0 编译；
- 用 objdump 检查 switch_to 的保存、恢复偏移；
- 明确说明当前分支若因 function2 未实现而链接失败，
  不能误判为本模块错误。
```

**AI 初版中需要核对的风险：**

- 容易直接使用 `lsatp()`，但它是 Sv32 编码；
- 容易把 `proc->pgdir` 原值直接写入 `satp`，忘记右移 `RISCV_PGSHIFT`；
- 容易在 `schedule()` 扫描链表头哨兵时把链表头误当成进程；
- 容易把 `runs++` 和 `proc_run()` 重复执行；
- 容易按 x86 或其他上下文字段的经验写错汇编偏移。

**我的修正：**

1. 使用 Sv39 模式：

```c
write_csr(satp,
          0x8000000000000000 | (next->pgdir >> RISCV_PGSHIFT));
```

2. `schedule()` 使用环形 `do-while`，并在 `le == &proc_list` 时跳过哨兵。
3. `next->runs++` 只放在 `schedule()` 中，`proc_run()` 不重复增加。
4. 通过 `struct context` 字段顺序确定汇编偏移，再用反汇编核对。

#### 第 2 轮：上下文切换汇编复核

**我的提示词：**

```text
请检查 switch_to 是否完整保存和恢复 struct context 的
ra、sp、s0~s11，并核对 a0/a1 的参数顺序与每个 STORE/LOAD 偏移。
不要修改结构体字段顺序；若发现问题只调整 switch.S。
```

**AI 输出：**

给出 RV64 下使用 `STORE`/`LOAD` 宏的保存—恢复序列。当前工程定义
`STORE=sd`、`LOAD=ld`、`REGBYTES=8`，因此每个字段占 8 字节。

**静态检查发现并规避的问题：**

1. 先保存 `ra`，再恢复 `sp`；不能在恢复时覆盖仍用于寻址的 `a1`。
2. 恢复顺序与保存顺序一致：`ra/sp/s0...s11`。
3. `ret` 使用刚恢复的 `ra`，首次切换会返回 `forkret`。

#### 第 3 轮：编译与反汇编验证

**执行的检查：**

```sh
make clean
make -j2
riscv64-unknown-elf-objdump -d obj/kern/process/switch.o
```

**结果：**

- 所有 `.c` 和 `.S` 文件编译通过；
- 链接阶段只缺 function2 的 `proc_init`，符合当前分支状态；
- `switch_to` 反汇编偏移为：

```text
ra=0, sp=8, s0=16, s1=24, s2=32, s3=40, s4=48, s5=56,
s6=64, s7=72, s8=80, s9=88, s10=96, s11=104
```

#### 第 4 轮：隔离式集成验证

**我的提示词：**

```text
不要修改罗茜正式分支，也不要提交贺佳怡的代码。
复制一份 /tmp 临时工程，在其中仅补入临时 function2 和 Lab3 Trap 返回链，
然后使用现有 GCC 10.2.0 和 QEMU 4.1.1 执行 make grade。
目标是验证 cpu_idle、schedule、proc_run、switch_to 能否通过
Test Schedule，并检查 initproc 三行输出。
```

**执行过程与结果：**

在临时副本中补入临时 function2 和 Trap 返回链后，完整评分通过：

```text
check PMM:              OK
check Process Create:   OK
check Schedule:         OK
Total Score: 30/30
```

临时依赖没有提交到正式分支。

### 2. Challenge 1：局部中断保存/恢复

**我的提示词：**

```text
解释 local_intr_save(intr_flag) 与 local_intr_restore(intr_flag)
如何实现开关中断。回答需要包括：
- sstatus.SIE 的读取；
- 原状态为开启/关闭时的不同处理；
- 为什么不能无条件开启中断；
- 如何保护 schedule() 和 proc_run()；
- 它与多核锁的区别。
```

**核对后的结论：**

`__intr_save()` 只在进入前 `SIE=1` 时关闭中断并返回真；
`__intr_restore()` 只在返回值为真时重新开启。因此它保存的是局部中断状态，
支持嵌套临界区，但不能代替多核互斥锁。
