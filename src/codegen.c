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
    CStackSlot slot;
} LocalBinding;

typedef struct {
    FunctionBuilder *b;
    CBlock           entry;
    Func            *f;
    ObjectModule    *module;
    Program         *prog;
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
    return ct.kind == CXType_Record;
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

static int store_bytes(CXType t) {
    CXType ct = canon(t);
    switch (ct.kind) {
        case CXType_Bool:
        case CXType_Char_S: case CXType_Char_U:
        case CXType_SChar:  case CXType_UChar:   return 1;
        case CXType_Short:  case CXType_UShort:  return 2;
        case CXType_Int:    case CXType_UInt:    return 4;
        case CXType_Long:   case CXType_ULong:
        case CXType_LongLong: case CXType_ULongLong:
        case CXType_Pointer:                      return 8;
        default: {
            long s = type_size(t);
            return (int)s;
        }
    }
}

static CType store_clift_type(CXType t) {
    switch (store_bytes(t)) {
        case 1: return I8;
        case 2: return I16;
        case 4: return I32;
        case 8: return I64;
    }
    return I32;
}

static CType compute_clift_type(CXType t) {
    if (store_bytes(t) == 8) return I64;
    return I32;
}

static int is_signed_type(CXType t) {
    CXType ct = canon(t);
    switch (ct.kind) {
        case CXType_Char_S: case CXType_SChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long: case CXType_LongLong:
            return 1;
        default:
            return 0;
    }
}

static CType clift_type(CXType t) {
    return compute_clift_type(t);
}

/* ABI type for a function argument: arrays decay to pointers. */
static CType abi_type_for_arg(CXType t) {
    if (is_array(t)) return I64;
    return clift_type(t);
}

static long aligned_size(CXType t) {
    long s = type_size(t);
    return (s + 7) & ~7L;
}

/* ─────────────  Struct field offset lookup  ───────────── */

typedef struct {
    const char *field_name;
    long        offset_bytes;
} FieldLookup;

static enum CXVisitorResult field_lookup_visitor(CXCursor field_c, CXClientData data) {
    FieldLookup *fl = data;
    CXString nm = clang_getCursorSpelling(field_c);
    if (strcmp(clang_getCString(nm), fl->field_name) == 0) {
        long bits = clang_Cursor_getOffsetOfField(field_c);
        clang_disposeString(nm);
        fl->offset_bytes = bits / 8;
        return CXVisit_Break;
    }
    clang_disposeString(nm);
    return CXVisit_Continue;
}

static long field_offset_bytes(CXType record_type, const char *field_name) {
    FieldLookup fl = { field_name, -1 };
    clang_Type_visitFields(canon(record_type), field_lookup_visitor, &fl);
    return fl.offset_bytes;
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
            case STMT_FOR:
                collect_locals(ctx, s->for_stmt.init);
                collect_locals(ctx, s->for_stmt.body);
                break;
            case STMT_DO: collect_locals(ctx, s->do_stmt.body); break;
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

static CValue narrow_to_store(CgCtx *ctx, CValue v, CXType t) {
    int sz = store_bytes(t);
    if (sz >= 4) return v;
    if (sz == 2) return CL_FunctionBuilder_ireduce(ctx->b, I16, v);
    if (sz == 1) {
        CValue mid = CL_FunctionBuilder_ireduce(ctx->b, I16, v);
        return CL_FunctionBuilder_ireduce(ctx->b, I8, mid);
    }
    return v;
}

static CValue lower_assign(CgCtx *ctx, Expr *e) {
    CValue addr = lower_lvalue(ctx, e->binop.lhs);
    CValue rhs  = lower_expr(ctx, e->binop.rhs);
    rhs = narrow_to_store(ctx, rhs, e->binop.lhs->type);
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
        case OP_ADD: {
            if (is_pointer(e->binop.lhs->type) && !is_pointer(e->binop.rhs->type)) {
                long stride = type_size(pointee(e->binop.lhs->type));
                CValue rhs64 = CL_FunctionBuilder_sextend(ctx->b, I64, rhs);
                CValue s = CL_FunctionBuilder_iconst(ctx->b, I64, stride);
                CValue scaled = CL_FunctionBuilder_imul(ctx->b, rhs64, s);
                return CL_FunctionBuilder_iadd(ctx->b, lhs, scaled);
            }
            if (is_pointer(e->binop.rhs->type) && !is_pointer(e->binop.lhs->type)) {
                long stride = type_size(pointee(e->binop.rhs->type));
                CValue lhs64 = CL_FunctionBuilder_sextend(ctx->b, I64, lhs);
                CValue s = CL_FunctionBuilder_iconst(ctx->b, I64, stride);
                CValue scaled = CL_FunctionBuilder_imul(ctx->b, lhs64, s);
                return CL_FunctionBuilder_iadd(ctx->b, scaled, rhs);
            }
            return CL_FunctionBuilder_iadd(ctx->b, lhs, rhs);
        }
        case OP_SUB: {
            if (is_pointer(e->binop.lhs->type) && !is_pointer(e->binop.rhs->type)) {
                long stride = type_size(pointee(e->binop.lhs->type));
                CValue rhs64 = CL_FunctionBuilder_sextend(ctx->b, I64, rhs);
                CValue s = CL_FunctionBuilder_iconst(ctx->b, I64, stride);
                CValue scaled = CL_FunctionBuilder_imul(ctx->b, rhs64, s);
                return CL_FunctionBuilder_isub(ctx->b, lhs, scaled);
            }
            return CL_FunctionBuilder_isub(ctx->b, lhs, rhs);
        }
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
        CType ct = abi_type_for_arg(arg->type);
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
            CXType base_t = e->index.array->type;
            CXType elem_t;
            CValue base;
            if (is_pointer(base_t)) {
                elem_t = pointee(base_t);
                base = lower_expr(ctx, e->index.array);
            } else {
                elem_t = array_elem(base_t);
                base = lower_lvalue(ctx, e->index.array);
            }
            long stride = type_size(elem_t);
            CValue ix = lower_expr(ctx, e->index.index);
            ix = CL_FunctionBuilder_sextend(ctx->b, I64, ix);
            CValue stride_c = CL_FunctionBuilder_iconst(ctx->b, I64, stride);
            CValue off = CL_FunctionBuilder_imul(ctx->b, ix, stride_c);
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
            long offset = field_offset_bytes(base_t, e->member.field);
            if (offset < 0) {
                CXString tn = clang_getTypeSpelling(canon(base_t));
                fprintf(stderr, "codegen: field '%s' not found in '%s'\n",
                        e->member.field, clang_getCString(tn));
                clang_disposeString(tn);
                exit(1);
            }
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

        case EXPR_STRING_LIT: {
            for (DataItem *d = ctx->prog->data; d; d = d->next) {
                if (strcmp(d->symbol, e->string_lit) == 0) {
                    return CL_ObjectModule_global_value(ctx->module, d->data_id, ctx->b);
                }
            }
            fprintf(stderr, "codegen: unknown string symbol '%s'\n", e->string_lit);
            exit(1);
        }

        case EXPR_VAR:
        case EXPR_DEREF:
        case EXPR_INDEX:
        case EXPR_MEMBER: {
            if (is_array(e->type)) return lower_lvalue(ctx, e);
            if (is_struct(e->type)) return lower_lvalue(ctx, e);

            CValue addr = lower_lvalue(ctx, e);
            int sz = store_bytes(e->type);
            CType st = store_clift_type(e->type);
            CValue loaded = CL_FunctionBuilder_load(ctx->b, st, addr, 0);

            if (sz < 4) {
                CType ct = compute_clift_type(e->type);
                if (is_signed_type(e->type))
                    loaded = CL_FunctionBuilder_sextend(ctx->b, ct, loaded);
                else
                    loaded = CL_FunctionBuilder_uextend(ctx->b, ct, loaded);
            }
            return loaded;
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
                    rhs = narrow_to_store(ctx, rhs, s->decl.type);
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
            case STMT_FOR: {
                if (s->for_stmt.init)
                    lower_stmt(ctx, s->for_stmt.init);

                CBlock head_blk = CL_FunctionBuilder_create_block(ctx->b);
                CBlock post_blk = CL_FunctionBuilder_create_block(ctx->b);
                CBlock body_blk = CL_FunctionBuilder_create_block(ctx->b);
                CBlock exit_blk = CL_FunctionBuilder_create_block(ctx->b);

                if (!ctx->terminated)
                    CL_FunctionBuilder_jump(ctx->b, head_blk, NULL, 0);
                ctx->terminated = 1;

                CL_FunctionBuilder_switch_to_block(ctx->b, head_blk);
                ctx->terminated = 0;
                CValue cond;
                if (s->for_stmt.cond) {
                    cond = lower_expr(ctx, s->for_stmt.cond);
                } else {
                    cond = CL_FunctionBuilder_iconst(ctx->b, I32, 1);
                }
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
                ctx->loop_continue = post_blk;
                lower_stmt(ctx, s->for_stmt.body);
                if (!ctx->terminated)
                    CL_FunctionBuilder_jump(ctx->b, post_blk, NULL, 0);
                ctx->loop_break    = saved_break;
                ctx->loop_continue = saved_cont;

                CL_FunctionBuilder_switch_to_block(ctx->b, post_blk);
                ctx->terminated = 0;
                if (s->for_stmt.post)
                    (void)lower_expr(ctx, s->for_stmt.post);
                CL_FunctionBuilder_jump(ctx->b, head_blk, NULL, 0);

                CL_FunctionBuilder_seal_block(ctx->b, head_blk);

                CL_FunctionBuilder_switch_to_block(ctx->b, exit_blk);
                ctx->terminated = 0;
                break;
            }
            case STMT_DO: {
                CBlock body_blk = CL_FunctionBuilder_create_block(ctx->b);
                CBlock head_blk = CL_FunctionBuilder_create_block(ctx->b);
                CBlock exit_blk = CL_FunctionBuilder_create_block(ctx->b);

                if (!ctx->terminated)
                    CL_FunctionBuilder_jump(ctx->b, body_blk, NULL, 0);
                ctx->terminated = 1;

                CL_FunctionBuilder_switch_to_block(ctx->b, body_blk);
                ctx->terminated = 0;
                CBlock saved_break = ctx->loop_break;
                CBlock saved_cont  = ctx->loop_continue;
                ctx->loop_break    = exit_blk;
                ctx->loop_continue = head_blk;
                lower_stmt(ctx, s->do_stmt.body);
                if (!ctx->terminated)
                    CL_FunctionBuilder_jump(ctx->b, head_blk, NULL, 0);
                ctx->loop_break    = saved_break;
                ctx->loop_continue = saved_cont;

                CL_FunctionBuilder_switch_to_block(ctx->b, head_blk);
                ctx->terminated = 0;
                CValue cond = lower_expr(ctx, s->do_stmt.cond);
                CL_FunctionBuilder_brif(ctx->b, cond,
                    body_blk, NULL, 0,
                    exit_blk, NULL, 0);
                CL_FunctionBuilder_seal_block(ctx->b, body_blk);
                CL_FunctionBuilder_seal_block(ctx->b, exit_blk);
                ctx->terminated = 1;

                CL_FunctionBuilder_switch_to_block(ctx->b, exit_blk);
                ctx->terminated = 0;
                break;
            }
            case STMT_BREAK: {
                if (ctx->loop_break == (CBlock)-1) {
                    fprintf(stderr, "codegen: break outside loop\n");
                    exit(1);
                }
                CL_FunctionBuilder_jump(ctx->b, ctx->loop_break, NULL, 0);
                ctx->terminated = 1;
                return;
            }
            case STMT_CONTINUE: {
                if (ctx->loop_continue == (CBlock)-1) {
                    fprintf(stderr, "codegen: continue outside loop\n");
                    exit(1);
                }
                CL_FunctionBuilder_jump(ctx->b, ctx->loop_continue, NULL, 0);
                ctx->terminated = 1;
                return;
            }
            default:
                fprintf(stderr, "codegen: unsupported stmt kind %d\n", (int)s->kind);
                exit(1);
        }
    }
}

/* ─────────────  Data emission  ───────────── */

static void emit_data(ObjectModule *mod, Program *p) {
    for (DataItem *d = p->data; d; d = d->next) {
        d->data_id = CL_ObjectModule_declare_data(mod, d->symbol, 0);
        CL_ObjectModule_define_data(mod, d->data_id, d->bytes, d->len);
    }
}

/* ─────────────  Function emission  ───────────── */

static void emit_function(ObjectModule *mod, Func *f, Program *prog) {
    if (!f->body) return;

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
    ctx.prog = prog;
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

    emit_data(mod, p);

    for (Func *f = p->funcs; f; f = f->next)
        emit_function(mod, f, p);

    return CL_ObjectModule_finish_and_emit(mod, out_path);
}
