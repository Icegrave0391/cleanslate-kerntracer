#include <unistd.h>

__attribute__((noinline))
int foo(void) {
    return 1;
}

int main(void) {
    int i;
    for (i = 0; i < 20000; i++) {
        usleep(1000);  /* 1ms */
        foo();
    }
    return 0;
}
