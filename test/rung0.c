int add(int a, int b) {
    return a + b;
}

int sub(int x, int y) {
    return x - y;
}

int neg(int n) {
    return -n;
}

int choose(int a, int b) {
    if (a > b) {
        return a;
    }
    return b;
}

int countdown(int n) {
    int total = 0;
    while (n > 0) {
        total = total + n;
        n = n - 1;
    }
    return total;
}
