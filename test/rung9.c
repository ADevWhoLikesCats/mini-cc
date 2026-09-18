int array_basic(void) {
    int a[3];
    a[0] = 10;
    a[1] = 20;
    a[2] = 30;
    return a[0] + a[1] + a[2];
}

int array_in_loop(void) {
    int a[5];
    int i = 0;
    while (i < 5) {
        a[i] = i * i;
        i = i + 1;
    }
    int sum = 0;
    i = 0;
    while (i < 5) {
        sum = sum + a[i];
        i = i + 1;
    }
    return sum;
}

int array_via_ptr(void) {
    int a[4];
    a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4;
    int *p = a;
    return p[0] + p[1] + p[2] + p[3];
}

int ptr_walk(void) {
    int a[4];
    a[0] = 5; a[1] = 6; a[2] = 7; a[3] = 8;
    int *p = a;
    int sum = 0;
    int i = 0;
    while (i < 4) {
        sum = sum + *p;
        p = p + 1;
        i = i + 1;
    }
    return sum;
}
