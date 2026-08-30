#include <stdio.h>
#include <unistd.h>

// A user-defined function with 2 arguments
int calculate(int a, int b) {
    printf("[Target App] calculate(%d, %d) executing...\n", a, b);
    return a * b;
}

int main(void) {
    printf("[Target App] Starting multiplication loop...\n\n");
    for (int i = 1; i <= 5; i++) {
        int result = calculate(i, i * 10);
        printf("[Target App] Returned result = %d\n\n", result);
        sleep(1);
    }
    printf("[Target App] Done!\n");
    return 0;
}
