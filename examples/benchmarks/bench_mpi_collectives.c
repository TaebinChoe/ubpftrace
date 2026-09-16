#define _GNU_SOURCE
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int iterations = 5000;
    if (argc > 1) {
        iterations = atoi(argv[1]);
    }

    double send_val = (double)rank * 1.5;
    double recv_val = 0.0;

    MPI_Barrier(MPI_COMM_WORLD);
    uint64_t start_ns = get_time_ns();

    for (int iter = 0; iter < iterations; iter++) {
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Allreduce(&send_val, &recv_val, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    uint64_t end_ns = get_time_ns();

    if (rank == 0) {
        double total_ms = (double)(end_ns - start_ns) / 1000000.0;
        double us_per_collective_cycle = (double)(end_ns - start_ns) / ((double)iterations * 1000.0);

        printf("[MPI-Collective-Bench] Iterations: %d | Total: %.3f ms | Latency: %.3f us/cycle\n",
               iterations, total_ms, us_per_collective_cycle);
        fflush(stdout);
    }

    MPI_Finalize();
    return 0;
}
