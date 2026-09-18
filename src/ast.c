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
    f->params[f->num_params].type = (CXType){0};
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

Expr *expr_string(const char *s) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = EXPR_STRING_LIT;
    e->string_lit = dup_str(s);
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

Expr *expr_addr_of(Expr *operand) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = EXPR_ADDR_OF;
    e->operand = operand;
    return e;
}

Expr *expr_deref(Expr *operand) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = EXPR_DEREF;
    e->operand = operand;
    return e;
}

Expr *expr_index(Expr *array, Expr *index) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = EXPR_INDEX;
    e->index.array = array;
    e->index.index = index;
    return e;
}

Expr *expr_member(Expr *base, const char *field, int is_arrow) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = EXPR_MEMBER;
    e->member.base = base;
    e->member.field = dup_str(field);
    e->member.is_arrow = is_arrow;
    return e;
}

void expr_free(Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_VAR: free(e->var_name); break;
        case EXPR_STRING_LIT: free(e->string_lit); break;
        case EXPR_BINOP: expr_free(e->binop.lhs); expr_free(e->binop.rhs); break;
        case EXPR_UNOP:  expr_free(e->unop.operand); break;
        case EXPR_CALL:
            for (int i = 0; i < e->call.nargs; i++) expr_free(e->call.args[i]);
            free(e->call.args);
            free(e->call.name);
            break;
        case EXPR_ADDR_OF:
        case EXPR_DEREF:
            expr_free(e->operand);
            break;
        case EXPR_INDEX:
            expr_free(e->index.array);
            expr_free(e->index.index);
            break;
        case EXPR_MEMBER:
            expr_free(e->member.base);
            free(e->member.field);
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
    s->decl.name = dup_str(name); s->decl.init = init; s->decl.type = (CXType){0};
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

Stmt *stmt_for(Stmt *init, Expr *cond, Expr *post, Stmt *body) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = STMT_FOR;
    s->for_stmt.init = init;
    s->for_stmt.cond = cond;
    s->for_stmt.post = post;
    s->for_stmt.body = body;
    return s;
}

Stmt *stmt_do(Stmt *body, Expr *cond) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = STMT_DO;
    s->do_stmt.cond = cond; s->do_stmt.body = body;
    return s;
}

Stmt *stmt_break(void) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = STMT_BREAK;
    return s;
}

Stmt *stmt_continue(void) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = STMT_CONTINUE;
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
            case STMT_FOR:
                stmt_free(s->for_stmt.init);
                expr_free(s->for_stmt.cond);
                expr_free(s->for_stmt.post);
                stmt_free(s->for_stmt.body);
                break;
            case STMT_DO:
                expr_free(s->do_stmt.cond);
                stmt_free(s->do_stmt.body);
                break;
            case STMT_BREAK:
            case STMT_CONTINUE:
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
        case EXPR_STRING_LIT: indent(ind); printf("Str(%s)\n", e->string_lit); break;
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
        case EXPR_ADDR_OF:
            indent(ind); puts("AddrOf");
            expr_print(e->operand, ind + 1);
            break;
        case EXPR_DEREF:
            indent(ind); puts("Deref");
            expr_print(e->operand, ind + 1);
            break;
        case EXPR_INDEX:
            indent(ind); puts("Index");
            expr_print(e->index.array, ind + 1);
            expr_print(e->index.index, ind + 1);
            break;
        case EXPR_MEMBER:
            indent(ind); printf("Member(%s%s)\n",
                e->member.is_arrow ? "->" : ".", e->member.field);
            expr_print(e->member.base, ind + 1);
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
            case STMT_FOR:
                indent(ind); puts("For");
                indent(ind + 1); puts("init:");
                stmt_print(s->for_stmt.init, ind + 2);
                indent(ind + 1); puts("cond:");
                expr_print(s->for_stmt.cond, ind + 2);
                indent(ind + 1); puts("post:");
                expr_print(s->for_stmt.post, ind + 2);
                indent(ind + 1); puts("body:");
                stmt_print(s->for_stmt.body, ind + 2);
                break;
            case STMT_DO:
                indent(ind); puts("Do");
                stmt_print(s->do_stmt.body, ind + 1);
                indent(ind); puts("while");
                expr_print(s->do_stmt.cond, ind + 1);
                break;
            case STMT_BREAK:    indent(ind); puts("Break"); break;
            case STMT_CONTINUE: indent(ind); puts("Continue"); break;
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
