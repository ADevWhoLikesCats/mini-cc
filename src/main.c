#include "ast.h"
#include "clang_bridge.h"
#include "sema.h"
#include "codegen.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#endif

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [options] <source.c>\n"
        "  --ast        print AST and exit\n"
        "  --emit-obj   emit object file (stub in rung 0)\n",
        argv0);
}

int main(int argc, char **argv) {
    const char *src = NULL;
    int print_ast = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ast") == 0) print_ast = 1;
        else if (strcmp(argv[i], "--emit-obj") == 0) { /* default */ }
        else if (argv[i][0] == '-') { usage(argv[0]); return 1; }
        else src = argv[i];
    }

    if (!src) { usage(argv[0]); return 1; }

#ifdef _WIN32
    char abs_path[4096];
    if (_fullpath(abs_path, src, sizeof(abs_path))) {
        src = abs_path;
    }
#endif

    Program *prog = clang_bridge_parse(src);
    if (!prog) return 1;

    if (sema_check(prog)) { program_free(prog); return 2; }

    if (print_ast) {
        program_print(prog);
        program_free(prog);
        return 0;
    }

    int rc = codegen_emit(prog, "out.o");
    program_free(prog);
    return rc;
}
