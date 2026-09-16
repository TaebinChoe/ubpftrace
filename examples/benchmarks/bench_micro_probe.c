#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

// Targeted probe function with noinline to prevent compiler optimization
__attribute__((noinline)) void target_probe_func(int arg0, int arg1) {
    __asm__ volatile("" : : "r"(arg0), "r"(arg1) : "memory");
}

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv) {
    long iterations = 500000;
    if (argc > 1) {
        iterations = atol(argv[1]);
    }

    // Warmup
    for (int i = 0; i < 10000; i++) {
        target_probe_func(i, i * 2);
    }

    uint64_t start_ns = get_time_ns();
    for (long i = 0; i < iterations; i++) {
        target_probe_func((int)i, (int)(i & 0xFF));
    }
    uint64_t end_ns = get_time_ns();

    double total_ms = (double)(end_ns - start_ns) / 1000000.0;
    double ns_per_call = (double)(end_ns - start_ns) / (double)iterations;

    printf("[MicroBench] Iterations: %ld | Total Time: %.3f ms | Latency: %.2f ns/op\n",
           iterations, total_ms, ns_per_call);

    return 0;
}
