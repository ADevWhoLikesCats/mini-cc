#include <stdio.h>

extern int id(int a);
extern int forty_two(void);

int main(void) {
    printf("id(42)      = %d\n", id(42));
    printf("id(-7)      = %d\n", id(-7));
    printf("forty_two() = %d\n", forty_two());
    return 0;
}
