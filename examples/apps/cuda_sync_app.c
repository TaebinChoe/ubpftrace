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

int main(int argc, char **argv) {
    printf("[CUDA App] Starting Deep Learning training step with injected synchronization bubbles...\n");

    size_t size = 4 * 1024 * 1024; // 4MB tensor buffer
    char *src = (char *)malloc(size);
    char *dst = (char *)malloc(size);
    if (!src || !dst) {
        perror("malloc");
        return 1;
    }
    memset(src, 0x42, size);

    const int NUM_STEPS = 5;

    for (int step = 0; step < NUM_STEPS; step++) {
        printf("\n--- Training Step %d/%d ---\n", step + 1, NUM_STEPS);

        // 1. Synchronous Host-to-Device Parameter Transfer
        printf("[CUDA App] Step %d: Synchronous cudaMemcpy (HostToDevice, 4 MB)...\n", step + 1);
        cudaMemcpy(dst, src, size, cudaMemcpyHostToDevice);
        usleep(4000); // 4ms transfer wait

        // 2. Stream Synchronization (Waiting on Forward Pass Kernel Queue)
        printf("[CUDA App] Step %d: Blocking cudaStreamSynchronize (Forward Pass)...\n", step + 1);
        cudaStreamSynchronize((cudaStream_t)(uintptr_t)0x1);
        usleep(8000); // 8ms bubble

        // 3. Synchronous Device-to-Host Loss / Metric Fetch (Anti-pattern: .item() / print loss)
        printf("[CUDA App] Step %d: Synchronous cudaMemcpy (DeviceToHost Loss Tensor)...\n", step + 1);
        cudaMemcpy(dst, src, 1024, cudaMemcpyDeviceToHost);
        usleep(3000); // 3ms pipeline bubble

        // 4. Explicit Full Device Barrier (cudaDeviceSynchronize)
        printf("[CUDA App] Step %d: Full GPU Device Synchronization Barrier...\n", step + 1);
        cudaDeviceSynchronize();
        usleep(12000); // 12ms pipeline stall

        // 5. Event Synchronization
        printf("[CUDA App] Step %d: cudaEventSynchronize (Gradient Reduction Event)...\n", step + 1);
        cudaEventSynchronize((cudaEvent_t)(uintptr_t)0x2);
        usleep(5000); // 5ms
    }

    printf("\n[CUDA App] Training sequence finished.\n");
    free(src);
    free(dst);
    return 0;
}
