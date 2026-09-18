#include <stdio.h>

extern int add(int, int);
extern int sub(int, int);
extern int mul(int, int);
extern int divide(int, int);
extern int modulo(int, int);
extern int nested(int, int);

int main(void) {
    printf("add(10, 3)    = %d  (expect 13)\n", add(10, 3));
    printf("sub(10, 3)    = %d  (expect 7)\n",  sub(10, 3));
    printf("mul(10, 3)    = %d  (expect 30)\n", mul(10, 3));
    printf("divide(10, 3) = %d  (expect 3)\n",  divide(10, 3));
    printf("modulo(10, 3) = %d  (expect 1)\n",  modulo(10, 3));
    printf("nested(10, 3) = %d  (expect 91)\n", nested(10, 3));
    return 0;
}
