# 2412634-罗茜 Lab4 提示词与 AI 迭代记录

## 1. 功能模块三：调度与上下文切换

### 第 1 轮：确定文件边界和调度链

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
4. switch_to() 按 struct context 顺序保存和恢复
   ra、sp、s0~s11。

约束：
- proc_run() 不能使用 libs/riscv.h 中遗留的 Sv32 lsatp()；
- 需要参考 pmm.c 的 enable_paging() 构造 Sv39 satp；
- 调度器和 proc_run() 的关键区必须使用 local_intr_save/restore；
- 汇编偏移必须与 proc.h 的 struct context 实际布局一致。

验证：
- 使用 GCC 10.2.0 编译；
- 用 objdump 检查 switch_to 的保存、恢复偏移；
- 明确说明当前分支若因 function2 未实现而链接失败，不能误判为本模块错误。
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

### 第 2 轮：上下文切换汇编复核

**我的提示词：**

```text
请检查 switch_to 是否完整保存和恢复 struct context 的
ra、sp、s0~s11，并核对 a0/a1 的参数顺序与每个 STORE/LOAD 偏移。
不要修改结构体字段顺序；若发现问题只调整 switch.S。
```

**AI 输出：**

给出 RV64 下使用 `STORE`/`LOAD` 宏的保存—恢复序列。由于当前工程在 RISC-V 编译器下定义了：

```c
#define STORE sd
#define LOAD  ld
#define REGBYTES 8
```

因此每个字段占 8 字节。

**静态检查发现并规避的问题：**

1. 先保存 `ra`，再恢复 `sp`；不能在执行恢复时覆盖仍用于寻址的 `a1`。
2. 恢复顺序与保存顺序一致：`ra/sp/s0...s11`。
3. `ret` 使用刚恢复的 `ra`，因此首次切换会返回 `forkret`。

### 第 3 轮：编译与反汇编验证

**执行的检查：**

```sh
make clean
make -j2
riscv64-unknown-elf-objdump -d obj/kern/process/switch.o
```

**结果：**

- 所有 `.c` 和 `.S` 文件均编译通过；
- 链接阶段只缺贺佳怡负责的 `proc_init`，符合当前分支状态；
- `switch_to` 反汇编显示偏移为：

```text
ra=0, sp=8, s0=16, s1=24, s2=32, s3=40, s4=48, s5=56,
s6=64, s7=72, s8=80, s9=88, s10=96, s11=104
```

### 第 4 轮：隔离式集成验证

**我的提示词：**

```text
不要修改罗茜正式分支，也不要提交贺佳怡的代码。
复制一份 /tmp 临时工程，在其中仅补入临时 function2 和 Lab3 Trap 返回链，
然后使用现有 GCC 10.2.0 和 QEMU 4.1.1 执行 make grade。
目标是验证罗茜的 cpu_idle、schedule、proc_run、switch_to 能否通过
Test Schedule，并检查 initproc 三行输出。若失败，先判断是临时依赖问题
还是罗茜模块问题。
```

**执行过程：**

1. 临时工程通过 Docker 复制进容器，不污染 Git 工作区。
2. GCC 10 不认识 Lab3 代码里的 `CSR_SBADADDR` 名称，临时改为框架已有的 `0x143`。
3. Lab3 Trap 文件缺少 Lab4 新增的 `forkrets`，只在临时副本补入。
4. 完整构建通过后执行 `make grade`。

**最终结果：**

```text
check PMM:              OK
check Process Create:   OK
check Schedule:         OK
Total Score: 30/30
```

这说明罗茜负责的调度与上下文切换在依赖完整时通过全部检查。正式分支仍保持文件边界，没有吸收临时 function2 或基线修复。

### 第 5 轮：集成前发现的基线问题

**复现现象：**

当前 `lab4` 基线的：

```text
lab4/code/kern/trap/trapentry.S
```

仍然包含：

```text
#lab3:填写你在lab3中实现的代码
```

`RESTORE_ALL` 为空且 `__alltraps` 未调用 `trap()`。这不是本模块应直接修改的文件，但会影响 `__trapret`、首次 `sret` 和时钟中断后的完整运行。

**处理：**

只向基线负责人提交这个复现点，不在罗茜分支修改 `trapentry.S`。完整 Schedule 验收应等待：

1. 贺佳怡的 function2 合并；
2. Lab3 Trap 入口基线补齐；
3. 再进行整分支 `make grade`。

## 2. Challenge 1：局部中断保存/恢复

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

`__intr_save()` 只在进入前 `SIE=1` 时关闭中断并返回真；`__intr_restore()` 只在返回值为真时重新开启。因此它保存的是局部中断状态，支持嵌套临界区，但不能代替多核互斥锁。

## 3. 当前状态说明

本分支已完成：

- function3 的 C 实现；
- `schedule()`；
- `switch_to()`；
- GCC 10.2.0 编译检查；
- `switch_to` 反汇编偏移检查；
- 隔离临时集成副本的 `make grade`，三项 PASS，`Total Score: 30/30`；
- Challenge 1 报告草稿。

正式共享分支尚待集成后完成：

- 合并贺佳怡的 function2；
- 由基线负责人补齐 Lab3 Trap 入口；
- 在共享集成分支重新执行 `make grade` 并留存最终日志。

