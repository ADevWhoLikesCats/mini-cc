#include <stdio.h>

extern int array_basic(void);
extern int array_in_loop(void);
extern int array_via_ptr(void);
extern int ptr_walk(void);

int main(void) {
    printf("array_basic()  = %d  (expect 60)\n", array_basic());
    printf("array_in_loop() = %d  (expect 30)\n", array_in_loop());
    printf("array_via_ptr() = %d  (expect 10)\n", array_via_ptr());
    printf("ptr_walk()     = %d  (expect 26)\n", ptr_walk());
    return 0;
}
