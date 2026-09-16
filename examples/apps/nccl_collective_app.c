#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

typedef enum {
    ncclFloat32 = 7
} ncclDataType_t;

typedef enum {
    ncclSum = 0
} ncclRedOp_t;

typedef void *ncclComm_t;
typedef int ncclResult_t;

extern ncclResult_t ncclAllReduce(const void *sendbuff, void *recvbuff, size_t count,
                                  ncclDataType_t datatype, ncclRedOp_t op,
                                  ncclComm_t comm, void *stream);

extern ncclResult_t ncclReduceScatter(const void *sendbuff, void *recvbuff, size_t recvcount,
                                      ncclDataType_t datatype, ncclRedOp_t op,
                                      ncclComm_t comm, void *stream);

extern ncclResult_t ncclAllGather(const void *sendbuff, void *recvbuff, size_t sendcount,
                                  ncclDataType_t datatype, ncclComm_t comm, void *stream);

extern ncclResult_t ncclBroadcast(const void *sendbuff, void *recvbuff, size_t count,
                                  ncclDataType_t datatype, int root, ncclComm_t comm, void *stream);

int main(int argc, char **argv) {
    const char *procid = getenv("SLURM_PROCID");
    int rank = procid ? atoi(procid) : 0;
    const char *nprocs = getenv("SLURM_NPROCS");
    int size = nprocs ? atoi(nprocs) : 1;

    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    printf("[NCCL App] Rank %2d/%2d on host '%s' initializing distributed AI training loop...\n",
           rank, size, hostname);
    fflush(stdout);

    const size_t num_elements = 1048576; // 1M floats = 4 MB per tensor
    float *sendbuf = (float *)malloc(num_elements * sizeof(float));
    float *recvbuf = (float *)malloc(num_elements * sizeof(float));
    if (!sendbuf || !recvbuf) {
        perror("malloc");
        return 1;
    }

    ncclComm_t fake_comm = (ncclComm_t)(uintptr_t)0x1000;
    void *fake_stream = (void *)(uintptr_t)0x2000;

    const int NUM_EPOCHS = 4;

    for (int epoch = 0; epoch < NUM_EPOCHS; epoch++) {
        if (rank == 0) {
            printf("\n--- NCCL Collective Epoch %d/%d ---\n", epoch + 1, NUM_EPOCHS);
        }

        // 1. AllReduce (Gradient Synchronization)
        ncclAllReduce(sendbuf, recvbuf, num_elements, ncclFloat32, ncclSum, fake_comm, fake_stream);
        usleep(2000);

        // 2. ReduceScatter (ZeRO-3 / FSDP Gradient Sharding)
        ncclReduceScatter(sendbuf, recvbuf, num_elements / 4, ncclFloat32, ncclSum, fake_comm, fake_stream);
        usleep(1500);

        // 3. AllGather (ZeRO-3 / FSDP Parameter Gathering)
        ncclAllGather(sendbuf, recvbuf, num_elements / 4, ncclFloat32, fake_comm, fake_stream);
        usleep(1500);

        // 4. Broadcast (Master Parameter / RNG Seed Sync)
        ncclBroadcast(sendbuf, recvbuf, num_elements / 2, ncclFloat32, 0, fake_comm, fake_stream);
        usleep(1000);
    }

    printf("[NCCL App] Rank %2d finished all collective operations.\n", rank);
    free(sendbuf);
    free(recvbuf);
    return 0;
}
