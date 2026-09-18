#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Simulated compute task taking task_id and data_size as arguments
void compute_task(int task_id, int data_size) {
    // Simulate ~100ms of work
    usleep(100000);
}

int main(int argc, char **argv) {
    int total_tasks = 20;
    if (argc > 1) {
        total_tasks = atoi(argv[1]);
    }

    printf("[TargetApp] Starting execution of %d tasks (~%.1f seconds)...\n",
           total_tasks, (double)total_tasks * 0.1);

    for (int i = 0; i < total_tasks; i++) {
        int data_size = (i * 17 + 10) % 100;
        compute_task(i, data_size);

        if ((i + 1) % 10 == 0 || (i + 1) == total_tasks) {
            printf("[TargetApp] Progress: %d / %d tasks (current task_id=%d, data_size=%d)\n",
                   i + 1, total_tasks, i, data_size);
        }
    }

    printf("[TargetApp] Finished all tasks successfully.\n");
    return 0;
}
