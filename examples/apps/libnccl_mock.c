#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

int ncclAllReduce(const void *sendbuff, void *recvbuff, size_t count, int datatype, int op, void *comm, void *stream) {
    usleep(3000); // simulate 3ms collective transfer
    return 0;
}

int ncclReduceScatter(const void *sendbuff, void *recvbuff, size_t recvcount, int datatype, int op, void *comm, void *stream) {
    usleep(2000); // simulate 2ms collective transfer
    return 0;
}

int ncclAllGather(const void *sendbuff, void *recvbuff, size_t sendcount, int datatype, void *comm, void *stream) {
    usleep(2500); // simulate 2.5ms collective transfer
    return 0;
}

int ncclBroadcast(const void *sendbuff, void *recvbuff, size_t count, int datatype, int root, void *comm, void *stream) {
    usleep(1000); // simulate 1ms collective transfer
    return 0;
}
