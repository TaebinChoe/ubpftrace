#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NUM_ITERATIONS 3
#define MSG_ELEMENTS 1024

void simulate_computation(int rank, int iter)
{
    // Simulate load imbalance (Rank 0 is a straggler doing 5x more work)
    int compute_delay_us = (rank == 0) ? 10000 : 2000;
    usleep(compute_delay_us);
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    printf("[Rank %d/%d] HPC Worker started.\n", rank, size);

    int *send_buf = (int *)malloc(MSG_ELEMENTS * sizeof(int));
    int *recv_buf = (int *)malloc(MSG_ELEMENTS * sizeof(int));

    for (int i = 0; i < MSG_ELEMENTS; i++) {
        send_buf[i] = rank * 1000 + i;
    }

    double local_sum = 0.0;
    double global_sum = 0.0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        // 1. Non-blocking Point-to-Point Halo Exchange (Deadlock-Free)
        if (size > 1) {
            int next = (rank + 1) % size;
            int prev = (rank - 1 + size) % size;
            MPI_Request reqs[2];

            MPI_Irecv(recv_buf, MSG_ELEMENTS, MPI_INT, prev, 100 + iter, MPI_COMM_WORLD, &reqs[0]);
            MPI_Isend(send_buf, MSG_ELEMENTS, MPI_INT, next, 100 + iter, MPI_COMM_WORLD, &reqs[1]);
            MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);
        } else {
            // Self-exchange test for single process execution
            MPI_Sendrecv(send_buf, 256, MPI_INT, 0, 100 + iter, recv_buf, 256, MPI_INT, 0, 100 + iter, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }

        // 2. Unbalanced Computation Phase (Straggler generation)
        simulate_computation(rank, iter);
        local_sum += (rank + 1) * 42.5;

        // 3. Collective Synchronization Barrier (Exposes wait times caused by load imbalance)
        MPI_Barrier(MPI_COMM_WORLD);

        // 4. Collective Allreduce Reduction
        MPI_Allreduce(&local_sum, &global_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    }

    printf("[Rank %d/%d] Completed iterations. Global sum: %.2f\n", rank, size, global_sum);
    fflush(stdout);

    free(send_buf);
    free(recv_buf);

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
