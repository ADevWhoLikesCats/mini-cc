int local_identity(int a) {
    int x = a;
    return x;
}

int reassign(int a) {
    int x = a;
    x = x + 1;
    x = x * 2;
    return x;
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

int branch(int a) {
    int result = 0;
    if (a > 0) {
        result = 1;
    } else {
        result = -1;
    }
    return result;
}

int collatz(int n) {
    int steps = 0;
    while (n != 1) {
        if (n % 2 == 0) {
            n = n / 2;
        } else {
            n = 3 * n + 1;
        }
        steps = steps + 1;
    }
    return steps;
}

int add(int a, int b) {
    return a + b;
}

int call_add(int a, int b) {
    return add(a, b);
}

int call_add_thrice(int a, int b) {
    return add(add(a, b), add(a, b));
}
