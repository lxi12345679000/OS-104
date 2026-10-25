# Lab 2 AI 提示词汇总

> 整理版：只保留三人最终使用或最能代表实现思路的 prompt，不是完整对话记录。

## 2412896-石欣哲

### 1. `page_init()` 实现

```text
在 ucore lab2 的 riscv64 内核中，只修改 kern/mm/pmm.c 的 page_init()，
不修改 pmm_init()、pmm_manager、测试函数和现有全局变量，保留 //lab2 function1 学号注释。

从 get_memory_base()/get_memory_size() 获取 DTB 物理内存范围；无效时 panic。
设置 va_pa_offset = PHYSICAL_MEMORY_OFFSET；用 PPN(mem_base + mem_size) 计算 npage，
保证 npage > nbase；把 pages 放在 end 之后并按 PGSIZE 向上对齐。

所有 npage - nbase 个 Page 描述符先设置 reserved；空闲区起点取 pages 数组末尾
转成物理地址后按页向上对齐，终点取内存末尾按页向下对齐。
只有起点小于终点时，才用 pa2page(free_start_pa) 和页数调用 init_memmap()。
输出 physical memory map 和当前分配器名称。
```

### 2. GDB 分支：编译调试版 QEMU

```text
在 WSL 的 qemu-4.1.1 源码目录中，执行 make distclean，
再用 ./configure --target-list=riscv32-softmmu,riscv64-softmmu --enable-debug
重新配置并 make -j$(nproc)。
不要执行 make install；只需生成带调试信息的 riscv64-softmmu/qemu-system-riscv64，
并把 ucore Makefile 的 QEMU 变量指向该文件。
```

### 3. GDB 分支：双 GDB 调试

```text
终端 1 执行 make debug 启动挂起的 QEMU；
终端 2 用 pgrep 找到 QEMU PID，启动普通 GDB，attach 后执行
handle SIGPIPE nostop noprint；
终端 3 执行 make gdb，连接 QEMU 的 GDB stub。

在终端 2 对 get_physical_address 设断点，并根据 kern_init 的虚拟地址设置条件断点；
两个 GDB 分别 continue，观察 QEMU 将
0xffffffffc02000d6 翻译为物理地址时的调用栈。
```

### 4. GDB 分支：Sv39 与 TLB 分析

```text
结合 get_physical_address 的源码和抓到的 bt，解释 Sv39 的 levels=3、
9 位 VPN 索引、PTE 读取和最终物理地址拼装；
再说明 riscv_cpu_tlb_fill 以及 TLB 命中/缺失的差异，
最后比较 QEMU 软件 TLB 与真实 CPU 硬件 TLB 的区别。
```

## 2412105-贺佳怡

### 1. Best-Fit 核心实现

```text
请帮我用 C 语言实现 ucore 操作系统实验中的 Best-Fit 页面分配算法。
需要实现 init、init_memmap、alloc_pages、free_pages 和 nr_free_pages，
并导出 struct pmm_manager 结构体。
分配时遍历空闲链表，找到满足需求且容量最小的块；
释放时正确与前后相邻空闲块合并。
```

### 2. SLUB 模块

```text
请帮我实现一个简化的 SLUB 分配器，包含两层架构：
第一层向 PMM 申请物理页，第二层将页切分为不同 size class 的对象。
需要实现 slub_alloc 和 slub_free，并提供一个 slub_check 测试函数。
```

### 3. SLUB 测试

```text
请帮我写一个测试函数 slub_check，覆盖 SLUB 分配的边界情况，
包括申请 0 字节、申请超出最大 size class 的超大对象、
以及释放后复用同一地址的逻辑，使用 assert 进行断言。
```

## 2412634-罗茜

### 1. First-Fit 分析

```text
阅读 kern/mm/default_pmm.c 的 default_init、default_init_memmap、
default_alloc_pages、default_free_pages 和 default_check，
说明 First-Fit 如何初始化空闲页、分配连续页、释放并合并相邻空闲块，
并回答 First-Fit 是否有进一步改进空间。
```

### 2. Buddy System 模块

```text
在 ucore lab2 中只新增 kern/mm/buddy_pmm.c/.h，不修改 pmm.c 和测试函数。
实现 order 空闲链表、按对齐的 2 的幂块初始化、向上取整分配、逐层分裂、
按页索引异或找伙伴并向上合并。
用 Page.property 记录块大小，PG_property 标记空闲块头；
定义 buddy_pmm_manager，提供 buddy_check。
要求 1/2/3/5/8 页分配和释放后计数恢复。
```

### 3. Buddy 测试

```text
为 buddy_pmm.c 编写 buddy_check，至少覆盖：
初始计数、1/2/3/5 页分配、非 2 的幂取整、大块分裂、
左右伙伴合并、碎片化后申请、空闲不足返回 NULL，
以及全部释放后总页数和各 order 计数恢复。
不要修改框架测试函数。
```
