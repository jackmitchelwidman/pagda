/* Syntax tree shared by the parser and the compiler. */
#ifndef AST_H
#define AST_H
#include "py.h"
#include <setjmp.h>

typedef struct Node Node;
typedef struct Scope Scope;

typedef struct {
    Str **names; Node **defaults; int n;          /* positional parameters; defaults[i] may be NULL */
    int nposonly;                                 /* how many of them come before '/' */
    Str *vararg;
    Str **kwonly; Node **kwdefaults; int nkwonly;
    Str *kwarg;
    /* Annotations; any entry may be NULL. */
    Node **ann, **kwann, *vararg_ann, *kwarg_ann;
} Params;

enum {
    /* expressions */
    E_CONST, E_NAME, E_BINOP, E_UNARY, E_NOT, E_BOOLOP, E_COMPARE, E_CALL, E_ATTR, E_SUBSCR, E_SLICE,
    E_LIST, E_TUPLE, E_SET, E_DICT, E_IFEXP, E_LAMBDA, E_LISTCOMP, E_SETCOMP, E_DICTCOMP, E_GENEXP,
    E_YIELD, E_YIELDFROM, E_STARRED, E_NAMED, E_FSTRING, E_FORMATTED, E_KEYWORD, N_COMP,
    /* statements */
    S_EXPR, S_ASSIGN, S_AUGASSIGN, S_ANNASSIGN, S_RETURN, S_IF, S_WHILE, S_FOR, S_BREAK, S_CONTINUE,
    S_PASS, S_DEF, S_CLASS, S_TRY, N_HANDLER, S_RAISE, S_WITH, N_WITHITEM, S_ASSERT, S_DEL,
    S_GLOBAL, S_NONLOCAL, S_IMPORT, S_IMPORTFROM, N_ALIAS, S_MODULE,
    /* match statement: S_MATCH a=subject l[0]=cases; N_CASE a=pattern b=guard l[0]=body */
    S_MATCH, N_CASE,
    /* patterns:
       P_VALUE   a=expression; op=1 compares by identity (None, True, False)
       P_CAPTURE val=name, or NULL for the wildcard _
       P_SEQ     l[0]=items, each a pattern or a P_STAR (val=name or NULL)
       P_MAP     l[0]=key expressions l[1]=patterns val=name after ** or NULL
       P_CLASS   a=class expression l[0]=positional patterns l[1]=keyword patterns l[2]=keyword names (E_CONST)
       P_OR      l[0]=alternatives
       P_AS      a=pattern val=name */
    P_VALUE, P_CAPTURE, P_SEQ, P_STAR, P_MAP, P_CLASS, P_OR, P_AS,
    /* def f[T: bound]: val=name a=bound c=default op=0 plain, 1 *Ts, 2 **P.
       Lives in l[2] of S_DEF and l[3] of S_CLASS. */
    N_TYPEPARAM,
    /* A hidden function that holds the type parameters of a generic def or class:
       l[0]=body statements, params=its parameters. Built by the compiler. */
    E_GENERIC
};

/* Comparison operators beyond CMP_*. */
enum { CMP_IN = 6, CMP_NOT_IN, CMP_IS, CMP_IS_NOT };

struct Node {
    int kind, line, op;
    Node *a, *b, *c;
    Node **l[4];
    int n[4];
    Obj *val, *val2;
    Params *params;
    Scope *scope;
    int *ops;
};

extern jmp_buf compile_jmp;
void *arena_alloc(size_t n);
void arena_free_all(void);
void syntax_error(const char *filename, int line, const char *fmt, ...) __attribute__((noreturn, format(printf, 3, 4)));
Node *parse_source(const char *src, const char *filename, int mode);
Str *unparse_expr(Node *n, const char *filename);
/* Checked files (agda.c). */
bool agda_wanted(Node *module);
void agda_check(Node *module, const char *filename);
extern int agda_mode;
extern bool agda_checked;   /* the last module compiled was a checked file */

#endif
