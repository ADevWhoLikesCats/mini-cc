#include "codegen.h"
#include "craneliftc.h"
#include <clang-c/Index.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int codegen_debug_clif = 0;

#define MAX_LOCALS 64

typedef struct {
    char      *name;
    CXType     type;
    CStackSlot slot;    /* memory home */
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
static CValue lower_lvalue(CgCtx *ctx, Expr *e);
static void   lower_stmt(CgCtx *ctx, Stmt *s);

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    memcpy(r, s, n);
    return r;
}

/* ─────────────  Type helpers  ───────────── */

static CXType canon(CXType t) { return clang_getCanonicalType(t); }

static int is_pointer(CXType t) { return canon(t).kind == CXType_Pointer; }

static int is_array(CXType t) {
    CXType ct = canon(t);
    return ct.kind == CXType_ConstantArray || ct.kind == CXType_IncompleteArray;
}

static int is_struct(CXType t) {
    CXType ct = canon(t);
    return ct.kind == CXType_Record || ct.kind == CXType_Elaborated;
}

static CXType pointee(CXType t) { return clang_getPointeeType(t); }

static CXType array_elem(CXType t) { return clang_getArrayElementType(t); }

static long type_size(CXType t) {
    CXType ct = canon(t);
    long s = clang_Type_getSizeOf(ct);
    if (s < 0) {
        fprintf(stderr, "codegen: sizeof failed for type kind %d\n", (int)ct.kind);
        exit(1);
    }
    return s;
}

static CType clift_type(CXType t) {
    CXType ct = canon(t);
    switch (ct.kind) {
        case CXType_Bool:
        case CXType_Char_S: case CXType_Char_U:
        case CXType_SChar:  case CXType_UChar:   return I8;
        case CXType_Short:  case CXType_UShort:  return I16;
        case CXType_Int:    case CXType_UInt:    return I32;
        case CXType_Long:   case CXType_ULong:
        case CXType_LongLong: case CXType_ULongLong:
        case CXType_Pointer:
        case CXType_ConstantArray:
        case CXType_IncompleteArray:             return I64;
        default:
            fprintf(stderr, "codegen: unsupported type kind %d (%s)\n",
                    (int)ct.kind, clang_getCString(clang_getTypeSpelling(ct)));
            exit(1);
    }
}

static long aligned_size(CXType t) {
    long s = type_size(t);
    /* round up to 8 for stack slot storage */
    return (s + 7) & ~7L;
}

/* ─────────────  Locals  ───────────── */

static LocalBinding *local_lookup(CgCtx *ctx, const char *name) {
    for (int i = 0; i < ctx->nlocals; i++)
        if (strcmp(ctx->locals[i].name, name) == 0)
            return &ctx->locals[i];
    return NULL;
}

static void local_add(CgCtx *ctx, const char *name, CXType type, CStackSlot slot) {
    if (ctx->nlocals >= MAX_LOCALS) {
        fprintf(stderr, "codegen: too many locals\n");
        exit(1);
    }
    ctx->locals[ctx->nlocals].name = dup_str(name);
    ctx->locals[ctx->nlocals].type = type;
    ctx->locals[ctx->nlocals].slot = slot;
    ctx->nlocals++;
}

static CStackSlot alloc_slot(CgCtx *ctx, CXType type) {
    long sz = aligned_size(type);
    if (sz < 8) sz = 8;
    return CL_FunctionBuilder_create_sized_stack_slot(ctx->b, (uint32_t)sz);
}

static void collect_locals(CgCtx *ctx, Stmt *s) {
    for (int i = 0; i < ctx->f->num_params; i++) {
        CXType pt = ctx->f->params[i].type;
        CStackSlot slot = alloc_slot(ctx, pt);
        local_add(ctx, ctx->f->params[i].name, pt, slot);
    }

    for (; s; s = s->next) {
        switch (s->kind) {
            case STMT_DECL: {
                if (local_lookup(ctx, s->decl.name)) break;
                CStackSlot slot = alloc_slot(ctx, s->decl.type);
                local_add(ctx, s->decl.name, s->decl.type, slot);
                break;
            }
            case STMT_BLOCK: collect_locals(ctx, s->block); break;
            case STMT_IF:
                collect_locals(ctx, s->if_stmt.then_body);
                collect_locals(ctx, s->if_stmt.else_body);
                break;
            case STMT_WHILE: collect_locals(ctx, s->while_stmt.body); break;
            default: break;
        }
    }
}

/* ─────────────  Binops  ───────────── */

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
    if (e->binop.op != OP_ASSIGN) return (CValue)-1;
    CValue addr = lower_lvalue(ctx, e->binop.lhs);
    CValue rhs  = lower_expr(ctx, e->binop.rhs);
    CL_FunctionBuilder_store(ctx->b, addr, rhs, 0);
    return rhs;
}

static CValue lower_binop(CgCtx *ctx, Expr *e) {
    if (e->binop.op == OP_ASSIGN) return lower_assign(ctx, e);

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
        case UNOP_NEG: return CL_FunctionBuilder_ineg(ctx->b, v);
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

    Signature *sig = CL_Signature_new(WindowsFastcall);

    for (int i = 0; i < nargs; i++) {
        Expr *arg = e->call.args[i];
        args[i] = lower_expr(ctx, arg);
        /* Param type: for now assume I32 unless pointer-like */
        CType ct = clift_type(arg->type);
        CL_Signature_params_push(sig, CL_AbiParam_new(ct));
    }
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

/* ─────────────  Lvalues and addresses  ───────────── */

static CValue lower_lvalue(CgCtx *ctx, Expr *e) {
    switch (e->kind) {
        case EXPR_VAR: {
            LocalBinding *lb = local_lookup(ctx, e->var_name);
            if (!lb) {
                fprintf(stderr, "codegen: undefined variable '%s'\n", e->var_name);
                exit(1);
            }
            return CL_FunctionBuilder_stack_addr(ctx->b, I64, lb->slot, 0);
        }
        case EXPR_DEREF: {
            return lower_expr(ctx, e->operand);
        }
        case EXPR_INDEX: {
            CXType arr_t = e->index.array->type;
            CXType elem_t = array_elem(arr_t);
            long stride = type_size(elem_t);
            CValue base = lower_lvalue(ctx, e->index.array);
            CValue ix   = lower_expr(ctx, e->index.index);
            CValue stride_c = CL_FunctionBuilder_iconst(ctx->b, I64, stride);
            CValue off  = CL_FunctionBuilder_imul(ctx->b, ix, stride_c);
            return CL_FunctionBuilder_iadd(ctx->b, base, off);
        }
        case EXPR_MEMBER: {
            Expr *base = e->member.base;
            CValue base_addr;
            CXType base_t;
            if (e->member.is_arrow) {
                base_addr = lower_expr(ctx, base);
                base_t = pointee(base->type);
            } else {
                base_addr = lower_lvalue(ctx, base);
                base_t = base->type;
            }
            long offset = clang_Type_getOffsetOf(canon(base_t), e->member.field) / 8;
            if (offset < 0) offset = 0;
            CValue off = CL_FunctionBuilder_iconst(ctx->b, I64, offset);
            return CL_FunctionBuilder_iadd(ctx->b, base_addr, off);
        }
        default:
            fprintf(stderr, "codegen: expression is not an lvalue (kind %d)\n", (int)e->kind);
            exit(1);
    }
}

/* ─────────────  Expressions  ───────────── */

static CValue lower_expr(CgCtx *ctx, Expr *e) {
    switch (e->kind) {
        case EXPR_INT_LIT:
            return CL_FunctionBuilder_iconst(ctx->b, I32, e->int_lit);

        case EXPR_VAR:
        case EXPR_DEREF:
        case EXPR_INDEX:
        case EXPR_MEMBER: {
            CValue addr = lower_lvalue(ctx, e);
            CType ct = clift_type(e->type);
            return CL_FunctionBuilder_load(ctx->b, ct, addr, 0);
        }

        case EXPR_ADDR_OF:
            return lower_lvalue(ctx, e->operand);

        case EXPR_BINOP: return lower_binop(ctx, e);
        case EXPR_UNOP:  return lower_unop(ctx, e);
        case EXPR_CALL:  return lower_call(ctx, e);

        default:
            fprintf(stderr, "codegen: unsupported expr kind %d\n", (int)e->kind);
            exit(1);
    }
}

/* ─────────────  Statements  ───────────── */

static void lower_stmt(CgCtx *ctx, Stmt *s) {
    for (; s; s = s->next) {
        if (ctx->terminated) return;

        switch (s->kind) {
            case STMT_DECL: {
                if (s->decl.init) {
                    LocalBinding *lb = local_lookup(ctx, s->decl.name);
                    if (!lb) {
                        fprintf(stderr, "codegen: decl without slot for '%s'\n", s->decl.name);
                        exit(1);
                    }
                    CValue rhs = lower_expr(ctx, s->decl.init);
                    CValue addr = CL_FunctionBuilder_stack_addr(ctx->b, I64, lb->slot, 0);
                    CL_FunctionBuilder_store(ctx->b, addr, rhs, 0);
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

/* ─────────────  Function emission  ───────────── */

static void emit_function(ObjectModule *mod, Func *f) {
    Signature *sig = CL_Signature_new(WindowsFastcall);
    for (int i = 0; i < f->num_params; i++)
        CL_Signature_params_push(sig, CL_AbiParam_new(clift_type(f->params[i].type)));
    CL_Signature_returns_push(sig, CL_AbiParam_new(clift_type(f->return_type)));

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
        LocalBinding *lb = local_lookup(&ctx, f->params[i].name);
        CValue pv = CL_FunctionBuilder_block_params(b, entry, i);
        CValue addr = CL_FunctionBuilder_stack_addr(b, I64, lb->slot, 0);
        CL_FunctionBuilder_store(b, addr, pv, 0);
    }

    CL_FunctionBuilder_seal_block(b, entry);
    lower_stmt(&ctx, f->body);

    if (!ctx.terminated) {
        CValue zero = CL_FunctionBuilder_iconst(b, I32, 0);
        CValue retvals[1] = { zero };
        CL_FunctionBuilder_return_(b, retvals, 1);
    }

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
