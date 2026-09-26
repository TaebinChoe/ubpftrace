#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

__attribute__((noinline)) int target_work(int iter) {
    volatile int x = iter * 3 + 1;
    return x;
}

int main(int argc, char **argv) {
    printf("TARGET_READY PID=%d\n", getpid());
    fflush(stdout);

    int iter = 0;
    while (1) {
        target_work(iter++);
        usleep(20000); // 20ms (50 calls/sec)
    }
    return 0;
}
