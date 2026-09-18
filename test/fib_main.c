#include <stdio.h>

extern int fib(int);
extern int fib_iter(int);

int main(void) {
    for (int i = 0; i < 15; i = i + 1) {
        printf("fib(%2d)      = %d\n", i, fib(i));
        printf("fib_iter(%2d) = %d\n", i, fib_iter(i));
    }
    return 0;
}
