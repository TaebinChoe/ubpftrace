#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>

// Simulated 4-OST Striped Checkpoint Writer with Injected OST Congestion Bottleneck
void perform_lustre_checkpoint(int fd, size_t num_chunks, size_t chunk_size) {
    char *buf = (char *)aligned_alloc(4096, chunk_size);
    if (!buf) {
        perror("aligned_alloc");
        return;
    }
    memset(buf, 0x5A, chunk_size);

    printf("[LustreApp] Initiating striped parallel checkpoint (%zu MB across 4 OSTs)...\n",
           (num_chunks * chunk_size) / (1024 * 1024));

    for (size_t i = 0; i < num_chunks; ++i) {
        off_t offset = (off_t)i * chunk_size;
        int stripe_idx = i % 4;

        // Deliberate Bottleneck Injection:
        // Simulate severe storage congestion / lock queue contention on OST #2 (the 3rd OST stripe)
        if (stripe_idx == 2) {
            // Congested OST: 35ms stall
            usleep(35000);
        } else {
            // Normal healthy OST: 200us fast write
            usleep(200);
        }

        ssize_t written = pwrite64(fd, buf, chunk_size, offset);
        if (written < 0) {
            perror("pwrite64");
        } else {
            printf("[LustreApp] Chunk %2zu (offset %2lu MB, Stripe OST #%d): wrote %zd bytes %s\n",
                   i, (unsigned long)(offset / (1024 * 1024)), stripe_idx, written,
                   (stripe_idx == 2) ? "[SLOW - CONGESTED OST]" : "[FAST]");
        }
    }

    // Storage Commit Barrier (fsync / fdatasync)
    printf("[LustreApp] Flusing dirty buffers to physical OST storage (fdatasync)...\n");
    usleep(5000); // 5ms commit time
    fdatasync(fd);

    free(buf);
}

int main(int argc, char **argv) {
    const char *filepath = "/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/scratch_lustre_striped.dat";
    if (argc > 1) {
        filepath = argv[1];
    }

    // Ensure 4-OST striping on Lustre file
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -f %s && lfs setstripe -c 4 -S 1M %s 2>/dev/null || true", filepath, filepath);
    system(cmd);

    int fd = open(filepath, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("[LustreApp] Target file opened: %s (fd=%d)\n", filepath, fd);
    
    // Write 16 chunks of 1MB each across 4 striped OSTs (4 complete stripe cycles)
    for (int epoch = 0; epoch < 2; epoch++) {
        printf("\n--- Checkpoint Epoch %d ---\n", epoch + 1);
        perform_lustre_checkpoint(fd, 16, 1024 * 1024);
    }

    close(fd);
    printf("\n[LustreApp] Checkpoint sequence finished successfully.\n");
    return 0;
}
