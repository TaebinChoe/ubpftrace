#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <omp.h>

void compute_task(int tid, int iter) {
    long sum = 0;
    for (int i = 0; i < 100000; i++) {
        sum += (i ^ (tid + iter));
    }
    if (sum == 0) printf("sum=0\n");
}

int main() {
    printf("[OpenMP App] Starting OpenMP test with %d threads...\n", omp_get_max_threads());

    long shared_counter = 0;

    // 1. Parallel loop with explicit barrier
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        for (int iter = 0; iter < 4; iter++) {
            compute_task(tid, iter);

            #pragma omp barrier

            #pragma omp critical
            {
                shared_counter += (tid + 1);
                usleep(2000); // 2ms inside critical section
            }
        }
    }

    printf("[OpenMP App] Completed parallel sections. shared_counter=%ld\n", shared_counter);
    return 0;
}
