#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <riscv.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

//lab4 请补充你在lab4中的代码

//lab5 function2 YOUR CODE update alloc_proc


// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc)
{
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL)
    {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc)
{
    list_del(&(proc->list_link));
    if (proc->optr != NULL)
    {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL)
    {
        proc->yptr->optr = proc->optr;
    }
    else
    {
        proc->parent->cptr = proc->optr;
    }
    nr_process--;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}






// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc)
{
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}

//lab5 function2 YOUR CODE update kernel_thread


// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm)
{
    struct Page *page;
    if ((page = alloc_page()) == NULL)
    {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir_va, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm)
{
    free_page(kva2page(mm->pgdir));
}

//lab5 function3 YOUR CODE update copy_mm



//lab5 function2 YOUR CODE


//lab5 function3 YOUR CODE


//lab5 function4 2412634
/*
 * 2412634 罗茜：回收一个僵尸子进程。
 * 父进程读取退出码后，从哈希表和进程树中摘除该子进程，
 * 最后释放内核栈和进程控制块。
 */
static int
reap_child(struct proc_struct *proc, int *code_store)
{
    if (code_store != NULL)
    {
        *code_store = proc->exit_code;
    }

    bool intr_flag;
    local_intr_save(intr_flag);
    {
        // 2412634：摘链必须关中断，避免进程树与哈希表出现半更新状态。
        unhash_proc(proc);
        remove_links(proc);
    }
    local_intr_restore(intr_flag);

    put_kstack(proc);
    kfree(proc);
    return 0;
}

/*
 * 2412634 罗茜：等待指定子进程或任意子进程。
 * pid == 0 表示任意子进程；成功回收后返回 0。
 */
int
do_wait(int pid, int *code_store)
{
    // 2412634：退出码将写入用户空间，先验证用户指针可写。
    if (code_store != NULL &&
        !user_mem_check(current->mm, (uintptr_t)code_store,
                        sizeof(int), 1))
    {
        return -E_INVAL;
    }

    struct proc_struct *proc;
    bool has_child;

repeat:
    proc = NULL;
    has_child = 0;

    if (pid != 0)
    {
        // 2412634：指定 pid 时，只允许等待当前进程的直属子进程。
        proc = find_proc(pid);
        if (proc != NULL && proc->parent == current)
        {
            has_child = 1;
            if (proc->state == PROC_ZOMBIE)
            {
                goto found;
            }
        }
    }
    else
    {
        // 2412634：pid 为 0，遍历当前进程的所有子进程。
        for (proc = current->cptr; proc != NULL; proc = proc->optr)
        {
            has_child = 1;
            if (proc->state == PROC_ZOMBIE)
            {
                goto found;
            }
        }
    }

    if (!has_child)
    {
        // 2412634：没有符合条件的子进程，立即返回错误。
        return -E_BAD_PROC;
    }

    bool intr_flag;
    local_intr_save(intr_flag);
    {
        // 2412634：有子进程但尚未退出时，父进程睡眠等待子进程唤醒。
        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;
    }
    local_intr_restore(intr_flag);

    schedule();
    if (current->flags & PF_EXITING)
    {
        // 2412634：等待期间父进程被杀死，应转入退出流程。
        do_exit(-E_KILLED);
    }
    goto repeat;

found:
    if (proc == idleproc || proc == initproc)
    {
        panic("cannot wait idleproc or initproc");
    }
    return reap_child(proc, code_store);
}

/*
 * 2412634 罗茜：当前进程退出。
 * 当前进程先释放用户地址空间，再成为僵尸进程，等待父进程回收 PCB 和内核栈。
 */
int
do_exit(int error_code)
{
    if (current == idleproc || current == initproc)
    {
        panic("idleproc or initproc requested exit");
    }

    struct mm_struct *mm = current->mm;
    current->mm = NULL;
    if (mm != NULL)
    {
        // 2412634：先切回内核页表，再释放即将不用的用户页表。
        lsatp(boot_pgdir_pa);
        flush_tlb();
        if (mm_count_dec(mm) == 0)
        {
            // 2412634：最后一个使用者退出时才真正销毁地址空间。
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
    }

    bool intr_flag;
    struct proc_struct *proc;
    local_intr_save(intr_flag);
    {
        // 2412634：先进入僵尸态并保存退出码，确保父进程能看到完整状态。
        current->flags |= PF_EXITING;
        current->exit_code = error_code;
        current->state = PROC_ZOMBIE;

        proc = current->parent;
        if (proc != NULL && (proc->wait_state & WT_CHILD))
        {
            // 2412634：父进程正在等待子进程退出，唤醒它执行回收。
            wakeup_proc(proc);
        }

        while ((proc = current->cptr) != NULL)
        {
            // 2412634：把孤儿子进程挂到 initproc 下，避免无人管理。
            current->cptr = proc->optr;
            proc->yptr = NULL;
            if ((proc->optr = initproc->cptr) != NULL)
            {
                initproc->cptr->yptr = proc;
            }
            proc->parent = initproc;
            initproc->cptr = proc;

            if (proc->state == PROC_ZOMBIE &&
                (initproc->wait_state & WT_CHILD))
            {
                wakeup_proc(initproc);
            }
        }
    }
    local_intr_restore(intr_flag);

    // 2412634：僵尸进程不再占用 CPU，立即切换到其他进程。
    schedule();
    panic("do_exit returned for pid %d", current->pid);
}

/*
 * 2412634 罗茜：主动让出 CPU。
 * 这里只设置调度标志，真正切换由 trap 返回路径调用 schedule()。
 */
int
do_yield(void)
{
    current->need_resched = 1;
    return 0;
}

/*
 * 2412634 罗茜：向目标进程发送退出请求。
 * 目标进程会在下一次返回用户态之前执行 do_exit()。
 */
int
do_kill(int pid)
{
    struct proc_struct *proc = find_proc(pid);
    if (proc == NULL || proc == idleproc || proc == initproc ||
        proc->state == PROC_ZOMBIE)
    {
        return -E_INVAL;
    }
    if (proc->flags & PF_EXITING)
    {
        return -E_KILLED;
    }

    proc->flags |= PF_EXITING;
    if (proc->state == PROC_SLEEPING ||
        (proc->wait_state & WT_INTERRUPTED))
    {
        // 2412634：睡眠或可中断等待中的进程需要被唤醒，才能处理退出请求。
        wakeup_proc(proc);
    }
    return 0;
}


#define __KERNEL_EXECVE(name, binary, size) ({           \
    cprintf("kernel_execve: pid = %d, name = \"%s\".\n", \
            current->pid, name);                         \
    kernel_execve(name, binary, (size_t)(size));         \
})

#define KERNEL_EXECVE(x) ({                                    \
    extern unsigned char _binary_obj___user_##x##_out_start[], \
        _binary_obj___user_##x##_out_size[];                   \
    __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,    \
                    _binary_obj___user_##x##_out_size);        \
})

#define __KERNEL_EXECVE2(x, xstart, xsize) ({   \
    extern unsigned char xstart[], xsize[];     \
    __KERNEL_EXECVE(#x, xstart, (size_t)xsize); \
})

#define KERNEL_EXECVE2(x, xstart, xsize) __KERNEL_EXECVE2(x, xstart, xsize)


//lab5 function2 YOUR CODE



