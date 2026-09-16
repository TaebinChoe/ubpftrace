#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv) {
    int iterations = 100;
    if (argc > 1) {
        iterations = atoi(argv[1]);
    }

    const size_t count = 1024; // 4KB
    float *sendbuf = (float *)malloc(count * sizeof(float));
    float *recvbuf = (float *)malloc(count * sizeof(float));

    ncclComm_t comm = (ncclComm_t)(uintptr_t)0x1000;
    void *stream = (void *)(uintptr_t)0x2000;

    uint64_t start_ns = get_time_ns();

    for (int iter = 0; iter < iterations; iter++) {
        ncclAllReduce(sendbuf, recvbuf, count, ncclFloat32, ncclSum, comm, stream);
        ncclReduceScatter(sendbuf, recvbuf, count / 4, ncclFloat32, ncclSum, comm, stream);
        ncclAllGather(sendbuf, recvbuf, count / 4, ncclFloat32, comm, stream);
        ncclBroadcast(sendbuf, recvbuf, count, ncclFloat32, 0, comm, stream);
    }

    uint64_t end_ns = get_time_ns();

    double total_ms = (double)(end_ns - start_ns) / 1000000.0;
    double us_per_cycle = (double)(end_ns - start_ns) / ((double)iterations * 1000.0);
    double ns_per_collective = (double)(end_ns - start_ns) / ((double)iterations * 4.0);

    printf("[NCCL-Bench] Iterations: %d | Total: %.3f ms | Latency: %.3f us/cycle | Cost: %.2f ns/call\n",
           iterations, total_ms, us_per_cycle, ns_per_collective);
    fflush(stdout);

    free(sendbuf);
    free(recvbuf);
    return 0;
}
