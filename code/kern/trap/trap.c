#include <defs.h>
#include <mmu.h>
#include <memlayout.h>
#include <clock.h>
#include <trap.h>
#include <riscv.h>
#include <stdio.h>
#include <assert.h>
#include <console.h>
#include <vmm.h>
#include <kdebug.h>
#include <unistd.h>
#include <syscall.h>
#include <error.h>
#include <sched.h>
#include <sync.h>
#include <sbi.h>
#include <pmm.h>
#include <proc.h>

#define TICK_NUM 100

static void print_ticks()
{
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S */
void idt_init(void)
{
    extern void __alltraps(void);
    /* Set sscratch register to 0, indicating to exception vector that we are
     * presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
    /* Allow kernel to access user memory */
    set_csr(sstatus, SSTATUS_SUM);
}

/* trap_in_kernel - test if trap happened in kernel */
bool trap_in_kernel(struct trapframe *tf)
{
    return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf)
{
    cprintf("trapframe at %p\n", tf);
    // cprintf("trapframe at 0x%x\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  tval 0x%08x\n", tf->tval);
    cprintf("  cause    0x%08x\n", tf->cause);
}

void print_regs(struct pushregs *gpr)
{
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

extern struct mm_struct *check_mm_struct;

void interrupt_handler(struct trapframe *tf)
{
    uintptr_t interrupt_flag = (uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1);
    uintptr_t irq = tf->cause & ~interrupt_flag;

    if (irq == IRQ_S_TIMER)
    {
        clock_set_next_event();
        ticks++;
        if (ticks % TICK_NUM == 0)
        {
            if (current != NULL)
            {
                current->need_resched = 1;
            }
            print_ticks();
        }
    }
    else if (irq == IRQ_S_SOFT)
    {
        clear_csr(sip, SIP_SSIP);
    }
    else
    {
        cprintf("unknown interrupt: cause %d\n", (int)irq);
        print_trapframe(tf);
    }
}

static uint32_t
page_fault_error_code(struct trapframe *tf)
{
    uint32_t error_code = 0;
    if (!trap_in_kernel(tf))
    {
        error_code |= PFAULT_USER;
    }
    if (tf->cause == CAUSE_STORE_PAGE_FAULT)
    {
        error_code |= PFAULT_WRITE;
    }

    if (current != NULL && current->mm != NULL)
    {
        pte_t *ptep = get_pte(current->mm->pgdir, tf->tval, 0);
        if (ptep != NULL && (*ptep & PTE_V))
        {
            error_code |= PFAULT_PRESENT;
        }
    }
    return error_code;
}

static void
handle_page_fault(struct trapframe *tf)
{
    if (current == NULL)
    {
        print_trapframe(tf);
        panic("page fault before process initialization");
    }

    bool in_kernel = trap_in_kernel(tf);
    struct mm_struct *mm = current->mm;
    int ret = do_pgfault(mm, page_fault_error_code(tf), tf->tval);
    if (ret == 0)
    {
        return;
    }

    print_trapframe(tf);
    if (in_kernel)
    {
        panic("page fault in kernel mode: %e", ret);
    }

    cprintf("killed by kernel: user page fault, ret=%e\n", ret);
    do_exit(-E_KILLED);
}

//lab5 function1 2412634
void exception_handler(struct trapframe *tf)
{
    switch (tf->cause)
    {
    case CAUSE_USER_ECALL:
        tf->epc += 4;
        syscall();
        break;
    case CAUSE_LOAD_PAGE_FAULT:
    case CAUSE_STORE_PAGE_FAULT:
        handle_page_fault(tf);
        break;
    default:
        print_trapframe(tf);
        if (current != NULL && !trap_in_kernel(tf))
        {
            cprintf("killed by kernel: unhandled user exception\n");
            do_exit(-E_KILLED);
        }
        panic("unhandled exception in kernel mode");
    }
}


static inline void trap_dispatch(struct trapframe *tf)
{
    if ((intptr_t)tf->cause < 0)
    {
        // interrupts
        interrupt_handler(tf);
    }
    else
    {
        // exceptions
        exception_handler(tf);
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap() returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf)
{
    // dispatch based on what type of trap occurred
    //    cputs("some trap");
    if (current == NULL)
    {
        trap_dispatch(tf);
    }
    else
    {
        struct trapframe *otf = current->tf;
        current->tf = tf;

        bool in_kernel = trap_in_kernel(tf);

        trap_dispatch(tf);

        current->tf = otf;
        if (!in_kernel)
        {
            if (current->flags & PF_EXITING)
            {
                do_exit(-E_KILLED);
            }
            if (current->need_resched)
            {
                schedule();
            }
        }
    }
}
