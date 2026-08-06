#include <stdio.h>
#include <stdlib.h>
int main() {
    for (int i = 0; i < 10; i++) {
        short v = (short)((rand() % 65536) - 32768);
        printf("%d -> %d (rand=%d)\n", i, v, rand());
    }
    return 0;
}
