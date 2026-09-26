#define _GNU_SOURCE
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define NUM_ITERATIONS 4
#define MSG_ELEMENTS 16384 // 64 KB per halo slice

// Simulated Computational Domain
// Rank 0 is assigned a fine-mesh refinement tile (16x compute workload -> ~25ms delay)
// Ranks 1..N-1 have standard coarse grid tiles (~1.5ms delay)
__attribute__((noinline, noclone))
void simulate_grid_computation(int rank, int iter) {
    if (rank == 0) {
        // Computational straggler: 25ms
        usleep(25000);
    } else {
        // Fast worker ranks: 1.5ms
        usleep(1500);
    }
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    printf("[HPC App] Rank %2d/%2d starting on '%s' %s\n",
           rank, size, hostname, (rank == 0) ? "[STRAGGLER - HEAVY LOAD]" : "[FAST WORKER]");
    fflush(stdout);

    int *send_buf = (int *)malloc(MSG_ELEMENTS * sizeof(int));
    int *recv_buf = (int *)malloc(MSG_ELEMENTS * sizeof(int));
    int *a2a_send = (int *)malloc(size * 1024 * sizeof(int));
    int *a2a_recv = (int *)malloc(size * 1024 * sizeof(int));

    for (int i = 0; i < MSG_ELEMENTS; i++) {
        send_buf[i] = rank * 1000 + i;
    }

    double local_energy = 100.0 * (rank + 1);
    double global_energy = 0.0;
    int num_iterations = (argc > 1) ? atoi(argv[1]) : 10000;
    if (num_iterations == 0) {
        num_iterations = -1; // Continuous mode
    }

    for (int iter = 0; num_iterations < 0 || iter < num_iterations; iter++) {
        if (rank == 0 && ((iter + 1) % 10 == 0 || (iter + 1) == num_iterations || iter < 4)) {
            if (num_iterations > 0) {
                printf("\n--- Simulation Iteration %d/%d ---\n", iter + 1, num_iterations);
            } else {
                printf("\n--- Simulation Iteration %d (Continuous Mode) ---\n", iter + 1);
            }
            fflush(stdout);
        }

        // 1. Non-blocking Boundary Halo Exchange
        if (size > 1) {
            int next = (rank + 1) % size;
            int prev = (rank - 1 + size) % size;
            MPI_Request reqs[2];

            MPI_Irecv(recv_buf, MSG_ELEMENTS, MPI_INT, prev, 100 + iter, MPI_COMM_WORLD, &reqs[0]);
            MPI_Isend(send_buf, MSG_ELEMENTS, MPI_INT, next, 100 + iter, MPI_COMM_WORLD, &reqs[1]);
            MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);
        }

        // 2. Unbalanced Physics Computation Phase (Straggler Generation)
        simulate_grid_computation(rank, iter);
        local_energy += (rank + 1) * 3.14159;

        // 3. Collective Synchronization Barrier (Fast ranks stall waiting for Rank 0)
        MPI_Barrier(MPI_COMM_WORLD);

        // 4. Global Collective Reduction
        MPI_Allreduce(&local_energy, &global_energy, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

        // 5. All-to-all Matrix Transpose Redistribution
        if (size > 1) {
            MPI_Alltoall(a2a_send, 1024, MPI_INT, a2a_recv, 1024, MPI_INT, MPI_COMM_WORLD);
        }
    }

    printf("[HPC App] Rank %2d finished simulation loop. Global Energy: %.2f\n", rank, global_energy);
    fflush(stdout);

    free(send_buf);
    free(recv_buf);
    free(a2a_send);
    free(a2a_recv);

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
