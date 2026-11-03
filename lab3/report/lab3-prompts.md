# Lab 3 AI 提示词汇总

> 整理版：只保留三人最终使用或最能代表实现思路的 prompt，不是完整对话记录。
> 本文件为石欣哲个人负责章节；贺佳怡、罗茜章节暂留空位，待补充。

## 2412896-石欣哲

### 1. 修复 `sbadaddr` 汇编兼容性错误

```text
【任务说明】
在当前 Lab3 工程中，kern/trap/trapentry.S 的 SAVE_ALL 宏在汇编时报告
"Error: unknown CSR `sbadaddr'"，导致 trapentry.o 无法生成。
请修复该工具链兼容性问题，使工程能够继续编译。
约束：
- 不得修改 SAVE_ALL 的寄存器保存顺序和 TrapFrame 布局；
- 不得修改 struct trapframe、trap() 函数签名；
- 不得修改 trap.c 中已有的 print_trapframe / print_regs；
- 只允许修改 trapentry.S 中与 sbadaddr 相关的 CSR 读取写法。
输出要求：给出修改后的具体行，并说明为什么这样改能消除汇编错误。

【依赖说明】
- kern/trap/trapentry.S 已 #include <riscv.h>；
- riscv.h 中已定义 CSR_SBADADDR 为 0x143；
- SAVE_ALL 当前将 sbadaddr 保存到 TrapFrame 的 34*REGBYTES(sp) 位置；
- 本次不要求实现 RESTORE_ALL。

【接口清单】
- 保持 SAVE_ALL 宏名不变；
- 保持 s3 临时寄存器用法不变；
- 保持保存到 34*REGBYTES(sp) 的目标位置不变；
- 只替换 CSR 的引用方式。

【行为规格】
- 前置：汇编器不识别 sbadaddr 名称；
- 后置：使用 riscv.h 中已有的 CSR_SBADADDR 宏引用同一 CSR（0x143），
  使汇编通过，且 TrapFrame 中 badvaddr 字段仍被正确保存；
- 不得引入新的 CSR 名称依赖；
- 不得改变 SAVE_ALL 对其它寄存器的保存行为。
```
**问题与结果：** 汇编器不认旧 CSR 名 `sbadaddr`。改用 `riscv.h` 已定义的 `CSR_SBADADDR`（0x143）后，`csrr s3, CSR_SBADADDR` 预处理器展开为数值 CSR，汇编通过，TrapFrame 布局不变。[reference:0]

### 2. 从 Lab 2 补全 Lab 3 占位代码

```text
【任务说明】
lab3/kern/mm/best_fit_pmm.c 和 lab3/kern/mm/pmm.c 中存在
"//lab2 请补充你在lab2中的代码" 占位，导致编译缺少 free_list/nr_free/page_init。
请从 Lab2 已完成实现中补齐这些占位内容，保留 Lab3 原有测试函数和注释。

【约束】
- 不得修改 basic_check/best_fit_check；
- 不得改 Lab3 的 trap 相关文件；
- 不得把 Lab2 实现标成 Lab3 负责人学号。

【验证】
make 通过，bin/kernel 生成。
```
**问题与结果：** `best_fit_pmm.c` 缺 `best_fit_init`、`best_fit_alloc_pages` 等实现，`pmm.c` 缺 `page_init()` 定义。按占位补回 Lab 2 实现，保留原作者注释，不标本人学号，`make` 通过。[reference:1]

### 3. 功能模块一：`RESTORE_ALL` 与 `__alltraps`

```text
【任务说明】
在当前 Lab3 工程的 kern/trap/trapentry.S 中，实现 Trap 上下文恢复宏 RESTORE_ALL，
并在 __alltraps 中建立汇编入口与 C 语言 trap(struct trapframe *tf) 之间的调用关系。
目标：Trap 发生时 SAVE_ALL 已把 36 个槽位保存到内核栈上的 TrapFrame；
RESTORE_ALL 从同一 TrapFrame 恢复处理器状态；__alltraps 把 TrapFrame
地址按 RISC-V 调用约定传给 trap()，trap() 返回后进入 __trapret 执行 RESTORE_ALL 和 sret。

约束：
- 不得修改 SAVE_ALL 的寄存器保存顺序和 TrapFrame 布局；
- 不得修改 struct trapframe、trap() 的函数签名；
- 不得修改 trap.c 中已有的 print_trapframe / print_regs / trap_dispatch；
- 只允许修改 trapentry.S 中 RESTORE_ALL 宏体和 __alltraps 中调用 trap() 的位置；
- 保留所有 lab3 注释，把 YOUR CODE 替换为本人学号。

【依赖说明】
- trapentry.S 已 #include <riscv.h>；
- SAVE_ALL 已保存 x0..x31 到偏移 0..31*REGBYTES，
  x2(sp) 的原始值保存在偏移 2*REGBYTES，
  sstatus/sepc/stval/scause 分别保存在 32/33/34/35*REGBYTES；
- riscv.h 定义了 STORE/LOAD/REGBYTES 和 CSR_SBADADDR；
- trap() 的 C 语言签名是 void trap(struct trapframe *tf)；
- __trapret 已存在，内部执行 RESTORE_ALL 和 sret。

【接口清单】
- 保持 RESTORE_ALL 宏名不变；
- 保持 __alltraps 符号名和 .align(2) 不变；
- 保持 __trapret 路径不变；
- 允许 RESTORE_ALL 内部使用 t0 作为临时寄存器读取 CSR；
- 允许 __alltraps 中使用 a0 传递 TrapFrame 地址。

【行为规格】
RESTORE_ALL：
- 前置：sp 指向当前 TrapFrame 起始地址；
- 后置：sstatus/sepc/stval/scause 被恢复到 TrapFrame 中保存的值；
- 后置：x1、x3..x31 从对应偏移恢复；
- 后置：x2(sp) 最后从偏移 2*REGBYTES 恢复，恢复后 sp 回到 Trap 发生前的值；
- 不得先恢复 sp 再恢复其它寄存器，否则后续偏移会错。

__alltraps：
- 前置：SAVE_ALL 已执行，sp 指向 TrapFrame；
- 后置：a0 = 当前 sp，即 TrapFrame 地址；
- 后置：调用 trap()，调用前后 sp 保持一致；
- 后置：trap() 返回后落入 __trapret。

验证：
- make clean && make 通过；
- objdump -d 检查 RESTORE_ALL 的偏移与 SAVE_ALL 对应；
- __alltraps 中 a0 在 jal trap 前被设置为 sp。
```
**问题与结果：** 核心约束是 `sp` 最后恢复、`a0` 传递 TrapFrame 地址、CSR 用 `t0` 中转。`objdump` 确认偏移正确，QEMU 启动到 `++ setup timer interrupts` 未崩溃；`100 ticks` 待功能模块二合并后验证。[reference:2]

## 2412105-贺佳怡

*（待贺佳怡补充 `interrupt_handler()` 相关 prompt）*

## 2412634-罗茜

*（待罗茜补充 `exception_handler()` 相关 prompt）*