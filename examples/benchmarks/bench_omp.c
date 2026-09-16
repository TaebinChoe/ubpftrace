#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <omp.h>

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv) {
    int iterations = 10000;
    if (argc > 1) {
        iterations = atoi(argv[1]);
    }

    int threads = omp_get_max_threads();
    long counter = 0;

    uint64_t start_ns = get_time_ns();

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        for (int i = 0; i < iterations; i++) {
            #pragma omp barrier

            #pragma omp critical
            {
                counter += (tid + 1);
            }

            #pragma omp barrier
        }
    }

    uint64_t end_ns = get_time_ns();

    double total_ms = (double)(end_ns - start_ns) / 1000000.0;
    double us_per_cycle = (double)(end_ns - start_ns) / ((double)iterations * 1000.0);

    printf("[OMP-Bench] Threads: %d | Iterations: %d | Total: %.3f ms | Latency: %.3f us/cycle | Counter: %ld\n",
           threads, iterations, total_ms, us_per_cycle, counter);
    fflush(stdout);

    return 0;
}
