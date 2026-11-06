# Lab 3 AI 提示词汇总

> 整理版：只保留三人最终使用或最能代表实现思路的 prompt，不是完整对话记录。

## 2412896-石欣哲

### 1. `sbadaddr` CSR 工具链兼容性修复

```text
〖任务说明〗
在当前 Lab3 工程中，kern/trap/trapentry.S 的 SAVE_ALL 宏在汇编时报告
"Error: unknown CSR `sbadaddr'"，导致 trapentry.o 无法生成。
请修复该工具链兼容性问题，使工程能够继续编译。

约束：
- 不得修改 SAVE_ALL 的寄存器保存顺序和 TrapFrame 布局；
- 不得修改 struct trapframe、trap() 函数签名；
- 不得修改 trap.c 中已有的 print_trapframe / print_regs；
- 只允许修改 trapentry.S 中与 sbadaddr 相关的 CSR 读取写法。

输出要求：给出修改后的具体行，并说明为什么这样改能消除汇编错误。

〖依赖说明〗
- kern/trap/trapentry.S 已 #include <riscv.h>；
- riscv.h 中已定义 CSR_SBADADDR 为 0x143；
- SAVE_ALL 当前将 sbadaddr 保存到 TrapFrame 的 34*REGBYTES(sp) 位置；
- 本次不要求实现 RESTORE_ALL。

〖接口清单〗
- 保持 SAVE_ALL 宏名不变；
- 保持 s3 临时寄存器用法不变；
- 保持保存到 34*REGBYTES(sp) 的目标位置不变；
- 只替换 CSR 的引用方式。

〖行为规格〗
- 前置：汇编器不识别 sbadaddr 名称；
- 后置：使用 riscv.h 中已有的 CSR_SBADADDR 宏引用同一 CSR（0x143），
  使汇编通过，且 TrapFrame 中 badvaddr 字段仍被正确保存；
- 不得引入新的 CSR 名称依赖；
- 不得改变 SAVE_ALL 对其它寄存器的保存行为。
```

### 2. Lab2 占位补齐（`best_fit_pmm.c` / `pmm.c`）

```text
〖任务说明〗
lab3/kern/mm/best_fit_pmm.c 和 lab3/kern/mm/pmm.c 中存在
"//lab2 请补充你在lab2中的代码" 占位，导致编译缺少
free_list / nr_free / page_init。
请从 Lab2 已完成实现中补齐这些占位内容，保留 Lab3 原有测试函数和注释。

〖约束〗
- 不得修改 basic_check / best_fit_check；
- 不得改 Lab3 的 trap 相关文件；
- 不得把 Lab2 实现标成 Lab3 负责人学号。

〖验证〗
- make 通过，bin/kernel 生成。
```

### 3. 功能模块一：`RESTORE_ALL` 与 `__alltraps` 调用 `trap()`

```text
〖任务说明〗
在当前 Lab3 工程的 kern/trap/trapentry.S 中，实现 Trap 上下文恢复宏 RESTORE_ALL，
并在 __alltraps 中建立汇编入口与 C 语言 trap(struct trapframe *tf) 之间的调用关系。

目标：
- Trap 发生时 SAVE_ALL 已把 36 个槽位保存到内核栈上的 TrapFrame；
- RESTORE_ALL 需要从同一 TrapFrame 恢复处理器状态；
- __alltraps 需要把 TrapFrame 地址按 RISC-V 调用约定传给 trap()，
  trap() 返回后进入 __trapret 执行 RESTORE_ALL 和 sret。

约束：
- 不得修改 SAVE_ALL 的寄存器保存顺序和 TrapFrame 布局；
- 不得修改 struct trapframe、trap() 的函数签名；
- 不得修改 trap.c 中已有的 print_trapframe / print_regs / trap_dispatch；
- 只允许修改 trapentry.S 中 RESTORE_ALL 宏体和 __alltraps 中调用 trap() 的位置；
- 保留所有 lab3 注释，把 YOUR CODE 替换为本人学号。

输出要求：
给出 RESTORE_ALL 宏体和 __alltraps 调用 trap() 的汇编代码，
并说明每条恢复指令与 SAVE_ALL 中保存槽位的对应关系。

〖依赖说明〗
- kern/trap/trapentry.S 已 #include <riscv.h>；
- SAVE_ALL 已保存 x0..x31 到偏移 0..31*REGBYTES，
  x2(sp) 的原始值保存在偏移 2*REGBYTES，
  sstatus / sepc / sbadaddr / scause 分别保存在 32 / 33 / 34 / 35*REGBYTES；
- riscv.h 中定义了 STORE / LOAD / REGBYTES 和 CSR_SBADADDR；
- trap() 的 C 语言签名是 void trap(struct trapframe *tf)；
- __trapret 已存在，内部执行 RESTORE_ALL 和 sret。

〖接口清单〗
- 保持 RESTORE_ALL 宏名不变；
- 保持 __alltraps 符号名和 .align(2) 不变；
- 保持 __trapret 路径不变；
- 允许在 RESTORE_ALL 内部使用 t0 作为临时寄存器读取 CSR；
- 允许在 __alltraps 中使用 a0 传递 TrapFrame 地址。

〖行为规格〗
RESTORE_ALL：
- 前置：sp 指向当前 TrapFrame 起始地址；
- 后置：sstatus / sepc / sbadaddr / scause 被恢复到 TrapFrame 中保存的值；
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

## 2412105-贺佳怡

### 1. 中断与时钟处理 Spec/Prompt

```text
我正在写 RISC-V uCore 的 `interrupt_handler` 函数，请帮我提供一个 C 语言的实现框架。
注意 `tf->cause` 最高位是标志位需要剥离；
时钟中断里要调用 `clock_set_next_event`；
如果是软件中断，请帮我加上清除中断标志的代码；
未知中断请用 `print_trapframe` 打印。
```

### 2. 调试问题记录

```text
本地 QEMU 8.2.2 自带的 OpenSBI 1.3 与实验启动方式不兼容，
make qemu 卡在 OpenSBI 界面无输出。改用 QEMU 4.1.1 后正常。
```

```text
Test 3 评分 0 分：代码中在 ticks == 200 时调用 sbi_shutdown()，
导致评分脚本抓不到 ticks=200 的 PASS 输出而超时。
删除主动关机代码后通过。
```

## 2412634-罗茜

### 1. 异常处理 Spec

```text
仅修改 `kern/trap/trap.c` 的 `exception_handler()`：

- 非法指令输出 `Illegal instruction at ...`。
- 断点输出 `Breakpoint at ...`。
- 按指令实际长度更新 `tf->epc`，不能固定加 4。
- 其他异常调用 `print_trapframe(tf)`。
- 不修改 `trapentry.S`、`interrupt_handler()`、`trap_dispatch()` 和 `trap()`。
```

### 2. 异常处理 AI 使用提示词（代码复核）

```text
请复核我本人已经完成的 exception_handler()：
是否正确处理非法指令、断点和未知异常，是否正确判断指令长度并更新 epc，
以及是否严格遵守只修改该函数的边界。只指出问题和需要修改的位置，
不要重写关键代码。最后检查 lab3 注释、学号和个人中文注释是否完整。
```

### 3. 迭代记录

```text
第一次：
本人完成异常分类、异常信息输出、指令长度判断、epc 更新和其他异常兜底。
检查结果：
- 只修改了 exception_handler()。
- 没有修改其他函数或文件。
- epc 按 2 或 4 字节推进。

第二次：
使用 AI 复核代码边界和异常分支覆盖情况。AI 没有重写关键代码；
本人根据复核意见检查注释，保留 lab3 标记，将 YOUR CODE 替换为 2412634，
并补充中文说明。
```

### 4. 问题与处理

```text
- 没有发现异常处理逻辑错误。
- 完整 QEMU 测试暂未通过的原因是 trapentry.S 尚未调用 trap()；
  这属于 Trap 入口模块，不是异常处理函数的问题。
- 在 Docker Ubuntu、GCC 10.2、QEMU 4.1.1 环境中执行 make clean && make 成功。
```