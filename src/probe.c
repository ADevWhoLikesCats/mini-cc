#include <clang-c/Index.h>
#include <stdio.h>

int main(void) {
    CXIndex idx = clang_createIndex(0, 0);
    if (!idx) {
        fprintf(stderr, "clang_createIndex returned NULL\n");
        return 1;
    }
    printf("libclang loaded, index=%p\n", (void *)idx);
    clang_disposeIndex(idx);
    return 0;
}
