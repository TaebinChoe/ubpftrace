#define _GNU_SOURCE
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    int iterations = 10000;
    if (argc > 1) {
        iterations = atoi(argv[1]);
    }

    const int count = 256; // 1 KB payload
    int *sendbuf = (int *)malloc(count * sizeof(int));
    int *recvbuf = (int *)malloc(count * sizeof(int));
    memset(sendbuf, 0x11 * (rank + 1), count * sizeof(int));

    MPI_Barrier(MPI_COMM_WORLD);
    uint64_t start_ns = get_time_ns();

    for (int iter = 0; iter < iterations; iter++) {
        if (size > 1) {
            int partner = (rank % 2 == 0) ? (rank + 1) % size : (rank - 1 + size) % size;
            int tag = 100 + (iter % 1000);
            if (rank % 2 == 0) {
                MPI_Send(sendbuf, count, MPI_INT, partner, tag, MPI_COMM_WORLD);
                MPI_Recv(recvbuf, count, MPI_INT, partner, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            } else {
                MPI_Recv(recvbuf, count, MPI_INT, partner, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Send(sendbuf, count, MPI_INT, partner, tag, MPI_COMM_WORLD);
            }
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    uint64_t end_ns = get_time_ns();

    if (rank == 0) {
        double total_ms = (double)(end_ns - start_ns) / 1000000.0;
        double us_per_roundtrip = (double)(end_ns - start_ns) / ((double)iterations * 1000.0);
        double msgs_per_sec = ((double)iterations * 2.0 * (double)size) / (total_ms / 1000.0);

        printf("[MPI-P2P-Bench] Iterations: %d | Total: %.3f ms | Latency: %.3f us/RTT | Rate: %.0f msgs/s\n",
               iterations, total_ms, us_per_roundtrip, msgs_per_sec);
        fflush(stdout);
    }

    free(sendbuf);
    free(recvbuf);
    MPI_Finalize();
    return 0;
}
