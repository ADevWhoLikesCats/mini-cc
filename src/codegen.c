#include "codegen.h"
#include "craneliftc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────  Rung 2: arithmetic  ─────────────
 *
 * Supported:
 *   - Functions whose body is a single "return <expr>;"
 *   - <expr> is one of:
 *       - int literal
 *       - parameter reference
 *       - <expr> + <expr>
 *       - <expr> - <expr>
 *       - <expr> * <expr>
 *       - <expr> / <expr>
 *       - <expr> % <expr>
 *   - All params and return value are I32
 *
 * Anything else prints an error and exits.
 */

static CValue lower_expr(FunctionBuilder *b, Expr *e, Func *f, CBlock entry);

static CValue lower_binop(FunctionBuilder *b, Expr *e, Func *f, CBlock entry) {
    CValue lhs = lower_expr(b, e->binop.lhs, f, entry);
    CValue rhs = lower_expr(b, e->binop.rhs, f, entry);
    switch (e->binop.op) {
        case OP_ADD: return CL_FunctionBuilder_iadd(b, lhs, rhs);
        case OP_SUB: return CL_FunctionBuilder_isub(b, lhs, rhs);
        case OP_MUL: return CL_FunctionBuilder_imul(b, lhs, rhs);
        case OP_DIV: return CL_FunctionBuilder_sdiv(b, lhs, rhs);
        case OP_MOD: return CL_FunctionBuilder_srem(b, lhs, rhs);
        default:
            fprintf(stderr, "codegen: unsupported binop at rung 2\n");
            exit(1);
    }
}

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

        case EXPR_BINOP:
            return lower_binop(b, e, f, entry);

        default:
            fprintf(stderr, "codegen: unsupported expression kind %d at rung 2\n",
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

    /* 5. Body must be a single return for rung 2. */
    if (!f->body || f->body->kind != STMT_RETURN || f->body->next != NULL) {
        fprintf(stderr, "codegen: rung 2 only supports a single return in '%s'\n",
                f->name);
        exit(1);
    }

    CValue rv = lower_expr(b, f->body->ret_expr, f, entry);
    CValue retvals[1] = { rv };
    CL_FunctionBuilder_return_(b, retvals, 1);

    /* 6. Finalize — consumes the builder. */
    CL_FunctionBuilder_finalize(b);
    CL_FunctionBuilderContext_dispose(fbctx);

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
