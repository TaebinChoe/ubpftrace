#define _GNU_SOURCE
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    printf("[P2P App] Rank %d/%d starting P2P communication loops...\n", rank, size);
    fflush(stdout);

    const int count = 2048;
    int *sendbuf = (int *)malloc(count * sizeof(int));
    int *recvbuf = (int *)malloc(count * sizeof(int));

    for (int i = 0; i < count; i++) {
        sendbuf[i] = rank * 1000 + i;
    }

    // 1. MPI_Send / MPI_Recv loop (Pairwise exchange to avoid ring deadlocks)
    for (int iter = 0; iter < 4; iter++) {
        if (size > 1) {
            int partner = (rank % 2 == 0) ? (rank + 1) % size : (rank - 1 + size) % size;
            int tag = 100 + iter;
            if (rank % 2 == 0) {
                MPI_Send(sendbuf, count, MPI_INT, partner, tag, MPI_COMM_WORLD);
                MPI_Recv(recvbuf, count, MPI_INT, partner, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            } else {
                MPI_Recv(recvbuf, count, MPI_INT, partner, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Send(sendbuf, count, MPI_INT, partner, tag, MPI_COMM_WORLD);
            }
        } else {
            MPI_Sendrecv(sendbuf, count, MPI_INT, 0, 10 + iter,
                         recvbuf, count, MPI_INT, 0, 10 + iter,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        usleep(5000);
    }

    // 2. Non-blocking MPI_Isend / MPI_Irecv
    for (int iter = 0; iter < 4; iter++) {
        if (size > 1) {
            int partner = (rank % 2 == 0) ? (rank + 1) % size : (rank - 1 + size) % size;
            int tag = 200 + iter;
            MPI_Request reqs[2];
            MPI_Irecv(recvbuf, count / 2, MPI_INT, partner, tag, MPI_COMM_WORLD, &reqs[0]);
            MPI_Isend(sendbuf, count / 2, MPI_INT, partner, tag, MPI_COMM_WORLD, &reqs[1]);
            MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);
        } else {
            MPI_Request reqs[2];
            MPI_Irecv(recvbuf, count / 2, MPI_INT, 0, 200 + iter, MPI_COMM_WORLD, &reqs[0]);
            MPI_Isend(sendbuf, count / 2, MPI_INT, 0, 200 + iter, MPI_COMM_WORLD, &reqs[1]);
            MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);
        }
        usleep(5000);
    }

    // 3. MPI_Sendrecv exchange
    for (int iter = 0; iter < 4; iter++) {
        if (size > 1) {
            int partner = (rank % 2 == 0) ? (rank + 1) % size : (rank - 1 + size) % size;
            int tag = 300 + iter;
            MPI_Sendrecv(sendbuf, count, MPI_INT, partner, tag,
                         recvbuf, count, MPI_INT, partner, tag,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        } else {
            MPI_Sendrecv(sendbuf, count, MPI_INT, 0, 300 + iter,
                         recvbuf, count, MPI_INT, 0, 300 + iter,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        usleep(5000);
    }

    printf("[P2P App] Rank %d finished all P2P exchanges.\n", rank);
    fflush(stdout);

    free(sendbuf);
    free(recvbuf);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
