#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    printf("[Alloc App] Starting allocations...\n");
    void *p1 = malloc(64);
    void *p2 = malloc(256);
    void *p3 = malloc(1024);
    void *p4 = malloc(4096);
    
    printf("[Alloc App] Allocations done: %p %p %p %p\n", p1, p2, p3, p4);
    
    free(p1);
    free(p2);
    free(p3);
    free(p4);
    
    printf("[Alloc App] Finished successfully.\n");
    return 0;
}
