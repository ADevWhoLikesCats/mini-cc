#include <stdio.h>

extern int struct_basic(void);
extern int struct_via_ptr(void);
extern int struct_mixed(void);
extern int struct_three(void);
extern int struct_in_loop(void);

int main(void) {
    printf("struct_basic()    = %d  (expect 7)\n",  struct_basic());
    printf("struct_via_ptr()  = %d  (expect 30)\n", struct_via_ptr());
    printf("struct_mixed()    = %d  (expect 12)\n", struct_mixed());
    printf("struct_three()    = %d  (expect 123)\n", struct_three());
    printf("struct_in_loop()  = %d  (expect 15)\n",  struct_in_loop());
    return 0;
}
