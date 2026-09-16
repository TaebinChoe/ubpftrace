#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

// Forward declarations of CUDA Runtime APIs
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

extern cudaError_t cudaStreamSynchronize(cudaStream_t stream);
extern cudaError_t cudaDeviceSynchronize(void);
extern cudaError_t cudaEventSynchronize(cudaEvent_t event);
extern cudaError_t cudaMemcpy(void *dst, const void *src, size_t count, enum cudaMemcpyKind kind);

int main() {
    printf("[CUDA App] Starting CUDA synchronization and memory test...\n");

    // Allocate host buffers
    size_t size = 1024 * 1024; // 1MB
    char *src = (char *)malloc(size);
    char *dst = (char *)malloc(size);
    memset(src, 0x42, size);

    for (int iter = 0; iter < 4; iter++) {
        // 1. cudaMemcpy (synchronous copy)
        cudaMemcpy(dst, src, size, cudaMemcpyHostToHost);
        usleep(2000);

        // 2. cudaStreamSynchronize
        cudaStreamSynchronize(NULL);
        usleep(3000);

        // 3. cudaDeviceSynchronize
        cudaDeviceSynchronize();
        usleep(1000);

        // 4. cudaEventSynchronize
        cudaEventSynchronize(NULL);
        usleep(1500);
    }

    printf("[CUDA App] Finished CUDA synchronization calls.\n");
    free(src);
    free(dst);
    return 0;
}
