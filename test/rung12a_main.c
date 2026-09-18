#include <stdio.h>

extern int char_roundtrip(void);
extern int char_array_basic(void);
extern int short_roundtrip(void);
extern int char_ptr(void);
extern int mixed_arithmetic(void);

int main(void) {
    printf("char_roundtrip()   = %d  (expect 65)\n",  char_roundtrip());
    printf("char_array_basic() = %d  (expect 183)\n", char_array_basic());
    printf("short_roundtrip()  = %d  (expect 1000)\n", short_roundtrip());
    printf("char_ptr()         = %d  (expect 6)\n",   char_ptr());
    printf("mixed_arithmetic() = %d  (expect 1110)\n", mixed_arithmetic());
    return 0;
}
