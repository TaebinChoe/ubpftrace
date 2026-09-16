#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv) {
    const char *filepath = "/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/scratch_lustre_bench.dat";
    int iterations = 1000;
    if (argc > 1) {
        iterations = atoi(argv[1]);
    }

    // Set 4-OST striping
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -f %s && lfs setstripe -c 4 -S 1M %s 2>/dev/null || true", filepath, filepath);
    system(cmd);

    int fd = open(filepath, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    size_t chunk_size = 65536; // 64 KB per write
    char *buf = (char *)aligned_alloc(4096, chunk_size);
    memset(buf, 0xEE, chunk_size);

    uint64_t start_ns = get_time_ns();
    for (int i = 0; i < iterations; i++) {
        off_t offset = (off_t)(i % 64) * chunk_size;
        ssize_t ret = pwrite64(fd, buf, chunk_size, offset);
        if (ret < 0) perror("pwrite64");

        if ((i + 1) % 100 == 0) {
            fdatasync(fd);
        }
    }
    fdatasync(fd);
    uint64_t end_ns = get_time_ns();

    close(fd);
    free(buf);

    double total_ms = (double)(end_ns - start_ns) / 1000000.0;
    double us_per_op = (double)(end_ns - start_ns) / ((double)iterations * 1000.0);
    double mb_written = (double)(iterations * chunk_size) / (1024.0 * 1024.0);
    double mb_per_sec = mb_written / (total_ms / 1000.0);

    printf("[LustreBench] Ops: %d | Total: %.3f ms | Latency: %.2f us/op | Throughput: %.2f MB/s\n",
           iterations, total_ms, us_per_op, mb_per_sec);
    fflush(stdout);

    return 0;
}
