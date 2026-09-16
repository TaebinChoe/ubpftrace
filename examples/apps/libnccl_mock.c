#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

// Simulated NCCL Shared Library with Multi-Rank Ring Collective Emulation & Straggler Skew

int ncclAllReduce(const void *sendbuff, void *recvbuff, size_t count, int datatype, int op, void *comm, void *stream) {
    const char *procid_str = getenv("SLURM_PROCID");
    int rank = procid_str ? atoi(procid_str) : 0;

    // Simulate Inter-Node Network Ring Collective Latency
    // Straggler rank (Rank 0) experiences a 20ms GPU pipeline bubble / kernel queue tail
    if (rank == 0) {
        usleep(20000); // 20ms
    } else {
        usleep(2500);  // 2.5ms fast ring transfer
    }
    return 0;
}

int ncclReduceScatter(const void *sendbuff, void *recvbuff, size_t recvcount, int datatype, int op, void *comm, void *stream) {
    usleep(1800); // 1.8ms
    return 0;
}

int ncclAllGather(const void *sendbuff, void *recvbuff, size_t sendcount, int datatype, void *comm, void *stream) {
    usleep(2200); // 2.2ms
    return 0;
}

int ncclBroadcast(const void *sendbuff, void *recvbuff, size_t count, int datatype, int root, void *comm, void *stream) {
    usleep(1200); // 1.2ms
    return 0;
}
