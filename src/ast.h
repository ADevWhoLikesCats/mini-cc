#pragma once
#include <stddef.h>
#include <clang-c/Index.h>

/* ─────────────  Expressions  ───────────── */

typedef enum {
    EXPR_INT_LIT,
    EXPR_STRING_LIT,
    EXPR_VAR,
    EXPR_BINOP,
    EXPR_UNOP,
    EXPR_CALL,
    EXPR_ADDR_OF,
    EXPR_DEREF,
    EXPR_INDEX,
    EXPR_MEMBER
} ExprKind;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_AND, OP_OR,
    OP_ASSIGN
} BinOpKind;

typedef enum {
    UNOP_NEG,
    UNOP_NOT
} UnOpKind;

typedef struct Expr Expr;
struct Expr {
    ExprKind kind;
    CXType   type;
    union {
        long int_lit;
        char *string_lit;
        char *var_name;
        struct { BinOpKind op; Expr *lhs, *rhs; } binop;
        struct { UnOpKind op; Expr *operand; } unop;
        struct { char *name; Expr **args; int nargs; } call;
        Expr *operand;
        struct { Expr *array; Expr *index; } index;
        struct { Expr *base; char *field; int is_arrow; } member;
    };
};

/* ─────────────  Statements  ───────────── */

typedef enum {
    STMT_RETURN,
    STMT_EXPR,
    STMT_DECL,
    STMT_BLOCK,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_DO,
    STMT_BREAK,
    STMT_CONTINUE
} StmtKind;

typedef struct Stmt Stmt;
struct Stmt {
    StmtKind kind;
    Stmt *next;
    union {
        Expr *ret_expr;
        Expr *expr;
        struct { char *name; CXType type; Expr *init; } decl;
        Stmt *block;
        struct { Expr *cond; Stmt *then_body; Stmt *else_body; } if_stmt;
        struct { Expr *cond; Stmt *body; } while_stmt;
        struct { Stmt *init; Expr *cond; Expr *post; Stmt *body; } for_stmt;
        struct { Expr *cond; Stmt *body; } do_stmt;
    };
};

typedef struct {
    char   *name;
    CXType  type;
} Param;

typedef struct Func Func;
struct Func {
    char   *name;
    CXType  return_type;
    int     num_params;
    Param  *params;
    Stmt   *body;
    Func   *next;
};

typedef struct {
    Func *funcs;
} Program;

/* ─────────────  Constructors  ───────────── */

Program *program_new(void);
void     program_free(Program *p);
Func    *func_new(const char *name);
void     func_add_param(Func *f, const char *name);
void     func_set_body(Func *f, Stmt *body);
void     program_add_func(Program *p, Func *f);

Expr *expr_int(long v);
Expr *expr_string(const char *s);
Expr *expr_var(const char *name);
Expr *expr_binop(BinOpKind op, Expr *l, Expr *r);
Expr *expr_unop(UnOpKind op, Expr *operand);
Expr *expr_call(const char *name, Expr **args, int nargs);
Expr *expr_addr_of(Expr *operand);
Expr *expr_deref(Expr *operand);
Expr *expr_index(Expr *array, Expr *index);
Expr *expr_member(Expr *base, const char *field, int is_arrow);
void  expr_free(Expr *e);

Stmt *stmt_return(Expr *e);
Stmt *stmt_expr(Expr *e);
Stmt *stmt_decl(const char *name, Expr *init);
Stmt *stmt_block(Stmt *body);
Stmt *stmt_if(Expr *cond, Stmt *then_body, Stmt *else_body);
Stmt *stmt_while(Expr *cond, Stmt *body);
Stmt *stmt_for(Stmt *init, Expr *cond, Expr *post, Stmt *body);
Stmt *stmt_do(Stmt *body, Expr *cond);
Stmt *stmt_break(void);
Stmt *stmt_continue(void);
void  stmt_list_append(Stmt **head, Stmt *s);
void  stmt_free(Stmt *s);

void program_print(const Program *p);
void expr_print(const Expr *e, int indent);
void stmt_print(const Stmt *s, int indent);
