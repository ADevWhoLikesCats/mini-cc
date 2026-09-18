#include <stdio.h>

extern int local_identity(int);
extern int reassign(int);
extern int choose(int, int);
extern int countdown(int);
extern int branch(int);
extern int collatz(int);
extern int call_add_thrice(int, int);

int main(void) {
    printf("local_identity(7)    = %d  (expect 7)\n",   local_identity(7));
    printf("reassign(5)          = %d  (expect 12)\n",  reassign(5));
    printf("choose(3, 5)         = %d  (expect 5)\n",   choose(3, 5));
    printf("choose(9, 5)         = %d  (expect 9)\n",   choose(9, 5));
    printf("countdown(5)         = %d  (expect 15)\n",  countdown(5));
    printf("branch(3)            = %d  (expect 1)\n",   branch(3));
    printf("branch(-3)           = %d  (expect -1)\n",  branch(-3));
    printf("collatz(27)          = %d  (expect 111)\n", collatz(27));
    printf("call_add_thrice(3,4) = %d  (expect 14)\n",  call_add_thrice(3, 4));
    return 0;
}
