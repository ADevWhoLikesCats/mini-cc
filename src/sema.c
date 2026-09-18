#include "sema.h"
#include <stdio.h>

/* Rung 0: libclang did the semantic analysis. This only sanity-checks
   the AST we built for structural invariants codegen relies on. */

static int verify_expr(const Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_INT_LIT: return 1;
        case EXPR_VAR:     return e->var_name != NULL;
        case EXPR_BINOP:   return verify_expr(e->binop.lhs) && verify_expr(e->binop.rhs);
        case EXPR_UNOP:    return verify_expr(e->unop.operand);
        case EXPR_CALL:
            for (int i = 0; i < e->call.nargs; i++)
                if (!verify_expr(e->call.args[i])) return 0;
            return 1;
        case EXPR_ADDR_OF:
        case EXPR_DEREF:
            return verify_expr(e->operand);
        case EXPR_INDEX:
            return verify_expr(e->index.array) && verify_expr(e->index.index);
        case EXPR_MEMBER:
            return verify_expr(e->member.base) && e->member.field != NULL;
    }
    return 0;
}

static int verify_stmt(const Stmt *s) {
    for (; s; s = s->next) {
        switch (s->kind) {
            case STMT_RETURN: if (!verify_expr(s->ret_expr)) return 0; break;
            case STMT_EXPR:   if (!verify_expr(s->expr)) return 0; break;
            case STMT_DECL:   if (s->decl.init && !verify_expr(s->decl.init)) return 0; break;
            case STMT_BLOCK:  if (!verify_stmt(s->block)) return 0; break;
            case STMT_IF:
                if (!verify_expr(s->if_stmt.cond)) return 0;
                if (!verify_stmt(s->if_stmt.then_body)) return 0;
                if (s->if_stmt.else_body && !verify_stmt(s->if_stmt.else_body)) return 0;
                break;
            case STMT_WHILE:
                if (!verify_expr(s->while_stmt.cond)) return 0;
                if (!verify_stmt(s->while_stmt.body)) return 0;
                break;
        }
    }
    return 1;
}

int sema_check(Program *p) {
    for (Func *f = p->funcs; f; f = f->next) {
        if (!verify_stmt(f->body)) {
            fprintf(stderr, "bridge: malformed AST in function '%s'\n", f->name);
            return 1;
        }
    }
    return 0;
}
