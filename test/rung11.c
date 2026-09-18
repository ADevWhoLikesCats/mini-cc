int for_sum(int n) {
    int sum = 0;
    for (int i = 0; i < n; i = i + 1) {
        sum = sum + i;
    }
    return sum;
}

int for_with_break(int n) {
    int sum = 0;
    for (int i = 0; i < n; i = i + 1) {
        if (i == 5) break;
        sum = sum + i;
    }
    return sum;
}

int for_with_continue(int n) {
    int sum = 0;
    for (int i = 0; i < n; i = i + 1) {
        if (i % 2 == 0) continue;
        sum = sum + i;
    }
    return sum;
}

int do_while_countdown(int n) {
    int count = 0;
    do {
        n = n - 1;
        count = count + 1;
    } while (n > 0);
    return count;
}

int nested_loops(void) {
    int total = 0;
    for (int i = 0; i < 3; i = i + 1) {
        for (int j = 0; j < 3; j = j + 1) {
            if (i == j) continue;
            total = total + 1;
        }
    }
    return total;
}
