#include "ast.h"
#include "clang_bridge.h"
#include "sema.h"
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#endif

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [options] <source.c> [extra.o ...]\n"
        "  -o <file>       output path (default: out.o or out.exe)\n"
        "  --link          invoke bin/ld.exe after codegen to produce an executable\n"
        "  --run           --link, then run the produced executable\n"
        "  --ast           print AST and exit\n"
        "  --debug-clif    dump Cranelift IR to stderr during codegen\n"
        "  --emit-obj      emit object file only (default without -o/--link)\n",
        argv0);
}

/* Find the directory containing ld.exe by searching common relative
 * locations. This is more reliable than GetModuleFileNameA on MSYS2. */
static void exe_dir(char *out, size_t cap) {
    const char *candidates[] = {
        "bin",
        "./bin",
        "../bin",
        "../../bin",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        char test[4096];
        snprintf(test, sizeof(test), "%s/ld.exe", candidates[i]);
        FILE *f = fopen(test, "rb");
        if (f) {
            fclose(f);
            snprintf(out, cap, "%s", candidates[i]);
            return;
        }
    }
    snprintf(out, cap, ".");
}

static int run_linker(char **extra_objs, int nextra,
                      const char *obj_path, const char *output) {
    char dir[4096];
    exe_dir(dir, sizeof(dir));

    char cmd[16384];
    int off = 0;
    off += snprintf(cmd + off, sizeof(cmd) - off,
        "\"%s/ld.exe\" -m i386pep -Bdynamic -o \"%s\" "
        "\"%s/crt2.o\" \"%s/crtbegin.o\"",
        dir, output, dir, dir);

    for (int i = 0; i < nextra; i++)
        off += snprintf(cmd + off, sizeof(cmd) - off, " \"%s\"", extra_objs[i]);
    off += snprintf(cmd + off, sizeof(cmd) - off, " \"%s\"", obj_path);

    off += snprintf(cmd + off, sizeof(cmd) - off,
        " -L\"%s/lib\""
        " -lmingw32 -lgcc -lgcc_eh -lmoldname -lmingwex -lmsvcrt"
        " -ladvapi32 -lshell32 -luser32 -lkernel32"
        " \"%s/crtend.o\"",
        dir, dir);

    fprintf(stderr, "[mini-cc] %s\n", cmd);
    return system(cmd);
}

int main(int argc, char **argv) {
    const char *src = NULL;
    const char *output = NULL;
    int print_ast = 0;
    int do_link = 0;
    int do_run = 0;

    char *extra_objs[64];
    int nextra = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ast") == 0) print_ast = 1;
        else if (strcmp(argv[i], "--debug-clif") == 0) codegen_debug_clif = 1;
        else if (strcmp(argv[i], "--emit-obj") == 0) { /* default */ }
        else if (strcmp(argv[i], "--link") == 0) do_link = 1;
        else if (strcmp(argv[i], "--run") == 0) { do_link = 1; do_run = 1; }
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        }
        else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            usage(argv[0]); return 1;
        }
        else if (strlen(argv[i]) > 2 && strcmp(argv[i] + strlen(argv[i]) - 2, ".o") == 0) {
            if (nextra < 64) extra_objs[nextra++] = argv[i];
        }
        else {
            if (src) {
                fprintf(stderr, "mini-cc: multiple source files not supported yet\n");
                return 1;
            }
            src = argv[i];
        }
    }

    if (!src) { usage(argv[0]); return 1; }

    if (output) {
        size_t n = strlen(output);
        if (n > 4 && strcmp(output + n - 4, ".exe") == 0) do_link = 1;
        if (n > 2 && strcmp(output + n - 2, ".o") == 0) do_link = 0;
    }

    char tmp_obj[4096];
    const char *obj_path;
    if (do_link) {
        if (output) {
            snprintf(tmp_obj, sizeof(tmp_obj), "%s", output);
            char *dot = strrchr(tmp_obj, '.');
            if (dot && dot != tmp_obj) *dot = '\0';
            size_t used = strlen(tmp_obj);
            if (used + 2 < sizeof(tmp_obj)) {
                tmp_obj[used]     = '.';
                tmp_obj[used + 1] = 'o';
                tmp_obj[used + 2] = '\0';
            }
        } else {
            snprintf(tmp_obj, sizeof(tmp_obj), "out.o");
        }
        obj_path = tmp_obj;
    } else if (output) {
        obj_path = output;
    } else {
        obj_path = "out.o";
    }

#ifdef _WIN32
    char abs_src[4096];
    if (_fullpath(abs_src, src, sizeof(abs_src))) src = abs_src;
#endif

    Program *prog = clang_bridge_parse(src);
    if (!prog) return 1;

    if (sema_check(prog)) { program_free(prog); return 2; }

    if (print_ast) {
        program_print(prog);
        program_free(prog);
        return 0;
    }

    int rc = codegen_emit(prog, obj_path);
    program_free(prog);
    if (rc != 0) return rc;

    if (do_link) {
        const char *final_output = output ? output : "out.exe";
        int lrc = run_linker(extra_objs, nextra, obj_path, final_output);
        if (lrc != 0) return 3;

        if (do_run) {
            char runcmd[8192];
            snprintf(runcmd, sizeof(runcmd), "\"%s\"", final_output);
            fprintf(stderr, "[mini-cc] %s\n", runcmd);
            return system(runcmd);
        }
    }

    return 0;
}
