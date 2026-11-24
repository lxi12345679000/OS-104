#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <assert.h>
#include <error.h>

/*
 * 2412634 罗茜：系统调用处理函数统一接收 a1~a5 参数，
 * 返回值最终写回用户态的 a0 寄存器。
 */
static int sys_exit(uint64_t arg[]);
static int sys_fork(uint64_t arg[]);
static int sys_wait(uint64_t arg[]);
static int sys_exec(uint64_t arg[]);
static int sys_yield(uint64_t arg[]);
static int sys_kill(uint64_t arg[]);
static int sys_getpid(uint64_t arg[]);
static int sys_putc(uint64_t arg[]);
static int sys_pgdir(uint64_t arg[]);

static int (*syscalls[])(uint64_t arg[]) = {
    [SYS_exit]              sys_exit,
    [SYS_fork]              sys_fork,
    [SYS_wait]              sys_wait,
    [SYS_exec]              sys_exec,
    [SYS_yield]             sys_yield,
    [SYS_kill]              sys_kill,
    [SYS_getpid]            sys_getpid,
    [SYS_putc]              sys_putc,
    [SYS_pgdir]             sys_pgdir,
};

#define NUM_SYSCALLS        ((sizeof(syscalls)) / (sizeof(syscalls[0])))

static int
sys_exit(uint64_t arg[])
{
    // 2412634：结束当前进程，并把退出码交给父进程。
    return do_exit((int)arg[0]);
}

static int
sys_fork(uint64_t arg[])
{
    // 2412634：fork 从当前 trapframe 复制用户上下文，子进程返回 0。
    struct trapframe *tf = current->tf;
    return do_fork(0, tf->gpr.sp, tf);
}

static int
sys_wait(uint64_t arg[])
{
    // 2412634：pid 为 0 表示等待任意子进程，退出码写回用户地址。
    return do_wait((int)arg[0], (int *)arg[1]);
}

static int
sys_exec(uint64_t arg[])
{
    // 2412634：a1~a4 依次为程序名、名称长度、ELF 地址和大小。
    return do_execve((const char *)arg[0], (size_t)arg[1],
                     (unsigned char *)arg[2], (size_t)arg[3]);
}

static int
sys_yield(uint64_t arg[])
{
    // 2412634：设置调度标志，具体切换由 trap 返回路径完成。
    return do_yield();
}

static int
sys_kill(uint64_t arg[])
{
    // 2412634：向目标进程设置退出请求，不直接释放其资源。
    return do_kill((int)arg[0]);
}

static int
sys_getpid(uint64_t arg[])
{
    // 2412634：返回当前进程的 pid。
    return current->pid;
}

static int
sys_putc(uint64_t arg[])
{
    // 2412634：用户态字符输出必须由内核代为访问控制台。
    cputchar((int)arg[0]);
    return 0;
}

static int
sys_pgdir(uint64_t arg[])
{
    // 2412634：打印当前页表，供调试使用。
    print_pgdir();
    return 0;
}

//lab5 function1 2412634
void
syscall(void)
{
    struct trapframe *tf = current->tf;

    // 2412634：a0 是系统调用号，a1 开始是参数。
    uint64_t num = tf->gpr.a0;
    int ret = -E_INVAL;

    // 2412634：先检查编号范围，避免越界访问系统调用表。
    if (num < NUM_SYSCALLS && syscalls[num] != NULL)
    {
        ret = syscalls[num](&tf->gpr.a1);
    }
    // 2412634：返回值覆盖 a0，sret 后用户库可直接读取。
    tf->gpr.a0 = ret;
}
