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
