#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    memcpy(r, s, n);
    return r;
}

Program *program_new(void) { return calloc(1, sizeof(Program)); }

Func *func_new(const char *name) {
    Func *f = calloc(1, sizeof(Func));
    f->name = dup_str(name);
    return f;
}

void func_add_param(Func *f, const char *name) {
    f->params = realloc(f->params, (f->num_params + 1) * sizeof(Param));
    f->params[f->num_params].name = dup_str(name);
    f->num_params++;
}

void func_set_body(Func *f, Stmt *body) { f->body = body; }

void program_add_func(Program *p, Func *f) {
    if (!p->funcs) { p->funcs = f; return; }
    Func *cur = p->funcs;
    while (cur->next) cur = cur->next;
    cur->next = f;
}

void program_free(Program *p) {
    if (!p) return;
    Func *f = p->funcs;
    while (f) {
        Func *n = f->next;
        free(f->name);
        for (int i = 0; i < f->num_params; i++) free(f->params[i].name);
        free(f->params);
        stmt_free(f->body);
        free(f);
        f = n;
    }
    free(p);
}

Expr *expr_int(long v) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = EXPR_INT_LIT;
    e->int_lit = v;
    return e;
}

Expr *expr_var(const char *name) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = EXPR_VAR;
    e->var_name = dup_str(name);
    return e;
}

Expr *expr_binop(BinOpKind op, Expr *l, Expr *r) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = EXPR_BINOP;
    e->binop.op = op; e->binop.lhs = l; e->binop.rhs = r;
    return e;
}

Expr *expr_unop(UnOpKind op, Expr *operand) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = EXPR_UNOP;
    e->unop.op = op; e->unop.operand = operand;
    return e;
}

Expr *expr_call(const char *name, Expr **args, int nargs) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = EXPR_CALL;
    e->call.name = dup_str(name);
    e->call.args = args;
    e->call.nargs = nargs;
    return e;
}

void expr_free(Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_VAR: free(e->var_name); break;
        case EXPR_BINOP: expr_free(e->binop.lhs); expr_free(e->binop.rhs); break;
        case EXPR_UNOP:  expr_free(e->unop.operand); break;
        case EXPR_CALL:
            for (int i = 0; i < e->call.nargs; i++) expr_free(e->call.args[i]);
            free(e->call.args);
            free(e->call.name);
            break;
        default: break;
    }
    free(e);
}

Stmt *stmt_return(Expr *e) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = STMT_RETURN; s->ret_expr = e; return s;
}

Stmt *stmt_expr(Expr *e) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = STMT_EXPR; s->expr = e; return s;
}

Stmt *stmt_decl(const char *name, Expr *init) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = STMT_DECL;
    s->decl.name = dup_str(name); s->decl.init = init;
    return s;
}

Stmt *stmt_block(Stmt *body) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = STMT_BLOCK; s->block = body; return s;
}

Stmt *stmt_if(Expr *cond, Stmt *then_body, Stmt *else_body) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = STMT_IF;
    s->if_stmt.cond = cond;
    s->if_stmt.then_body = then_body;
    s->if_stmt.else_body = else_body;
    return s;
}

Stmt *stmt_while(Expr *cond, Stmt *body) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = STMT_WHILE;
    s->while_stmt.cond = cond; s->while_stmt.body = body;
    return s;
}

void stmt_list_append(Stmt **head, Stmt *s) {
    if (!*head) { *head = s; return; }
    Stmt *cur = *head;
    while (cur->next) cur = cur->next;
    cur->next = s;
}

void stmt_free(Stmt *s) {
    while (s) {
        Stmt *n = s->next;
        switch (s->kind) {
            case STMT_RETURN: expr_free(s->ret_expr); break;
            case STMT_EXPR:   expr_free(s->expr); break;
            case STMT_DECL:   free(s->decl.name); expr_free(s->decl.init); break;
            case STMT_BLOCK:  stmt_free(s->block); break;
            case STMT_IF:
                expr_free(s->if_stmt.cond);
                stmt_free(s->if_stmt.then_body);
                stmt_free(s->if_stmt.else_body);
                break;
            case STMT_WHILE:
                expr_free(s->while_stmt.cond);
                stmt_free(s->while_stmt.body);
                break;
        }
        free(s);
        s = n;
    }
}

static void indent(int n) { for (int i = 0; i < n; i++) fputs("  ", stdout); }

static const char *binop_str(BinOpKind k) {
    switch (k) {
        case OP_ADD: return "+"; case OP_SUB: return "-";
        case OP_MUL: return "*"; case OP_DIV: return "/"; case OP_MOD: return "%";
        case OP_EQ: return "=="; case OP_NE: return "!=";
        case OP_LT: return "<"; case OP_LE: return "<=";
        case OP_GT: return ">"; case OP_GE: return ">=";
        case OP_AND: return "&&"; case OP_OR: return "||";
        case OP_ASSIGN: return "=";
    }
    return "?";
}

void expr_print(const Expr *e, int ind) {
    if (!e) { indent(ind); puts("(null expr)"); return; }
    switch (e->kind) {
        case EXPR_INT_LIT: indent(ind); printf("Int(%ld)\n", e->int_lit); break;
        case EXPR_VAR:     indent(ind); printf("Var(%s)\n", e->var_name); break;
        case EXPR_BINOP:
            indent(ind); printf("BinOp(%s)\n", binop_str(e->binop.op));
            expr_print(e->binop.lhs, ind + 1);
            expr_print(e->binop.rhs, ind + 1);
            break;
        case EXPR_UNOP:
            indent(ind); printf("UnOp(%s)\n", e->unop.op == UNOP_NEG ? "-" : "!");
            expr_print(e->unop.operand, ind + 1);
            break;
        case EXPR_CALL:
            indent(ind); printf("Call(%s, %d args)\n", e->call.name, e->call.nargs);
            for (int i = 0; i < e->call.nargs; i++)
                expr_print(e->call.args[i], ind + 1);
            break;
    }
}

void stmt_print(const Stmt *s, int ind) {
    for (; s; s = s->next) {
        switch (s->kind) {
            case STMT_RETURN:
                indent(ind); puts("Return");
                expr_print(s->ret_expr, ind + 1);
                break;
            case STMT_EXPR:
                indent(ind); puts("ExprStmt");
                expr_print(s->expr, ind + 1);
                break;
            case STMT_DECL:
                indent(ind); printf("Decl(%s)\n", s->decl.name);
                if (s->decl.init) expr_print(s->decl.init, ind + 1);
                break;
            case STMT_BLOCK:
                indent(ind); puts("Block {");
                stmt_print(s->block, ind + 1);
                indent(ind); puts("}");
                break;
            case STMT_IF:
                indent(ind); puts("If");
                expr_print(s->if_stmt.cond, ind + 1);
                indent(ind); puts("then:");
                stmt_print(s->if_stmt.then_body, ind + 1);
                if (s->if_stmt.else_body) {
                    indent(ind); puts("else:");
                    stmt_print(s->if_stmt.else_body, ind + 1);
                }
                break;
            case STMT_WHILE:
                indent(ind); puts("While");
                expr_print(s->while_stmt.cond, ind + 1);
                indent(ind); puts("body:");
                stmt_print(s->while_stmt.body, ind + 1);
                break;
        }
    }
}

void program_print(const Program *p) {
    for (Func *f = p->funcs; f; f = f->next) {
        printf("func %s(", f->name);
        for (int i = 0; i < f->num_params; i++)
            printf("%s%s", f->params[i].name, i + 1 < f->num_params ? ", " : "");
        puts(") {");
        stmt_print(f->body, 1);
        puts("}\n");
    }
}
