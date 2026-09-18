struct Point {
    int x;
    int y;
};

struct Pair {
    int a;
    int b;
    int c;
};

int struct_basic(void) {
    struct Point p;
    p.x = 3;
    p.y = 4;
    return p.x + p.y;
}

int struct_via_ptr(void) {
    struct Point p;
    struct Point *q = &p;
    q->x = 10;
    q->y = 20;
    return q->x + q->y;
}

int struct_mixed(void) {
    struct Point p;
    struct Point *q = &p;
    p.x = 5;
    q->y = 7;
    return p.x + p.y;
}

int struct_three(void) {
    struct Pair pr;
    pr.a = 1;
    pr.b = 2;
    pr.c = 3;
    return pr.a * 100 + pr.b * 10 + pr.c;
}

int struct_in_loop(void) {
    struct Point p;
    p.x = 0;
    p.y = 1;
    int i = 0;
    while (i < 5) {
        p.x = p.x + p.y;
        p.y = p.y + 1;
        i = i + 1;
    }
    return p.x;
}
