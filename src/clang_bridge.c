#include "clang_bridge.h"
#include <clang-c/Index.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum CXCursorKind CXCursorKind;

/* Per-translation-unit context. */
typedef struct {
    Program *prog;
} BridgeCtx;

static BridgeCtx g_ctx;

static char *cxstr_to_cstr(CXString s) {
    const char *c = clang_getCString(s);
    size_t n = strlen(c ? c : "") + 1;
    char *r = malloc(n);
    memcpy(r, c ? c : "", n);
    clang_disposeString(s);
    return r;
}

static int map_binop(enum CXBinaryOperatorKind k, BinOpKind *out) {
    switch (k) {
        case CXBinaryOperator_Add: *out = OP_ADD; return 1;
        case CXBinaryOperator_Sub: *out = OP_SUB; return 1;
        case CXBinaryOperator_Mul: *out = OP_MUL; return 1;
        case CXBinaryOperator_Div: *out = OP_DIV; return 1;
        case CXBinaryOperator_Rem: *out = OP_MOD; return 1;
        case CXBinaryOperator_EQ:  *out = OP_EQ;  return 1;
        case CXBinaryOperator_NE:  *out = OP_NE;  return 1;
        case CXBinaryOperator_LT:  *out = OP_LT;  return 1;
        case CXBinaryOperator_LE:  *out = OP_LE;  return 1;
        case CXBinaryOperator_GT:  *out = OP_GT;  return 1;
        case CXBinaryOperator_GE:  *out = OP_GE;  return 1;
        case CXBinaryOperator_LAnd: *out = OP_AND; return 1;
        case CXBinaryOperator_LOr:  *out = OP_OR;  return 1;
        case CXBinaryOperator_Assign: *out = OP_ASSIGN; return 1;
        default:
            fprintf(stderr, "unsupported binary operator kind: %d\n", (int)k);
            return 0;
    }
}

static Expr *lower_expr(CXCursor c);

static Expr *lower_binop(CXCursor c) {
    enum CXBinaryOperatorKind bk = clang_getCursorBinaryOperatorKind(c);
    BinOpKind op;
    if (!map_binop(bk, &op)) return NULL;

    CXCursor kids[2];
    int idx = 0;
    enum CXChildVisitResult vis(CXCursor ch, CXCursor parent, CXClientData data) {
        (void)parent; (void)data;
        if (idx < 2) kids[idx++] = ch;
        return CXChildVisit_Continue;
    }
    clang_visitChildren(c, vis, NULL);
    if (idx != 2) return NULL;
    Expr *e = expr_binop(op, lower_expr(kids[0]), lower_expr(kids[1]));
    e->type = clang_getCursorType(c);
    return e;
}

static Expr *lower_unop(CXCursor c) {
    enum CXUnaryOperatorKind uk = clang_getCursorUnaryOperatorKind(c);
    Expr *operand = NULL;
    enum CXChildVisitResult vis(CXCursor ch, CXCursor parent, CXClientData data) {
        (void)parent; (void)data;
        operand = lower_expr(ch);
        return CXChildVisit_Break;
    }
    clang_visitChildren(c, vis, NULL);
    if (!operand) return NULL;

    Expr *e = NULL;
    switch (uk) {
        case CXUnaryOperator_Minus:  e = expr_unop(UNOP_NEG, operand); break;
        case CXUnaryOperator_LNot:   e = expr_unop(UNOP_NOT, operand); break;
        case CXUnaryOperator_AddrOf: e = expr_addr_of(operand); break;
        case CXUnaryOperator_Deref:  e = expr_deref(operand); break;
        default:
            fprintf(stderr, "unsupported unary operator kind: %d\n", (int)uk);
            return NULL;
    }
    e->type = clang_getCursorType(c);
    return e;
}

static Expr *lower_int_literal(CXCursor c) {
    CXEvalResult ev = clang_Cursor_Evaluate(c);
    long v;
    if (ev) {
        v = (long)clang_EvalResult_getAsLongLong(ev);
        clang_EvalResult_dispose(ev);
    } else {
        CXString sp = clang_getCursorSpelling(c);
        v = strtol(clang_getCString(sp), NULL, 0);
        clang_disposeString(sp);
    }
    Expr *e = expr_int(v);
    e->type = clang_getCursorType(c);
    return e;
}

static Expr *lower_string_literal(CXCursor c) {
    CXEvalResult ev = clang_Cursor_Evaluate(c);
    if (ev) {
        if (clang_EvalResult_getKind(ev) == CXEval_StrLiteral) {
            const char *str = clang_EvalResult_getAsStr(ev);
            if (str) {
                size_t len = strlen(str) + 1;
                DataItem *d = program_add_string(g_ctx.prog,
                                                 (const unsigned char *)str, len);
                clang_EvalResult_dispose(ev);
                Expr *e = expr_string(d->symbol);
                e->type = clang_getCursorType(c);
                return e;
            }
        }
        clang_EvalResult_dispose(ev);
    }

    /* Fallback: parse the cursor's spelling. */
    CXString sp = clang_getCursorSpelling(c);
    const char *raw = clang_getCString(sp);
    if (raw && raw[0] == '"') {
        size_t rl = strlen(raw);
        if (rl >= 2 && raw[rl-1] == '"') {
            size_t inner = rl - 2;
            unsigned char *buf = malloc(inner + 1);
            memcpy(buf, raw + 1, inner);
            buf[inner] = 0;
            DataItem *d = program_add_string(g_ctx.prog, buf, inner + 1);
            free(buf);
            clang_disposeString(sp);
            Expr *e = expr_string(d->symbol);
            e->type = clang_getCursorType(c);
            return e;
        }
    }

    fprintf(stderr, "clang_bridge: could not extract string literal (raw='%s')\n",
            raw ? raw : "(null)");
    clang_disposeString(sp);
    return NULL;
}

static Expr *lower_decl_ref(CXCursor c) {
    char *name = cxstr_to_cstr(clang_getCursorSpelling(c));
    Expr *e = expr_var(name);
    free(name);
    e->type = clang_getCursorType(c);
    return e;
}

static Expr *lower_call(CXCursor c) {
    char *name = cxstr_to_cstr(clang_getCursorSpelling(c));
    int nargs = clang_Cursor_getNumArguments(c);
    Expr **args = NULL;
    if (nargs > 0) args = calloc(nargs, sizeof(Expr*));
    for (int i = 0; i < nargs; i++) {
        CXCursor a = clang_Cursor_getArgument(c, i);
        args[i] = lower_expr(a);
    }
    Expr *e = expr_call(name, args, nargs);
    free(name);
    e->type = clang_getCursorType(c);
    return e;
}

static Expr *lower_index(CXCursor c) {
    CXCursor kids[2];
    int idx = 0;
    enum CXChildVisitResult vis(CXCursor ch, CXCursor parent, CXClientData data) {
        (void)parent; (void)data;
        if (idx < 2) kids[idx++] = ch;
        return CXChildVisit_Continue;
    }
    clang_visitChildren(c, vis, NULL);
    if (idx != 2) return NULL;
    Expr *e = expr_index(lower_expr(kids[0]), lower_expr(kids[1]));
    e->type = clang_getCursorType(c);
    return e;
}

static Expr *lower_member(CXCursor c) {
    Expr *base = NULL;
    enum CXChildVisitResult vis(CXCursor ch, CXCursor parent, CXClientData data) {
        (void)parent; (void)data;
        base = lower_expr(ch);
        return CXChildVisit_Break;
    }
    clang_visitChildren(c, vis, NULL);

    char *field = cxstr_to_cstr(clang_getCursorSpelling(c));
    int is_arrow = 0;
    if (base) {
        CXType bt = clang_getCanonicalType(base->type);
        is_arrow = (bt.kind == CXType_Pointer);
    }
    Expr *e = expr_member(base, field, is_arrow);
    free(field);
    e->type = clang_getCursorType(c);
    return e;
}

typedef struct { Expr *result; } ExprCtx;

static enum CXChildVisitResult expr_visitor(CXCursor c, CXCursor parent, CXClientData data) {
    (void)parent;
    ExprCtx *ctx = data;
    ctx->result = lower_expr(c);
    return CXChildVisit_Break;
}

static Expr *lower_expr(CXCursor c) {
    enum CXCursorKind k = clang_getCursorKind(c);
    switch (k) {
        case CXCursor_IntegerLiteral:    return lower_int_literal(c);
        case CXCursor_StringLiteral:     return lower_string_literal(c);
        case CXCursor_DeclRefExpr:       return lower_decl_ref(c);
        case CXCursor_BinaryOperator:    return lower_binop(c);
        case CXCursor_UnaryOperator:     return lower_unop(c);
        case CXCursor_CallExpr:          return lower_call(c);
        case CXCursor_ArraySubscriptExpr: return lower_index(c);
        case CXCursor_MemberRefExpr:     return lower_member(c);
        case CXCursor_UnexposedExpr:
        case CXCursor_ParenExpr: {
            ExprCtx ctx = {0};
            clang_visitChildren(c, expr_visitor, &ctx);
            return ctx.result;
        }
        default: {
            CXString sp = clang_getCursorKindSpelling(k);
            CXString sp2 = clang_getCursorSpelling(c);
            fprintf(stderr, "unsupported expression kind: %s (spelling='%s')\n",
                    clang_getCString(sp), clang_getCString(sp2));
            clang_disposeString(sp);
            clang_disposeString(sp2);
            return NULL;
        }
    }
}

/* ─────────────  Statement lowering  ───────────── */

typedef struct { Stmt *head, *tail; } StmtList;

static void stmtlist_push(StmtList *l, Stmt *s) {
    if (!s) return;
    if (!l->head) { l->head = l->tail = s; return; }
    l->tail->next = s;
    l->tail = s;
}

static Stmt *lower_stmt(CXCursor c);

static enum CXChildVisitResult stmt_list_visitor(CXCursor c, CXCursor parent, CXClientData data) {
    (void)parent;
    StmtList *l = data;
    stmtlist_push(l, lower_stmt(c));
    return CXChildVisit_Continue;
}

static Stmt *lower_block(CXCursor c) {
    StmtList list = {0};
    clang_visitChildren(c, stmt_list_visitor, &list);
    return stmt_block(list.head);
}

static Stmt *lower_return(CXCursor c) {
    ExprCtx ctx = {0};
    clang_visitChildren(c, expr_visitor, &ctx);
    return stmt_return(ctx.result);
}

static Stmt *lower_decl_stmt(CXCursor c) {
    char *name = cxstr_to_cstr(clang_getCursorSpelling(c));
    Expr *init = NULL;

    enum CXChildVisitResult vis(CXCursor ch, CXCursor parent, CXClientData data) {
        (void)parent;
        enum CXCursorKind k = clang_getCursorKind(ch);
        if (k == CXCursor_TypeRef) return CXChildVisit_Continue;
        if (clang_isExpression(k)) {
            Expr **slot = data;
            *slot = lower_expr(ch);
            return CXChildVisit_Break;
        }
        return CXChildVisit_Continue;
    }
    clang_visitChildren(c, vis, &init);

    Stmt *s = stmt_decl(name, init);
    s->decl.type = clang_getCursorType(c);
    free(name);
    return s;
}

static Stmt *lower_if(CXCursor c) {
    CXCursor kids[3];
    int idx = 0;
    enum CXChildVisitResult vis(CXCursor ch, CXCursor parent, CXClientData data) {
        (void)parent; (void)data;
        if (idx < 3) kids[idx++] = ch;
        return CXChildVisit_Continue;
    }
    clang_visitChildren(c, vis, NULL);

    Expr *cond = NULL;
    Stmt *then_body = NULL, *else_body = NULL;
    if (idx >= 1) cond = lower_expr(kids[0]);
    if (idx >= 2) then_body = lower_stmt(kids[1]);
    if (idx >= 3) else_body = lower_stmt(kids[2]);
    return stmt_if(cond, then_body, else_body);
}

static Stmt *lower_while(CXCursor c) {
    CXCursor kids[2];
    int idx = 0;
    enum CXChildVisitResult vis(CXCursor ch, CXCursor parent, CXClientData data) {
        (void)parent; (void)data;
        if (idx < 2) kids[idx++] = ch;
        return CXChildVisit_Continue;
    }
    clang_visitChildren(c, vis, NULL);

    Expr *cond = NULL;
    Stmt *body = NULL;
    if (idx >= 1) cond = lower_expr(kids[0]);
    if (idx >= 2) body = lower_stmt(kids[1]);
    return stmt_while(cond, body);
}

static Stmt *lower_do(CXCursor c) {
    CXCursor kids[2];
    int idx = 0;
    enum CXChildVisitResult vis(CXCursor ch, CXCursor parent, CXClientData data) {
        (void)parent; (void)data;
        if (idx < 2) kids[idx++] = ch;
        return CXChildVisit_Continue;
    }
    clang_visitChildren(c, vis, NULL);

    Stmt *body = NULL;
    Expr *cond = NULL;
    if (idx >= 1) body = lower_stmt(kids[0]);
    if (idx >= 2) cond = lower_expr(kids[1]);
    return stmt_do(body, cond);
}

static Stmt *lower_for(CXCursor c) {
    Stmt *init = NULL;
    Expr *cond = NULL, *post = NULL;
    Stmt *body = NULL;

    enum CXChildVisitResult vis(CXCursor ch, CXCursor parent, CXClientData data) {
        (void)parent; (void)data;
        enum CXCursorKind k = clang_getCursorKind(ch);
        if (k == CXCursor_DeclStmt) {
            init = lower_stmt(ch);
        } else if (k == CXCursor_CompoundStmt) {
            body = lower_stmt(ch);
        } else if (k == CXCursor_NullStmt) {
            /* skip */
        } else if (clang_isExpression(k)) {
            if (!cond && !body) cond = lower_expr(ch);
            else if (!post && body) post = lower_expr(ch);
            else if (!cond) cond = lower_expr(ch);
            else post = lower_expr(ch);
        }
        return CXChildVisit_Continue;
    }
    clang_visitChildren(c, vis, NULL);

    return stmt_for(init, cond, post, body);
}

static Stmt *lower_stmt(CXCursor c) {
    enum CXCursorKind k = clang_getCursorKind(c);
    switch (k) {
        case CXCursor_CompoundStmt: return lower_block(c);
        case CXCursor_ReturnStmt:   return lower_return(c);
        case CXCursor_DeclStmt: {
            StmtList list = {0};
            enum CXChildVisitResult dv(CXCursor ch, CXCursor parent, CXClientData data) {
                (void)parent;
                StmtList *l = data;
                if (clang_getCursorKind(ch) == CXCursor_VarDecl)
                    stmtlist_push(l, lower_decl_stmt(ch));
                return CXChildVisit_Continue;
            }
            clang_visitChildren(c, dv, &list);
            return list.head;
        }
        case CXCursor_IfStmt:       return lower_if(c);
        case CXCursor_WhileStmt:    return lower_while(c);
        case CXCursor_DoStmt:       return lower_do(c);
        case CXCursor_ForStmt:      return lower_for(c);
        case CXCursor_BreakStmt:    return stmt_break();
        case CXCursor_ContinueStmt: return stmt_continue();
        case CXCursor_NullStmt:     return NULL;
        default: {
            if (clang_isExpression(k)) {
                ExprCtx ctx = {0};
                ctx.result = lower_expr(c);
                return stmt_expr(ctx.result);
            }
            CXString sp = clang_getCursorKindSpelling(k);
            fprintf(stderr, "unsupported statement kind: %s\n", clang_getCString(sp));
            clang_disposeString(sp);
            return NULL;
        }
    }
}

/* ─────────────  Top-level  ───────────── */

static enum CXChildVisitResult top_level_visitor(CXCursor c, CXCursor parent, CXClientData data) {
    (void)parent;
    (void)data;

    CXSourceLocation loc = clang_getCursorLocation(c);
    if (!clang_Location_isFromMainFile(loc)) return CXChildVisit_Continue;

    if (clang_getCursorKind(c) != CXCursor_FunctionDecl) return CXChildVisit_Continue;

    char *name = cxstr_to_cstr(clang_getCursorSpelling(c));
    Func *f = func_new(name);
    free(name);

    f->return_type = clang_getResultType(clang_getCursorType(c));

    int nargs = clang_Cursor_getNumArguments(c);
    for (int i = 0; i < nargs; i++) {
        CXCursor a = clang_Cursor_getArgument(c, i);
        char *pname = cxstr_to_cstr(clang_getCursorSpelling(a));
        func_add_param(f, pname);
        f->params[f->num_params - 1].type = clang_getCursorType(a);
        free(pname);
    }

    /* Only definitions have bodies. Declarations without bodies are extern. */
    if (clang_isCursorDefinition(c)) {
        enum CXChildVisitResult body_vis(CXCursor ch, CXCursor parent2, CXClientData d) {
            (void)parent2;
            Func *fn = d;
            if (clang_getCursorKind(ch) == CXCursor_CompoundStmt) {
                fn->body = lower_block(ch)->block;
                return CXChildVisit_Break;
            }
            return CXChildVisit_Continue;
        }
        clang_visitChildren(c, body_vis, f);
    }

    program_add_func(g_ctx.prog, f);
    return CXChildVisit_Continue;
}

Program *clang_bridge_parse(const char *path) {
    CXIndex index = clang_createIndex(0, 0);
    const char *args[] = { "-x", "c", "-std=c11" };
    CXTranslationUnit tu = clang_parseTranslationUnit(
        index, path, args, 3, NULL, 0,
        CXTranslationUnit_DetailedPreprocessingRecord);
    if (!tu) {
        fprintf(stderr, "clang_bridge: failed to parse %s\n", path);
        clang_disposeIndex(index);
        return NULL;
    }

    unsigned ndiag = clang_getNumDiagnostics(tu);
    int had_error = 0;
    for (unsigned i = 0; i < ndiag; i++) {
        CXDiagnostic d = clang_getDiagnostic(tu, i);
        if (clang_getDiagnosticSeverity(d) >= CXDiagnostic_Error) had_error = 1;
        CXString s = clang_formatDiagnostic(d, clang_defaultDiagnosticDisplayOptions());
        fprintf(stderr, "%s\n", clang_getCString(s));
        clang_disposeString(s);
        clang_disposeDiagnostic(d);
    }
    if (had_error) {
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return NULL;
    }

    Program *prog = program_new();
    g_ctx.prog = prog;

    CXCursor root = clang_getTranslationUnitCursor(tu);
    clang_visitChildren(root, top_level_visitor, NULL);

    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    return prog;
}

void clang_bridge_shutdown(void) { }
