#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <omp.h>

// Simulated Thread Compute Task
// Thread 0 performs heavy refinement compute (15ms delay)
// Other threads finish early (1ms delay)
void do_thread_compute(int tid, int iter) {
    if (tid == 0) {
        usleep(15000); // 15ms
    } else {
        usleep(1000);  // 1ms
    }
}

int main(int argc, char **argv) {
    int num_threads = omp_get_max_threads();
    printf("[OpenMP App] Initializing OpenMP worker pool with %d threads...\n", num_threads);

    long shared_accumulator = 0;
    const int NUM_ITERATIONS = 4;

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        if (tid == 0) {
            printf("[OpenMP App] Parallel region launched with %d active threads.\n", omp_get_num_threads());
        }

        for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
            // 1. Thread Workload Imbalance Compute Phase
            do_thread_compute(tid, iter);

            // 2. OpenMP Barrier Synchronization (Threads 1..N-1 stall waiting for Thread 0)
            #pragma omp barrier

            // 3. Serialized Hot Critical Section (Severe Lock Convoy Contention)
            #pragma omp critical
            {
                shared_accumulator += (tid + 1) * (iter + 1);
                usleep(1500); // 1.5ms hold time per thread inside critical section
            }

            // 4. Second synchronization barrier
            #pragma omp barrier
        }
    }

    printf("[OpenMP App] Finished OpenMP computations. Final accumulator: %ld\n", shared_accumulator);
    return 0;
}
