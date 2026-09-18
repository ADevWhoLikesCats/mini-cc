#include "codegen.h"
#include "craneliftc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*** debug cranelift ir flag ***/
int codegen_debug_clif = 0;

#define MAX_LOCALS 64

typedef struct {
    char     *name;
    CVariable slot;
} LocalBinding;

typedef struct {
    FunctionBuilder *b;
    CBlock           entry;
    Func            *f;
    ObjectModule    *module;
    LocalBinding     locals[MAX_LOCALS];
    int              nlocals;
    CBlock           loop_break;
    CBlock           loop_continue;
    int              terminated;
} CgCtx;

static CValue lower_expr(CgCtx *ctx, Expr *e);
static void   lower_stmt(CgCtx *ctx, Stmt *s);

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    memcpy(r, s, n);
    return r;
}

static CVariable local_lookup(CgCtx *ctx, const char *name) {
    for (int i = 0; i < ctx->nlocals; i++)
        if (strcmp(ctx->locals[i].name, name) == 0)
            return ctx->locals[i].slot;
    return (CVariable)-1;
}

static void local_add(CgCtx *ctx, const char *name, CVariable slot) {
    if (ctx->nlocals >= MAX_LOCALS) {
        fprintf(stderr, "codegen: too many locals\n");
        exit(1);
    }
    ctx->locals[ctx->nlocals].name = dup_str(name);
    ctx->locals[ctx->nlocals].slot = slot;
    ctx->nlocals++;
}

static void collect_locals(CgCtx *ctx, Stmt *s) {
    for (int i = 0; i < ctx->f->num_params; i++) {
        CVariable slot = CL_Variable_from_u32((uint32_t)ctx->nlocals);
        CL_FunctionBuilder_declare_var(ctx->b, slot, I32);
        local_add(ctx, ctx->f->params[i].name, slot);
    }

    for (; s; s = s->next) {
        switch (s->kind) {
            case STMT_DECL: {
                if ((int32_t)local_lookup(ctx, s->decl.name) != -1) break;
                CVariable slot = CL_Variable_from_u32((uint32_t)ctx->nlocals);
                CL_FunctionBuilder_declare_var(ctx->b, slot, I32);
                local_add(ctx, s->decl.name, slot);
                break;
            }
            case STMT_BLOCK:
                collect_locals(ctx, s->block);
                break;
            case STMT_IF:
                collect_locals(ctx, s->if_stmt.then_body);
                collect_locals(ctx, s->if_stmt.else_body);
                break;
            case STMT_WHILE:
                collect_locals(ctx, s->while_stmt.body);
                break;
            default:
                break;
        }
    }
}

static int cmp_cc_for(BinOpKind op) {
    switch (op) {
        case OP_EQ: return Equal;
        case OP_NE: return NotEqual;
        case OP_LT: return SignedLessThan;
        case OP_LE: return SignedLessThanOrEqual;
        case OP_GT: return SignedGreaterThan;
        case OP_GE: return SignedGreaterThanOrEqual;
        default:    return -1;
    }
}

static CValue lower_assign(CgCtx *ctx, Expr *e) {
    if (e->binop.lhs->kind != EXPR_VAR) {
        fprintf(stderr, "codegen: assignment to non-variable lvalue\n");
        exit(1);
    }
    const char *name = e->binop.lhs->var_name;
    CVariable slot = local_lookup(ctx, name);
    if ((int32_t)slot == -1) {
        fprintf(stderr, "codegen: assignment to undeclared variable '%s'\n", name);
        exit(1);
    }
    CValue rhs = lower_expr(ctx, e->binop.rhs);
    CL_FunctionBuilder_def_var(ctx->b, slot, rhs);
    return rhs;
}

static CValue lower_binop(CgCtx *ctx, Expr *e) {
    if (e->binop.op == OP_ASSIGN)
        return lower_assign(ctx, e);

    CValue lhs = lower_expr(ctx, e->binop.lhs);
    CValue rhs = lower_expr(ctx, e->binop.rhs);

    int cc = cmp_cc_for(e->binop.op);
    if (cc >= 0) {
        CValue cmp = CL_FunctionBuilder_icmp(ctx->b, (CIntCC)cc, lhs, rhs);
        return CL_FunctionBuilder_uextend(ctx->b, I32, cmp);
    }

    switch (e->binop.op) {
        case OP_ADD: return CL_FunctionBuilder_iadd(ctx->b, lhs, rhs);
        case OP_SUB: return CL_FunctionBuilder_isub(ctx->b, lhs, rhs);
        case OP_MUL: return CL_FunctionBuilder_imul(ctx->b, lhs, rhs);
        case OP_DIV: return CL_FunctionBuilder_sdiv(ctx->b, lhs, rhs);
        case OP_MOD: return CL_FunctionBuilder_srem(ctx->b, lhs, rhs);
        default:
            fprintf(stderr, "codegen: unsupported binop op=%d\n", (int)e->binop.op);
            exit(1);
    }
}

static CValue lower_unop(CgCtx *ctx, Expr *e) {
    CValue v = lower_expr(ctx, e->unop.operand);
    switch (e->unop.op) {
        case UNOP_NEG:
            return CL_FunctionBuilder_ineg(ctx->b, v);
        case UNOP_NOT: {
            CValue zero = CL_FunctionBuilder_iconst(ctx->b, I32, 0);
            CValue cmp  = CL_FunctionBuilder_icmp(ctx->b, Equal, v, zero);
            return CL_FunctionBuilder_uextend(ctx->b, I32, cmp);
        }
        default:
            fprintf(stderr, "codegen: unsupported unop\n");
            exit(1);
    }
}

static CValue lower_call(CgCtx *ctx, Expr *e) {
    int nargs = e->call.nargs;
    CValue *args = NULL;
    if (nargs > 0) args = calloc(nargs, sizeof(CValue));
    for (int i = 0; i < nargs; i++)
        args[i] = lower_expr(ctx, e->call.args[i]);

    Signature *sig = CL_Signature_new(WindowsFastcall);
    for (int i = 0; i < nargs; i++)
        CL_Signature_params_push(sig, CL_AbiParam_new(I32));
    CL_Signature_returns_push(sig, CL_AbiParam_new(I32));

    uint32_t fid = CL_ObjectModule_declare_function(ctx->module, e->call.name, sig);
    CFuncRef fref = CL_ObjectModule_declare_func_in_func(ctx->module, fid, ctx->b);
    CInst call_inst = CL_FunctionBuilder_call(ctx->b, fref, args, nargs);
    free(args);

    CValue result;
    size_t n = CL_FunctionBuilder_inst_results(ctx->b, call_inst, &result, 1);
    if (n != 1) {
        fprintf(stderr, "codegen: call to '%s' returned %zu values\n", e->call.name, n);
        exit(1);
    }
    return result;
}

static CValue lower_expr(CgCtx *ctx, Expr *e) {
    switch (e->kind) {
        case EXPR_INT_LIT:
            return CL_FunctionBuilder_iconst(ctx->b, I32, e->int_lit);

        case EXPR_VAR: {
            CVariable slot = local_lookup(ctx, e->var_name);
            if ((int32_t)slot != -1)
                return CL_FunctionBuilder_use_var(ctx->b, slot);
            fprintf(stderr, "codegen: undefined variable '%s'\n", e->var_name);
            exit(1);
        }

        case EXPR_BINOP: return lower_binop(ctx, e);
        case EXPR_UNOP:  return lower_unop(ctx, e);
        case EXPR_CALL:  return lower_call(ctx, e);

        default:
            fprintf(stderr, "codegen: unsupported expr kind %d\n", (int)e->kind);
            exit(1);
    }
}

static void lower_stmt(CgCtx *ctx, Stmt *s) {
    for (; s; s = s->next) {
        if (ctx->terminated) return;

        switch (s->kind) {
            case STMT_DECL: {
                CVariable slot = local_lookup(ctx, s->decl.name);
                if (s->decl.init) {
                    CValue v = lower_expr(ctx, s->decl.init);
                    CL_FunctionBuilder_def_var(ctx->b, slot, v);
                }
                break;
            }
            case STMT_EXPR: {
                (void)lower_expr(ctx, s->expr);
                break;
            }
            case STMT_RETURN: {
                CValue rv = lower_expr(ctx, s->ret_expr);
                CValue retvals[1] = { rv };
                CL_FunctionBuilder_return_(ctx->b, retvals, 1);
                ctx->terminated = 1;
                return;
            }
            case STMT_BLOCK: {
                lower_stmt(ctx, s->block);
                break;
            }
            case STMT_IF: {
                CValue cond = lower_expr(ctx, s->if_stmt.cond);

                CBlock then_blk  = CL_FunctionBuilder_create_block(ctx->b);
                CBlock else_blk  = CL_FunctionBuilder_create_block(ctx->b);
                CBlock merge_blk = CL_FunctionBuilder_create_block(ctx->b);

                int has_else = s->if_stmt.else_body != NULL;
                CBlock else_target = has_else ? else_blk : merge_blk;

                CL_FunctionBuilder_brif(ctx->b, cond,
                    then_blk, NULL, 0,
                    else_target, NULL, 0);

                /* Now that brif has added preds, seal them. */
                CL_FunctionBuilder_seal_block(ctx->b, then_blk);
                if (has_else) CL_FunctionBuilder_seal_block(ctx->b, else_blk);
                CL_FunctionBuilder_seal_block(ctx->b, merge_blk);
                ctx->terminated = 1;

                CL_FunctionBuilder_switch_to_block(ctx->b, then_blk);
                ctx->terminated = 0;
                lower_stmt(ctx, s->if_stmt.then_body);
                int then_fell = !ctx->terminated;
                if (then_fell) CL_FunctionBuilder_jump(ctx->b, merge_blk, NULL, 0);

                int else_fell = 0;
                if (has_else) {
                    CL_FunctionBuilder_switch_to_block(ctx->b, else_blk);
                    ctx->terminated = 0;
                    lower_stmt(ctx, s->if_stmt.else_body);
                    else_fell = !ctx->terminated;
                    if (else_fell) CL_FunctionBuilder_jump(ctx->b, merge_blk, NULL, 0);
                } else {
                    else_fell = 1;
                }

                if (then_fell || else_fell) {
                    CL_FunctionBuilder_switch_to_block(ctx->b, merge_blk);
                    ctx->terminated = 0;
                } else {
                    ctx->terminated = 1;
                }
                break;
            }
            case STMT_WHILE: {
                CBlock head_blk = CL_FunctionBuilder_create_block(ctx->b);
                CBlock body_blk = CL_FunctionBuilder_create_block(ctx->b);
                CBlock exit_blk = CL_FunctionBuilder_create_block(ctx->b);

                if (!ctx->terminated)
                    CL_FunctionBuilder_jump(ctx->b, head_blk, NULL, 0);
                ctx->terminated = 1;

                CL_FunctionBuilder_switch_to_block(ctx->b, head_blk);
                ctx->terminated = 0;
                CValue cond = lower_expr(ctx, s->while_stmt.cond);
                CL_FunctionBuilder_brif(ctx->b, cond,
                    body_blk, NULL, 0,
                    exit_blk, NULL, 0);

                CL_FunctionBuilder_seal_block(ctx->b, body_blk);
                CL_FunctionBuilder_seal_block(ctx->b, exit_blk);
                ctx->terminated = 1;

                CL_FunctionBuilder_switch_to_block(ctx->b, body_blk);
                ctx->terminated = 0;
                CBlock saved_break = ctx->loop_break;
                CBlock saved_cont  = ctx->loop_continue;
                ctx->loop_break    = exit_blk;
                ctx->loop_continue = head_blk;
                lower_stmt(ctx, s->while_stmt.body);
                if (!ctx->terminated)
                    CL_FunctionBuilder_jump(ctx->b, head_blk, NULL, 0);
                ctx->loop_break    = saved_break;
                ctx->loop_continue = saved_cont;

                CL_FunctionBuilder_seal_block(ctx->b, head_blk);

                CL_FunctionBuilder_switch_to_block(ctx->b, exit_blk);
                ctx->terminated = 0;
                break;
            }
            default:
                fprintf(stderr, "codegen: unsupported stmt kind %d\n", (int)s->kind);
                exit(1);
        }
    }
}

static void emit_function(ObjectModule *mod, Func *f) {
    Signature *sig = CL_Signature_new(WindowsFastcall);
    for (int i = 0; i < f->num_params; i++)
        CL_Signature_params_push(sig, CL_AbiParam_new(I32));
    CL_Signature_returns_push(sig, CL_AbiParam_new(I32));

    uint32_t fid = CL_ObjectModule_declare_function(mod, f->name, sig);

    UserFuncName *ufn = CL_UserFuncName_user(0, 0);
    Function *fn = CL_Function_with_name_signature(ufn, sig);

    FunctionBuilderContext *fbctx = CL_FunctionBuilderContext_new();
    FunctionBuilder *b = CL_FunctionBuilder_new(fn, fbctx);

    CBlock entry = CL_FunctionBuilder_create_block(b);
    CL_FunctionBuilder_append_block_params_for_function_params(b, entry);
    CL_FunctionBuilder_switch_to_block(b, entry);

    CgCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.b = b;
    ctx.entry = entry;
    ctx.f = f;
    ctx.module = mod;
    ctx.loop_break = (CBlock)-1;
    ctx.loop_continue = (CBlock)-1;
    ctx.terminated = 0;

    collect_locals(&ctx, f->body);

    for (int i = 0; i < f->num_params; i++) {
        CValue param_val = CL_FunctionBuilder_block_params(b, entry, i);
        CVariable slot = local_lookup(&ctx, f->params[i].name);
        CL_FunctionBuilder_def_var(b, slot, param_val);
    }

    CL_FunctionBuilder_seal_block(b, entry);
    lower_stmt(&ctx, f->body);

    if (!ctx.terminated) {
        CValue zero = CL_FunctionBuilder_iconst(b, I32, 0);
        CValue retvals[1] = { zero };
        CL_FunctionBuilder_return_(b, retvals, 1);
    }

    /* Debug: dump CLIF before defining if debug flag was passed.. */
    if (codegen_debug_clif) {
        char *clif = CL_Function_display(fn);
        fprintf(stderr, "=== CLIF for %s ===\n%s\n", f->name, clif);
        cstr_free(clif);
    }

    CL_FunctionBuilder_finalize(b);
    CL_FunctionBuilderContext_dispose(fbctx);

    CL_ObjectModule_define_function(mod, fid, fn);
}

int codegen_emit(Program *p, const char *out_path) {
    ObjectModule *mod = CL_ObjectModule_new("mini_cc");
    for (Func *f = p->funcs; f; f = f->next)
        emit_function(mod, f);
    return CL_ObjectModule_finish_and_emit(mod, out_path);
}
