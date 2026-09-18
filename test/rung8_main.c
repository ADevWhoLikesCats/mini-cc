#include <stdio.h>

extern int deref_store(void);
extern int deref_load(void);
extern int ptr_swap(void);
extern int call_through_ptr(void);
extern int loop_ptr(void);

int main(void) {
    printf("deref_store()      = %d  (expect 42)\n", deref_store());
    printf("deref_load()       = %d  (expect 99)\n", deref_load());
    printf("ptr_swap()         = %d  (expect 21)\n", ptr_swap());
    printf("call_through_ptr() = %d  (expect 11)\n", call_through_ptr());
    printf("loop_ptr()         = %d  (expect 10)\n", loop_ptr());
    return 0;
}
