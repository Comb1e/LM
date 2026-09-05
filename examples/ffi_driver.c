#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

extern int64_t twice(int64_t value);

int main(void) {
    printf("%" PRId64 "\n", twice(21));
    return 0;
}
