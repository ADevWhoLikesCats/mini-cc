int neg(int a) {
    return -a;
}

int nott(int a) {
    return !a;
}

int eq(int a, int b) {
    return a == b;
}

int ne(int a, int b) {
    return a != b;
}

int lt(int a, int b) {
    return a < b;
}

int le(int a, int b) {
    return a <= b;
}

int gt(int a, int b) {
    return a > b;
}

int ge(int a, int b) {
    return a >= b;
}

int cmp_chain(int a, int b) {
    return (a < b) + (a == b);
}
