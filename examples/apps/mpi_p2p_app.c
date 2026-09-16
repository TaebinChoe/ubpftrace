#define _GNU_SOURCE
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define HALO_SIZE_SMALL  4096    // 16 KB
#define HALO_SIZE_LARGE  16384   // 64 KB
#define AGG_SIZE_INTS    262144  // 1 MB (Rendezvous protocol payload)

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    printf("[P2P App] Rank %2d/%2d on node '%s' initialized.\n", rank, size, hostname);
    fflush(stdout);

    // Dedicated non-overlapping buffers for simultaneous Isend / Irecv
    int *send_left  = (int *)malloc(HALO_SIZE_LARGE * sizeof(int));
    int *recv_left  = (int *)malloc(HALO_SIZE_LARGE * sizeof(int));
    int *send_right = (int *)malloc(HALO_SIZE_LARGE * sizeof(int));
    int *recv_right = (int *)malloc(HALO_SIZE_LARGE * sizeof(int));
    int *agg_buf    = (int *)malloc(AGG_SIZE_INTS * sizeof(int));
    int token[64];

    if (!send_left || !recv_left || !send_right || !recv_right || !agg_buf) {
        fprintf(stderr, "[P2P App] Error: Failed to allocate memory buffers\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    memset(send_left,  0x11 * (rank + 1), HALO_SIZE_LARGE * sizeof(int));
    memset(send_right, 0x22 * (rank + 1), HALO_SIZE_LARGE * sizeof(int));
    memset(agg_buf,    0x33 * (rank + 1), AGG_SIZE_INTS * sizeof(int));
    memset(token,      0x44, sizeof(token));

    const int NUM_EPOCHS = 3;

    for (int epoch = 0; epoch < NUM_EPOCHS; epoch++) {
        if (rank == 0) {
            printf("\n--- Communication Epoch %d/%d ---\n", epoch + 1, NUM_EPOCHS);
        }

        // ====================================================================
        // Pattern 1: 2D Ring Halo Boundary Exchange (Non-blocking Isend / Irecv)
        // Explicit matched tags for rightward and leftward boundary flows
        // ====================================================================
        if (size > 1) {
            int left_peer  = (rank - 1 + size) % size;
            int right_peer = (rank + 1) % size;

            int tag_to_right = 1000 + epoch * 10 + 1;
            int tag_to_left  = 1000 + epoch * 10 + 2;

            MPI_Request reqs[4];
            // 1. Rightward boundary flow: send to right_peer, recv from left_peer
            MPI_Irecv(recv_left,  HALO_SIZE_LARGE, MPI_INT, left_peer,  tag_to_right, MPI_COMM_WORLD, &reqs[0]);
            MPI_Isend(send_right, HALO_SIZE_LARGE, MPI_INT, right_peer, tag_to_right, MPI_COMM_WORLD, &reqs[1]);

            // 2. Leftward boundary flow: send to left_peer, recv from right_peer
            MPI_Irecv(recv_right, HALO_SIZE_SMALL, MPI_INT, right_peer, tag_to_left,  MPI_COMM_WORLD, &reqs[2]);
            MPI_Isend(send_left,  HALO_SIZE_SMALL, MPI_INT, left_peer,  tag_to_left,  MPI_COMM_WORLD, &reqs[3]);

            MPI_Waitall(4, reqs, MPI_STATUSES_IGNORE);
        }
        usleep(3000); // 3ms compute between phases

        // ====================================================================
        // Pattern 2: Asymmetric Worker-to-Master Aggregation (Rendezvous Fan-In)
        // All workers (ranks 1..size-1) stream heavy tensor shards (1 MB) to Rank 0
        // ====================================================================
        if (size > 1) {
            if (rank == 0) {
                for (int worker = 1; worker < size; worker++) {
                    int tag = 2000 + epoch * 100 + worker;
                    MPI_Recv(agg_buf, AGG_SIZE_INTS, MPI_INT, worker, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
            } else {
                int tag = 2000 + epoch * 100 + rank;
                MPI_Send(agg_buf, AGG_SIZE_INTS, MPI_INT, 0, tag, MPI_COMM_WORLD);
            }
        }
        usleep(2000);

        // ====================================================================
        // Pattern 3: Master Sync Token Reply (Send / Recv)
        // Rank 0 replies with small acknowledgment / control tokens (256 bytes)
        // ====================================================================
        if (size > 1) {
            if (rank == 0) {
                for (int worker = 1; worker < size; worker++) {
                    token[0] = 0xCAFE;
                    int tag = 3000 + epoch * 100 + worker;
                    MPI_Send(token, 64, MPI_INT, worker, tag, MPI_COMM_WORLD);
                }
            } else {
                int tag = 3000 + epoch * 100 + rank;
                MPI_Recv(token, 64, MPI_INT, 0, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
        }
        usleep(1000);
    }

    printf("[P2P App] Rank %2d finished all communication patterns.\n", rank);
    fflush(stdout);

    free(send_left);
    free(recv_left);
    free(send_right);
    free(recv_right);
    free(agg_buf);

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
