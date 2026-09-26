#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <execinfo.h>

static void segv_handler(int sig, siginfo_t *info, void *ucontext) {
    (void)sig; (void)ucontext;
    fprintf(stderr, "\n[TARGET CRASH] Caught SIGSEGV at address %p\n", info->si_addr);
    void *buf[64];
    int n = backtrace(buf, 64);
    backtrace_symbols_fd(buf, n, STDERR_FILENO);
    _exit(139);
}

__attribute__((noinline)) void target_work(int iter) {
    volatile int x = iter * 2;
    (void)x;
}

int main() {
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segv_handler;
    sigaction(SIGSEGV, &sa, NULL);

    printf("MRE target running with PID: %d\n", getpid());
    fflush(stdout);
    int iter = 0;
    while (1) {
        target_work(iter++);
        usleep(20000); // 20ms per iteration
    }
    return 0;
}
