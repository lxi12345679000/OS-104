#include <assert.h>
#include <clock.h>
#include <console.h>
#include <defs.h>
#include <kdebug.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
#include <sbi.h>
#include <stdio.h>
#include <trap.h>


#define TICK_NUM 100

static void print_ticks() {
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S
 */
void idt_init(void) {

    extern void __alltraps(void);
    /* Set sup0 scratch register to 0, indicating to exception vector
       that we are presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
}

/* trap_in_kernel - test if trap happened in kernel */
bool trap_in_kernel(struct trapframe *tf) {
    return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf) {
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    cprintf("  cause    0x%08x\n", tf->cause);
}

void print_regs(struct pushregs *gpr) {
    cprintf("  zero     0x%08x\n", gpr->zero);
    cprintf("  ra       0x%08x\n", gpr->ra);
    cprintf("  sp       0x%08x\n", gpr->sp);
    cprintf("  gp       0x%08x\n", gpr->gp);
    cprintf("  tp       0x%08x\n", gpr->tp);
    cprintf("  t0       0x%08x\n", gpr->t0);
    cprintf("  t1       0x%08x\n", gpr->t1);
    cprintf("  t2       0x%08x\n", gpr->t2);
    cprintf("  s0       0x%08x\n", gpr->s0);
    cprintf("  s1       0x%08x\n", gpr->s1);
    cprintf("  a0       0x%08x\n", gpr->a0);
    cprintf("  a1       0x%08x\n", gpr->a1);
    cprintf("  a2       0x%08x\n", gpr->a2);
    cprintf("  a3       0x%08x\n", gpr->a3);
    cprintf("  a4       0x%08x\n", gpr->a4);
    cprintf("  a5       0x%08x\n", gpr->a5);
    cprintf("  a6       0x%08x\n", gpr->a6);
    cprintf("  a7       0x%08x\n", gpr->a7);
    cprintf("  s2       0x%08x\n", gpr->s2);
    cprintf("  s3       0x%08x\n", gpr->s3);
    cprintf("  s4       0x%08x\n", gpr->s4);
    cprintf("  s5       0x%08x\n", gpr->s5);
    cprintf("  s6       0x%08x\n", gpr->s6);
    cprintf("  s7       0x%08x\n", gpr->s7);
    cprintf("  s8       0x%08x\n", gpr->s8);
    cprintf("  s9       0x%08x\n", gpr->s9);
    cprintf("  s10      0x%08x\n", gpr->s10);
    cprintf("  s11      0x%08x\n", gpr->s11);
    cprintf("  t3       0x%08x\n", gpr->t3);
    cprintf("  t4       0x%08x\n", gpr->t4);
    cprintf("  t5       0x%08x\n", gpr->t5);
    cprintf("  t6       0x%08x\n", gpr->t6);
}

//lab3 function2 2412105
//lab3 function2 interrupt_handler 2412105
void interrupt_handler(struct trapframe *tf) {
    // 获取中断编号：去掉 cause 最高位的中断标志位
    uintptr_t interrupt_flag =
        (uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1);
    uintptr_t cause = tf->cause & ~interrupt_flag;

    // 1. 处理时钟中断
    if (cause == IRQ_S_TIMER) {
        // 设置下一次时钟中断，保证时钟中断持续发生
        clock_set_next_event();

        // 时钟中断计数加一
        ticks++;

        // 每累计 100 次时钟中断，输出一次信息
        if (ticks % TICK_NUM == 0) {
            print_ticks();
        }
    }
    // 2. 处理 supervisor 软件中断
    else if (cause == IRQ_S_SOFT) {
        // 清除软件中断请求
        sbi_clear_ipi();

        cprintf("Supervisor software interrupt\n");
    }
    // 3. 处理其他未知中断
    else {
        cprintf("Unknown interrupt: cause %d\n", (int)cause);

        // 输出完整的 Trap 上下文信息
        print_trapframe(tf);
    }
}

//lab3 function2 exception_handler 2412634
void exception_handler(struct trapframe *tf) {
    /*
     * 2412634：
     * 负责异常分类与返回地址修正。非法指令和断点异常需要输出现场信息，
     * 并根据异常指令的实际长度更新 epc；其他异常打印 TrapFrame 便于调试。
     */
    switch (tf->cause) {
    case CAUSE_ILLEGAL_INSTRUCTION:
    case CAUSE_BREAKPOINT: {
        /*
         * 2412634：计算异常指令长度。
         * RISC-V 指令至少按 16 位对齐。低两位为 3 表示 32 位指令，
         * 其余编码表示 16 位压缩指令；据此计算异常指令的实际长度。
         */
        uint16_t instruction = *(volatile uint16_t *)tf->epc;
        uintptr_t instruction_length = (instruction & 0x3) == 0x3 ? 4 : 2;

        /* 2412634：按异常类型输出对应的异常指令地址。 */
        if (tf->cause == CAUSE_ILLEGAL_INSTRUCTION) {
            cprintf("Illegal instruction at %p\n", (void *)tf->epc);
        } else {
            cprintf("Breakpoint at %p\n", (void *)tf->epc);
        }

        /*
         * 2412634：跳过触发异常的指令，
         * 避免 sret 后再次执行同一条异常指令形成死循环。
         */
        tf->epc += instruction_length;
        break;
    }
    default:
        /*
         * 2412634：本实验未单独要求的异常统一打印 TrapFrame，
         * 保留完整的寄存器、epc、badvaddr 和 cause 信息用于定位问题。
         */
        print_trapframe(tf);
        break;
    }
}

static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {
        // interrupts
        interrupt_handler(tf);
    } else {
        // exceptions
        exception_handler(tf);
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap()
 * returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf) {
    // dispatch based on what type of trap occurred
    trap_dispatch(tf);
}
