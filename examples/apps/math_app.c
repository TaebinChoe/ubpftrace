#include <stdio.h>
#include <math.h>
#include <unistd.h>

int main(void)
{
    printf("[Math App] Starting calculations...\n");
    for (int i = 1; i <= 5; i++) {
        double res_sin = sin((double)i);
        double res_sqrt = sqrt((double)(i * 100));
        printf("[Math App] i=%d -> sin=%.4f, sqrt=%.4f\n", i, res_sin, res_sqrt);
        usleep(50000);
    }
    printf("[Math App] Finished calculations.\n");
    return 0;
}
