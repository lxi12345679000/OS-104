#include <stdio.h>
#include <ulib.h>

static volatile int value __attribute__((aligned(4096))) = 7;

int main(void)
{
    int status;
    int child = fork();
    assert(child >= 0);
    if (child == 0) {
        assert(value == 7);
        value = 11;
        int grandchild = fork();
        assert(grandchild >= 0);
        if (grandchild == 0) {
            assert(value == 11);
            value = 13;
            exit(value);
        }
        assert(waitpid(grandchild, &status) == 0 && status == 13);
        assert(value == 11);
        exit(value);
    }
    assert(waitpid(child, &status) == 0 && status == 11);
    assert(value == 7);
    value = 17;
    assert(value == 17);
    cprintf("cowtest pass.\n");
    return 0;
}
