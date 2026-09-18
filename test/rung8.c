int deref_store(void) {
    int x = 0;
    int *p = &x;
    *p = 42;
    return x;
}

int deref_load(void) {
    int x = 99;
    int *p = &x;
    return *p;
}

int ptr_swap(void) {
    int a = 1;
    int b = 2;
    int *p = &a;
    int *q = &b;
    int tmp = *p;
    *p = *q;
    *q = tmp;
    return a * 10 + b;
}

int through_ptr(int *p) {
    return *p + 1;
}

int call_through_ptr(void) {
    int x = 10;
    return through_ptr(&x);
}

int loop_ptr(void) {
    int x = 0;
    int *p = &x;
    int i = 0;
    while (i < 5) {
        *p = *p + i;
        i = i + 1;
    }
    return x;
}
