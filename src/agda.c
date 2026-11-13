/* Checked files.

   A file that imports `dependent` at its top level asks for type checking. Before
   such a file runs, this code translates it to Agda, runs the `agda` program on
   the result, and reports any error against the Python line it came from. The
   types are only used for checking: once Agda accepts the file, the ordinary
   compiler and VM run it.

   The translation covers a pure subset of Python. Any statement or expression
   outside the subset is an error here; none is passed over. The rules, in short:

     class Vec[A, n: Nat](Data): ...          a family of types
     @constructor
     def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...
     def f[A, n: Nat](xs: Vec[A, n]) -> Nat:  names in [] exist only for the checker
         match xs: ...                        becomes Agda clauses
         if n == 0: ... else: ... n - 1 ...   splits a Nat into zero and suc
     if c: ... else: ...                  a `with` on c
     match decide(c): case Yes(p): ...    a `with c in p`
     Fn[k: Nat, P(k)]                     a function type whose result mentions the argument
     x: T = e, print(e), assert e             at the top level

   Each function body is turned into clauses by walking its statements with the
   left-hand side so far (`lhs`) and the statements still to run (`K`). A match or
   a zero test on a variable replaces that variable in the left-hand side by a
   pattern. Any other match or test adds a `with` column. A run of statements
   with no further split becomes one expression. */
#include "ast.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern const char agda_prelude[];
int agda_mode;   /* 0 run normally, 1 print the Agda text and exit (--agda) */

#define USCORE "\xCB\x8D"      /* stands for _ inside Agda names, where _ has a special meaning */
#define PRIME "\xE2\x80\xB2"   /* added to a name that Agda reserves */

/* ---------- small helpers on the arena ---------- */

static char *astr(const char *s) {
    size_t n = strlen(s);
    char *r = arena_alloc(n + 1);
    memcpy(r, s, n + 1);
    return r;
}

__attribute__((format(printf, 1, 2))) static char *af(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *r = arena_alloc((size_t)n + 1);
    vsnprintf(r, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return r;
}

/* Generated text plus, for each line of it, the Python line it came from. */
typedef struct { char *s; size_t n, cap; int *lines; int nl, lcap; } Out;

static void o_put(Out *o, int pyline, const char *s) {
    size_t len = strlen(s);
    if (o->n + len + 1 > o->cap) {
        size_t cap = (o->n + len + 1) * 2 + 256;
        char *ns = arena_alloc(cap);
        if (o->n) memcpy(ns, o->s, o->n);
        o->s = ns; o->cap = cap;
    }
    memcpy(o->s + o->n, s, len + 1);
    o->n += len;
    for (const char *p = s; *p; p++) {
        if (*p != '\n') continue;
        if (o->nl == o->lcap) {
            int cap = o->lcap * 2 + 64;
            int *nlines = arena_alloc((size_t)cap * sizeof(int));
            if (o->nl) memcpy(nlines, o->lines, (size_t)o->nl * sizeof(int));
            o->lines = nlines; o->lcap = cap;
        }
        o->lines[o->nl++] = pyline;
    }
}

/* ---------- data structures ---------- */

enum { SK_FUNC, SK_DATA, SK_CTOR, SK_VALUE, SK_TYPE, SK_SPECIAL };

typedef struct Sym Sym;
struct Sym {
    Str *py;
    const char *agda;
    int kind, arity, line;
    Node *node;
    bool prelude;
    Sym *family;                     /* constructor: the type it builds */
    Sym **ctors; int nctors;         /* data type: its constructors */
    int nparams;                     /* data type: how many leading [] names are Agda parameters */
    const char **tp_names, **p_names; /* Agda names of the [] names and the () parameters */
    bool sig_done, body_done, busy, in_body;
    bool rt_ref;                     /* a function body uses it when the program runs */
};

/* A local name. `term` is the Agda text it stands for. */
typedef struct Var Var;
struct Var {
    Str *py;
    const char *term;
    const char *pred;    /* Agda text for `name - 1`, known after a zero test */
    bool split;          /* term is a variable of the left-hand side, so it can be matched on */
    bool nat, erased, captured;
    int depth, tp;
    Var *next;
};

typedef struct {
    Var *env;
    bool in_type, executing;
    int lambda_depth;
    const char **used; int nused, ucap;   /* Agda names taken in this definition */
    const char **lhs; int nlhs, lhscap;   /* the ones that are variables of the left-hand side */
    Str **locals; int nlocals, lcap;      /* names this function assigns somewhere */
    bool *tp_used;
} Ctx;

typedef struct {
    const char *filename;
    Sym **syms; int nsyms, cap;
    Str **imported; int nimported; bool import_all, future_ann;
    Out out, *cur;
    Ctx c;
    int stmt_line;
    Sym *fn;            /* the function whose body is being written */
    jmp_buf *try_jmp;   /* set while translating code that may turn out to be unreachable */
} T;

/* Statements still to run: a list of segments. */
typedef struct K K;
struct K { Node **l; int n, i; K *next; };

static const struct { const char *py, *agda; int kind, arity; } prelude_names[] = {
    {"Type", "Set", SK_TYPE, 0}, {"Nat", "Nat", SK_TYPE, 0}, {"Empty", "\xE2\x8A\xA5", SK_TYPE, 0},
    {"Eq", NULL, SK_TYPE, 2}, {"Callable", NULL, SK_TYPE, 2}, {"Fn", NULL, SK_TYPE, 1},
    {"Dec", "Dec\xE2\x82\x80", SK_TYPE, 1}, {"Yes", "Yes", SK_CTOR, 1}, {"No", "No", SK_CTOR, 1}, {"decide", "decide", SK_FUNC, 1},
    {"Data", NULL, SK_SPECIAL, 0}, {"constructor", NULL, SK_SPECIAL, 0},
    {"Refl", "refl", SK_CTOR, 0}, {"refl", "refl", SK_FUNC, 0}, {"suc", "suc", SK_FUNC, 1},
    {"sym", "sym", SK_FUNC, 1}, {"trans", "trans", SK_FUNC, 2}, {"cong", "cong", SK_FUNC, 2},
    {"subst", "subst", SK_FUNC, 3}, {"pred", "pred", SK_FUNC, 1}, {"monus", "monus", SK_FUNC, 2},
    {"absurd", "absurd", SK_FUNC, 1}, {"concat", "concat", SK_FUNC, 2}, {"unreachable", NULL, SK_SPECIAL, 0},
};
#define NPRELUDE ((int)(sizeof prelude_names / sizeof prelude_names[0]))

/* Names that mean something to Agda or to the prelude. A Python name equal to one
   of these gets a prime added. */
static const char *const reserved[] = {
    "Set", "Prop", "Nat", "zero", "suc", "Bool", "true", "false", "String", "Level", "the", "if", "then", "else",
    "not", "sym", "trans", "cong", "subst", "pred", "monus", "absurd", "concat", "refl", "abstract", "codata", "coinductive",
    "constructor", "data", "do", "eta-equality", "field", "forall", "hiding", "import", "in", "inductive", "infix",
    "infixl", "infixr", "instance", "interleaved", "let", "macro", "module", "mutual", "no-eta-equality", "opaque",
    "open", "overlap", "pattern", "postulate", "primitive", "private", "public", "quote", "quoteTerm", "record",
    "renaming", "rewrite", "syntax", "tactic", "to", "unfolding", "unquote", "unquoteDecl", "unquoteDef", "using",
    "variable", "where", "with", "Yes", "No", "decide", "EqNat", "EqBool", "EqString", "\xCE\xBB", "\xE2\x88\x80", NULL};

static const char *const sub_digit[] = {"\xE2\x82\x80", "\xE2\x82\x81", "\xE2\x82\x82", "\xE2\x82\x83", "\xE2\x82\x84",
                                        "\xE2\x82\x85", "\xE2\x82\x86", "\xE2\x82\x87", "\xE2\x82\x88", "\xE2\x82\x89"};

/* ---------- errors ---------- */

__attribute__((noreturn)) static void check_fail(const char *filename, int line, const char *msg) {
    Exc *e = (Exc *)exc_new(E_TypeCheckError, (Tuple *)tuple_pack(1, str_c(msg)));
    e->dict = (Dict *)dict_new();
    if (line > 0) dict_setc(e->dict, "lineno", int_new(line));
    dict_setc(e->dict, "filename", str_c(filename));
    dict_setc(e->dict, "msg", str_c(msg));
    vm.exc = (Obj *)e;
    longjmp(compile_jmp, 1);
}

__attribute__((noreturn, format(printf, 3, 4))) static void terr(T *t, int line, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    check_fail(t->filename, line, buf);
}

/* ---------- names ---------- */

static bool is_reserved(const char *s) {
    for (int i = 0; reserved[i]; i++) if (strcmp(reserved[i], s) == 0) return true;
    return false;
}

static const char *mangle(const char *py) {
    size_t n = strlen(py);
    char *r = arena_alloc(n * 2 + 1), *w = r;
    for (const char *p = py; *p; p++) {
        if (*p == '_') { *w++ = USCORE[0]; *w++ = USCORE[1]; }
        else *w++ = *p;
    }
    *w = 0;
    return r;
}

static const char *top_name(const char *py) {
    const char *m = mangle(py);
    return is_reserved(m) ? af("%s" PRIME, m) : m;
}

static Sym *find_sym(T *t, Str *name) {
    for (int i = 0; i < t->nsyms; i++) if (str_eq(t->syms[i]->py, name)) return t->syms[i];
    return NULL;
}

static Sym *add_sym(T *t, Str *name, int kind, int arity, Node *node, int line) {
    Sym *s = arena_alloc(sizeof(Sym));
    s->py = name; s->kind = kind; s->arity = arity; s->node = node; s->line = line;
    if (t->nsyms == t->cap) {
        int cap = t->cap * 2 + 32;
        Sym **ns = arena_alloc((size_t)cap * sizeof *ns);
        if (t->nsyms) memcpy(ns, t->syms, (size_t)t->nsyms * sizeof *ns);
        t->syms = ns; t->cap = cap;
    }
    t->syms[t->nsyms++] = s;
    return s;
}

static bool name_taken(T *t, const char *cand) {
    if (is_reserved(cand)) return true;
    for (int i = 0; i < t->nsyms; i++) if (t->syms[i]->agda && strcmp(t->syms[i]->agda, cand) == 0) return true;
    for (int i = 0; i < t->c.nused; i++) if (strcmp(t->c.used[i], cand) == 0) return true;
    return false;
}

static void mark_used(T *t, const char *name);

/* A new Agda variable name, unique in the current definition. Every binding gets
   its own name, so one Python name bound twice never clashes in Agda. */
static const char *fresh_agda(T *t, const char *base) {
    /* Drop an old numeric suffix so names do not grow. */
    size_t n = strlen(base);
    while (n >= 3 && (unsigned char)base[n - 3] == 0xE2 && (unsigned char)base[n - 2] == 0x82 &&
           (unsigned char)base[n - 1] >= 0x80 && (unsigned char)base[n - 1] <= 0x89)
        n -= 3;
    char *stem = arena_alloc(n + 1);
    memcpy(stem, base, n);
    stem[n] = 0;
    const char *cand = stem;
    for (int i = 1; name_taken(t, cand); i++) {
        char digits[64] = "", num[16];
        snprintf(num, sizeof num, "%d", i);
        for (char *d = num; *d; d++) strcat(digits, sub_digit[*d - '0']);
        cand = af("%s%s", stem, digits);
    }
    mark_used(t, cand);
    return cand;
}

static void mark_used(T *t, const char *name) {
    if (t->c.nused == t->c.ucap) {
        int cap = t->c.ucap * 2 + 32;
        const char **nu = arena_alloc((size_t)cap * sizeof *nu);
        if (t->c.nused) memcpy(nu, t->c.used, (size_t)t->c.nused * sizeof *nu);
        t->c.used = nu; t->c.ucap = cap;
    }
    t->c.used[t->c.nused++] = name;
}

static const char *fresh(T *t, const char *py) { return fresh_agda(t, mangle(py)); }

/* Records a name as a variable of the left-hand side. Only such a name can be
   matched on: a one-word constant like zero or a constructor without fields is
   not a variable, however much it looks like one. */
static const char *lhs_var(T *t, const char *name) {
    if (t->c.nlhs == t->c.lhscap) {
        int cap = t->c.lhscap * 2 + 32;
        const char **nl = arena_alloc((size_t)cap * sizeof *nl);
        if (t->c.nlhs) memcpy(nl, t->c.lhs, (size_t)t->c.nlhs * sizeof *nl);
        t->c.lhs = nl; t->c.lhscap = cap;
    }
    t->c.lhs[t->c.nlhs++] = name;
    return name;
}
static bool is_lhs_var(T *t, const char *text) {
    for (int i = 0; i < t->c.nlhs; i++) if (strcmp(t->c.lhs[i], text) == 0) return true;
    return false;
}

/* Replaces the whole word `name` by `repl` in Agda text. Words end at spaces,
   brackets and line ends; string literals are left alone. */
static const char *subst_ident(const char *text, const char *name, const char *repl) {
    size_t nl = strlen(name), rl = strlen(repl), cap = strlen(text) + 1;
    for (const char *p = text; (p = strstr(p, name)); p += nl) cap += rl;
    char *out = arena_alloc(cap), *w = out;
    const char *p = text;
    while (*p) {
        if (*p == '"') {
            const char *q = p + 1;
            while (*q && *q != '"') q += (*q == '\\' && q[1]) ? 2 : 1;
            if (*q) q++;
            memcpy(w, p, (size_t)(q - p)); w += q - p; p = q;
            continue;
        }
        if (strchr(" (){};\n\x01", *p)) { *w++ = *p++; continue; }
        const char *q = p;
        while (*q && !strchr(" (){};\n\x01", *q)) q++;
        if ((size_t)(q - p) == nl && memcmp(p, name, nl) == 0) { memcpy(w, repl, rl); w += rl; }
        else { memcpy(w, p, (size_t)(q - p)); w += q - p; }
        p = q;
    }
    *w = 0;
    return out;
}

/* ---------- name lookup ---------- */

static bool imported(T *t, const char *name) {
    if (t->import_all) return true;
    for (int i = 0; i < t->nimported; i++) if (str_eqc(t->imported[i], name)) return true;
    return false;
}

static Sym *prelude_sym(T *t, Str *name) {
    static Sym table[NPRELUDE + 2];
    static bool ready;
    if (!ready) {
        for (int i = 0; i < NPRELUDE; i++) {
            table[i].agda = prelude_names[i].agda; table[i].kind = prelude_names[i].kind;
            table[i].arity = prelude_names[i].arity; table[i].prelude = true;
        }
        table[NPRELUDE].agda = "Bool"; table[NPRELUDE].kind = SK_TYPE; table[NPRELUDE].prelude = true;
        table[NPRELUDE + 1].agda = "String"; table[NPRELUDE + 1].kind = SK_TYPE; table[NPRELUDE + 1].prelude = true;
        ready = true;
    }
    for (int i = 0; i < NPRELUDE; i++)
        if (str_eqc(name, prelude_names[i].py) && imported(t, prelude_names[i].py)) { table[i].py = name; return &table[i]; }
    if (str_eqc(name, "bool")) { table[NPRELUDE].py = name; return &table[NPRELUDE]; }
    if (str_eqc(name, "str")) { table[NPRELUDE + 1].py = name; return &table[NPRELUDE + 1]; }
    return NULL;
}

static bool sym_is(Sym *s, const char *py) { return s && s->prelude && str_eqc(s->py, py); }

static void emit_func_sig(T *t, Sym *s);
static void emit_data_sig(T *t, Sym *s);
static void emit_data_body(T *t, Sym *s);
static void emit_func_body(T *t, Sym *s);

/* Makes sure Agda has seen what it needs of `s` before the text being written,
   and checks that Python will have defined it by the time it is used. */
static void need(T *t, Sym *s, int line) {
    if (s->prelude) return;
    switch (s->kind) {
    case SK_FUNC:
        /* A function used inside a type has to be worked out by Agda, so its whole
           definition must come first. Elsewhere its signature is enough. */
        if (t->c.in_type && !s->body_done && !s->in_body && !s->busy) emit_func_body(t, s);
        else if (!s->sig_done) emit_func_sig(t, s);
        break;
    case SK_DATA: if (!s->sig_done) emit_data_sig(t, s); break;
    case SK_CTOR: if (!s->family->body_done) emit_data_body(t, s->family); break;
    case SK_VALUE:
        if (!s->body_done)
            terr(t, line, "'%s' is defined on line %d, below this use. Move the definition above it.", s->py->s, s->line);
        break;
    }
    if (t->c.in_type) return;
    if (t->c.executing) {
        if (s->line > t->stmt_line)
            terr(t, line, "'%s' is used here before its definition on line %d", s->py->s, s->line);
    } else s->rt_ref = true;
}

typedef struct { Var *v; Sym *s; } Ref;

static Ref lookup(T *t, Str *name, int line) {
    Ref r = {0};
    for (Var *v = t->c.env; v; v = v->next)
        if (v->py && str_eq(v->py, name)) {
            if (v->erased && !t->c.in_type)
                terr(t, line, "'%s' is a name in square brackets. Such names exist only for the type checker, "
                              "so they can appear in annotations and nowhere else.", name->s);
            if (v->tp >= 0 && t->c.tp_used) t->c.tp_used[v->tp] = true;
            if (t->c.lambda_depth > v->depth) v->captured = true;
            r.v = v;
            return r;
        }
    for (int i = 0; i < t->c.nlocals; i++)
        if (str_eq(t->c.locals[i], name)) {
            if (t->try_jmp) longjmp(*t->try_jmp, 1);
            terr(t, line, "'%s' is read here, but no assignment to it has run on this path", name->s);
        }
    r.s = find_sym(t, name);
    if (!r.s) r.s = prelude_sym(t, name);
    if (r.s) { need(t, r.s, line); return r; }
    for (int i = 0; i < NPRELUDE; i++)
        if (str_eqc(name, prelude_names[i].py))
            terr(t, line, "'%s' is not imported. Add it to the 'from dependent import ...' line.", name->s);
    if (str_eqc(name, "int") || str_eqc(name, "float") || str_eqc(name, "complex"))
        terr(t, line, "'%s' is not available in a checked file. The number type is Nat (0, 1, 2, ...).", name->s);
    if (str_eqc(name, "print"))
        terr(t, line, "print is only allowed as a statement at the top level of a checked file");
    terr(t, line, "name '%s' is not defined in this checked file", name->s);
}

static void bind(T *t, Str *py, const char *term, bool split, bool nat, int line) {
    for (Var *v = t->c.env; v; v = v->next)
        if (v->py && py && str_eq(v->py, py)) {
            if (v->captured)
                terr(t, line, "'%s' is assigned again after a lambda used it. In Python the lambda would see the "
                              "new value. Use a different name.", py->s);
            break;
        }
    Var *v = arena_alloc(sizeof(Var));
    v->py = py; v->term = term; v->split = split; v->nat = nat; v->depth = t->c.lambda_depth; v->tp = -1;
    v->next = t->c.env;
    t->c.env = v;
}

/* Whether `text` is a left-hand-side variable that can be matched on. */
static bool var_by_term(T *t, const char *text, bool *nat) {
    for (Var *v = t->c.env; v; v = v->next) {
        if (v->split && strcmp(v->term, text) == 0 && is_lhs_var(t, text)) { *nat = v->nat; return true; }
        if (v->pred && strcmp(v->pred, text) == 0 && is_lhs_var(t, text)) { *nat = true; return true; }
    }
    return false;
}

/* A copy of the environment in which left-hand-side variable `name` has become `repl`. */
static Var *env_subst(T *t, const char *name, const char *repl, bool repl_is_var, const char *pred) {
    Var *head = NULL, **tail = &head;
    for (Var *v = t->c.env; v; v = v->next) {
        Var *nv = arena_alloc(sizeof(Var));
        *nv = *v;
        if (strcmp(v->term, name) == 0) {
            nv->term = repl; nv->split = v->split && repl_is_var; nv->pred = pred;
        } else {
            nv->term = subst_ident(v->term, name, repl);
            if (v->pred) nv->pred = subst_ident(v->pred, name, repl);
        }
        nv->next = NULL;
        *tail = nv;
        tail = &nv->next;
    }
    return head;
}

/* ---------- expressions ---------- */

static const char *E(T *t, Node *n);
static const char *tr_type(T *t, Node *n);

static const char *str_lit(Str *s) {
    char *r = arena_alloc((size_t)s->len * 6 + 3), *w = r;
    *w++ = '"';
    for (int64_t i = 0; i < s->len; i++) {
        unsigned char ch = (unsigned char)s->s[i];
        if (ch == '"' || ch == '\\') { *w++ = '\\'; *w++ = (char)ch; }
        else if (ch == '\n') { *w++ = '\\'; *w++ = 'n'; }
        else if (ch == '\t') { *w++ = '\\'; *w++ = 't'; }
        else if (ch < 0x20 || ch == 0x7F) w += sprintf(w, "\\%d\\&", ch);
        else *w++ = (char)ch;
    }
    *w++ = '"';
    *w = 0;
    return r;
}

static const char *const_text(T *t, Node *n) {
    Obj *v = n->val;
    if (v == Py_True) return "true";
    if (v == Py_False) return "false";
    if (KIND(v) == K_INT) {
        if (int_sign(v) < 0) terr(t, n->line, "negative numbers are not available in a checked file; Nat starts at 0");
        SB b;
        sb_init(&b);
        int_format(&b, v, 10, false);
        sb_putc(&b, 0);
        char *r = astr(b.s);
        sb_free(&b);
        return r;
    }
    if (IS_STR(v)) return str_lit(AS_STR(v));
    if (v == Py_None) terr(t, n->line, "None is not available in a checked file");
    if (KIND(v) == K_FLOAT) terr(t, n->line, "floating-point numbers are not available in a checked file");
    terr(t, n->line, "this constant is not available in a checked file");
}

static const char *fun_wrap(T *t, Sym *s, int line) {
    static const char *const w[] = {NULL, "fun\xE2\x82\x81", "fun\xE2\x82\x82", "fun\xE2\x82\x83", "fun\xE2\x82\x84"};
    if (s->arity == 0)
        terr(t, line, "'%s' takes no arguments. Write %s() to get its value.", s->py->s, s->py->s);
    if (s->arity > 4) terr(t, line, "a function passed as a value can take at most 4 arguments");
    return af("(%s %s)", w[s->arity], s->agda);
}

/* A name used as a value, with no call or subscript. */
static const char *name_ref(T *t, Node *n) {
    Ref r = lookup(t, AS_STR(n->val), n->line);
    if (r.v) return r.v->term;
    Sym *s = r.s;
    switch (s->kind) {
    case SK_VALUE: return s->agda;
    case SK_FUNC: case SK_CTOR: return fun_wrap(t, s, n->line);
    case SK_DATA: case SK_TYPE:
        if (s->arity) terr(t, n->line, "'%s' needs %d argument%s in square brackets", s->py->s, s->arity, s->arity == 1 ? "" : "s");
        return s->agda;
    default: terr(t, n->line, "'%s' cannot be used as a value", s->py->s);
    }
}

static const char *join_args(T *t, Node **l, int n) {
    const char *r = "";
    for (int i = 0; i < n; i++) {
        if (l[i]->kind == E_KEYWORD) terr(t, l[i]->line, "keyword arguments are not available in a checked file");
        if (l[i]->kind == E_STARRED) terr(t, l[i]->line, "* arguments are not available in a checked file");
        r = af("%s %s", r, E(t, l[i]));
    }
    return r;
}

static const char *call_expr(T *t, Node *n) {
    static const char *const cn[] = {"call\xE2\x82\x80", "call\xE2\x82\x81", "call\xE2\x82\x82", "call\xE2\x82\x83", "call\xE2\x82\x84"};
    int argc = n->n[0];
    const char *callee;
    if (n->a->kind == E_NAME) {
        Ref r = lookup(t, AS_STR(n->a->val), n->line);
        Sym *s = r.s;
        if (s && (s->kind == SK_FUNC || s->kind == SK_CTOR)) {
            if (argc != s->arity)
                terr(t, n->line, "%s() takes %d argument%s but %d %s given", s->py->s, s->arity, s->arity == 1 ? "" : "s",
                     argc, argc == 1 ? "was" : "were");
            const char *args = join_args(t, n->l[0], argc);
            return argc ? af("(%s%s)", s->agda, args) : s->agda;
        }
        if (sym_is(s, "unreachable"))
            terr(t, n->line, "unreachable() is written as the whole body of 'case _:', as in: case _: return unreachable()");
        if (s && (s->kind == SK_DATA || s->kind == SK_TYPE || s->kind == SK_SPECIAL))
            terr(t, n->line, "'%s' cannot be called", s->py->s);
        callee = r.v ? r.v->term : s->agda;
    } else callee = E(t, n->a);
    if (argc > 4) terr(t, n->line, "a function held in a variable can take at most 4 arguments");
    return af("(%s %s%s)", cn[argc], callee, join_args(t, n->l[0], argc));
}

static bool ann_is_nat(T *t, Node *ann);

/* The type of functions that take `args` and give `res`. An argument written
   `name: type` can be mentioned by the types after it and by the result. In Agda
   each later type is a function of the arguments before it:
   Fn[k: Nat, P(k), Q] is Fun\xE2\x82\x82 Nat (\xCE\xBB k \xE2\x86\x92 P k) (\xCE\xBB k _ \xE2\x86\x92 Q). */
static const char *fun_type(T *t, Node **args, int np, Node *res, int line) {
    static const char *const fn[] = {"Fun\xE2\x82\x80", "Fun\xE2\x82\x81", "Fun\xE2\x82\x82", "Fun\xE2\x82\x83", "Fun\xE2\x82\x84"};
    if (np > 4) terr(t, line, "a function type can have at most 4 argument types");
    if (res->kind == E_SLICE) terr(t, line, "the last item of Fn[...] is the result type, which has no name");
    Var *saved = t->c.env;
    const char *text = fn[np], *binders = "";
    for (int i = 0; i < np; i++) {
        Node *a = args[i], *ty = a;
        Str *name = NULL;
        if (a->kind == E_SLICE) {
            if (!a->a || a->a->kind != E_NAME || !a->b || a->c)
                terr(t, a->line, "an argument in Fn[...] is written as a type, or as name: type");
            /* Python never evaluates an annotation here, so the name need not exist when
               the program runs. Anywhere else Python would look the name up and fail. */
            if (!t->c.in_type)
                terr(t, a->line, "Fn[...] with a named argument can only be written in an annotation. "
                                 "Here Python would evaluate it when the program runs, and '%s' would not be defined.",
                     AS_STR(a->a->val)->s);
            name = AS_STR(a->a->val);
            ty = a->b;
        }
        const char *tt = tr_type(t, ty);
        text = i ? af("%s (\xCE\xBB%s \xE2\x86\x92 %s)", text, binders, tt) : af("%s %s", text, tt);
        const char *b = "_";
        if (name) {
            b = fresh(t, name->s);
            bind(t, name, b, false, false, a->line);
            t->c.env->erased = true;
        }
        binders = af("%s %s", binders, b);
    }
    const char *rt = tr_type(t, res);
    t->c.env = saved;
    return np ? af("(%s (\xCE\xBB%s \xE2\x86\x92 %s))", text, binders, rt) : af("(%s %s)", text, rt);
}

static const char *subscript_expr(T *t, Node *n) {
    if (n->a->kind != E_NAME) terr(t, n->line, "indexing is not available in a checked file");
    Ref r = lookup(t, AS_STR(n->a->val), n->line);
    Sym *s = r.s;
    if (!s || (s->kind != SK_DATA && s->kind != SK_TYPE))
        terr(t, n->line, "indexing is not available in a checked file; square brackets are only for type arguments");
    Node *one[1] = {n->b}, **args = one;
    int argc = 1;
    if (n->b->kind == E_TUPLE) { args = n->b->l[0]; argc = n->b->n[0]; }
    if (sym_is(s, "Callable")) {
        if (argc != 2 || args[0]->kind != E_LIST)
            terr(t, n->line, "write Callable[[argument types], result type]");
        for (int i = 0; i < args[0]->n[0]; i++)
            if (args[0]->l[0][i]->kind == E_SLICE) terr(t, n->line, "to name an argument, write Fn[name: type, ..., result type]");
        return fun_type(t, args[0]->l[0], args[0]->n[0], args[1], n->line);
    }
    if (sym_is(s, "Fn")) return fun_type(t, args, argc - 1, args[argc - 1], n->line);
    if (argc != s->arity || !s->arity)
        terr(t, n->line, "'%s' takes %d argument%s in square brackets but %d %s given", s->py->s, s->arity,
             s->arity == 1 ? "" : "s", argc, argc == 1 ? "was" : "were");
    if (sym_is(s, "Eq")) {
        const char *a = E(t, args[0]);
        return af("(%s \xE2\x89\xA1 %s)", a, E(t, args[1]));
    }
    const char *text = s->agda;
    for (int i = 0; i < argc; i++) text = af("%s %s", text, E(t, args[i]));
    return af("(%s)", text);
}

static const char *E(T *t, Node *n) {
    switch (n->kind) {
    case E_CONST: return const_text(t, n);
    case E_NAME: return name_ref(t, n);
    case E_CALL: return call_expr(t, n);
    case E_SUBSCR: return subscript_expr(t, n);
    case E_BINOP: {
        if (n->op == OP_SUB) {
            /* Nat has no general subtraction: 0 - 1 would differ between Agda and Python. */
            if (n->a->kind == E_NAME && n->b->kind == E_CONST && KIND(n->b->val) == K_INT && int_cmp(n->b->val, int_new(1)) == 0) {
                Ref r = lookup(t, AS_STR(n->a->val), n->line);
                if (r.v && r.v->pred) return r.v->pred;
            }
            terr(t, n->line, "subtraction is only allowed as 'n - 1' where 'if n == 0' has ruled out zero. "
                             "Use monus(a, b) for subtraction that stops at 0.");
        }
        if (n->op != OP_ADD && n->op != OP_MUL)
            terr(t, n->line, "this operator is not available in a checked file; Nat has + and *");
        const char *a = E(t, n->a);
        /* + is addition of Nat. Next to a string literal it joins strings. */
        if (n->op == OP_ADD && ((n->a->kind == E_CONST && IS_STR(n->a->val)) || (n->b->kind == E_CONST && IS_STR(n->b->val))))
            return af("(concat %s %s)", a, E(t, n->b));
        if (n->op == OP_ADD && n->b->kind == E_CONST && KIND(n->b->val) == K_INT && int_sign(n->b->val) > 0 &&
            int_cmp(n->b->val, int_new(32)) <= 0) {
            /* n + 2 is written suc (suc n), the form Agda can match constructors against. */
            int64_t k = 0;
            int_fits(n->b->val, &k);
            for (int64_t i = 0; i < k; i++) a = af("(suc %s)", a);
            return a;
        }
        return af("(%s %s %s)", a, n->op == OP_ADD ? "+" : "*", E(t, n->b));
    }
    case E_COMPARE: {
        const char *left = E(t, n->a), *all = NULL;
        for (int i = 0; i < n->n[0]; i++) {
            const char *right = E(t, n->l[0][i]), *one;
            switch (n->ops[i]) {
            case CMP_EQ: one = af("(%s == %s)", left, right); break;
            case CMP_NE: one = af("(not (%s == %s))", left, right); break;
            case CMP_LT: one = af("(%s < %s)", left, right); break;
            case CMP_LE: one = af("(%s <= %s)", left, right); break;
            case CMP_GT: one = af("(%s < %s)", right, left); break;
            case CMP_GE: one = af("(%s <= %s)", right, left); break;
            default: terr(t, n->line, "'in' and 'is' are not available in a checked file");
            }
            all = all ? af("(%s \xE2\x88\xA7 %s)", all, one) : one;
            left = right;
        }
        return all;
    }
    case E_NOT: return af("(not %s)", E(t, n->a));
    case E_BOOLOP: {
        const char *all = E(t, n->l[0][0]);
        for (int i = 1; i < n->n[0]; i++) all = af("(%s %s %s)", all, n->op ? "\xE2\x88\xA8" : "\xE2\x88\xA7", E(t, n->l[0][i]));
        return all;
    }
    case E_IFEXP: {
        const char *c = E(t, n->a), *a = E(t, n->b);
        return af("(if %s then %s else %s)", c, a, E(t, n->c));
    }
    case E_LAMBDA: {
        static const char *const w[] = {"fun\xE2\x82\x80", "fun\xE2\x82\x81", "fun\xE2\x82\x82", "fun\xE2\x82\x83", "fun\xE2\x82\x84"};
        Params *p = n->params;
        if (p->nkwonly || p->vararg || p->kwarg || p->nposonly) terr(t, n->line, "a lambda in a checked file takes plain parameters only");
        if (p->n > 4) terr(t, n->line, "a lambda can take at most 4 arguments");
        Var *saved = t->c.env;
        t->c.lambda_depth++;
        const char *names = "";
        for (int i = 0; i < p->n; i++) {
            if (p->defaults[i]) terr(t, n->line, "default values are not available in a checked file");
            const char *a = fresh(t, p->names[i]->s);
            bind(t, p->names[i], a, false, false, n->line);
            names = af("%s %s", names, a);
        }
        const char *body = E(t, n->a);
        t->c.lambda_depth--;
        t->c.env = saved;
        if (!p->n) return af("(%s %s)", w[0], body);
        return af("(%s (\xCE\xBB%s \xE2\x86\x92 %s))", w[p->n], names, body);
    }
    case E_ATTR: terr(t, n->line, "attribute access is not available in a checked file; use match to take a value apart");
    case E_TUPLE: case E_LIST: case E_SET: case E_DICT:
        terr(t, n->line, "tuples, lists, sets and dicts are not available in a checked file; declare a Data type");
    case E_NAMED: terr(t, n->line, "':=' is not available in a checked file");
    case E_FSTRING: terr(t, n->line, "f-strings are not available in a checked file");
    case E_UNARY: terr(t, n->line, "this operator is not available in a checked file");
    default: terr(t, n->line, "this expression is not available in a checked file");
    }
}

/* An annotation. At its top, `a == b` is the type of proofs that a equals b. */
static const char *tr_type(T *t, Node *n) {
    bool saved = t->c.in_type;
    t->c.in_type = true;
    const char *r;
    if (n->kind == E_COMPARE && n->n[0] == 1 && n->ops[0] == CMP_EQ) {
        const char *a = E(t, n->a);
        r = af("(%s \xE2\x89\xA1 %s)", a, E(t, n->l[0][0]));
    } else r = E(t, n);
    t->c.in_type = saved;
    return r;
}

static bool ann_is_nat(T *t, Node *ann) {
    if (!ann || ann->kind != E_NAME) return false;
    for (Var *v = t->c.env; v; v = v->next) if (v->py && str_eq(v->py, AS_STR(ann->val))) return false;
    if (find_sym(t, AS_STR(ann->val))) return false;
    return sym_is(prelude_sym(t, AS_STR(ann->val)), "Nat");
}

/* ---------- signatures ---------- */

static void check_params(T *t, Node *def) {
    Params *p = def->params;
    if (p->vararg || p->kwarg || p->nkwonly || p->nposonly)
        terr(t, def->line, "a checked function takes plain parameters only: no *, ** or /");
    for (int i = 0; i < p->n; i++) {
        if (p->defaults[i]) terr(t, def->line, "default values are not available in a checked file");
        if (!p->ann[i]) terr(t, def->line, "parameter '%s' needs a type annotation", p->names[i]->s);
    }
    if (!def->b) terr(t, def->line, "'%s' needs a return annotation", AS_STR(def->val)->s);
    for (int i = 0; i < def->n[2]; i++) {
        Node *tp = def->l[2][i];
        if (tp->op || tp->c) terr(t, tp->line, "a name in square brackets cannot use *, ** or a default in a checked file");
    }
}

static Var *erased_var(T *t, Str *py, const char *term, int index) {
    bind(t, py, term, false, false, 0);
    t->c.env->erased = true;
    t->c.env->tp = index;
    return t->c.env;
}

static void emit_func_sig(T *t, Sym *s) {
    Node *def = s->node;
    if (s->busy) terr(t, def->line, "the annotations of '%s' depend on '%s' itself", s->py->s, s->py->s);
    s->busy = true;
    check_params(t, def);
    Ctx saved = t->c;
    t->c = (Ctx){0};
    int ntp = def->n[2], np = def->params->n;
    s->tp_names = arena_alloc((size_t)(ntp + 1) * sizeof(char *));
    s->p_names = arena_alloc((size_t)(np + 1) * sizeof(char *));
    const char *text = "";
    for (int i = 0; i < ntp; i++) {
        Node *tp = def->l[2][i];
        const char *bound = tp->a ? tr_type(t, tp->a) : "Set";
        s->tp_names[i] = fresh(t, AS_STR(tp->val)->s);
        erased_var(t, AS_STR(tp->val), s->tp_names[i], i);
        text = af("%s{@0 %s : %s} ", text, s->tp_names[i], bound);
    }
    for (int i = 0; i < np; i++) {
        const char *ty = tr_type(t, def->params->ann[i]);
        s->p_names[i] = fresh(t, def->params->names[i]->s);
        bind(t, def->params->names[i], s->p_names[i], false, false, def->line);
        text = af("%s(%s : %s) ", text, s->p_names[i], ty);
    }
    const char *ret = tr_type(t, def->b);
    t->c = saved;
    o_put(&t->out, def->line, ntp + np ? af("%s : %s\xE2\x86\x92 %s\n", s->agda, text, ret) : af("%s : %s\n", s->agda, ret));
    s->sig_done = true;
    s->busy = false;
}

/* The arguments of a constructor's return annotation. */
static int ret_args(Sym *ctor, Node ***args) {
    Node *ret = ctor->node->b;
    if (ret->kind == E_NAME) { *args = NULL; return 0; }
    if (ret->b->kind == E_TUPLE) { *args = ret->b->l[0]; return ret->b->n[0]; }
    *args = &ret->b;
    return 1;
}

/* Which [] name of the constructor, if any, is the bare name at args[i]. */
static int tparam_index(Node *def, Node *arg) {
    if (arg->kind != E_NAME) return -1;
    for (int i = 0; i < def->n[2]; i++) if (str_eq(AS_STR(def->l[2][i]->val), AS_STR(arg->val))) return i;
    return -1;
}

static void emit_data_sig(T *t, Sym *s) {
    Node *cls = s->node;
    if (s->busy) terr(t, cls->line, "the declaration of '%s' depends on '%s' itself", s->py->s, s->py->s);
    s->busy = true;
    /* A leading [] name is an Agda parameter when every constructor passes it
       through unchanged. The rest are indices, which constructors may fix. */
    int np = s->arity;
    for (int c = 0; c < s->nctors; c++) {
        Node **args;
        int n = ret_args(s->ctors[c], &args), i = 0;
        if (n != s->arity)
            terr(t, s->ctors[c]->node->line, "the return annotation must give '%s' its %d argument%s", s->py->s, s->arity,
                 s->arity == 1 ? "" : "s");
        for (; i < n && i < np; i++) {
            int k = tparam_index(s->ctors[c]->node, args[i]);
            bool repeat = false;
            for (int j = 0; j < i; j++) if (tparam_index(s->ctors[c]->node, args[j]) == k) repeat = true;
            if (k < 0 || repeat) break;
        }
        np = i;
    }
    s->nparams = np;
    Ctx saved = t->c;
    t->c = (Ctx){0};
    s->tp_names = arena_alloc((size_t)(s->arity + 1) * sizeof(char *));
    const char *params = "", *indices = "";
    for (int i = 0; i < s->arity; i++) {
        Node *tp = cls->l[3][i];
        if (tp->op || tp->c) terr(t, tp->line, "a name in square brackets cannot use *, ** or a default in a checked file");
        const char *bound = tp->a ? tr_type(t, tp->a) : "Set";
        s->tp_names[i] = fresh(t, AS_STR(tp->val)->s);
        erased_var(t, AS_STR(tp->val), s->tp_names[i], -1);
        if (i < np) params = af("%s (@0 %s : %s)", params, s->tp_names[i], bound);
        else indices = af("%s(@0 %s : %s) \xE2\x86\x92 ", indices, s->tp_names[i], bound);
    }
    t->c = saved;
    o_put(&t->out, cls->line, af("data %s%s : %sSet\n", s->agda, params, indices));
    s->sig_done = true;
    s->busy = false;
}

static void emit_data_body(T *t, Sym *s) {
    if (!s->sig_done) emit_data_sig(t, s);
    if (s->busy) terr(t, s->node->line, "the constructors of '%s' depend on '%s' in a way that cannot be ordered", s->py->s, s->py->s);
    s->busy = true;
    Out chunk = {0}, *outer = t->cur;
    Ctx saved = t->c;
    const char *head = af("data %s", s->agda);
    for (int i = 0; i < s->nparams; i++) head = af("%s %s", head, s->tp_names[i]);
    o_put(&chunk, s->node->line, af("%s where\n", head));
    for (int c = 0; c < s->nctors; c++) {
        Sym *k = s->ctors[c];
        Node *def = k->node, **args;
        int nargs = ret_args(k, &args);
        check_params(t, def);
        t->c = (Ctx){0};
        t->cur = &chunk;
        for (int i = 0; i < s->nparams; i++) mark_used(t, s->tp_names[i]);
        const char *text = "";
        for (int i = 0; i < def->n[2]; i++) {
            Node *tp = def->l[2][i];
            int pos = -1;
            for (int j = 0; j < s->nparams; j++) if (tparam_index(def, args[j]) == i) pos = j;
            if (pos >= 0) { erased_var(t, AS_STR(tp->val), s->tp_names[pos], -1); continue; }
            const char *bound = tp->a ? tr_type(t, tp->a) : "Set";
            const char *a = fresh(t, AS_STR(tp->val)->s);
            erased_var(t, AS_STR(tp->val), a, -1);
            text = af("%s{@0 %s : %s} ", text, a, bound);
        }
        for (int i = 0; i < def->params->n; i++) {
            const char *ty = tr_type(t, def->params->ann[i]);
            const char *a = fresh(t, def->params->names[i]->s);
            bind(t, def->params->names[i], a, false, false, def->line);
            text = af("%s(%s : %s) ", text, a, ty);
        }
        t->c.in_type = true;
        const char *res = s->agda;
        for (int i = 0; i < nargs; i++) res = af("%s %s", res, i < s->nparams ? s->tp_names[i] : E(t, args[i]));
        o_put(&chunk, def->line, *text ? af("  %s : %s\xE2\x86\x92 %s\n", k->agda, text, res) : af("  %s : %s\n", k->agda, res));
    }
    t->c = saved;
    t->cur = outer;
    for (int i = 0, at = 0; i < chunk.nl; i++) {
        char *nl = strchr(chunk.s + at, '\n');
        *nl = 0;
        o_put(&t->out, chunk.lines[i], af("%s\n", chunk.s + at));
        at = (int)(nl - chunk.s) + 1;
    }
    s->body_done = true;
    s->busy = false;
}

/* ---------- function bodies ---------- */

static K *k_norm(K *k) {
    while (k && k->i >= k->n) k = k->next;
    return k;
}
static K *k_push(Node **l, int n, K *rest) {
    if (!n) return k_norm(rest);
    K *k = arena_alloc(sizeof(K));
    k->l = l; k->n = n; k->next = rest;
    return k;
}
static K *k_tail(K *k) {
    K *r = arena_alloc(sizeof(K));
    *r = *k;
    r->i++;
    return k_norm(r);
}

static bool skippable(Node *s) {
    return s->kind == S_PASS || (s->kind == S_EXPR && s->a->kind == E_CONST);
}

/* `x == 0`, `x != 0`, `x > 0` or `0 < x`, where x is a name or `name - 1`. */
static Node *zero_test(Node *c, bool *then_is_zero) {
    if (c->kind != E_COMPARE || c->n[0] != 1) return NULL;
    Node *a = c->a, *b = c->l[0][0];
    int op = c->ops[0];
    bool a_zero = a->kind == E_CONST && KIND(a->val) == K_INT && a->val != Py_True && a->val != Py_False && int_sign(a->val) == 0;
    bool b_zero = b->kind == E_CONST && KIND(b->val) == K_INT && b->val != Py_True && b->val != Py_False && int_sign(b->val) == 0;
    Node *x;
    if (b_zero && (op == CMP_EQ || op == CMP_NE || op == CMP_GT)) { x = a; *then_is_zero = op == CMP_EQ; }
    else if (a_zero && (op == CMP_EQ || op == CMP_NE || op == CMP_LT)) { x = b; *then_is_zero = op == CMP_EQ; }
    else return NULL;
    if (x->kind == E_NAME) return x;
    if (x->kind == E_BINOP && x->op == OP_SUB && x->a->kind == E_NAME) return x;
    return NULL;
}

static bool stmt_splits(Node *s) {
    /* Every if is a split. A test that is not a zero test becomes a `with` column, so
       inside each branch Agda has replaced the condition by true or false in the types. */
    return s->kind == S_MATCH || s->kind == S_IF;
}
static bool has_split(K *k) {
    for (k = k_norm(k); k; k = k->next)
        for (int i = k->i; i < k->n; i++) {
            if (stmt_splits(k->l[i])) return true;
            if (k->l[i]->kind == S_RETURN) return false;
        }
    return false;
}

/* Whether the statements match on `name` or compare it with zero. */
static bool splits_on(Node **l, int n, Str *name) {
    for (int i = 0; i < n; i++) {
        Node *s = l[i];
        bool z;
        if (s->kind == S_IF) {
            Node *x = zero_test(s->a, &z);
            if (x && x->kind == E_BINOP) x = x->a;
            if (x && str_eq(AS_STR(x->val), name)) return true;
            if (splits_on(s->l[0], s->n[0], name) || splits_on(s->l[1], s->n[1], name)) return true;
        } else if (s->kind == S_MATCH) {
            if (s->a->kind == E_NAME && str_eq(AS_STR(s->a->val), name)) return true;
            for (int j = 0; j < s->n[0]; j++) if (splits_on(s->l[0][j]->l[0], s->l[0][j]->n[0], name)) return true;
        } else if ((s->kind == S_ASSIGN && s->a->kind == E_NAME && str_eq(AS_STR(s->a->val), name)) ||
                   (s->kind == S_ANNASSIGN && s->b && s->b->kind == E_NAME && str_eq(AS_STR(s->b->val), name)))
            return true;   /* another name for it, which may be split on in turn */
    }
    return false;
}
static bool k_splits_on(K *k, Str *name) {
    for (k = k_norm(k); k; k = k->next) if (splits_on(k->l + k->i, k->n - k->i, name)) return true;
    return false;
}

static void add_local(T *t, Str *name) {
    if (!name) return;
    for (int i = 0; i < t->c.nlocals; i++) if (str_eq(t->c.locals[i], name)) return;
    if (t->c.nlocals == t->c.lcap) {
        int cap = t->c.lcap * 2 + 16;
        Str **nl = arena_alloc((size_t)cap * sizeof *nl);
        if (t->c.nlocals) memcpy(nl, t->c.locals, (size_t)t->c.nlocals * sizeof *nl);
        t->c.locals = nl; t->c.lcap = cap;
    }
    t->c.locals[t->c.nlocals++] = name;
}
static void pattern_locals(T *t, Node *p) {
    if (!p) return;
    if (p->kind == P_CAPTURE || p->kind == P_STAR || p->kind == P_AS || p->kind == P_MAP) add_local(t, AS_STR(p->val));
    if (p->kind == P_AS) pattern_locals(t, p->a);
    for (int j = 0; j < 2; j++) for (int i = 0; i < p->n[j]; i++) if (p->kind != P_MAP || j == 1) pattern_locals(t, p->l[j][i]);
}
static void collect_locals(T *t, Node **l, int n) {
    for (int i = 0; i < n; i++) {
        Node *s = l[i];
        if (s->kind == S_ASSIGN) { for (int j = 0; j < s->n[0]; j++) if (s->l[0][j]->kind == E_NAME) add_local(t, AS_STR(s->l[0][j]->val)); }
        else if (s->kind == S_ANNASSIGN) { if (s->a->kind == E_NAME) add_local(t, AS_STR(s->a->val)); }
        else if (s->kind == S_IF) { collect_locals(t, s->l[0], s->n[0]); collect_locals(t, s->l[1], s->n[1]); }
        else if (s->kind == S_MATCH)
            for (int j = 0; j < s->n[0]; j++) { pattern_locals(t, s->l[0][j]->a); collect_locals(t, s->l[0][j]->l[0], s->l[0][j]->n[0]); }
    }
}

/* The name and value of `x = e` or `x: T = e`. */
static Str *assign_parts(T *t, Node *s, Node **value, Node **ann) {
    *ann = NULL;
    if (s->kind == S_ASSIGN) {
        if (s->n[0] != 1 || s->l[0][0]->kind != E_NAME)
            terr(t, s->line, "an assignment in a checked file gives one plain name one value");
        *value = s->a;
        return AS_STR(s->l[0][0]->val);
    }
    if (s->a->kind != E_NAME) terr(t, s->line, "an assignment in a checked file gives one plain name one value");
    if (!s->b) terr(t, s->line, "an annotation without a value is not available in a checked file");
    *value = s->b; *ann = s->c;
    return AS_STR(s->a->val);
}

__attribute__((noreturn)) static void bad_stmt(T *t, Node *s, bool top) {
    const char *what = "this statement";
    switch (s->kind) {
    case S_WHILE: case S_FOR: what = "a loop"; break;
    case S_TRY: case S_RAISE: what = "exception handling"; break;
    case S_AUGASSIGN: what = "an assignment like +="; break;
    case S_DEF: what = "a nested def"; break;
    case S_CLASS: what = "a nested class"; break;
    case S_GLOBAL: case S_NONLOCAL: what = "global or nonlocal"; break;
    case S_IMPORT: case S_IMPORTFROM: what = "an import"; break;
    case S_ASSERT: what = "assert"; break;
    case S_RETURN: what = "return"; break;
    case S_MATCH: what = "match"; break;
    case S_IF: what = "if"; break;
    case S_EXPR: what = "an expression used as a statement"; break;
    case S_DEL: what = "del"; break;
    case S_WITH: what = "a with block"; break;
    default: break;
    }
    if (top)
        terr(t, s->line, "%s is not available at the top level of a checked file. The top level holds imports, "
                         "Data classes, defs, assignments, print and assert.", what);
    terr(t, s->line, "%s is not available inside a checked function. A checked function is built from "
                     "assignments, if, match and return.", what);
}

/* A run of statements with no split left, written as one expression. */
static void leaf(T *t, K *k, int line) {
    for (;;) {
        k = k_norm(k);
        if (!k) terr(t, line, "this function can reach its end without a return");
        Node *s = k->l[k->i];
        K *rest = k_tail(k);
        if (skippable(s)) { k = rest; continue; }
        switch (s->kind) {
        case S_RETURN:
            if (!s->a) terr(t, s->line, "return needs a value in a checked function");
            for (int j = k->i + 1; j < k->n; j++)
                if (!skippable(k->l[j]))
                    terr(t, k->l[j]->line, "this statement can never run: the return above it always leaves the function");
            o_put(t->cur, s->line, af("  %s\n", E(t, s->a)));
            return;
        case S_ASSIGN: case S_ANNASSIGN: {
            Node *value, *ann;
            Str *name = assign_parts(t, s, &value, &ann);
            const char *e = E(t, value), *ty = ann ? tr_type(t, ann) : NULL;
            const char *a = fresh(t, name->s);
            o_put(t->cur, s->line, ty ? af("  let %s : %s ; %s = %s in\n", a, ty, a, e) : af("  let %s = %s in\n", a, e));
            bind(t, name, a, false, false, s->line);
            k = rest;
            continue;
        }
        default: bad_stmt(t, s, false);
        }
    }
}

/* A match pattern as Agda text. Captures are bound in the environment. */
static const char *pattern(T *t, Node *p, bool nat) {
    switch (p->kind) {
    case P_CAPTURE: {
        const char *a = lhs_var(t, fresh(t, p->val ? AS_STR(p->val)->s : "_"));
        if (p->val) bind(t, AS_STR(p->val), a, true, nat, p->line);
        return a;
    }
    case P_VALUE:
        if (p->a->kind != E_CONST) terr(t, p->line, "a dotted name is not available as a pattern in a checked file");
        return const_text(t, p->a);
    case P_AS: {
        const char *inner = pattern(t, p->a, nat);
        bind(t, AS_STR(p->val), inner, is_lhs_var(t, inner), nat, p->line);
        return inner;
    }
    case P_CLASS: {
        if (p->a->kind != E_NAME) terr(t, p->line, "a pattern names a constructor directly");
        if (p->n[1]) terr(t, p->line, "keyword patterns are not available in a checked file");
        bool in_type = t->c.in_type;
        t->c.in_type = false;
        Ref r = lookup(t, AS_STR(p->a->val), p->line);
        t->c.in_type = in_type;
        if (!r.s || r.s->kind != SK_CTOR) terr(t, p->line, "'%s' is not a constructor", AS_STR(p->a->val)->s);
        Sym *s = r.s;
        if (p->n[0] != s->arity)
            terr(t, p->line, "the pattern %s(...) must list all %d field%s of the constructor", s->py->s, s->arity,
                 s->arity == 1 ? "" : "s");
        if (!s->arity) return s->agda;
        const char *text = s->agda;
        for (int i = 0; i < p->n[0]; i++) {
            bool sub_nat = false;
            if (!s->prelude) {
                Ctx saved = t->c;
                t->c.env = NULL;
                sub_nat = ann_is_nat(t, s->node->params->ann[i]);
                t->c = saved;
            }
            text = af("%s %s", text, pattern(t, p->l[0][i], sub_nat));
        }
        return af("(%s)", text);
    }
    case P_OR: terr(t, p->line, "'|' is only available at the top of a case in a checked file");
    default: terr(t, p->line, "sequence and mapping patterns are not available in a checked file");
    }
}

static void clauses(T *t, const char *lhs, K *k, int line);

/* A case whose whole body is "return unreachable()". */
static bool is_unreachable(T *t, Node *c) {
    Node *only = NULL;
    for (int i = 0; i < c->n[0]; i++) {
        if (skippable(c->l[0][i])) continue;
        if (only) return false;
        only = c->l[0][i];
    }
    if (!only || only->kind != S_RETURN || !only->a || only->a->kind != E_CALL || only->a->n[0]) return false;
    Node *f = only->a->a;
    if (f->kind != E_NAME || !str_eqc(AS_STR(f->val), "unreachable")) return false;
    for (Var *v = t->c.env; v; v = v->next) if (v->py && str_eq(v->py, AS_STR(f->val))) return false;
    return !find_sym(t, AS_STR(f->val)) && sym_is(prelude_sym(t, AS_STR(f->val)), "unreachable");
}

/* The c of `decide(c)`, when decide is the one imported from dependent. */
static Node *decide_arg(T *t, Node *e) {
    if (e->kind != E_CALL || e->n[0] != 1 || e->a->kind != E_NAME || !str_eqc(AS_STR(e->a->val), "decide")) return NULL;
    if (e->l[0][0]->kind == E_KEYWORD || e->l[0][0]->kind == E_STARRED) return NULL;
    for (Var *v = t->c.env; v; v = v->next) if (v->py && str_eq(v->py, AS_STR(e->a->val))) return NULL;
    if (find_sym(t, AS_STR(e->a->val)) || !sym_is(prelude_sym(t, AS_STR(e->a->val)), "decide")) return NULL;
    return e->l[0][0];
}

/* When no case of a match fits, Python carries on with what follows the match, so
   that code becomes a last clause. If it reads a name that only the cases assign,
   it can only be correct when the cases cover everything: the clause is left out,
   and Agda then insists that they do. */
static void fall_through(T *t, const char *lhs, K *rest, int line) {
    jmp_buf jb, *outer = t->try_jmp;
    Sym *fn = t->fn;
    Ctx before = t->c;
    Out *cur = t->cur;
    size_t n = cur->n;
    int nl = cur->nl;
    if (setjmp(jb) == 0) {
        t->try_jmp = &jb;
        clauses(t, lhs, rest, line);
        t->try_jmp = outer;
        return;
    }
    t->try_jmp = outer;
    t->c = before;
    t->cur = cur;
    cur->n = n; cur->nl = nl;
    if (cur->s) cur->s[n] = 0;
    /* Definitions that were being written when the attempt stopped are no longer in progress. */
    t->fn = fn;
    for (int i = 0; i < t->nsyms; i++) { t->syms[i]->busy = false; t->syms[i]->in_body = t->syms[i] == fn; }
}

static void clauses(T *t, const char *lhs, K *k, int line) {
    for (;;) {
        k = k_norm(k);
        if (!k) terr(t, line, "this function can reach its end without a return");
        Node *s = k->l[k->i];
        K *rest = k_tail(k);
        if (skippable(s)) { k = rest; continue; }
        if (!has_split(k)) {
            o_put(t->cur, s->line, af("%s =\n", lhs));
            leaf(t, k, line);
            return;
        }
        switch (s->kind) {
        case S_ASSIGN: case S_ANNASSIGN: {
            Node *value, *ann;
            Str *name = assign_parts(t, s, &value, &ann);
            const char *e = E(t, value);
            bool nat = false;
            if (var_by_term(t, e, &nat)) {
                /* Another name for a variable that is already there. An annotation is
                   still checked, in a column that nothing reads. */
                if (ann) {
                    o_put(t->cur, s->line, af("%s with the %s %s\n", lhs, tr_type(t, ann), e));
                    lhs = af("%s | %s", lhs, fresh(t, "_"));
                }
                bind(t, name, e, true, nat, s->line);
            } else if (k_splits_on(rest, name)) {
                /* The name is taken apart later, so it has to be a variable of the
                   left-hand side: a column. */
                const char *a = lhs_var(t, fresh(t, name->s));
                o_put(t->cur, s->line, ann ? af("%s with the %s %s\n", lhs, tr_type(t, ann), e) : af("%s with %s\n", lhs, e));
                lhs = af("%s | %s", lhs, a);
                bind(t, name, a, true, ann_is_nat(t, ann), s->line);
            } else {
                /* Otherwise the name stands for its expression, so Agda still sees that
                   the two are the same thing. */
                bind(t, name, ann ? af("(the %s %s)", tr_type(t, ann), e) : e, false, false, s->line);
            }
            k = rest;
            continue;
        }
        case S_IF: {
            K *kt = k_push(s->l[0], s->n[0], rest), *ke = k_push(s->l[1], s->n[1], rest);
            Var *saved = t->c.env;
            bool then_is_zero = false, nat = false;
            Node *x = zero_test(s->a, &then_is_zero);
            const char *xt = x ? E(t, x) : NULL;
            if (x && var_by_term(t, xt, &nat)) {
                /* Only a Nat can be compared with 0, and a Nat is zero or suc of
                   something: two sets of clauses. */
                const char *p = lhs_var(t, fresh_agda(t, xt));
                const char *suc = af("(suc %s)", p);
                Var *env_zero = env_subst(t, xt, "zero", false, NULL), *env_suc = env_subst(t, xt, suc, false, p);
                const char *lhs_zero = subst_ident(lhs, xt, "zero"), *lhs_suc = subst_ident(lhs, xt, suc);
                t->c.env = then_is_zero ? env_zero : env_suc;
                clauses(t, then_is_zero ? lhs_zero : lhs_suc, kt, s->line);
                t->c.env = then_is_zero ? env_suc : env_zero;
                clauses(t, then_is_zero ? lhs_suc : lhs_zero, ke, s->line);
            } else {
                o_put(t->cur, s->line, af("%s with %s\n", lhs, E(t, s->a)));
                clauses(t, af("%s | true", lhs), kt, s->line);
                t->c.env = saved;
                clauses(t, af("%s | false", lhs), ke, s->line);
            }
            t->c.env = saved;
            return;
        }
        case S_MATCH: {
            Node *cond = decide_arg(t, s->a);
            if (cond) {
                /* match decide(c): case Yes(p) / case No(p). Agda's `with c in eq`: the
                   condition becomes true or false in the types of each branch, and p is
                   the proof that c equals that value. */
                /* The proof gets one Agda name for both cases: the first name a case gives it. */
                const char *eq_py = "proof";
                for (int i = s->n[0] - 1; i >= 0; i--) {
                    Node *p = s->l[0][i]->a;
                    if (p->kind == P_CLASS && p->n[0] == 1 && p->l[0][0]->kind == P_CAPTURE && p->l[0][0]->val)
                        eq_py = AS_STR(p->l[0][0]->val)->s;
                }
                const char *eq = fresh(t, eq_py);
                o_put(t->cur, s->line, af("%s with %s in %s\n", lhs, E(t, cond), eq));
                Var *saved = t->c.env;
                bool seen[2] = {false, false};
                for (int i = 0; i < s->n[0]; i++) {
                    Node *c = s->l[0][i], *p = c->a;
                    if (c->b) terr(t, c->line, "'case ... if' is not available in a checked file; use an if inside the case");
                    bool shape = p->kind == P_CLASS && p->a->kind == E_NAME && !p->n[1] && p->n[0] == 1 && p->l[0][0]->kind == P_CAPTURE;
                    Ref r = {0};
                    if (shape) r = lookup(t, AS_STR(p->a->val), p->line);
                    bool yes = shape && sym_is(r.s, "Yes");
                    if (!yes && !(shape && sym_is(r.s, "No")))
                        terr(t, c->line, "the cases under 'match decide(...)' are 'case Yes(p):' and 'case No(p):'");
                    if (seen[yes]) terr(t, c->line, "this case is written twice");
                    seen[yes] = true;
                    t->c.env = saved;
                    if (p->l[0][0]->val) bind(t, AS_STR(p->l[0][0]->val), eq, false, false, c->line);
                    clauses(t, af("%s | %s", lhs, yes ? "true" : "false"), k_push(c->l[0], c->n[0], rest), c->line);
                }
                t->c.env = saved;
                if (!seen[0] || !seen[1])
                    terr(t, s->line, "'match decide(...)' needs both cases: 'case Yes(p):' and 'case No(p):'");
                return;
            }
            const char *target = E(t, s->a);
            bool nat = false;
            if (!var_by_term(t, target, &nat)) {
                const char *a = lhs_var(t, fresh(t, "m"));
                o_put(t->cur, s->line, af("%s with %s\n", lhs, target));
                lhs = af("%s | %s", lhs, a);
                target = a;
            }
            Var *saved = t->c.env;
            bool catch_all = false;
            for (int i = 0; i < s->n[0]; i++) {
                Node *c = s->l[0][i];
                if (c->b) terr(t, c->line, "'case ... if' is not available in a checked file; use an if inside the case");
                if (c->a->kind == P_CAPTURE) catch_all = true;
                if (is_unreachable(t, c)) {
                    /* Agda's absurd pattern: accepted only when no value can get here. */
                    if (c->a->kind != P_CAPTURE)
                        terr(t, c->line, "unreachable() goes under 'case _:', and Agda then checks that no value fits");
                    o_put(t->cur, c->line, af("%s\n", subst_ident(lhs, target, "()")));
                    continue;
                }
                Node *one[1] = {c->a}, **alts = one;
                int nalts = 1;
                if (c->a->kind == P_OR) { alts = c->a->l[0]; nalts = c->a->n[0]; }
                for (int j = 0; j < nalts; j++) {
                    t->c.env = saved;
                    const char *p = pattern(t, alts[j], nat);
                    /* The new bindings must not be rewritten: they are the replacement. */
                    Var *bound = t->c.env;
                    t->c.env = saved;
                    Var *env = env_subst(t, target, p, is_lhs_var(t, p), NULL);
                    Var *head = NULL, **tail = &head;
                    for (Var *v = bound; v != saved; v = v->next) {
                        Var *nv = arena_alloc(sizeof(Var));
                        *nv = *v;
                        *tail = nv;
                        tail = &nv->next;
                    }
                    *tail = env;
                    t->c.env = head;
                    clauses(t, subst_ident(lhs, target, p), k_push(c->l[0], c->n[0], rest), c->line);
                }
            }
            t->c.env = saved;
            if (rest && !catch_all) fall_through(t, lhs, rest, s->line);
            return;
        }
        default: bad_stmt(t, s, false);
        }
    }
}

static void emit_func_body(T *t, Sym *s) {
    Node *def = s->node;
    Sym *outer_fn = t->fn;
    s->in_body = true;
    if (!s->sig_done) emit_func_sig(t, s);
    t->fn = s;
    Out chunk = {0}, *outer = t->cur;
    Ctx saved = t->c;
    t->c = (Ctx){0};
    t->cur = &chunk;
    int ntp = def->n[2], np = def->params->n;
    t->c.tp_used = arena_alloc((size_t)ntp + 1);
    const char *lhs = af("%s\x01", s->agda);
    for (int i = 0; i < ntp; i++) { mark_used(t, s->tp_names[i]); erased_var(t, AS_STR(def->l[2][i]->val), s->tp_names[i], i); }
    for (int i = 0; i < np; i++) {
        mark_used(t, s->p_names[i]);
        lhs_var(t, s->p_names[i]);
        bind(t, def->params->names[i], s->p_names[i], true, ann_is_nat(t, def->params->ann[i]), def->line);
        lhs = af("%s %s", lhs, s->p_names[i]);
    }
    collect_locals(t, def->l[0], def->n[0]);
    clauses(t, lhs, k_push(def->l[0], def->n[0], NULL), def->line);
    /* Bind the [] names the body mentioned, so its annotations can refer to them. */
    const char *binders = "";
    for (int i = 0; i < ntp; i++) if (t->c.tp_used[i]) binders = af("%s {%s = %s}", binders, s->tp_names[i], s->tp_names[i]);
    t->c = saved;
    t->cur = outer;
    for (int i = 0, at = 0; i < chunk.nl; i++) {
        char *nl = strchr(chunk.s + at, '\n');
        *nl = 0;
        char *mark = strchr(chunk.s + at, '\x01');
        if (mark) { *mark = 0; o_put(&t->out, chunk.lines[i], af("%s%s%s\n", chunk.s + at, binders, mark + 1)); }
        else o_put(&t->out, chunk.lines[i], af("%s\n", chunk.s + at));
        at = (int)(nl - chunk.s) + 1;
    }
    s->body_done = true;
    s->in_body = false;
    t->fn = outer_fn;
}

/* ---------- the top level ---------- */

static bool is_main_guard(Node *s) {
    if (s->kind != S_IF || s->n[1]) return false;
    Node *c = s->a;
    return c->kind == E_COMPARE && c->n[0] == 1 && c->ops[0] == CMP_EQ && c->a->kind == E_NAME &&
           str_eqc(AS_STR(c->a->val), "__name__") && c->l[0][0]->kind == E_CONST && IS_STR(c->l[0][0]->val) &&
           str_eqc(AS_STR(c->l[0][0]->val), "__main__");
}

static bool trivial_body(Node **l, int n) {
    for (int i = 0; i < n; i++) if (!skippable(l[i])) return false;
    return true;
}

static void declare(T *t, Str *name, int line) {
    Sym *old = find_sym(t, name);
    if (old) terr(t, line, "'%s' is already defined on line %d; a checked file defines each name once", name->s, old->line);
    for (int i = 0; i < NPRELUDE; i++)
        if (str_eqc(name, prelude_names[i].py) && imported(t, prelude_names[i].py))
            terr(t, line, "'%s' is imported from dependent and cannot be defined again", name->s);
}

/* Reads the imports. They are read before any name is declared, so a later import
   cannot go unnoticed behind an earlier def of the same name. */
static void read_imports(T *t, Node **l, int n) {
    for (int i = 0; i < n; i++) {
        Node *s = l[i];
        if (s->kind == S_IMPORT) terr(t, s->line, "write 'from dependent import ...'; a checked file has no other imports");
        if (s->kind != S_IMPORTFROM) continue;
        Str *mod = AS_STR(s->val);
        if (str_eqc(mod, "__future__")) {
            for (int j = 0; j < s->n[0]; j++) if (str_eqc(AS_STR(s->l[0][j]->val), "annotations")) t->future_ann = true;
        } else if (str_eqc(mod, "dependent")) {
            if (s->op) { t->import_all = true; continue; }
            for (int j = 0; j < s->n[0]; j++) {
                Node *a = s->l[0][j];
                if (a->val2 && !str_eq(AS_STR(a->val2), AS_STR(a->val)))
                    terr(t, s->line, "'import ... as' is not available for dependent in a checked file");
                bool known = false;
                for (int k = 0; k < NPRELUDE; k++) if (str_eqc(AS_STR(a->val), prelude_names[k].py)) known = true;
                if (!known) terr(t, s->line, "dependent has no name '%s'", AS_STR(a->val)->s);
                Str **ni = arena_alloc((size_t)(t->nimported + 1) * sizeof *ni);
                if (t->nimported) memcpy(ni, t->imported, (size_t)t->nimported * sizeof *ni);
                ni[t->nimported++] = AS_STR(a->val);
                t->imported = ni;
            }
        } else terr(t, s->line, "a checked file can import only from dependent and __future__");
    }
}

/* First pass: learn every top-level name, so later definitions can be referred to. */
static void prepass(T *t, Node **l, int n, bool guard) {
    for (int i = 0; i < n; i++) {
        Node *s = l[i];
        if (guard && s->kind != S_ASSIGN && s->kind != S_ANNASSIGN && s->kind != S_EXPR && s->kind != S_ASSERT && s->kind != S_PASS)
            terr(t, s->line, "the block under 'if __name__ == \"__main__\":' holds assignments, print and assert only");
        switch (s->kind) {
        case S_CLASS: {
            Str *name = AS_STR(s->val);
            declare(t, name, s->line);
            bool ok = s->n[2] == 1 && s->l[2][0]->kind == E_NAME && !find_sym(t, AS_STR(s->l[2][0]->val)) &&
                      sym_is(prelude_sym(t, AS_STR(s->l[2][0]->val)), "Data");
            if (!ok || s->n[1]) terr(t, s->line, "a class in a checked file is written 'class %s[...](Data): ...'", name->s);
            if (!trivial_body(s->l[0], s->n[0])) terr(t, s->line, "the body of a Data class is '...'; its constructors are separate defs");
            Sym *d = add_sym(t, name, SK_DATA, s->n[3], s, s->line);
            d->agda = top_name(name->s);
            break;
        }
        case S_DEF: {
            Str *name = AS_STR(s->val);
            declare(t, name, s->line);
            if (!s->n[1]) {
                Sym *f = add_sym(t, name, SK_FUNC, s->params->n, s, s->line);
                f->agda = top_name(name->s);
                break;
            }
            bool ok = s->n[1] == 1 && s->l[1][0]->kind == E_NAME && !find_sym(t, AS_STR(s->l[1][0]->val)) &&
                      sym_is(prelude_sym(t, AS_STR(s->l[1][0]->val)), "constructor");
            if (!ok) terr(t, s->line, "the only decorator in a checked file is @constructor");
            if (!trivial_body(s->l[0], s->n[0])) terr(t, s->line, "the body of a constructor is '...'");
            Node *ret = s->b;
            if (ret && ret->kind == E_SUBSCR) ret = ret->a;
            Sym *fam = ret && ret->kind == E_NAME ? find_sym(t, AS_STR(ret->val)) : NULL;
            if (!fam || fam->kind != SK_DATA)
                terr(t, s->line, "a constructor's return annotation names its Data class, which must be defined above it");
            Sym *k = add_sym(t, name, SK_CTOR, s->params->n, s, s->line);
            k->agda = top_name(name->s);
            k->family = fam;
            Sym **nc = arena_alloc((size_t)(fam->nctors + 1) * sizeof *nc);
            if (fam->nctors) memcpy(nc, fam->ctors, (size_t)fam->nctors * sizeof *nc);
            nc[fam->nctors++] = k;
            fam->ctors = nc;
            break;
        }
        case S_ASSIGN: case S_ANNASSIGN: {
            Node *value, *ann;
            Str *name = assign_parts(t, s, &value, &ann);
            declare(t, name, s->line);
            Sym *v = add_sym(t, name, SK_VALUE, 0, s, s->line);
            v->agda = top_name(name->s);
            break;
        }
        case S_IF:
            if (!is_main_guard(s)) break;
            /* The block is skipped when the file is imported, so nothing outside it may depend on it. */
            if (i + 1 != n) terr(t, s->line, "'if __name__ == \"__main__\":' must be the last statement of a checked file");
            prepass(t, s->l[0], s->n[0], true);
            break;
        default: break;
        }
    }
}

/* A top-level statement that runs code: every function it could reach must be defined by now. */
static void begin_executed(T *t, Node *s) {
    t->stmt_line = s->line;
    for (int i = 0; i < t->nsyms; i++) {
        Sym *x = t->syms[i];
        if (x->rt_ref && x->line > s->line)
            terr(t, s->line, "this line runs code, and a function above it uses '%s', which is defined later on line %d. "
                             "Move that definition above this line.", x->py->s, x->line);
    }
    t->c = (Ctx){0};
    t->c.executing = true;
}

static void toplevel(T *t, Node **l, int n) {
    for (int i = 0; i < n; i++) {
        Node *s = l[i];
        t->cur = &t->out;
        switch (s->kind) {
        case S_IMPORTFROM: case S_PASS: break;
        case S_CLASS: {
            Sym *d = find_sym(t, AS_STR(s->val));
            if (!d->body_done) emit_data_body(t, d);
            break;
        }
        case S_DEF: {
            Sym *f = find_sym(t, AS_STR(s->val));
            if (f->kind == SK_FUNC && !f->body_done) emit_func_body(t, f);
            break;
        }
        case S_ASSIGN: case S_ANNASSIGN: {
            Node *value, *ann;
            Str *name = assign_parts(t, s, &value, &ann);
            Sym *v = find_sym(t, name);
            begin_executed(t, s);
            const char *ty = ann ? tr_type(t, ann) : NULL;
            const char *e = E(t, value);
            if (ty) o_put(&t->out, s->line, af("%s : %s\n", v->agda, ty));
            o_put(&t->out, s->line, af("%s = %s\n", v->agda, e));
            v->body_done = true;
            break;
        }
        case S_EXPR: {
            if (s->a->kind == E_CONST) break;
            Node *c = s->a;
            if (c->kind != E_CALL || c->a->kind != E_NAME || !str_eqc(AS_STR(c->a->val), "print") || find_sym(t, AS_STR(c->a->val)))
                terr(t, s->line, "the only expression statement in a checked file is a call to print");
            begin_executed(t, s);
            for (int j = 0; j < c->n[0]; j++) {
                Node *a = c->l[0][j];
                if (a->kind == E_STARRED) terr(t, a->line, "* arguments are not available in a checked file");
                if (a->kind == E_KEYWORD) {
                    bool ok = a->val && (str_eqc(AS_STR(a->val), "sep") || str_eqc(AS_STR(a->val), "end"));
                    if (!ok) terr(t, a->line, "print in a checked file takes sep= and end= only");
                    o_put(&t->out, s->line, af("_ : String\n_ = %s\n", E(t, a->a)));
                } else if (a->kind != E_CONST) o_put(&t->out, s->line, af("_ = %s\n", E(t, a)));
            }
            break;
        }
        case S_ASSERT: {
            /* Checked now, by Agda working the expression out. */
            if (s->b && s->b->kind != E_CONST) terr(t, s->line, "an assert message in a checked file is a plain string");
            begin_executed(t, s);
            o_put(&t->out, s->line, af("_ : %s \xE2\x89\xA1 true\n_ = refl\n", E(t, s->a)));
            break;
        }
        case S_IF:
            if (is_main_guard(s)) { toplevel(t, s->l[0], s->n[0]); break; }
            terr(t, s->line, "'if' at the top level of a checked file is only available as: if __name__ == \"__main__\":");
        default: bad_stmt(t, s, true);
        }
    }
}

/* ---------- running Agda ---------- */

static uint64_t fnv(uint64_t h, const char *s) {
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; }
    return h;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    SB b;
    sb_init(&b);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) sb_put(&b, buf, n);
    fclose(f);
    sb_putc(&b, 0);
    char *r = astr(b.s);
    sb_free(&b);
    return r;
}

static bool write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fputs(text, f) >= 0;
    return fclose(f) == 0 && ok;
}

static void mkdirs(char *path) {
    for (char *p = path + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(path, 0700); *p = '/'; }
    mkdir(path, 0700);
}

/* Runs `agda file` in dir. Returns its exit status, or -1 when agda could not be started. */
static int run_agda(const char *dir, const char *file, char **output) {
    const char *agda = getenv("PYC_AGDA");
    if (!agda || !*agda) agda = "agda";
    int fd[2];
    if (pipe(fd) < 0) return -1;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }
    if (pid == 0) {
        dup2(fd[1], 1);
        dup2(fd[1], 2);
        close(fd[0]); close(fd[1]);
        if (chdir(dir) < 0) _exit(126);
        execlp(agda, agda, "--no-libraries", file, (char *)NULL);
        _exit(127);
    }
    close(fd[1]);
    SB b;
    sb_init(&b);
    char buf[4096];
    for (;;) {
        ssize_t n = read(fd[0], buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        sb_put(&b, buf, (size_t)n);
    }
    close(fd[0]);
    sb_putc(&b, 0);
    *output = astr(b.s);
    sb_free(&b);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status) == 127 || WEXITSTATUS(status) == 126 ? -1 : WEXITSTATUS(status);
}

/* Rewrites Agda's report in Python terms: generated-file positions become Python
   line numbers and names get their underscores back. Sets *first to the Python
   line of the first error. */
static const char *map_report(T *t, const char *text, const char *module, int *first) {
    SB b;
    sb_init(&b);
    size_t ml = strlen(module);
    *first = 0;
    bool skipping = false, any = false;
    for (const char *line = text; *line;) {
        const char *end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        const char *p = line;
        while (p < end && *p == ' ') p++;
        bool progress = (size_t)(end - p) >= 9 && (memcmp(p, "Checking ", 9) == 0 || memcmp(p, "Finished ", 9) == 0);
        /* A block starts with "path:L.C-C: error: [Kind]" or "error: [Kind]". */
        const char *tag = NULL;
        for (const char *q = line; q + 8 < end; q++)
            if (memcmp(q, "error: [", 8) == 0 || memcmp(q, "warning: ", 9) == 0) { tag = q; break; }
        bool header = tag && (tag == line || tag[-1] == ' ') && (end[-1] == ']' || memcmp(tag, "warning: -W", 11) == 0);
        if (header) {
            skipping = memcmp(tag, "warning", 7) == 0;
            if (!skipping) {
                int py = 0;
                const char *m = line;
                while ((m = strstr(m, module)) && m < tag) {
                    if (memcmp(m + ml, ".agda:", 6) == 0) {
                        int l = atoi(m + ml + 6);
                        if (l >= 1 && l <= t->out.nl) py = t->out.lines[l - 1];
                        break;
                    }
                    m++;
                }
                /* Later errors usually follow from the first one, so the first one with a
                   position is the one shown. */
                if (*first) break;
                if (py) { *first = py; b.n = 0; }
                else if (any) sb_putc(&b, '\n');
                any = true;
                if (memcmp(tag, "error: [UnsolvedMetaVariables]", 30) == 0 || memcmp(tag, "error: [UnsolvedConstraints]", 28) == 0)
                    sb_puts(&b, "Agda could not work out a type here. Add a type annotation.\n");
                else if (memcmp(tag, "error: [TerminationIssue]", 25) == 0)
                    sb_puts(&b, "Agda could not see that this recursion ends. Each recursive call must be on a smaller argument.\n");
            }
        } else if (!progress && !skipping) {
            for (p = line; p < end;) {
                /* "/dir/Cxxxx.agda:12.3-9" becomes "line N". */
                const char *m = NULL;
                if ((size_t)(end - p) > ml + 6 && memcmp(p, module, ml) == 0 && memcmp(p + ml, ".agda:", 6) == 0) m = p;
                if (m) {
                    while (b.n && b.s[b.n - 1] != ' ' && b.s[b.n - 1] != '(' && b.s[b.n - 1] != '\n') b.n--;
                    int l = atoi(m + ml + 6);
                    sb_printf(&b, "line %d", l >= 1 && l <= t->out.nl ? t->out.lines[l - 1] : 0);
                    p = m + ml + 6;
                    while (p < end && (isdigit((unsigned char)*p) || *p == '.' || *p == '-')) p++;
                    continue;
                }
                if (memcmp(p, USCORE, 2) == 0 && p + 1 < end) { sb_putc(&b, '_'); p += 2; continue; }
                sb_putc(&b, *p++);
            }
            sb_putc(&b, '\n');
        }
        line = *end ? end + 1 : end;
    }
    while (b.n && b.s[b.n - 1] == '\n') b.n--;
    sb_putc(&b, 0);
    const char *r = astr(b.s);
    sb_free(&b);
    return r;
}

static const char *cache_dir(void) {
    const char *x = getenv("XDG_CACHE_HOME"), *h = getenv("HOME");
    char *dir;
    if (x && *x) dir = af("%s/pyc", x);
    else if (h && *h) dir = af("%s/.cache/pyc", h);
    else dir = af("/tmp/pyc-cache-%d", (int)getuid());
    dir = af("%s/%016llx", dir, (unsigned long long)fnv(1469598103934665603ULL, agda_prelude));
    mkdirs(dir);
    return dir;
}

bool agda_wanted(Node *m) {
    for (int i = 0; i < m->n[0]; i++) {
        Node *s = m->l[0][i];
        if (s->kind == S_IMPORTFROM && str_eqc(AS_STR(s->val), "dependent")) return true;
        if (s->kind == S_IMPORT)
            for (int j = 0; j < s->n[0]; j++) if (str_eqc(AS_STR(s->l[0][j]->val), "dependent")) return true;
    }
    return false;
}

/* Checks a parsed module. Returns when Agda accepts it; otherwise raises
   TypeCheckError through compile_jmp. */
void agda_check(Node *m, const char *filename) {
    T t = {0};
    t.filename = filename;
    read_imports(&t, m->l[0], m->n[0]);
    prepass(&t, m->l[0], m->n[0], false);
    if (!t.future_ann)
        terr(&t, 1, "a checked file must start with: from __future__ import annotations");
    toplevel(&t, m->l[0], m->n[0]);

    /* The module is named after a hash of its text, so the name is also the cache key. */
    const char *body = t.out.s ? t.out.s : "";
    uint64_t h = fnv(fnv(1469598103934665603ULL, agda_prelude), body);
    const char *module = af("C%016llx", (unsigned long long)h);
    const char *header = af("{-# OPTIONS --erasure --safe #-}\nmodule %s where\nopen import PycPrelude\n", module);
    const char *text = af("%s%s", header, body);
    /* Three header lines come before the first generated line. */
    int *lines = arena_alloc((size_t)(t.out.nl + 3) * sizeof(int));
    if (t.out.nl) memcpy(lines + 3, t.out.lines, (size_t)t.out.nl * sizeof(int));
    lines[0] = lines[1] = lines[2] = 0;
    t.out.lines = lines;
    t.out.nl += 3;

    if (agda_mode == 1) {
        fputs(text, stdout);
        fflush(stdout);
        exit(0);
    }

    const char *dir = cache_dir();
    const char *prelude_path = af("%s/PycPrelude.agda", dir), *path = af("%s/%s.agda", dir, module);
    const char *ok_path = af("%s/%s.ok", dir, module);
    char *have = read_file(prelude_path);
    if ((!have || strcmp(have, agda_prelude) != 0) && !write_file(prelude_path, agda_prelude))
        terr(&t, 0, "cannot write the type checker's files in %s", dir);
    /* A file that passed before, with the same text and prelude, is not checked again. */
    char *old = read_file(path), *mark = read_file(ok_path);
    if (old && mark && strcmp(old, text) == 0) return;
    remove(ok_path);
    if (!write_file(path, text)) terr(&t, 0, "cannot write the type checker's files in %s", dir);

    char *output = NULL;
    int status = run_agda(dir, af("%s.agda", module), &output);
    if (status < 0) remove(path);
    if (status < 0)
        terr(&t, 0, "this file imports dependent, which asks for type checking, and the agda program could not be "
                    "started. Install Agda, or set PYC_AGDA to its path. The file was not run.");
    if (status == 0) {
        write_file(ok_path, "ok\n");
        remove(af("%s/%s.agdai", dir, module));
        return;
    }
    int first = 0;
    const char *report = map_report(&t, output, module, &first);
    remove(path);
    if (!*report) report = af("agda failed with exit status %d and no message", status);
    check_fail(filename, first, report);
}
