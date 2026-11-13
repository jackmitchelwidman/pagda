/* Compiler: scope analysis, then bytecode generation from the syntax tree. */
#include "ast.h"
#include "opcode.h"

/* ---------- scopes ---------- */
enum { SC_MODULE, SC_FUNCTION, SC_CLASS };
enum { F_BOUND = 1, F_PARAM = 2, F_GLOBAL = 4, F_NONLOCAL = 8, F_USED = 16 };
typedef struct { Str *name; int flags; } Sym;
typedef struct { Str **v; int n, cap; } Names;
struct Scope {
    int type;
    Scope *parent;
    Sym *syms; int nsyms, cap;
    Names cells, frees, vars;
    Scope **kids; int nkids, kcap;
    bool generator, comp;
    bool transparent;   /* the hidden scope around a generic def or class */
};

enum { FB_WHILE, FB_FOR, FB_TRY, FB_FINALLY_TRY, FB_HANDLER, FB_WITH, FB_FINALLY_END };
typedef struct JumpList { int at; struct JumpList *next; } JumpList;
typedef struct { int type, target; JumpList *breaks; Node **final; int nfinal; Str *name; } FB;
#define MAXFB 64

typedef struct Unit {
    struct Unit *parent;
    Scope *scope;
    uint32_t *code; int *lines; int n, cap;
    List *consts, *names;
    FB fb[MAXFB]; int nfb;
    int line, firstline, maxblocks;
    Str *name, *qualname;
} Unit;

typedef struct { const char *filename; Str *fname; Unit *u; int mode; bool future_ann, checked; } C;
bool agda_checked;
static Unit *unit_chain;

__attribute__((noreturn, format(printf, 3, 4)))
static void cerror(C *c, int line, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    syntax_error(c->filename, line, "%s", buf);
}

static int names_find(Names *v, Str *name) {
    for (int i = 0; i < v->n; i++) if (str_eq(v->v[i], name)) return i;
    return -1;
}
static int names_add(Names *v, Str *name) {
    int i = names_find(v, name);
    if (i >= 0) return i;
    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 8;
        Str **nv = arena_alloc((size_t)cap * sizeof *nv);
        if (v->n) memcpy(nv, v->v, (size_t)v->n * sizeof *nv);
        v->v = nv; v->cap = cap;
    }
    v->v[v->n] = name;
    return v->n++;
}

static Scope *scope_new(int type, Scope *parent) {
    Scope *s = arena_alloc(sizeof(Scope));
    s->type = type;
    s->parent = parent;
    if (parent) {
        if (parent->nkids == parent->kcap) {
            int cap = parent->kcap ? parent->kcap * 2 : 4;
            Scope **nk = arena_alloc((size_t)cap * sizeof *nk);
            if (parent->nkids) memcpy(nk, parent->kids, (size_t)parent->nkids * sizeof *nk);
            parent->kids = nk; parent->kcap = cap;
        }
        parent->kids[parent->nkids++] = s;
    }
    return s;
}

static Sym *sym_find(Scope *s, Str *name) {
    for (int i = 0; i < s->nsyms; i++) if (str_eq(s->syms[i].name, name)) return &s->syms[i];
    return NULL;
}

static void sym_add(Scope *s, Str *name, int flags) {
    Sym *e = sym_find(s, name);
    if (e) { e->flags |= flags; return; }
    if (s->nsyms == s->cap) {
        int cap = s->cap ? s->cap * 2 : 8;
        Sym *ns = arena_alloc((size_t)cap * sizeof *ns);
        if (s->nsyms) memcpy(ns, s->syms, (size_t)s->nsyms * sizeof *ns);
        s->syms = ns; s->cap = cap;
    }
    s->syms[s->nsyms].name = name;
    s->syms[s->nsyms++].flags = flags;
}

static void sym_expr(C *c, Scope *s, Node *n);
static void sym_stmts(C *c, Scope *s, Node **l, int n);

static void sym_target(C *c, Scope *s, Node *n) {
    switch (n->kind) {
    case E_NAME: sym_add(s, AS_STR(n->val), F_BOUND); break;
    case E_TUPLE: case E_LIST: for (int i = 0; i < n->n[0]; i++) sym_target(c, s, n->l[0][i]); break;
    case E_STARRED: sym_target(c, s, n->a); break;
    default: sym_expr(c, s, n);
    }
}

static void sym_params(C *c, Scope *outer, Scope *inner, Params *p) {
    for (int i = 0; i < p->n; i++) {
        if (p->defaults[i]) sym_expr(c, outer, p->defaults[i]);
        sym_add(inner, p->names[i], F_BOUND | F_PARAM);
    }
    for (int i = 0; i < p->nkwonly; i++) {
        if (p->kwdefaults[i]) sym_expr(c, outer, p->kwdefaults[i]);
        sym_add(inner, p->kwonly[i], F_BOUND | F_PARAM);
    }
    if (p->vararg) sym_add(inner, p->vararg, F_BOUND | F_PARAM);
    if (p->kwarg) sym_add(inner, p->kwarg, F_BOUND | F_PARAM);
}

static void sym_comp(C *c, Scope *s, Node *n) {
    Scope *in = scope_new(SC_FUNCTION, s);
    in->comp = true;
    n->scope = in;
    if (n->kind == E_GENEXP) in->generator = true;
    sym_add(in, AS_STR(str_c(".0")), F_BOUND | F_PARAM);
    for (int i = 0; i < n->n[0]; i++) {
        Node *g = n->l[0][i];
        sym_expr(c, i == 0 ? s : in, g->b);
        sym_target(c, in, g->a);
        for (int j = 0; j < g->n[0]; j++) sym_expr(c, in, g->l[0][j]);
    }
    sym_expr(c, in, n->a);
    if (n->b) sym_expr(c, in, n->b);
}

static void sym_expr(C *c, Scope *s, Node *n) {
    if (!n) return;
    switch (n->kind) {
    case E_CONST: break;
    case E_NAME:
        sym_add(s, AS_STR(n->val), F_USED);
        if (s->type == SC_FUNCTION && str_eqc(AS_STR(n->val), "super")) sym_add(s, N_class, F_USED);
        break;
    case E_BINOP: sym_expr(c, s, n->a); sym_expr(c, s, n->b); break;
    case E_UNARY: case E_NOT: case E_STARRED: case E_KEYWORD: case E_ATTR: sym_expr(c, s, n->a); break;
    case E_BOOLOP: case E_LIST: case E_TUPLE: case E_SET: case E_FSTRING:
        for (int i = 0; i < n->n[0]; i++) sym_expr(c, s, n->l[0][i]);
        break;
    case E_COMPARE: case E_CALL:
        sym_expr(c, s, n->a);
        for (int i = 0; i < n->n[0]; i++) sym_expr(c, s, n->l[0][i]);
        break;
    case E_SUBSCR: case E_FORMATTED: sym_expr(c, s, n->a); sym_expr(c, s, n->b); break;
    case E_SLICE: case E_IFEXP: sym_expr(c, s, n->a); sym_expr(c, s, n->b); sym_expr(c, s, n->c); break;
    case E_DICT:
        for (int i = 0; i < n->n[0]; i++) { sym_expr(c, s, n->l[0][i]); sym_expr(c, s, n->l[1][i]); }
        break;
    case E_LAMBDA: {
        Scope *in = scope_new(SC_FUNCTION, s);
        n->scope = in;
        sym_params(c, s, in, n->params);
        sym_expr(c, in, n->a);
        break;
    }
    case E_GENERIC: {
        Scope *in = scope_new(SC_FUNCTION, s);
        in->transparent = true;
        n->scope = in;
        sym_params(c, s, in, n->params);
        sym_stmts(c, in, n->l[0], n->n[0]);
        break;
    }
    case E_LISTCOMP: case E_SETCOMP: case E_DICTCOMP: case E_GENEXP: sym_comp(c, s, n); break;
    case E_YIELD: case E_YIELDFROM:
        if (s->type != SC_FUNCTION) cerror(c, n->line, "'yield' outside function");
        if (s->comp && !s->generator) cerror(c, n->line, "'yield' inside a comprehension");
        s->generator = true;
        sym_expr(c, s, n->a);
        break;
    case E_NAMED: {
        Str *name = AS_STR(n->a->val);
        Scope *t = s;
        while (t->comp) t = t->parent;
        if (t != s) {
            if (t->type == SC_CLASS) cerror(c, n->line, "assignment expression within a comprehension cannot be used in a class body");
            sym_add(t, name, F_BOUND);
            for (Scope *q = s; q != t; q = q->parent) sym_add(q, name, t->type == SC_MODULE ? F_GLOBAL : F_NONLOCAL);
        } else sym_add(s, name, F_BOUND);
        sym_expr(c, s, n->b);
        break;
    }
    default: break;
    }
}

/* ---------- generic defs and classes ----------
   "def f[T](x): ..." runs as if written

       def <generic parameters of f>(.d0):
           T = <type parameter "T">
           def f(x=.d0): ...
           f.__type_params__ = (T,)
           return f
       f = <generic parameters of f>(default values)

   so T is visible to the function's annotations and to nothing outside. */

static Node *nd(int kind, int line) {
    Node *n = arena_alloc(sizeof(Node));
    n->kind = kind; n->line = line;
    return n;
}
static Node *nd_name(const char *s, int line) { Node *n = nd(E_NAME, line); n->val = str_c(s); return n; }
static Node *nd_const(Obj *v, int line) { Node *n = nd(E_CONST, line); n->val = v; return n; }
static Node **nd_array(int n) { return arena_alloc((size_t)(n + 1) * sizeof(Node *)); }
static Node *nd_call(Node *f, Node **args, int n, int line) {
    Node *c = nd(E_CALL, line);
    c->a = f; c->l[0] = args; c->n[0] = n;
    return c;
}
static Node *nd_assign(Node *target, Node *value, int line) {
    Node *a = nd(S_ASSIGN, line);
    a->l[0] = nd_array(1); a->l[0][0] = target; a->n[0] = 1; a->a = value;
    return a;
}

static Node *generic_wrap(Node *n) {
    bool is_def = n->kind == S_DEF;
    int line = n->line, ti = is_def ? 2 : 3, nt = n->n[ti];
    Node **tps = n->l[ti];
    const char *name = AS_STR(n->val)->s;
    Node *inner = nd(n->kind, line);
    *inner = *n;
    inner->n[1] = 0; inner->n[ti] = 0; inner->c = NULL;
    /* Inside the hidden function the new object is held under a name no program can
       spell. A recursive call by name then finds the outer, decorated binding, as in CPython. */
    inner->val2 = str_c(".generic");
    Params *hp = arena_alloc(sizeof(Params));
    Node **call_args = NULL;
    int nargs = 0;
    if (is_def) {
        /* Default values are worked out outside and passed in. */
        Params *op = n->params, *ip = arena_alloc(sizeof(Params));
        *ip = *op;
        ip->defaults = nd_array(op->n); ip->kwdefaults = nd_array(op->nkwonly);
        hp->names = arena_alloc((size_t)(op->n + op->nkwonly + 1) * sizeof(Str *));
        hp->defaults = nd_array(op->n + op->nkwonly);
        hp->ann = nd_array(op->n + op->nkwonly);
        call_args = nd_array(op->n + op->nkwonly);
        for (int i = 0; i < op->n + op->nkwonly; i++) {
            Node *d = i < op->n ? op->defaults[i] : op->kwdefaults[i - op->n];
            if (!d) continue;
            Str *hidden = AS_STR(str_fmt(".d%d", nargs));
            hp->names[nargs] = hidden;
            call_args[nargs++] = d;
            Node *ref = nd(E_NAME, d->line);
            ref->val = (Obj *)hidden;
            if (i < op->n) ip->defaults[i] = ref; else ip->kwdefaults[i - op->n] = ref;
        }
        hp->n = nargs;
        inner->params = ip;
    } else {
        /* class C[T](Base) also inherits from Generic[T]. */
        Node **targs = nd_array(nt);
        for (int i = 0; i < nt; i++) { targs[i] = nd(E_NAME, line); targs[i]->val = tps[i]->val; }
        Node **bases = nd_array(n->n[2] + 1);
        for (int i = 0; i < n->n[2]; i++) bases[i] = n->l[2][i];
        bases[n->n[2]] = nd_call(nd_name(".generic_base", line), targs, nt, line);
        inner->l[2] = bases; inner->n[2] = n->n[2] + 1;
    }
    if (!hp->names) {
        hp->names = arena_alloc(sizeof(Str *)); hp->defaults = nd_array(0); hp->ann = nd_array(0);
    }
    hp->kwonly = arena_alloc(sizeof(Str *)); hp->kwdefaults = nd_array(0); hp->kwann = nd_array(0);

    Node **body = nd_array(nt + 3);
    Node **tnames = nd_array(nt);
    for (int i = 0; i < nt; i++) {
        Node **a = nd_array(2);
        a[0] = nd_const(tps[i]->val, line);
        a[1] = nd_const(int_new(tps[i]->op), line);
        Node *target = nd(E_NAME, line);
        target->val = tps[i]->val;
        body[i] = nd_assign(target, nd_call(nd_name(".type_param", line), a, 2, line), line);
        tnames[i] = nd(E_NAME, line);
        tnames[i]->val = tps[i]->val;
    }
    body[nt] = inner;
    Node *self = nd(E_NAME, line);
    self->val = inner->val2;
    Node *attr = nd(E_ATTR, line);
    attr->a = self; attr->val = str_c("__type_params__");
    Node *tuple = nd(E_TUPLE, line);
    tuple->l[0] = tnames; tuple->n[0] = nt;
    body[nt + 1] = nd_assign(attr, tuple, line);
    Node *ret = nd(S_RETURN, line);
    ret->a = nd(E_NAME, line);
    ret->a->val = inner->val2;
    body[nt + 2] = ret;

    Node *hidden = nd(E_GENERIC, line);
    hidden->val = str_fmt("<generic parameters of %s>", name);
    hidden->params = hp;
    hidden->l[0] = body; hidden->n[0] = nt + 3;
    Node *value = nd_call(hidden, call_args, nargs, line);
    for (int i = n->n[1] - 1; i >= 0; i--) {
        Node **a = nd_array(1);
        a[0] = value;
        value = nd_call(n->l[1][i], a, 1, n->l[1][i]->line);
    }
    Node *target = nd(E_NAME, line);
    target->val = n->val;
    return nd_assign(target, value, line);
}

/* ---------- match patterns: names ---------- */

static void pattern_names(C *c, Node *p, Names *out) {
    if (!p) return;
    Str *name = NULL;
    switch (p->kind) {
    case P_CAPTURE: case P_STAR: name = AS_STR(p->val); break;
    case P_AS: pattern_names(c, p->a, out); name = AS_STR(p->val); break;
    case P_SEQ: for (int i = 0; i < p->n[0]; i++) pattern_names(c, p->l[0][i], out); break;
    case P_MAP:
        for (int i = 0; i < p->n[1]; i++) pattern_names(c, p->l[1][i], out);
        name = AS_STR(p->val);
        break;
    case P_CLASS:
        for (int i = 0; i < p->n[0]; i++) pattern_names(c, p->l[0][i], out);
        for (int i = 0; i < p->n[1]; i++) pattern_names(c, p->l[1][i], out);
        break;
    case P_OR: {
        Names first = {0};
        pattern_names(c, p->l[0][0], &first);
        for (int i = 1; i < p->n[0]; i++) {
            Names alt = {0};
            pattern_names(c, p->l[0][i], &alt);
            bool same = alt.n == first.n;
            for (int j = 0; same && j < alt.n; j++) if (names_find(&first, alt.v[j]) < 0) same = false;
            if (!same) cerror(c, p->line, "alternative patterns bind different names");
        }
        for (int i = 0; i < first.n; i++) {
            if (names_find(out, first.v[i]) >= 0) cerror(c, p->line, "multiple assignments to name '%s' in pattern", first.v[i]->s);
            names_add(out, first.v[i]);
        }
        break;
    }
    default: break;
    }
    if (name) {
        if (names_find(out, name) >= 0) cerror(c, p->line, "multiple assignments to name '%s' in pattern", name->s);
        names_add(out, name);
    }
}

static void sym_pattern(C *c, Scope *s, Node *p) {
    switch (p->kind) {
    case P_VALUE: sym_expr(c, s, p->a); break;
    case P_CAPTURE: case P_STAR: if (p->val) sym_add(s, AS_STR(p->val), F_BOUND); break;
    case P_AS: sym_pattern(c, s, p->a); sym_add(s, AS_STR(p->val), F_BOUND); break;
    case P_SEQ: case P_OR: for (int i = 0; i < p->n[0]; i++) sym_pattern(c, s, p->l[0][i]); break;
    case P_MAP:
        for (int i = 0; i < p->n[0]; i++) { sym_expr(c, s, p->l[0][i]); sym_pattern(c, s, p->l[1][i]); }
        if (p->val) sym_add(s, AS_STR(p->val), F_BOUND);
        break;
    case P_CLASS:
        sym_expr(c, s, p->a);
        for (int i = 0; i < p->n[0]; i++) sym_pattern(c, s, p->l[0][i]);
        for (int i = 0; i < p->n[1]; i++) sym_pattern(c, s, p->l[1][i]);
        break;
    default: break;
    }
}

/* A pattern that matches every value. */
static bool irrefutable(Node *p) {
    if (p->kind == P_CAPTURE) return true;
    if (p->kind == P_AS) return irrefutable(p->a);
    if (p->kind == P_OR) for (int i = 0; i < p->n[0]; i++) if (irrefutable(p->l[0][i])) return true;
    return false;
}

static void sym_stmt(C *c, Scope *s, Node *n) {
    switch (n->kind) {
    case S_EXPR: sym_expr(c, s, n->a); break;
    case S_ASSIGN:
        sym_expr(c, s, n->a);
        for (int i = 0; i < n->n[0]; i++) sym_target(c, s, n->l[0][i]);
        break;
    case S_AUGASSIGN: sym_expr(c, s, n->a); sym_target(c, s, n->a); sym_expr(c, s, n->b); break;
    case S_ANNASSIGN:
        if (n->b) { sym_expr(c, s, n->b); sym_target(c, s, n->a); }
        else if (n->a->kind == E_NAME && s->type == SC_FUNCTION) sym_add(s, AS_STR(n->a->val), F_BOUND);
        if (s->type == SC_CLASS && n->a->kind == E_NAME) {
            /* Class bodies record annotations in __annotations__. */
            if (!c->future_ann) sym_expr(c, s, n->c);
            sym_add(s, N_annotations, F_BOUND | F_USED);
        }
        break;
    case S_RETURN: sym_expr(c, s, n->a); break;
    case S_IF: case S_WHILE:
        sym_expr(c, s, n->a);
        sym_stmts(c, s, n->l[0], n->n[0]);
        sym_stmts(c, s, n->l[1], n->n[1]);
        break;
    case S_FOR:
        sym_expr(c, s, n->b);
        sym_target(c, s, n->a);
        sym_stmts(c, s, n->l[0], n->n[0]);
        sym_stmts(c, s, n->l[1], n->n[1]);
        break;
    case S_DEF: {
        if (n->n[2]) { n->c = generic_wrap(n); sym_stmt(c, s, n->c); break; }
        sym_add(s, AS_STR(n->val2 ? n->val2 : n->val), F_BOUND);
        for (int i = 0; i < n->n[1]; i++) sym_expr(c, s, n->l[1][i]);
        if (!c->future_ann) {
            Params *p = n->params;
            for (int i = 0; i < p->n; i++) sym_expr(c, s, p->ann[i]);
            for (int i = 0; i < p->nkwonly; i++) sym_expr(c, s, p->kwann[i]);
            sym_expr(c, s, p->vararg_ann); sym_expr(c, s, p->kwarg_ann); sym_expr(c, s, n->b);
        }
        Scope *in = scope_new(SC_FUNCTION, s);
        n->scope = in;
        sym_params(c, s, in, n->params);
        sym_stmts(c, in, n->l[0], n->n[0]);
        break;
    }
    case S_MATCH:
        sym_expr(c, s, n->a);
        for (int i = 0; i < n->n[0]; i++) {
            Node *k = n->l[0][i];
            Names seen = {0};
            pattern_names(c, k->a, &seen);
            if (!k->b && i + 1 < n->n[0] && irrefutable(k->a)) {
                Node *p = k->a;
                while (p->kind == P_AS) p = p->a;
                if (p->kind == P_CAPTURE && p->val)
                    cerror(c, k->line, "name capture '%s' makes remaining patterns unreachable", AS_STR(p->val)->s);
                cerror(c, k->line, "wildcard makes remaining patterns unreachable");
            }
            sym_pattern(c, s, k->a);
            sym_expr(c, s, k->b);
            sym_stmts(c, s, k->l[0], k->n[0]);
        }
        break;
    case S_CLASS: {
        if (n->n[3]) { n->c = generic_wrap(n); sym_stmt(c, s, n->c); break; }
        sym_add(s, AS_STR(n->val2 ? n->val2 : n->val), F_BOUND);
        for (int i = 0; i < n->n[1]; i++) sym_expr(c, s, n->l[1][i]);
        for (int i = 0; i < n->n[2]; i++) sym_expr(c, s, n->l[2][i]);
        Scope *in = scope_new(SC_CLASS, s);
        n->scope = in;
        sym_stmts(c, in, n->l[0], n->n[0]);
        break;
    }
    case S_TRY:
        sym_stmts(c, s, n->l[0], n->n[0]);
        for (int i = 0; i < n->n[1]; i++) {
            Node *h = n->l[1][i];
            sym_expr(c, s, h->a);
            if (h->val) sym_add(s, AS_STR(h->val), F_BOUND);
            sym_stmts(c, s, h->l[0], h->n[0]);
        }
        sym_stmts(c, s, n->l[2], n->n[2]);
        sym_stmts(c, s, n->l[3], n->n[3]);
        break;
    case S_RAISE: case S_ASSERT: sym_expr(c, s, n->a); sym_expr(c, s, n->b); break;
    case S_WITH:
        for (int i = 0; i < n->n[0]; i++) {
            sym_expr(c, s, n->l[0][i]->a);
            if (n->l[0][i]->b) sym_target(c, s, n->l[0][i]->b);
        }
        sym_stmts(c, s, n->l[1], n->n[1]);
        break;
    case S_DEL: for (int i = 0; i < n->n[0]; i++) sym_target(c, s, n->l[0][i]); break;
    case S_GLOBAL: case S_NONLOCAL:
        for (int i = 0; i < n->n[0]; i++) {
            Str *name = AS_STR(n->l[0][i]->val);
            Sym *e = sym_find(s, name);
            if (e && (e->flags & F_PARAM)) cerror(c, n->line, "name '%s' is parameter and %s", name->s, n->kind == S_GLOBAL ? "global" : "nonlocal");
            if (n->kind == S_NONLOCAL && s->type == SC_MODULE) cerror(c, n->line, "nonlocal declaration not allowed at module level");
            sym_add(s, name, n->kind == S_GLOBAL ? F_GLOBAL : F_NONLOCAL);
        }
        break;
    case S_IMPORT:
        for (int i = 0; i < n->n[0]; i++) {
            Node *a = n->l[0][i];
            if (a->val2) sym_add(s, AS_STR(a->val2), F_BOUND);
            else {
                Str *full = AS_STR(a->val);
                const char *dot = memchr(full->s, '.', full->len);
                sym_add(s, dot ? AS_STR(str_new(full->s, (size_t)(dot - full->s))) : full, F_BOUND);
            }
        }
        break;
    case S_IMPORTFROM:
        if (n->op && s->type != SC_MODULE) cerror(c, n->line, "import * only allowed at module level");
        for (int i = 0; i < n->n[0]; i++) {
            Node *a = n->l[0][i];
            sym_add(s, AS_STR(a->val2 ? a->val2 : a->val), F_BOUND);
        }
        break;
    default: break;
    }
}

static void sym_stmts(C *c, Scope *s, Node **l, int n) {
    for (int i = 0; i < n; i++) sym_stmt(c, s, l[i]);
}

static void lookup_free(C *c, Scope *s, Str *name, bool nonlocal) {
    Scope *chain[128];
    int nchain = 0;
    chain[nchain++] = s;
    for (Scope *p = s->parent; p; p = p->parent) {
        if (nchain >= 127) cerror(c, 0, "functions nested too deeply");
        if (p->type == SC_MODULE) break;
        if (p->type == SC_CLASS) {
            if (!nonlocal && str_eq(name, N_class)) {
                names_add(&p->cells, name);
                for (int i = 0; i < nchain; i++) names_add(&chain[i]->frees, name);
                return;
            }
            chain[nchain++] = p;
            continue;
        }
        Sym *ps = sym_find(p, name);
        if (ps && (ps->flags & F_GLOBAL)) break;
        if (ps && (ps->flags & F_BOUND) && !(ps->flags & F_NONLOCAL)) {
            names_add(&p->cells, name);
            for (int i = 0; i < nchain; i++) names_add(&chain[i]->frees, name);
            return;
        }
        chain[nchain++] = p;
    }
    if (nonlocal) cerror(c, 0, "no binding for nonlocal '%s' found", name->s);
}

static void analyze(C *c, Scope *s) {
    if (s->type != SC_MODULE)
        for (int i = 0; i < s->nsyms; i++) {
            int f = s->syms[i].flags;
            if (f & F_GLOBAL) continue;
            if ((f & F_BOUND) && !(f & F_NONLOCAL)) {
                if (s->type == SC_FUNCTION) names_add(&s->vars, s->syms[i].name);
                continue;
            }
            lookup_free(c, s, s->syms[i].name, (f & F_NONLOCAL) != 0);
        }
    for (int i = 0; i < s->nkids; i++) analyze(c, s->kids[i]);
}

/* ---------- code emission ---------- */
static void unit_begin(C *c, Scope *scope, Str *name, int line) {
    Unit *u = calloc(1, sizeof(Unit));
    u->parent = c->u;
    u->scope = scope;
    u->consts = (List *)list_new(0);
    u->names = (List *)list_new(0);
    u->name = name;
    u->line = u->firstline = line;
    Unit *par = c->u;
    while (par && par->scope->transparent) par = par->parent;
    if (!par || par->scope->type == SC_MODULE) u->qualname = name;
    else if (par->scope->type == SC_CLASS) u->qualname = AS_STR(str_fmt("%s.%s", par->qualname->s, name->s));
    else u->qualname = AS_STR(str_fmt("%s.<locals>.%s", par->qualname->s, name->s));
    c->u = u;
    unit_chain = u;
}

static int emit(C *c, int op, int arg) {
    Unit *u = c->u;
    if (u->n == u->cap) {
        u->cap = u->cap ? u->cap * 2 : 64;
        u->code = realloc(u->code, (size_t)u->cap * sizeof *u->code);
        u->lines = realloc(u->lines, (size_t)u->cap * sizeof *u->lines);
    }
    if (arg < 0 || arg >= (1 << 24)) cerror(c, u->line, "code too large");
    u->code[u->n] = INSTR(op, arg);
    u->lines[u->n] = u->line;
    return u->n++;
}
#define EMIT(op, arg) emit(c, OP_##op##_, arg)
static int emit_jump(C *c, int op) { return emit(c, op, 0); }
#define JUMP_TO(op) emit_jump(c, OP_##op##_)
static void patch(C *c, int at) { c->u->code[at] = INSTR(I_OP(c->u->code[at]), c->u->n); }

static int add_const(C *c, Obj *v) {
    List *l = c->u->consts;
    for (int64_t i = 0; i < l->n; i++) {
        Obj *o = l->items[i];
        if (o == v) return (int)i;
        if (o->type != v->type) continue;
        if (IS_STR(o) && str_eq(AS_STR(o), AS_STR(v))) return (int)i;
        if (KIND(o) == K_INT && int_cmp(o, v) == 0) return (int)i;
    }
    list_append(l, v);
    return (int)l->n - 1;
}
static void load_const(C *c, Obj *v) { EMIT(LOAD_CONST, add_const(c, v)); }

static int add_name(C *c, Str *name) {
    List *l = c->u->names;
    for (int64_t i = 0; i < l->n; i++) if (str_eq(AS_STR(l->items[i]), name)) return (int)i;
    list_append(l, (Obj *)name);
    return (int)l->n - 1;
}

static int stack_effect(int op, int arg, bool jump) {
    switch (op) {
    case OP_POP_TOP_: case OP_STORE_FAST_: case OP_STORE_DEREF_: case OP_STORE_GLOBAL_: case OP_STORE_NAME_:
    case OP_DELETE_ATTR_: case OP_BINARY_SUBSCR_: case OP_BINARY_OP_: case OP_INPLACE_OP_: case OP_COMPARE_OP_:
    case OP_IS_OP_: case OP_CONTAINS_OP_: case OP_POP_JUMP_IF_FALSE_: case OP_POP_JUMP_IF_TRUE_:
    case OP_LIST_APPEND_: case OP_SET_ADD_: case OP_LIST_EXTEND_: case OP_SET_UPDATE_: case OP_DICT_UPDATE_:
    case OP_DICT_MERGE_: case OP_RETURN_VALUE_: case OP_YIELD_FROM_: case OP_POP_EXCEPT_: case OP_RERAISE_:
    case OP_EXC_MATCH_: case OP_WITH_EXIT_: case OP_IMPORT_STAR_: case OP_PRINT_EXPR_:
        return -1;
    case OP_DUP_TOP_: case OP_LOAD_CONST_: case OP_LOAD_FAST_: case OP_LOAD_DEREF_: case OP_LOAD_CLOSURE_:
    case OP_LOAD_CLASSDEREF_: case OP_LOAD_GLOBAL_: case OP_LOAD_NAME_: case OP_LOAD_METHOD_: case OP_PUSH_NULL_:
    case OP_BEFORE_WITH_: case OP_WITH_EXCEPT_: case OP_IMPORT_NAME_: case OP_IMPORT_FROM_:
        return 1;
    case OP_DUP_TOP_TWO_: return 2;
    case OP_STORE_ATTR_: case OP_DELETE_SUBSCR_: case OP_MAP_ADD_: return -2;
    case OP_STORE_SUBSCR_: return -3;
    case OP_JUMP_IF_FALSE_OR_POP_: case OP_JUMP_IF_TRUE_OR_POP_: return jump ? 0 : -1;
    case OP_FOR_ITER_: return jump ? -1 : 1;
    case OP_SETUP_TRY_: return jump ? 1 : 0;
    case OP_BUILD_TUPLE_: case OP_BUILD_LIST_: case OP_BUILD_SET_: case OP_BUILD_STRING_: case OP_BUILD_SLICE_:
        return 1 - arg;
    case OP_BUILD_MAP_: return 1 - 2 * arg;
    case OP_UNPACK_SEQUENCE_: return arg - 1;
    case OP_UNPACK_EX_: return (arg & 0xFF) + (arg >> 8);
    case OP_FORMAT_VALUE_: return (arg & 4) ? -1 : 0;
    case OP_CALL_: case OP_BUILD_CLASS_: return -(arg + 1);
    case OP_CALL_KW_: return -(arg + 2);
    case OP_CALL_EX_: return -(2 + (arg & 1));
    case OP_MAKE_FUNCTION_: return -(((arg & 1) != 0) + ((arg & 2) != 0) + ((arg & 4) != 0) + ((arg & 8) != 0));
    case OP_MATCH_SEQ_: case OP_MATCH_MAP_: return 1;
    case OP_MATCH_CLASS_: return -2;
    case OP_RAISE_: return -arg;
    default: return 0;
    }
}

static int compute_stacksize(C *c, Unit *u) {
    int n = u->n, max = 0, nwork = 0;
    int *depth = malloc((size_t)n * sizeof(int)), *work = malloc((size_t)n * sizeof(int) * 2 + 8);
    for (int i = 0; i < n; i++) depth[i] = -1;
    depth[0] = 0;
    work[nwork++] = 0;
    while (nwork) {
        int i = work[--nwork];
        for (;;) {
            int d = depth[i], op = I_OP(u->code[i]), arg = I_ARG(u->code[i]);
            bool is_jump = op == OP_JUMP_ || op == OP_POP_JUMP_IF_FALSE_ || op == OP_POP_JUMP_IF_TRUE_ ||
                           op == OP_JUMP_IF_FALSE_OR_POP_ || op == OP_JUMP_IF_TRUE_OR_POP_ || op == OP_FOR_ITER_ ||
                           op == OP_SETUP_TRY_;
            if (is_jump) {
                int td = d + stack_effect(op, arg, true);
                if (td > max) max = td;
                if (arg < n && depth[arg] < td) { depth[arg] = td; if (nwork < 2 * n) work[nwork++] = arg; }
            }
            if (op == OP_JUMP_ || op == OP_RETURN_VALUE_ || op == OP_RERAISE_ || op == OP_RAISE_) break;
            int nd = d + stack_effect(op, arg, false);
            if (nd < 0) { free(depth); free(work); cerror(c, u->lines[i], "internal error: stack underflow in compiler"); }
            if (nd > max) max = nd;
            if (i + 1 >= n || depth[i + 1] >= nd) break;
            depth[++i] = nd;
        }
    }
    free(depth); free(work);
    return max;
}

static Tuple *names_tuple(Names *a, Names *b) {
    Tuple *t = (Tuple *)tuple_new(a->n + (b ? b->n : 0));
    for (int i = 0; i < a->n; i++) t->items[i] = (Obj *)a->v[i];
    if (b) for (int i = 0; i < b->n; i++) t->items[a->n + i] = (Obj *)b->v[i];
    return t;
}

static Code *unit_finish(C *c) {
    Unit *u = c->u;
    Scope *s = u->scope;
    Code *co = gc_alloc(T_code, sizeof(Code));
    co->name = u->name; co->qualname = u->qualname; co->filename = c->fname;
    co->stacksize = compute_stacksize(c, u) + 2;
    co->code = u->code; co->lines = u->lines; co->ncode = u->n;
    co->consts = (Tuple *)tuple_from(u->consts->items, u->consts->n);
    co->names = (Tuple *)tuple_from(u->names->items, u->names->n);
    co->varnames = names_tuple(&s->vars, NULL);
    co->cellnames = names_tuple(&s->cells, &s->frees);
    co->nlocals = s->vars.n; co->ncells = s->cells.n; co->nfree = s->frees.n;
    co->firstline = u->firstline;
    co->maxblocks = u->maxblocks + 1;
    if (s->generator) co->flags |= CO_GENERATOR;
    if (s->type == SC_CLASS) co->flags |= CO_CLASSBODY;
    if (s->cells.n) {
        co->cell2arg = malloc((size_t)s->cells.n * sizeof(int));
        for (int i = 0; i < s->cells.n; i++) {
            Sym *sym = sym_find(s, s->cells.v[i]);
            co->cell2arg[i] = (sym && (sym->flags & F_PARAM)) ? names_find(&s->vars, s->cells.v[i]) : -1;
        }
    }
    c->u = u->parent;
    unit_chain = c->u;
    free(u);
    return co;
}

/* ---------- names ---------- */
enum { CTX_LOAD, CTX_STORE, CTX_DEL };

static void name_op(C *c, Str *name, int ctx) {
    Scope *s = c->u->scope;
    Sym *sym = s->type == SC_MODULE ? NULL : sym_find(s, name);
    int i;
    if (s->type == SC_FUNCTION) {
        if ((i = names_find(&s->cells, name)) >= 0 || ((i = names_find(&s->frees, name)) >= 0 && (i += s->cells.n, true))) {
            emit(c, ctx == CTX_LOAD ? OP_LOAD_DEREF_ : ctx == CTX_STORE ? OP_STORE_DEREF_ : OP_DELETE_DEREF_, i);
            return;
        }
        if (sym && (sym->flags & F_BOUND) && !(sym->flags & F_GLOBAL)) {
            i = names_find(&s->vars, name);
            emit(c, ctx == CTX_LOAD ? OP_LOAD_FAST_ : ctx == CTX_STORE ? OP_STORE_FAST_ : OP_DELETE_FAST_, i);
            return;
        }
    } else if (s->type == SC_CLASS && !(sym && (sym->flags & F_GLOBAL))) {
        bool local = sym && (sym->flags & F_BOUND) && !(sym->flags & F_NONLOCAL);
        if (!local && (i = names_find(&s->frees, name)) >= 0) {
            i += s->cells.n;
            if (ctx == CTX_LOAD) { EMIT(LOAD_CLASSDEREF, i | (add_name(c, name) << 8)); return; }
            emit(c, ctx == CTX_STORE ? OP_STORE_DEREF_ : OP_DELETE_DEREF_, i);
            return;
        }
        emit(c, ctx == CTX_LOAD ? OP_LOAD_NAME_ : ctx == CTX_STORE ? OP_STORE_NAME_ : OP_DELETE_NAME_, add_name(c, name));
        return;
    }
    emit(c, ctx == CTX_LOAD ? OP_LOAD_GLOBAL_ : ctx == CTX_STORE ? OP_STORE_GLOBAL_ : OP_DELETE_GLOBAL_, add_name(c, name));
}

static void load_closure(C *c, Str *name) {
    Scope *s = c->u->scope;
    int i = names_find(&s->cells, name);
    if (i < 0) { i = names_find(&s->frees, name); if (i >= 0) i += s->cells.n; }
    if (i < 0) cerror(c, c->u->line, "internal error: closure variable '%s' not found", name->s);
    EMIT(LOAD_CLOSURE, i);
}

/* ---------- expressions ---------- */
static void expr(C *c, Node *n);
static void stmts(C *c, Node **l, int n);
static void store(C *c, Node *t);

static void seq_with_stars(C *c, Node *n, int kind) {
    bool star = false;
    for (int i = 0; i < n->n[0]; i++) if (n->l[0][i]->kind == E_STARRED) star = true;
    if (!star) {
        if (kind == E_TUPLE && n->n[0] > 0) {
            bool all_const = true;
            for (int i = 0; i < n->n[0]; i++) if (n->l[0][i]->kind != E_CONST) all_const = false;
            if (all_const) {
                Tuple *t = (Tuple *)tuple_new(n->n[0]);
                for (int i = 0; i < n->n[0]; i++) t->items[i] = n->l[0][i]->val;
                load_const(c, (Obj *)t);
                return;
            }
        }
        for (int i = 0; i < n->n[0]; i++) expr(c, n->l[0][i]);
        emit(c, kind == E_TUPLE ? OP_BUILD_TUPLE_ : kind == E_LIST ? OP_BUILD_LIST_ : OP_BUILD_SET_, n->n[0]);
        return;
    }
    emit(c, kind == E_SET ? OP_BUILD_SET_ : OP_BUILD_LIST_, 0);
    for (int i = 0; i < n->n[0]; i++) {
        Node *e = n->l[0][i];
        if (e->kind == E_STARRED) { expr(c, e->a); emit(c, kind == E_SET ? OP_SET_UPDATE_ : OP_LIST_EXTEND_, 1); }
        else { expr(c, e); emit(c, kind == E_SET ? OP_SET_ADD_ : OP_LIST_APPEND_, 1); }
    }
    if (kind == E_TUPLE) EMIT(LIST_TO_TUPLE, 0);
}

static void make_closure(C *c, Code *co, int flags, Scope *child) {
    if (child->frees.n) {
        for (int i = 0; i < child->frees.n; i++) load_closure(c, child->frees.v[i]);
        EMIT(BUILD_TUPLE, child->frees.n);
        flags |= 8;
    }
    load_const(c, (Obj *)co);
    EMIT(MAKE_FUNCTION, flags);
}

static Obj *docstring(Node **body, int n) {
    if (n > 0 && body[0]->kind == S_EXPR && body[0]->a->kind == E_CONST && IS_STR(body[0]->a->val)) return body[0]->a->val;
    return NULL;
}

/* Compiles a def or lambda and leaves the function object on the stack. */
static void function(C *c, Node *n, Str *name) {
    Params *p = n->params;
    int flags = 0, nd = 0;
    for (int i = 0; i < p->n; i++) if (p->defaults[i]) { expr(c, p->defaults[i]); nd++; }
    if (nd) { EMIT(BUILD_TUPLE, nd); flags |= 1; }
    nd = 0;
    for (int i = 0; i < p->nkwonly; i++)
        if (p->kwdefaults[i]) { load_const(c, (Obj *)p->kwonly[i]); expr(c, p->kwdefaults[i]); nd++; }
    if (nd) { EMIT(BUILD_MAP, nd); flags |= 2; }
    if (n->kind == S_DEF) {
        /* __annotations__: evaluated now, or kept as source text under the future import. */
        nd = 0;
#define ANN(nm, a) do { if (a) { load_const(c, (Obj *)(nm)); \
            if (c->future_ann) load_const(c, (Obj *)unparse_expr(a, c->filename)); else expr(c, a); nd++; } } while (0)
        for (int i = 0; i < p->n; i++) ANN(p->names[i], p->ann[i]);
        ANN(p->vararg, p->vararg_ann);
        for (int i = 0; i < p->nkwonly; i++) ANN(p->kwonly[i], p->kwann[i]);
        ANN(p->kwarg, p->kwarg_ann);
        ANN(str_c("return"), n->b);
#undef ANN
        if (nd) { EMIT(BUILD_MAP, nd); flags |= 4; }
    }
    unit_begin(c, n->scope, name, n->line);
    Obj *doc = NULL;
    if (n->kind == E_LAMBDA) {
        expr(c, n->a);
        EMIT(RETURN_VALUE, 0);
    } else {
        if (n->kind == S_DEF) doc = docstring(n->l[0], n->n[0]);
        stmts(c, n->l[0], n->n[0]);
        load_const(c, Py_None);
        EMIT(RETURN_VALUE, 0);
    }
    Scope *child = c->u->scope;
    Code *co = unit_finish(c);
    co->argcount = p->n; co->posonly = p->nposonly; co->kwonly = p->nkwonly; co->doc = doc;
    if (p->vararg) co->flags |= CO_VARARGS;
    if (p->kwarg) co->flags |= CO_VARKW;
    make_closure(c, co, flags, child);
}

static void comp_loops(C *c, Node *n, int gi, int depth) {
    Node *g = n->l[0][gi];
    if (gi == 0) EMIT(LOAD_FAST, 0);
    else { expr(c, g->b); EMIT(GET_ITER, 0); }
    int top = c->u->n, j_end = JUMP_TO(FOR_ITER);
    store(c, g->a);
    for (int i = 0; i < g->n[0]; i++) {
        expr(c, g->l[0][i]);
        emit(c, OP_POP_JUMP_IF_FALSE_, top);
    }
    if (gi + 1 < n->n[0]) comp_loops(c, n, gi + 1, depth + 1);
    else switch (n->kind) {
    case E_LISTCOMP: expr(c, n->a); EMIT(LIST_APPEND, depth + 1); break;
    case E_SETCOMP: expr(c, n->a); EMIT(SET_ADD, depth + 1); break;
    case E_DICTCOMP: expr(c, n->a); expr(c, n->b); EMIT(MAP_ADD, depth + 1); break;
    default: expr(c, n->a); EMIT(YIELD_VALUE, 0); EMIT(POP_TOP, 0);
    }
    emit(c, OP_JUMP_, top);
    patch(c, j_end);
}

static void comprehension(C *c, Node *n) {
    const char *nm = n->kind == E_LISTCOMP ? "<listcomp>" : n->kind == E_SETCOMP ? "<setcomp>" :
                     n->kind == E_DICTCOMP ? "<dictcomp>" : "<genexpr>";
    unit_begin(c, n->scope, AS_STR(str_c(nm)), n->line);
    if (n->kind == E_LISTCOMP) EMIT(BUILD_LIST, 0);
    else if (n->kind == E_SETCOMP) EMIT(BUILD_SET, 0);
    else if (n->kind == E_DICTCOMP) EMIT(BUILD_MAP, 0);
    comp_loops(c, n, 0, 1);
    if (n->kind == E_GENEXP) load_const(c, Py_None);
    EMIT(RETURN_VALUE, 0);
    Scope *child = c->u->scope;
    Code *co = unit_finish(c);
    co->argcount = 1;
    make_closure(c, co, 0, child);
    EMIT(PUSH_NULL, 0);
    expr(c, n->l[0][0]->b);
    EMIT(GET_ITER, 0);
    EMIT(CALL, 1);
}

static void call(C *c, Node *n) {
    bool star = false;
    int nkw = 0, npos = 0;
    for (int i = 0; i < n->n[0]; i++) {
        Node *a = n->l[0][i];
        if (a->kind == E_STARRED || (a->kind == E_KEYWORD && !a->val)) star = true;
        if (a->kind == E_KEYWORD) nkw++; else npos++;
    }
    if (n->a->kind == E_ATTR) { expr(c, n->a->a); c->u->line = n->line; EMIT(LOAD_METHOD, add_name(c, AS_STR(n->a->val))); }
    else { expr(c, n->a); EMIT(PUSH_NULL, 0); }
    if (!star) {
        for (int i = 0; i < n->n[0]; i++) if (n->l[0][i]->kind != E_KEYWORD) expr(c, n->l[0][i]);
        if (!nkw) { EMIT(CALL, npos); return; }
        Tuple *names = (Tuple *)tuple_new(nkw);
        int k = 0;
        for (int i = 0; i < n->n[0]; i++) {
            Node *a = n->l[0][i];
            if (a->kind != E_KEYWORD) continue;
            for (int j = 0; j < k; j++)
                if (str_eq(AS_STR(names->items[j]), AS_STR(a->val))) cerror(c, a->line, "keyword argument repeated: %s", AS_STR(a->val)->s);
            names->items[k++] = a->val;
            expr(c, a->a);
        }
        /* The names tuple is unique per call site, so add it directly. */
        list_append(c->u->consts, (Obj *)names);
        EMIT(LOAD_CONST, (int)c->u->consts->n - 1);
        EMIT(CALL_KW, npos + nkw);
        return;
    }
    EMIT(BUILD_LIST, 0);
    for (int i = 0; i < n->n[0]; i++) {
        Node *a = n->l[0][i];
        if (a->kind == E_KEYWORD) continue;
        if (a->kind == E_STARRED) { expr(c, a->a); EMIT(LIST_EXTEND, 1); }
        else { expr(c, a); EMIT(LIST_APPEND, 1); }
    }
    if (nkw) {
        EMIT(BUILD_MAP, 0);
        for (int i = 0; i < n->n[0]; i++) {
            Node *a = n->l[0][i];
            if (a->kind != E_KEYWORD) continue;
            if (!a->val) { expr(c, a->a); EMIT(DICT_MERGE, 1); }
            else { load_const(c, a->val); expr(c, a->a); EMIT(MAP_ADD, 1); }
        }
    }
    EMIT(CALL_EX, nkw ? 1 : 0);
}

static void expr(C *c, Node *n) {
    int saved_line = c->u->line;
    c->u->line = n->line;
    switch (n->kind) {
    case E_CONST: load_const(c, n->val); break;
    case E_NAME: name_op(c, AS_STR(n->val), CTX_LOAD); break;
    case E_BINOP: expr(c, n->a); expr(c, n->b); EMIT(BINARY_OP, n->op); break;
    case E_UNARY: expr(c, n->a); EMIT(UNARY_OP, n->op); break;
    case E_NOT: expr(c, n->a); EMIT(UNARY_NOT, 0); break;
    case E_BOOLOP: {
        JumpList *jl = NULL;
        for (int i = 0; i < n->n[0]; i++) {
            expr(c, n->l[0][i]);
            if (i + 1 < n->n[0]) {
                JumpList *j = arena_alloc(sizeof *j);
                j->at = emit_jump(c, n->op ? OP_JUMP_IF_TRUE_OR_POP_ : OP_JUMP_IF_FALSE_OR_POP_);
                j->next = jl;
                jl = j;
            }
        }
        for (; jl; jl = jl->next) patch(c, jl->at);
        break;
    }
    case E_COMPARE: {
        expr(c, n->a);
        JumpList *jl = NULL;
        for (int i = 0; i < n->n[0]; i++) {
            expr(c, n->l[0][i]);
            bool last = i + 1 == n->n[0];
            if (!last) { EMIT(DUP_TOP, 0); EMIT(ROT_THREE, 0); }
            int op = n->ops[i];
            if (op <= CMP_GE) EMIT(COMPARE_OP, op);
            else if (op == CMP_IN || op == CMP_NOT_IN) EMIT(CONTAINS_OP, op == CMP_NOT_IN);
            else EMIT(IS_OP, op == CMP_IS_NOT);
            if (!last) {
                JumpList *j = arena_alloc(sizeof *j);
                j->at = JUMP_TO(JUMP_IF_FALSE_OR_POP);
                j->next = jl;
                jl = j;
            }
        }
        if (jl) {
            int j_end = JUMP_TO(JUMP);
            for (; jl; jl = jl->next) patch(c, jl->at);
            EMIT(ROT_TWO, 0);
            EMIT(POP_TOP, 0);
            patch(c, j_end);
        }
        break;
    }
    case E_CALL: call(c, n); break;
    case E_ATTR: expr(c, n->a); c->u->line = n->line; EMIT(LOAD_ATTR, add_name(c, AS_STR(n->val))); break;
    case E_SUBSCR: expr(c, n->a); expr(c, n->b); c->u->line = n->line; EMIT(BINARY_SUBSCR, 0); break;
    case E_SLICE:
        if (n->a) expr(c, n->a); else load_const(c, Py_None);
        if (n->b) expr(c, n->b); else load_const(c, Py_None);
        if (n->c) expr(c, n->c);
        EMIT(BUILD_SLICE, n->c ? 3 : 2);
        break;
    case E_LIST: case E_TUPLE: case E_SET: seq_with_stars(c, n, n->kind); break;
    case E_DICT: {
        bool star = false;
        for (int i = 0; i < n->n[0]; i++) if (!n->l[0][i]) star = true;
        if (!star) {
            for (int i = 0; i < n->n[0]; i++) { expr(c, n->l[0][i]); expr(c, n->l[1][i]); }
            EMIT(BUILD_MAP, n->n[0]);
        } else {
            EMIT(BUILD_MAP, 0);
            for (int i = 0; i < n->n[0]; i++) {
                if (!n->l[0][i]) { expr(c, n->l[1][i]); EMIT(DICT_UPDATE, 1); }
                else { expr(c, n->l[0][i]); expr(c, n->l[1][i]); EMIT(MAP_ADD, 1); }
            }
        }
        break;
    }
    case E_IFEXP: {
        expr(c, n->a);
        int j_else = JUMP_TO(POP_JUMP_IF_FALSE);
        expr(c, n->b);
        int j_end = JUMP_TO(JUMP);
        patch(c, j_else);
        expr(c, n->c);
        patch(c, j_end);
        break;
    }
    case E_LAMBDA: function(c, n, AS_STR(str_c("<lambda>"))); break;
    case E_GENERIC: function(c, n, AS_STR(n->val)); break;
    case E_LISTCOMP: case E_SETCOMP: case E_DICTCOMP: case E_GENEXP: comprehension(c, n); break;
    case E_YIELD:
        if (n->a) expr(c, n->a); else load_const(c, Py_None);
        EMIT(YIELD_VALUE, 0);
        break;
    case E_YIELDFROM:
        expr(c, n->a);
        EMIT(GET_YIELD_FROM_ITER, 0);
        load_const(c, Py_None);
        EMIT(YIELD_FROM, 0);
        break;
    case E_STARRED: cerror(c, n->line, "can't use starred expression here");
    case E_NAMED: expr(c, n->b); EMIT(DUP_TOP, 0); name_op(c, AS_STR(n->a->val), CTX_STORE); break;
    case E_FSTRING:
        for (int i = 0; i < n->n[0]; i++) expr(c, n->l[0][i]);
        EMIT(BUILD_STRING, n->n[0]);
        break;
    case E_FORMATTED: {
        expr(c, n->a);
        int flags = n->op == 's' ? 1 : n->op == 'r' ? 2 : n->op == 'a' ? 3 : 0;
        if (n->b) { expr(c, n->b); flags |= 4; }
        EMIT(FORMAT_VALUE, flags);
        break;
    }
    default: cerror(c, n->line, "invalid expression");
    }
    c->u->line = saved_line;
}

static void store(C *c, Node *t) {
    switch (t->kind) {
    case E_NAME: name_op(c, AS_STR(t->val), CTX_STORE); break;
    case E_ATTR: expr(c, t->a); EMIT(STORE_ATTR, add_name(c, AS_STR(t->val))); break;
    case E_SUBSCR: expr(c, t->a); expr(c, t->b); EMIT(STORE_SUBSCR, 0); break;
    case E_TUPLE: case E_LIST: {
        int star = -1;
        for (int i = 0; i < t->n[0]; i++) if (t->l[0][i]->kind == E_STARRED) star = i;
        if (star >= 0) {
            if (star > 255 || t->n[0] - star - 1 > 65535) cerror(c, t->line, "too many expressions in star-unpacking assignment");
            EMIT(UNPACK_EX, star | ((t->n[0] - star - 1) << 8));
        } else EMIT(UNPACK_SEQUENCE, t->n[0]);
        for (int i = 0; i < t->n[0]; i++) store(c, t->l[0][i]->kind == E_STARRED ? t->l[0][i]->a : t->l[0][i]);
        break;
    }
    default: cerror(c, t->line, "cannot assign to expression");
    }
}

static void del_target(C *c, Node *t) {
    switch (t->kind) {
    case E_NAME: name_op(c, AS_STR(t->val), CTX_DEL); break;
    case E_ATTR: expr(c, t->a); EMIT(DELETE_ATTR, add_name(c, AS_STR(t->val))); break;
    case E_SUBSCR: expr(c, t->a); expr(c, t->b); EMIT(DELETE_SUBSCR, 0); break;
    case E_TUPLE: case E_LIST: for (int i = 0; i < t->n[0]; i++) del_target(c, t->l[0][i]); break;
    default: cerror(c, t->line, "cannot delete expression");
    }
}

/* ---------- statements ---------- */
static FB *push_fb(C *c, int type) {
    Unit *u = c->u;
    if (u->nfb >= MAXFB) cerror(c, u->line, "too many statically nested blocks");
    int blocks = 0;
    for (int i = 0; i < u->nfb; i++) if (u->fb[i].type != FB_WHILE && u->fb[i].type != FB_FOR) blocks++;
    if (type != FB_WHILE && type != FB_FOR && blocks + 1 > MAXBLOCKS - 1) cerror(c, u->line, "too many statically nested blocks");
    if (type != FB_WHILE && type != FB_FOR && blocks + 1 > u->maxblocks) u->maxblocks = blocks + 1;
    FB *fb = &u->fb[u->nfb++];
    memset(fb, 0, sizeof *fb);
    fb->type = type;
    return fb;
}
static void pop_fb(C *c) { c->u->nfb--; }

/* Emits the code that leaves block number i. If keep_tos, a return value sits on top. */
static void leave_fb(C *c, int i, bool keep_tos) {
    Unit *u = c->u;
    FB fb = u->fb[i];
    int saved = u->nfb;
    u->nfb = i;
    switch (fb.type) {
    case FB_WHILE: break;
    case FB_FOR: if (keep_tos) EMIT(ROT_TWO, 0); EMIT(POP_TOP, 0); break;
    case FB_TRY: EMIT(POP_BLOCK, 0); break;
    case FB_FINALLY_TRY: EMIT(POP_BLOCK, 0); stmts(c, fb.final, fb.nfinal); break;
    case FB_HANDLER: case FB_FINALLY_END: if (keep_tos) EMIT(ROT_TWO, 0); EMIT(POP_EXCEPT, 0); break;
    case FB_WITH: EMIT(POP_BLOCK, 0); if (keep_tos) EMIT(ROT_TWO, 0); EMIT(WITH_EXIT, 0); break;
    }
    u->nfb = saved;
}

static void try_except(C *c, Node *n) {
    int j_setup = JUMP_TO(SETUP_TRY);
    push_fb(c, FB_TRY);
    stmts(c, n->l[0], n->n[0]);
    pop_fb(c);
    EMIT(POP_BLOCK, 0);
    int j_else = JUMP_TO(JUMP);
    patch(c, j_setup);
    JumpList *ends = NULL;
    for (int i = 0; i < n->n[1]; i++) {
        Node *h = n->l[1][i];
        int j_next = -1;
        c->u->line = h->line;
        if (h->a) {
            EMIT(DUP_TOP, 0);
            expr(c, h->a);
            EMIT(EXC_MATCH, 0);
            j_next = JUMP_TO(POP_JUMP_IF_FALSE);
        }
        if (h->val) { EMIT(DUP_TOP, 0); name_op(c, AS_STR(h->val), CTX_STORE); }
        push_fb(c, FB_HANDLER);
        stmts(c, h->l[0], h->n[0]);
        pop_fb(c);
        EMIT(POP_EXCEPT, 0);
        if (h->val) {
            load_const(c, Py_None);
            name_op(c, AS_STR(h->val), CTX_STORE);
            name_op(c, AS_STR(h->val), CTX_DEL);
        }
        JumpList *j = arena_alloc(sizeof *j);
        j->at = JUMP_TO(JUMP);
        j->next = ends;
        ends = j;
        if (j_next >= 0) patch(c, j_next);
    }
    EMIT(RERAISE, 0);
    patch(c, j_else);
    stmts(c, n->l[2], n->n[2]);
    for (; ends; ends = ends->next) patch(c, ends->at);
}

static void with_stmt(C *c, Node *n, int idx) {
    Node *item = n->l[0][idx];
    expr(c, item->a);
    EMIT(BEFORE_WITH, 0);
    if (item->b) store(c, item->b); else EMIT(POP_TOP, 0);
    int j_setup = JUMP_TO(SETUP_TRY);
    push_fb(c, FB_WITH);
    if (idx + 1 < n->n[0]) with_stmt(c, n, idx + 1);
    else stmts(c, n->l[1], n->n[1]);
    pop_fb(c);
    EMIT(POP_BLOCK, 0);
    EMIT(WITH_EXIT, 0);
    int j_end = JUMP_TO(JUMP);
    patch(c, j_setup);
    EMIT(WITH_EXCEPT, 0);
    int j_suppress = JUMP_TO(POP_JUMP_IF_TRUE);
    EMIT(RERAISE, 0);
    patch(c, j_suppress);
    EMIT(POP_EXCEPT, 0);
    EMIT(POP_TOP, 0);
    patch(c, j_end);
}

static int find_loop(C *c, int line, const char *what) {
    for (int i = c->u->nfb - 1; i >= 0; i--)
        if (c->u->fb[i].type == FB_WHILE || c->u->fb[i].type == FB_FOR) return i;
    cerror(c, line, "'%s' outside loop", what);
}

static void patch_breaks(C *c, JumpList *jl) { for (; jl; jl = jl->next) patch(c, jl->at); }

/* ---------- match statement ----------
   A pattern is compiled with its subject on top of the stack and removes it.
   On a mismatch it jumps away with `depth` values still to be removed; the
   jumps are collected by that count and resolved to a run of POP_TOPs. */

#define MAXPD 64
typedef struct { JumpList *at[MAXPD]; } Fails;

static void pfail(C *c, Fails *f, int op, int depth, int line) {
    if (depth >= MAXPD) cerror(c, line, "pattern nested too deeply");
    JumpList *j = arena_alloc(sizeof *j);
    j->at = emit_jump(c, op);
    j->next = f->at[depth];
    f->at[depth] = j;
}

static void fails_resolve(C *c, Fails *f) {
    bool any = false;
    for (int k = MAXPD - 1; k >= 1; k--) {
        if (f->at[k]) { patch_breaks(c, f->at[k]); any = true; }
        if (any) EMIT(POP_TOP, 0);
    }
    patch_breaks(c, f->at[0]);
}

static void pattern(C *c, Node *p, Fails *f, int d);

/* Sub-patterns for n values on the stack, first value on top. */
static void subpatterns(C *c, Node **l, int n, int done, int total, Fails *f, int d) {
    for (int i = 0; i < n; i++) pattern(c, l[i], f, d + total - done - i);
}

static void pattern(C *c, Node *p, Fails *f, int d) {
    c->u->line = p->line;
    switch (p->kind) {
    case P_CAPTURE: case P_STAR:
        if (p->val) name_op(c, AS_STR(p->val), CTX_STORE); else EMIT(POP_TOP, 0);
        break;
    case P_VALUE:
        if (p->op) { load_const(c, p->a->val); EMIT(IS_OP, 0); }
        else { expr(c, p->a); EMIT(COMPARE_OP, CMP_EQ); }
        pfail(c, f, OP_POP_JUMP_IF_FALSE_, d - 1, p->line);
        break;
    case P_AS:
        EMIT(DUP_TOP, 0);
        pattern(c, p->a, f, d + 1);
        name_op(c, AS_STR(p->val), CTX_STORE);
        break;
    case P_OR: {
        JumpList *ends = NULL;
        for (int i = 0; i + 1 < p->n[0]; i++) {
            Fails g = {0};
            EMIT(DUP_TOP, 0);
            pattern(c, p->l[0][i], &g, 1);
            EMIT(POP_TOP, 0);
            JumpList *j = arena_alloc(sizeof *j);
            j->at = JUMP_TO(JUMP);
            j->next = ends;
            ends = j;
            fails_resolve(c, &g);
        }
        pattern(c, p->l[0][p->n[0] - 1], f, d);
        patch_breaks(c, ends);
        break;
    }
    case P_SEQ: {
        int n = p->n[0], star = -1;
        for (int i = 0; i < n; i++) if (p->l[0][i]->kind == P_STAR) star = i;
        if (n > 255) cerror(c, p->line, "too many items in sequence pattern");
        EMIT(MATCH_SEQ, ((star >= 0 ? n - 1 : n) << 1) | (star >= 0));
        pfail(c, f, OP_POP_JUMP_IF_FALSE_, d, p->line);
        if (star >= 0) EMIT(UNPACK_EX, star | ((n - 1 - star) << 8));
        else EMIT(UNPACK_SEQUENCE, n);
        subpatterns(c, p->l[0], n, 0, n, f, d - 1);
        break;
    }
    case P_MAP: {
        int n = p->n[0];
        EMIT(MATCH_MAP, 0);
        pfail(c, f, OP_POP_JUMP_IF_FALSE_, d, p->line);
        if (n) {
            for (int i = 0; i < n; i++) expr(c, p->l[0][i]);
            EMIT(BUILD_TUPLE, n);
            EMIT(MATCH_KEYS, 0);
            EMIT(DUP_TOP, 0);
            load_const(c, Py_None);
            EMIT(IS_OP, 0);
            pfail(c, f, OP_POP_JUMP_IF_TRUE_, d + 1, p->line);
            EMIT(UNPACK_SEQUENCE, n);
            subpatterns(c, p->l[1], n, 0, n, f, d);
        }
        if (p->val) {
            for (int i = 0; i < n; i++) expr(c, p->l[0][i]);
            EMIT(BUILD_TUPLE, n);
            EMIT(MATCH_KEYS, 1);
            name_op(c, AS_STR(p->val), CTX_STORE);
        }
        EMIT(POP_TOP, 0);
        break;
    }
    case P_CLASS: {
        int npos = p->n[0], nkw = p->n[1], total = npos + nkw;
        expr(c, p->a);
        Tuple *names = (Tuple *)tuple_new(nkw);
        for (int i = 0; i < nkw; i++) names->items[i] = p->l[2][i]->val;
        list_append(c->u->consts, (Obj *)names);
        EMIT(LOAD_CONST, (int)c->u->consts->n - 1);
        EMIT(MATCH_CLASS, npos);
        EMIT(DUP_TOP, 0);
        load_const(c, Py_None);
        EMIT(IS_OP, 0);
        pfail(c, f, OP_POP_JUMP_IF_TRUE_, d, p->line);
        EMIT(UNPACK_SEQUENCE, total);
        subpatterns(c, p->l[0], npos, 0, total, f, d - 1);
        subpatterns(c, p->l[1], nkw, npos, total, f, d - 1);
        break;
    }
    default: cerror(c, p->line, "invalid pattern");
    }
}

static void match_stmt(C *c, Node *n) {
    expr(c, n->a);
    JumpList *ends = NULL;
    for (int i = 0; i < n->n[0]; i++) {
        Node *k = n->l[0][i];
        Fails f = {0};
        EMIT(DUP_TOP, 0);
        pattern(c, k->a, &f, 1);
        if (k->b) { expr(c, k->b); pfail(c, &f, OP_POP_JUMP_IF_FALSE_, 0, k->line); }
        EMIT(POP_TOP, 0);
        stmts(c, k->l[0], k->n[0]);
        JumpList *j = arena_alloc(sizeof *j);
        j->at = JUMP_TO(JUMP);
        j->next = ends;
        ends = j;
        fails_resolve(c, &f);
    }
    EMIT(POP_TOP, 0);
    patch_breaks(c, ends);
}

static void class_def(C *c, Node *n) {
    Str *name = AS_STR(n->val);
    for (int i = 0; i < n->n[1]; i++) { expr(c, n->l[1][i]); EMIT(PUSH_NULL, 0); }
    unit_begin(c, n->scope, name, n->line);
    name_op(c, N_name, CTX_LOAD);
    name_op(c, N_module, CTX_STORE);
    load_const(c, (Obj *)c->u->qualname);
    name_op(c, N_qualname, CTX_STORE);
    Obj *doc = docstring(n->l[0], n->n[0]);
    if (doc) { load_const(c, doc); name_op(c, N_doc, CTX_STORE); }
    if (sym_find(c->u->scope, N_annotations)) { EMIT(BUILD_MAP, 0); name_op(c, N_annotations, CTX_STORE); }
    stmts(c, n->l[0], n->n[0]);
    if (names_find(&c->u->scope->cells, N_class) >= 0) EMIT(LOAD_CLOSURE, names_find(&c->u->scope->cells, N_class));
    else load_const(c, Py_None);
    EMIT(RETURN_VALUE, 0);
    Scope *child = c->u->scope;
    Code *co = unit_finish(c);
    make_closure(c, co, 0, child);
    load_const(c, (Obj *)name);
    for (int i = 0; i < n->n[2]; i++) {
        Node *b = n->l[2][i];
        if (b->kind == E_KEYWORD) cerror(c, b->line, "class keyword arguments (such as metaclass=) are not supported");
        if (b->kind == E_STARRED) cerror(c, b->line, "starred base classes are not supported");
        expr(c, b);
    }
    EMIT(BUILD_CLASS, n->n[2]);
    for (int i = 0; i < n->n[1]; i++) EMIT(CALL, 1);
    name_op(c, n->val2 ? AS_STR(n->val2) : name, CTX_STORE);
}

static void stmt(C *c, Node *n) {
    Unit *u = c->u;
    u->line = n->line;
    switch (n->kind) {
    case S_EXPR:
        if (c->mode == MODE_SINGLE && u->scope->type == SC_MODULE && !u->parent) { expr(c, n->a); EMIT(PRINT_EXPR, 0); }
        else if (n->a->kind != E_CONST) { expr(c, n->a); EMIT(POP_TOP, 0); }
        break;
    case S_ASSIGN:
        expr(c, n->a);
        for (int i = 0; i < n->n[0]; i++) {
            if (i + 1 < n->n[0]) EMIT(DUP_TOP, 0);
            store(c, n->l[0][i]);
        }
        break;
    case S_AUGASSIGN: {
        Node *t = n->a;
        if (t->kind == E_NAME) {
            name_op(c, AS_STR(t->val), CTX_LOAD);
            expr(c, n->b);
            EMIT(INPLACE_OP, n->op);
            name_op(c, AS_STR(t->val), CTX_STORE);
        } else if (t->kind == E_ATTR) {
            int ni = add_name(c, AS_STR(t->val));
            expr(c, t->a);
            EMIT(DUP_TOP, 0);
            EMIT(LOAD_ATTR, ni);
            expr(c, n->b);
            EMIT(INPLACE_OP, n->op);
            EMIT(ROT_TWO, 0);
            EMIT(STORE_ATTR, ni);
        } else {
            expr(c, t->a);
            expr(c, t->b);
            EMIT(DUP_TOP_TWO, 0);
            EMIT(BINARY_SUBSCR, 0);
            expr(c, n->b);
            EMIT(INPLACE_OP, n->op);
            EMIT(ROT_THREE, 0);
            EMIT(STORE_SUBSCR, 0);
        }
        break;
    }
    case S_ANNASSIGN:
        if (n->b) { expr(c, n->b); store(c, n->a); }
        if (u->scope->type == SC_CLASS && n->a->kind == E_NAME) {
            if (c->future_ann) load_const(c, (Obj *)unparse_expr(n->c, c->filename)); else expr(c, n->c);
            name_op(c, N_annotations, CTX_LOAD);
            load_const(c, n->a->val);
            EMIT(STORE_SUBSCR, 0);
        }
        break;
    case S_RETURN:
        if (u->scope->type != SC_FUNCTION && c->mode != MODE_EVAL) cerror(c, n->line, "'return' outside function");
        if (n->a) expr(c, n->a); else load_const(c, Py_None);
        for (int i = u->nfb - 1; i >= 0; i--) leave_fb(c, i, true);
        EMIT(RETURN_VALUE, 0);
        break;
    case S_IF: {
        expr(c, n->a);
        int j_else = JUMP_TO(POP_JUMP_IF_FALSE);
        stmts(c, n->l[0], n->n[0]);
        if (n->n[1]) {
            int j_end = JUMP_TO(JUMP);
            patch(c, j_else);
            stmts(c, n->l[1], n->n[1]);
            patch(c, j_end);
        } else patch(c, j_else);
        break;
    }
    case S_WHILE: {
        int top = u->n, j_exit = -1;
        bool always = n->a->kind == E_CONST && (n->a->val == Py_True || (KIND(n->a->val) == K_INT && int_sign(n->a->val) != 0));
        if (!always) { expr(c, n->a); j_exit = JUMP_TO(POP_JUMP_IF_FALSE); }
        int fi = u->nfb;
        push_fb(c, FB_WHILE)->target = top;
        stmts(c, n->l[0], n->n[0]);
        JumpList *breaks = u->fb[fi].breaks;
        pop_fb(c);
        u->line = n->line;
        emit(c, OP_JUMP_, top);
        if (j_exit >= 0) patch(c, j_exit);
        stmts(c, n->l[1], n->n[1]);
        patch_breaks(c, breaks);
        break;
    }
    case S_FOR: {
        expr(c, n->b);
        EMIT(GET_ITER, 0);
        int top = u->n, j_exit = JUMP_TO(FOR_ITER);
        store(c, n->a);
        int fi = u->nfb;
        push_fb(c, FB_FOR)->target = top;
        stmts(c, n->l[0], n->n[0]);
        JumpList *breaks = u->fb[fi].breaks;
        pop_fb(c);
        u->line = n->line;
        emit(c, OP_JUMP_, top);
        patch(c, j_exit);
        stmts(c, n->l[1], n->n[1]);
        patch_breaks(c, breaks);
        break;
    }
    case S_BREAK: case S_CONTINUE: {
        int li = find_loop(c, n->line, n->kind == S_BREAK ? "break" : "continue");
        for (int i = u->nfb - 1; i > li; i--) leave_fb(c, i, false);
        if (n->kind == S_CONTINUE) { emit(c, OP_JUMP_, u->fb[li].target); break; }
        if (u->fb[li].type == FB_FOR) EMIT(POP_TOP, 0);
        JumpList *j = arena_alloc(sizeof *j);
        j->at = JUMP_TO(JUMP);
        j->next = u->fb[li].breaks;
        u->fb[li].breaks = j;
        break;
    }
    case S_PASS: case S_GLOBAL: case S_NONLOCAL: break;
    case S_MATCH: match_stmt(c, n); break;
    case S_DEF:
        if (n->c) { stmt(c, n->c); break; }
        for (int i = 0; i < n->n[1]; i++) { expr(c, n->l[1][i]); EMIT(PUSH_NULL, 0); }
        function(c, n, AS_STR(n->val));
        for (int i = 0; i < n->n[1]; i++) EMIT(CALL, 1);
        name_op(c, AS_STR(n->val2 ? n->val2 : n->val), CTX_STORE);
        break;
    case S_CLASS: if (n->c) stmt(c, n->c); else class_def(c, n); break;
    case S_TRY:
        if (n->op) {
            int j_setup = JUMP_TO(SETUP_TRY);
            FB *fb = push_fb(c, FB_FINALLY_TRY);
            fb->final = n->l[3]; fb->nfinal = n->n[3];
            if (n->n[1]) try_except(c, n); else stmts(c, n->l[0], n->n[0]);
            pop_fb(c);
            EMIT(POP_BLOCK, 0);
            stmts(c, n->l[3], n->n[3]);
            int j_end = JUMP_TO(JUMP);
            patch(c, j_setup);
            push_fb(c, FB_FINALLY_END);
            stmts(c, n->l[3], n->n[3]);
            pop_fb(c);
            EMIT(RERAISE, 0);
            patch(c, j_end);
        } else try_except(c, n);
        break;
    case S_RAISE:
        if (n->a) expr(c, n->a);
        if (n->b) expr(c, n->b);
        EMIT(RAISE, n->b ? 2 : n->a ? 1 : 0);
        break;
    case S_WITH: with_stmt(c, n, 0); break;
    case S_ASSERT: {
        expr(c, n->a);
        int j_ok = JUMP_TO(POP_JUMP_IF_TRUE);
        load_const(c, (Obj *)E_AssertionError);
        if (n->b) { EMIT(PUSH_NULL, 0); expr(c, n->b); EMIT(CALL, 1); }
        EMIT(RAISE, 1);
        patch(c, j_ok);
        break;
    }
    case S_DEL: for (int i = 0; i < n->n[0]; i++) del_target(c, n->l[0][i]); break;
    case S_IMPORT:
        for (int i = 0; i < n->n[0]; i++) {
            Node *a = n->l[0][i];
            Str *full = AS_STR(a->val);
            if (str_eqc(full, "dependent"))
                cerror(c, n->line, "write 'from dependent import ...' at the top level of a file, which is then type-checked");
            EMIT(IMPORT_NAME, (add_name(c, full) << 1) | (a->val2 ? 1 : 0));
            if (a->val2) name_op(c, AS_STR(a->val2), CTX_STORE);
            else {
                const char *dot = memchr(full->s, '.', full->len);
                name_op(c, dot ? AS_STR(str_new(full->s, (size_t)(dot - full->s))) : full, CTX_STORE);
            }
        }
        break;
    case S_IMPORTFROM:
        if (str_eqc(AS_STR(n->val), "__future__") && !n->n[3])
            cerror(c, n->line, "from __future__ imports must occur at the beginning of the file");
        if (str_eqc(AS_STR(n->val), "dependent") && !c->checked)
            cerror(c, n->line, "dependent is imported at the top level of a file, which is then type-checked");
        EMIT(IMPORT_NAME, (add_name(c, AS_STR(n->val)) << 1) | 1);
        if (n->op) { EMIT(IMPORT_STAR, 0); break; }
        for (int i = 0; i < n->n[0]; i++) {
            Node *a = n->l[0][i];
            EMIT(IMPORT_FROM, add_name(c, AS_STR(a->val)));
            name_op(c, AS_STR(a->val2 ? a->val2 : a->val), CTX_STORE);
        }
        EMIT(POP_TOP, 0);
        break;
    default: cerror(c, n->line, "invalid statement");
    }
}

static void stmts(C *c, Node **l, int n) {
    for (int i = 0; i < n; i++) stmt(c, l[i]);
}

Code *compile_source(const char *src, const char *filename, int mode) {
    Code *volatile result = NULL;
    gc_paused++;
    compile_incomplete = false;
    unit_chain = NULL;
    if (setjmp(compile_jmp) == 0) {
        Node *m = parse_source(src, filename, mode);
        C c = {0};
        /* from __future__ imports: allowed only before all other statements. */
        bool head = true;
        for (int i = 0; i < m->n[0]; i++) {
            static const char *const features[] = {"nested_scopes", "generators", "division", "absolute_import",
                "with_statement", "print_function", "unicode_literals", "barry_as_FLUFL", "generator_stop", "annotations", NULL};
            Node *st = m->l[0][i];
            if (st->kind != S_IMPORTFROM || !str_eqc(AS_STR(st->val), "__future__")) {
                if (!(st->kind == S_EXPR && st->a->kind == E_CONST && IS_STR(st->a->val) && i == 0)) head = false;
                continue;
            }
            if (!head) syntax_error(filename, st->line, "from __future__ imports must occur at the beginning of the file");
            if (st->op) syntax_error(filename, st->line, "future feature * is not defined");
            for (int j = 0; j < st->n[0]; j++) {
                Str *f = AS_STR(st->l[0][j]->val);
                bool known = false;
                for (int k = 0; features[k]; k++) if (str_eqc(f, features[k])) known = true;
                if (!known) syntax_error(filename, st->line, "future feature %s is not defined", f->s);
                if (str_eqc(f, "annotations")) c.future_ann = true;
            }
            st->n[3] = 1;   /* marks this import as one found in the allowed place */
        }
        /* A file that imports dependent is type-checked before anything else happens. */
        if (mode != MODE_EVAL && agda_wanted(m)) {
            if (mode == MODE_SINGLE)
                syntax_error(filename, 1, "code that imports dependent is type-checked as a whole file; it cannot be typed at the prompt");
            agda_check(m, filename);
            c.checked = true;
        }
        if (mode == MODE_FILE) agda_checked = c.checked;
        c.filename = filename;
        c.fname = AS_STR(str_c(filename));
        c.mode = mode;

        Scope *top = scope_new(SC_MODULE, NULL);
        sym_stmts(&c, top, m->l[0], m->n[0]);
        analyze(&c, top);
        unit_begin(&c, top, AS_STR(str_c("<module>")), 1);
        Obj *doc = mode == MODE_FILE ? docstring(m->l[0], m->n[0]) : NULL;
        if (doc) { emit(&c, OP_LOAD_CONST_, add_const(&c, doc)); emit(&c, OP_STORE_GLOBAL_, add_name(&c, N_doc)); }
        stmts(&c, m->l[0], m->n[0]);
        emit(&c, OP_LOAD_CONST_, add_const(&c, Py_None));
        emit(&c, OP_RETURN_VALUE_, 0);
        result = unit_finish(&c);
    } else {
        while (unit_chain) {
            Unit *u = unit_chain;
            unit_chain = u->parent;
            free(u->code); free(u->lines); free(u);
        }
    }
    arena_free_all();
    gc_paused--;
    return result;
}
