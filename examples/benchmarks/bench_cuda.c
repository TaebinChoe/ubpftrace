#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

typedef int cudaError_t;
typedef void *cudaStream_t;
typedef void *cudaEvent_t;
enum cudaMemcpyKind {
    cudaMemcpyHostToHost = 0,
    cudaMemcpyHostToDevice = 1,
    cudaMemcpyDeviceToHost = 2,
    cudaMemcpyDeviceToDevice = 3,
    cudaMemcpyDefault = 4
};

extern cudaError_t cudaStreamCreate(cudaStream_t *stream);
extern cudaError_t cudaStreamDestroy(cudaStream_t stream);
extern cudaError_t cudaEventCreate(cudaEvent_t *event);
extern cudaError_t cudaEventDestroy(cudaEvent_t event);
extern cudaError_t cudaStreamSynchronize(cudaStream_t stream);
extern cudaError_t cudaDeviceSynchronize(void);
extern cudaError_t cudaEventSynchronize(cudaEvent_t event);
extern cudaError_t cudaMemcpy(void *dst, const void *src, size_t count, enum cudaMemcpyKind kind);

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv) {
    int iterations = 5000;
    if (argc > 1) {
        iterations = atoi(argv[1]);
    }

    size_t size = 4096; // 4KB buffer
    char *src = (char *)malloc(size);
    char *dst = (char *)malloc(size);
    memset(src, 0x55, size);

    cudaStream_t stream = NULL;
    cudaEvent_t event = NULL;
    cudaStreamCreate(&stream);
    cudaEventCreate(&event);

    uint64_t start_ns = get_time_ns();

    for (int iter = 0; iter < iterations; iter++) {
        cudaMemcpy(dst, src, size, cudaMemcpyHostToHost);
        cudaStreamSynchronize(stream);
        cudaDeviceSynchronize();
        cudaEventSynchronize(event);
    }

    uint64_t end_ns = get_time_ns();

    double total_ms = (double)(end_ns - start_ns) / 1000000.0;
    double us_per_cycle = (double)(end_ns - start_ns) / ((double)iterations * 1000.0);
    double ns_per_call = (double)(end_ns - start_ns) / ((double)iterations * 4.0);

    printf("[CUDA-Bench] Iterations: %d | Total: %.3f ms | Latency: %.3f us/cycle | Cost: %.2f ns/call\n",
           iterations, total_ms, us_per_cycle, ns_per_call);
    fflush(stdout);

    if (stream) cudaStreamDestroy(stream);
    if (event) cudaEventDestroy(event);
    free(src);
    free(dst);
    return 0;
}
