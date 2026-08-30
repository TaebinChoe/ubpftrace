#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(void) {
    printf("=== Target Application Starting ===\n\n");

    for (int i = 1; i <= 3; i++) {
        // 1. Call libc puts()
        puts("[App] Message via standard library puts()");

        // 2. Call libc write()
        const char *msg = "[App] Message via standard library write()\n";
        write(STDOUT_FILENO, msg, strlen(msg));

        printf("\n");
        sleep(1);
    }

    printf("=== Target Application Done ===\n");
    return 0;
}
