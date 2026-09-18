#include <stdio.h>

extern int neg(int);
extern int nott(int);
extern int eq(int, int);
extern int ne(int, int);
extern int lt(int, int);
extern int le(int, int);
extern int gt(int, int);
extern int ge(int, int);
extern int cmp_chain(int, int);

int main(void) {
    printf("neg(5)          = %d  (expect -5)\n", neg(5));
    printf("neg(-3)         = %d  (expect 3)\n",  neg(-3));
    printf("nott(0)         = %d  (expect 1)\n",  nott(0));
    printf("nott(7)         = %d  (expect 0)\n",  nott(7));
    printf("eq(3,3)         = %d  (expect 1)\n",  eq(3,3));
    printf("eq(3,4)         = %d  (expect 0)\n",  eq(3,4));
    printf("ne(3,4)         = %d  (expect 1)\n",  ne(3,4));
    printf("ne(3,3)         = %d  (expect 0)\n",  ne(3,3));
    printf("lt(3,4)         = %d  (expect 1)\n",  lt(3,4));
    printf("lt(4,3)         = %d  (expect 0)\n",  lt(4,3));
    printf("le(3,3)         = %d  (expect 1)\n",  le(3,3));
    printf("le(4,3)         = %d  (expect 0)\n",  le(4,3));
    printf("gt(4,3)         = %d  (expect 1)\n",  gt(4,3));
    printf("gt(3,4)         = %d  (expect 0)\n",  gt(3,4));
    printf("ge(3,3)         = %d  (expect 1)\n",  ge(3,3));
    printf("ge(3,4)         = %d  (expect 0)\n",  ge(3,4));
    printf("cmp_chain(2,5)  = %d  (expect 1)\n",  cmp_chain(2,5));
    printf("cmp_chain(5,5)  = %d  (expect 1)\n",  cmp_chain(5,5));
    printf("cmp_chain(9,5)  = %d  (expect 0)\n",  cmp_chain(9,5));
    return 0;
}
