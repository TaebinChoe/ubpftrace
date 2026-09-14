#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

// Forward declarations of NCCL types and APIs
typedef enum {
    ncclInt8 = 0,
    ncclChar = 0,
    ncclUint8 = 1,
    ncclInt32 = 2,
    ncclInt = 2,
    ncclUint32 = 3,
    ncclInt64 = 4,
    ncclUint64 = 5,
    ncclFloat16 = 6,
    ncclHalf = 6,
    ncclFloat32 = 7,
    ncclFloat = 7,
    ncclFloat64 = 8,
    ncclDouble = 8,
    ncclBfloat16 = 9
} ncclDataType_t;

typedef enum {
    ncclSum = 0,
    ncclProd = 1,
    ncclMax = 2,
    ncclMin = 3,
    ncclAvg = 4
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

int main() {
    printf("[NCCL App] Starting distributed AI collective communication loops...\n");

    const size_t count = 1048576; // 1M elements
    float *sendbuf = (float *)malloc(count * sizeof(float));
    float *recvbuf = (float *)malloc(count * sizeof(float));

    ncclComm_t fake_comm = (ncclComm_t)(uintptr_t)0x1000;
    void *fake_stream = (void *)(uintptr_t)0x2000;

    for (int iter = 0; iter < 4; iter++) {
        // 1. AllReduce
        ncclAllReduce(sendbuf, recvbuf, count, ncclFloat32, ncclSum, fake_comm, fake_stream);
        usleep(3000);

        // 2. ReduceScatter
        ncclReduceScatter(sendbuf, recvbuf, count / 4, ncclFloat32, ncclSum, fake_comm, fake_stream);
        usleep(2000);

        // 3. AllGather
        ncclAllGather(sendbuf, recvbuf, count / 4, ncclFloat32, fake_comm, fake_stream);
        usleep(2000);

        // 4. Broadcast
        ncclBroadcast(sendbuf, recvbuf, count, ncclFloat32, 0, fake_comm, fake_stream);
        usleep(1000);
    }

    printf("[NCCL App] Finished all collective operations.\n");
    free(sendbuf);
    free(recvbuf);
    return 0;
}
