#include <stdio.h>

extern int for_sum(int);
extern int for_with_break(int);
extern int for_with_continue(int);
extern int do_while_countdown(int);
extern int nested_loops(void);

int main(void) {
    printf("for_sum(5)             = %d  (expect 10)\n", for_sum(5));
    printf("for_with_break(10)     = %d  (expect 10)\n", for_with_break(10));
    printf("for_with_continue(10)  = %d  (expect 25)\n", for_with_continue(10));
    printf("do_while_countdown(5)  = %d  (expect 5)\n",  do_while_countdown(5));
    printf("nested_loops()         = %d  (expect 6)\n",  nested_loops());
    return 0;
}
