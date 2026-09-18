int char_roundtrip(void) {
    char c = 65;
    return c;
}

int char_array_basic(void) {
    char s[6];
    s[0] = 72;
    s[1] = 101;
    s[2] = 108;
    s[3] = 108;
    s[4] = 111;
    s[5] = 0;
    return s[0] + s[4];
}

int short_roundtrip(void) {
    short s = 1000;
    return s;
}

int char_ptr(void) {
    char buf[3];
    char *p = buf;
    *p = 1;
    *(p + 1) = 2;
    *(p + 2) = 3;
    return buf[0] + buf[1] + buf[2];
}

int mixed_arithmetic(void) {
    char c = 10;
    short s = 100;
    int i = 1000;
    return c + s + i;
}
