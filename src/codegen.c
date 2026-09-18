#include "codegen.h"
#include "craneliftc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────  Rung 1: identity functions  ─────────────
 *
 * Supported:
 *   - Functions whose body is a single "return <expr>;"
 *   - <expr> is an int literal or a parameter reference
 *   - All params and return value are I32
 *
 * Anything else prints an error and exits.
 */

static CValue lower_expr(FunctionBuilder *b, Expr *e, Func *f, CBlock entry) {
    switch (e->kind) {
        case EXPR_INT_LIT:
            return CL_FunctionBuilder_iconst(b, I32, e->int_lit);

        case EXPR_VAR:
            for (int i = 0; i < f->num_params; i++) {
                if (strcmp(f->params[i].name, e->var_name) == 0)
                    return CL_FunctionBuilder_block_params(b, entry, i);
            }
            fprintf(stderr, "codegen: undefined variable '%s' in '%s'\n",
                    e->var_name, f->name);
            exit(1);

        default:
            fprintf(stderr, "codegen: unsupported expression kind %d at rung 1\n",
                    (int)e->kind);
            exit(1);
    }
}

static void emit_function(ObjectModule *mod, Func *f) {
    /* 1. Signature: all params I32, return I32. */
    Signature *sig = CL_Signature_new(WindowsFastcall);
    for (int i = 0; i < f->num_params; i++)
        CL_Signature_params_push(sig, CL_AbiParam_new(I32));
    CL_Signature_returns_push(sig, CL_AbiParam_new(I32));

    /* 2. Declare in the module. */
    uint32_t fid = CL_ObjectModule_declare_function(mod, f->name, sig);

    /* 3. Build the Function. Consumes sig and ufn. */
    UserFuncName *ufn = CL_UserFuncName_user(0, 0);
    Function *fn = CL_Function_with_name_signature(ufn, sig);

    FunctionBuilderContext *fbctx = CL_FunctionBuilderContext_new();
    FunctionBuilder *b = CL_FunctionBuilder_new(fn, fbctx);

    /* 4. Entry block + block params. */
    CBlock entry = CL_FunctionBuilder_create_block(b);
    CL_FunctionBuilder_append_block_params_for_function_params(b, entry);
    CL_FunctionBuilder_switch_to_block(b, entry);
    CL_FunctionBuilder_seal_block(b, entry);

    /* 5. Body must be a single return for rung 1. */
    if (!f->body || f->body->kind != STMT_RETURN || f->body->next != NULL) {
        fprintf(stderr, "codegen: rung 1 only supports a single return in '%s'\n",
                f->name);
        exit(1);
    }

    CValue rv = lower_expr(b, f->body->ret_expr, f, entry);
    fprintf(stderr, "[debug] function '%s': return value id = %u\n", f->name, rv);
    CValue retvals[1] = { rv };
    CL_FunctionBuilder_return_(b, retvals, 1);

    /* 6. Finalize — consumes the builder. */
    CL_FunctionBuilder_finalize(b);
    CL_FunctionBuilderContext_dispose(fbctx);

    /* Debug: dump the finalized CLIF. */
    char *clif = CL_Function_display(fn);
    fprintf(stderr, "=== CLIF for %s ===\n%s\n", f->name, clif);
    cstr_free(clif);

    /* 7. Define in the module — consumes the Function. */
    CL_ObjectModule_define_function(mod, fid, fn);
}

int codegen_emit(Program *p, const char *out_path) {
    ObjectModule *mod = CL_ObjectModule_new("mini_cc");

    for (Func *f = p->funcs; f; f = f->next)
        emit_function(mod, f);

    /* Consumes the module. */
    return CL_ObjectModule_finish_and_emit(mod, out_path);
}
