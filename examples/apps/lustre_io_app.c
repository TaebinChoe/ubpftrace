#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>

void perform_lustre_io(int fd, size_t num_chunks, size_t chunk_size) {
    char *buf = (char *)aligned_alloc(4096, chunk_size);
    if (!buf) {
        perror("aligned_alloc");
        return;
    }
    memset(buf, 0xAB, chunk_size);

    for (size_t i = 0; i < num_chunks; ++i) {
        off_t offset = (off_t)i * chunk_size;
        ssize_t written = pwrite(fd, buf, chunk_size, offset);
        if (written < 0) {
            perror("pwrite");
        } else {
            printf("[LustreApp] Wrote %zd bytes at offset %lu MB\n", written, (unsigned long)(offset / (1024 * 1024)));
        }
        usleep(10000); // 10ms delay
    }

    free(buf);
}

int main(int argc, char **argv) {
    const char *filepath = "/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/scratch_lustre_test.dat";
    if (argc > 1) {
        filepath = argv[1];
    }

    int fd = open(filepath, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("[LustreApp] Target file opened: %s (fd=%d)\n", filepath, fd);
    
    // Write 8 chunks of 1MB each across the stripes
    perform_lustre_io(fd, 8, 1024 * 1024);

    close(fd);
    printf("[LustreApp] Done.\n");
    return 0;
}
