int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int fib_iter(int n) {
    if (n < 2) {
        return n;
    }
    int a = 0;
    int b = 1;
    int i = 2;
    while (i <= n) {
        int t = a + b;
        a = b;
        b = t;
        i = i + 1;
    }
    return b;
}
