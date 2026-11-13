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

enum { SK_FUNC, SK_DATA, SK_CTOR, SK_VALUE, SK_TYPE, SK_SPECIAL, SK_RECORD, SK_FIELD };
enum { TK_NAT = 1, TK_INT, TK_BOOL, TK_STR, TK_LIST, TK_TUPLE, TK_DICT, TK_FUN, TK_REC, TK_OTHER };

typedef struct Sym Sym;
typedef struct Ty Ty;
/* What is known of a type: list (a = items), dict (a = keys, b = values), tuple
   (items), function (items = arguments, a = result), class or Data family (sym). */
struct Ty { int k; Ty *a, *b; Ty **items; int n; Sym *sym; };

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
    Ty **p_tys; Ty *ret_ty;          /* def: what is known of its parameter and result types */
    Ty *ty;                          /* top-level value or field: what is known of its type */
    bool tys_done;
    Sym *owner; Str *mname;          /* method: its class and its name there */
    Sym **fields; int nfields;       /* class with fields */
    Sym **methods; int nmethods;
    const char *ctor;                /* class with fields: the Agda constructor */
    bool has_eq;
    bool trusted;                    /* @trusted def: its type is taken on trust */
    bool external;                   /* imported from another checked file */
};

/* A local name. `term` is the Agda text it stands for. An entry with no name is
   a fact: `term` is a proof that the condition `fact` is true or false (`fpol`). */
typedef struct Var Var;
struct Var {
    Str *py;
    const char *term;
    const char *pred;    /* Agda text for `name - 1`, known after a zero test */
    bool split;          /* term is a variable of the left-hand side, so it can be matched on */
    bool nat, erased, captured;
    bool fresh0;         /* bound to a new list or dict that nothing else referred to */
    bool dead;           /* the variable of a for loop that has ended */
    int depth, tp, id;
    Ty *ty;
    const char *fact; bool fpol;
    int fid;             /* fact from an `if` written as a with: which one, counted from 1 */
    Var *next;
};

/* The names (by id) that have been handed on since they were bound. */
typedef struct Leak Leak;
struct Leak { int id; Leak *next; };

typedef struct {
    Var *env;
    bool in_type, executing;
    int lambda_depth, noleak;
    Leak *leaks;
    Ty *ret_ty;                           /* what the function being written returns */
    Str **ints; int nints;                /* locals that hold an int somewhere in the function */
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
    int next_id, nholes;
    int *auto_lines; int nauto, acap;    /* Python lines where refl stands in for a proof */
    bool *fact_used; int nfacts, fcap;   /* whether anything asked for the proof of each `if` */
    bool unsafe;        /* the file has a @trusted def, or imports a file that has */
    const char *imports; /* the `open import` lines for the checked files this one imports */
    int nimport_lines;
    Node **import_stmts; int nimport_stmts;   /* `from other_file import ...` statements */
    const char *module;  /* the Agda module this file became */
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
    {"to_nat", "toNat", SK_FUNC, 1}, {"known", NULL, SK_SPECIAL, 0}, {"decreases", NULL, SK_SPECIAL, 0},
    {"hole", NULL, SK_SPECIAL, 0}, {"dataclass", NULL, SK_SPECIAL, 0}, {"trusted", NULL, SK_SPECIAL, 0},
};
/* Type names that need no import. */
static const struct { const char *py, *agda; int arity; } builtin_types[] = {
    {"bool", "Bool", 0}, {"str", "String", 0}, {"int", "Int", 0}, {"list", NULL, 1}, {"tuple", NULL, 2}, {"dict", NULL, 2},
};
#define NBUILTIN ((int)(sizeof builtin_types / sizeof builtin_types[0]))
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
    "variable", "where", "with", "Yes", "No", "decide",
    "notFalse", "notTrue", "unNot", "ifd", "natSub", "toNat", "divNat", "modNat", "divInt", "modInt", "powNat", "powInt",
    "minNat", "maxNat", "minInt", "maxInt", "showNat", "showInt", "len", "mapL", "concatMap", "eqList", "elem", "index",
    "setAt", "take", "drop", "slice", "countFrom", "revOnto", "reverse", "enumFrom", "zip", "sumNat", "sumInt", "anyL",
    "allL", "insertBy", "sortBy", "sortedNat", "sortedInt", "strLen", "Dict", "mkDict", "items", "hasKey", "has", "getOr",
    "dget", "lookupKey", "dindex", "putKey", "dset", "dictOf", "dictFrom", "dlen", "dkeys", "dvalues", "Step", "next",
    "stop", "ret", "Out", "out", "early", "runLoop", "forEach", "forUpTo", "forRange", "WStep", "wnext", "wstop", "wret",
    "whileFuel", "whileDo", "Int", "pos", "negsuc", "List", "fst", "snd", "tt", "eqInt", "EqInt", "EqPair", "EqUnit", "EqList",
    "EqNat", "EqBool", "EqString", "\xCE\xBB", "\xE2\x88\x80", NULL};

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
    static Sym table[NPRELUDE + NBUILTIN];
    static bool ready;
    if (!ready) {
        for (int i = 0; i < NPRELUDE; i++) {
            table[i].agda = prelude_names[i].agda; table[i].kind = prelude_names[i].kind;
            table[i].arity = prelude_names[i].arity; table[i].prelude = true;
        }
        for (int i = 0; i < NBUILTIN; i++) {
            Sym *b = &table[NPRELUDE + i];
            b->agda = builtin_types[i].agda; b->kind = SK_TYPE; b->arity = builtin_types[i].arity; b->prelude = true;
        }
        ready = true;
    }
    for (int i = 0; i < NPRELUDE; i++)
        if (str_eqc(name, prelude_names[i].py) && imported(t, prelude_names[i].py)) { table[i].py = name; return &table[i]; }
    for (int i = 0; i < NBUILTIN; i++)
        if (str_eqc(name, builtin_types[i].py)) { table[NPRELUDE + i].py = name; return &table[NPRELUDE + i]; }
    return NULL;
}

static bool sym_is(Sym *s, const char *py) { return s && s->prelude && str_eqc(s->py, py); }

static void emit_func_sig(T *t, Sym *s);
static void emit_data_sig(T *t, Sym *s);
static void emit_data_body(T *t, Sym *s);
static void emit_func_body(T *t, Sym *s);
static void emit_record(T *t, Sym *s);

/* Makes sure Agda has seen what it needs of `s` before the text being written,
   and checks that Python will have defined it by the time it is used. */
static void need(T *t, Sym *s, int line) {
    if (s->prelude || s->external) return;
    switch (s->kind) {
    case SK_RECORD: if (!s->body_done) emit_record(t, s); break;
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
static Ty ty_nat = {TK_NAT, 0, 0, 0, 0, 0}, ty_int = {TK_INT, 0, 0, 0, 0, 0}, ty_bool = {TK_BOOL, 0, 0, 0, 0, 0},
          ty_str = {TK_STR, 0, 0, 0, 0, 0};

static Ref lookup(T *t, Str *name, int line) {
    Ref r = {0};
    for (Var *v = t->c.env; v; v = v->next)
        if (v->py && str_eq(v->py, name)) {
            if (v->erased && !t->c.in_type)
                terr(t, line, "'%s' is a name in square brackets. Such names exist only for the type checker, "
                              "so they can appear in annotations and nowhere else.", name->s);
            if (v->dead)
                terr(t, line, "'%s' was the variable of a for loop that has ended. Inside the loop, give its value to "
                              "another name to use it afterwards.", name->s);
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
    if (str_eqc(name, "float") || str_eqc(name, "complex"))
        terr(t, line, "'%s' is not available in a checked file. The number types are Nat (0, 1, 2, ...) and int.", name->s);
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
    v->id = ++t->next_id;
    if (nat) v->ty = &ty_nat;
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
            if (v->fact) nv->fact = subst_ident(v->fact, name, repl);
        }
        nv->next = NULL;
        *tail = nv;
        tail = &nv->next;
    }
    return head;
}

/* ---------- what the translator knows about types ---------- */

/* The translation is mostly untyped: Agda does the checking. A few choices need
   the type of a value all the same: whether + adds Nats, ints, strings or lists,
   whether a literal 3 is a Nat or an int, what xs[i] and p.x mean. For these the
   translator follows the annotations. NULL means "not known", and then numbers
   are taken to be Nat. A wrong guess here cannot make a wrong program pass: Agda
   still checks the text that comes out. */

static int tk(Ty *ty) { return ty ? ty->k : 0; }

static Ty *mk_ty(int k, Ty *a, Ty *b) {
    Ty *r = arena_alloc(sizeof(Ty));
    memset(r, 0, sizeof *r);
    r->k = k; r->a = a; r->b = b;
    return r;
}

static Ty *ty_tuple(Ty **items, int n) {
    Ty *r = mk_ty(TK_TUPLE, NULL, NULL);
    r->items = arena_alloc((size_t)(n + 1) * sizeof(Ty *));
    for (int i = 0; i < n; i++) r->items[i] = items[i];
    r->n = n;
    return r;
}

static Var *find_var(T *t, Str *name) {
    for (Var *v = t->c.env; v; v = v->next) if (v->py && str_eq(v->py, name)) return v;
    return NULL;
}

static bool is_tparam_of(Node *def, Str *nm) {
    if (!def) return false;
    int slot = def->kind == S_CLASS ? 3 : 2;
    for (int i = 0; i < def->n[slot]; i++) if (str_eq(AS_STR(def->l[slot][i]->val), nm)) return true;
    return false;
}

/* The type an annotation stands for. `def` and `cls` are the def and class whose
   square-bracket names are in scope; with neither, the current environment is. */
static Ty *ty_ann(T *t, Node *ann, Node *def, Node *cls) {
    if (!ann) return NULL;
    Node *head = ann->kind == E_SUBSCR ? ann->a : ann;
    if (head->kind != E_NAME) return NULL;
    Str *nm = AS_STR(head->val);
    if (def || cls) { if (is_tparam_of(def, nm) || is_tparam_of(cls, nm)) return NULL; }
    else if (find_var(t, nm)) return NULL;
    Sym *s = find_sym(t, nm);
    if (s) {
        if (s->kind != SK_RECORD && s->kind != SK_DATA) return NULL;
        Ty *r = mk_ty(s->kind == SK_RECORD ? TK_REC : TK_OTHER, NULL, NULL);
        r->sym = s;
        return r;
    }
    if (ann->kind == E_NAME) {
        if (str_eqc(nm, "int")) return &ty_int;
        if (str_eqc(nm, "bool")) return &ty_bool;
        if (str_eqc(nm, "str")) return &ty_str;
        if (str_eqc(nm, "Nat") && imported(t, "Nat")) return &ty_nat;
        return NULL;
    }
    Node *one[1] = {ann->b}, **args = one;
    int argc = 1;
    if (ann->b->kind == E_TUPLE) { args = ann->b->l[0]; argc = ann->b->n[0]; }
    if (str_eqc(nm, "list") && argc == 1) return mk_ty(TK_LIST, ty_ann(t, args[0], def, cls), NULL);
    if (str_eqc(nm, "dict") && argc == 2) return mk_ty(TK_DICT, ty_ann(t, args[0], def, cls), ty_ann(t, args[1], def, cls));
    if (str_eqc(nm, "tuple")) {
        Ty **items = arena_alloc((size_t)(argc + 1) * sizeof(Ty *));
        for (int i = 0; i < argc; i++) items[i] = ty_ann(t, args[i], def, cls);
        return ty_tuple(items, argc);
    }
    bool callable = str_eqc(nm, "Callable") && imported(t, "Callable") && argc == 2 && args[0]->kind == E_LIST;
    bool fn = str_eqc(nm, "Fn") && imported(t, "Fn");
    if (callable || fn) {
        Node **ps = callable ? args[0]->l[0] : args;
        int np = callable ? args[0]->n[0] : argc - 1;
        Ty **items = arena_alloc((size_t)(np + 1) * sizeof(Ty *));
        for (int i = 0; i < np; i++) items[i] = ty_ann(t, ps[i]->kind == E_SLICE ? ps[i]->b : ps[i], def, cls);
        Ty *r = ty_tuple(items, np);
        r->k = TK_FUN;
        r->a = ty_ann(t, args[argc - 1], def, cls);
        return r;
    }
    return NULL;
}

/* The parameter and result types of a def, for its callers. */
static void sym_tys(T *t, Sym *s) {
    if (s->tys_done || s->prelude || !s->node || s->node->kind != S_DEF) return;
    s->tys_done = true;
    Node *def = s->node, *cls = s->owner ? s->owner->node : NULL;
    int np = def->params->n;
    s->p_tys = arena_alloc((size_t)(np + 1) * sizeof(Ty *));
    for (int i = 0; i < np; i++) s->p_tys[i] = def->params->ann[i] ? ty_ann(t, def->params->ann[i], def, cls) : NULL;
    if (s->owner && np && !def->params->ann[0]) {   /* self */
        s->p_tys[0] = mk_ty(TK_REC, NULL, NULL);
        s->p_tys[0]->sym = s->owner;
    }
    s->ret_ty = def->b ? ty_ann(t, def->b, def, cls) : NULL;
}

static bool is_int_const(Node *n) {
    return n->kind == E_CONST && KIND(n->val) == K_INT && n->val != Py_True && n->val != Py_False;
}

/* The name of a built-in function, when `f` is that name and the file has not
   given the name another meaning. */
static const char *plain_builtin(T *t, Node *f) {
    static const char *const names[] = {"len", "int", "abs", "str", "min", "max", "sum", "any", "all", "sorted", "list",
                                        "range", "enumerate", "zip", "reversed", NULL};
    if (f->kind != E_NAME) return NULL;
    Str *nm = AS_STR(f->val);
    if (find_var(t, nm) || find_sym(t, nm)) return NULL;
    for (int i = 0; names[i]; i++) if (str_eqc(nm, names[i])) return names[i];
    return NULL;
}

static Sym *field_of(Sym *rec, Str *name);
static Sym *method_of(T *t, Sym *rec, Str *name);
static Ty *ty_of(T *t, Node *n);

/* The type of the items that iterating over `n` gives. */
static Ty *iter_elem(T *t, Node *n) {
    if (n->kind == E_CALL) {
        const char *b = plain_builtin(t, n->a);
        int argc = n->n[0];
        if (b && !strcmp(b, "range")) return &ty_nat;
        if (b && argc == 1 && (!strcmp(b, "reversed") || !strcmp(b, "list") || !strcmp(b, "sorted"))) return iter_elem(t, n->l[0][0]);
        if (b && argc == 1 && !strcmp(b, "enumerate")) {
            Ty *items[2] = {&ty_nat, iter_elem(t, n->l[0][0])};
            return ty_tuple(items, 2);
        }
        if (b && argc == 2 && !strcmp(b, "zip")) {
            Ty *items[2] = {iter_elem(t, n->l[0][0]), iter_elem(t, n->l[0][1])};
            return ty_tuple(items, 2);
        }
        if (n->a->kind == E_ATTR && argc == 0) {
            Ty *d = ty_of(t, n->a->a);
            Str *m = AS_STR(n->a->val);
            if (tk(d) == TK_DICT && str_eqc(m, "keys")) return d->a;
            if (tk(d) == TK_DICT && str_eqc(m, "values")) return d->b;
            if (tk(d) == TK_DICT && str_eqc(m, "items")) { Ty *items[2] = {d->a, d->b}; return ty_tuple(items, 2); }
        }
    }
    Ty *ty = ty_of(t, n);
    if (tk(ty) == TK_LIST || tk(ty) == TK_DICT) return ty->a;
    return NULL;
}

static Ty *ty_of(T *t, Node *n) {
    switch (n->kind) {
    case E_CONST:
        if (n->val == Py_True || n->val == Py_False) return &ty_bool;
        if (KIND(n->val) == K_INT) return int_sign(n->val) < 0 ? &ty_int : &ty_nat;
        if (IS_STR(n->val)) return &ty_str;
        return NULL;
    case E_NAME: {
        Var *v = find_var(t, AS_STR(n->val));
        if (v) return v->ty;
        Sym *s = find_sym(t, AS_STR(n->val));
        return s && s->kind == SK_VALUE ? s->ty : NULL;
    }
    case E_CALL: {
        int argc = n->n[0];
        if (n->a->kind == E_ATTR) {
            Ty *r = ty_of(t, n->a->a);
            Str *m = AS_STR(n->a->val);
            if (tk(r) == TK_LIST && str_eqc(m, "copy")) return r;
            if (tk(r) == TK_DICT && str_eqc(m, "copy")) return r;
            if (tk(r) == TK_DICT && str_eqc(m, "get")) return r->b;
            if (tk(r) == TK_REC) { Sym *ms = method_of(t, r->sym, m); if (ms) { sym_tys(t, ms); return ms->ret_ty; } }
            return NULL;
        }
        if (n->a->kind != E_NAME) return NULL;
        const char *b = plain_builtin(t, n->a);
        if (b) {
            if (!strcmp(b, "len") || !strcmp(b, "abs")) return &ty_nat;
            if (!strcmp(b, "int")) return &ty_int;
            if (!strcmp(b, "str")) return &ty_str;
            if (!strcmp(b, "any") || !strcmp(b, "all")) return &ty_bool;
            if (!strcmp(b, "min") || !strcmp(b, "max")) {
                for (int i = 0; i < argc; i++) if (tk(ty_of(t, n->l[0][i])) == TK_INT) return &ty_int;
                return &ty_nat;
            }
            if (!strcmp(b, "sum")) return argc == 1 && tk(iter_elem(t, n->l[0][0])) == TK_INT ? &ty_int : &ty_nat;
            if (argc >= 1) return mk_ty(TK_LIST, iter_elem(t, n), NULL);
            return NULL;
        }
        Var *v = find_var(t, AS_STR(n->a->val));
        if (v) return tk(v->ty) == TK_FUN ? v->ty->a : NULL;
        Sym *s = find_sym(t, AS_STR(n->a->val));
        if (s && s->kind == SK_RECORD) { Ty *r = mk_ty(TK_REC, NULL, NULL); r->sym = s; return r; }
        if (s && s->kind == SK_FUNC) { sym_tys(t, s); return s->ret_ty; }
        if (s && s->kind == SK_VALUE) return tk(s->ty) == TK_FUN ? s->ty->a : NULL;
        if (!s) {
            Str *nm = AS_STR(n->a->val);
            if (str_eqc(nm, "suc") || str_eqc(nm, "pred") || str_eqc(nm, "monus") || str_eqc(nm, "to_nat")) return &ty_nat;
            if (str_eqc(nm, "concat")) return &ty_str;
        }
        return NULL;
    }
    case E_UNARY: return n->op == '-' ? &ty_int : NULL;
    case E_BINOP: {
        Ty *a = ty_of(t, n->a), *b = ty_of(t, n->b);
        if (tk(a) == TK_INT || tk(b) == TK_INT) return &ty_int;
        if (tk(a) == TK_STR || tk(b) == TK_STR) return &ty_str;
        if (tk(a) == TK_LIST) return a;
        if (tk(b) == TK_LIST) return b;
        return &ty_nat;
    }
    case E_COMPARE: case E_NOT: case E_BOOLOP: return &ty_bool;
    case E_IFEXP: { Ty *a = ty_of(t, n->b); return a ? a : ty_of(t, n->c); }
    case E_TUPLE: {
        Ty **items = arena_alloc((size_t)(n->n[0] + 1) * sizeof(Ty *));
        for (int i = 0; i < n->n[0]; i++) items[i] = ty_of(t, n->l[0][i]);
        return ty_tuple(items, n->n[0]);
    }
    case E_LIST: return mk_ty(TK_LIST, n->n[0] ? ty_of(t, n->l[0][0]) : NULL, NULL);
    case E_LISTCOMP: return mk_ty(TK_LIST, NULL, NULL);
    case E_DICT:
        return mk_ty(TK_DICT, n->n[0] && n->l[0][0] ? ty_of(t, n->l[0][0]) : NULL, n->n[0] ? ty_of(t, n->l[1][0]) : NULL);
    case E_SUBSCR: {
        Ty *b = ty_of(t, n->a);
        if (tk(b) == TK_LIST) return n->b->kind == E_SLICE ? b : b->a;
        if (tk(b) == TK_DICT) return b->b;
        if (tk(b) == TK_TUPLE && is_int_const(n->b)) {
            int64_t i = -1;
            if (int_fits(n->b->val, &i) && i >= 0 && i < b->n) return b->items[i];
        }
        return NULL;
    }
    case E_ATTR: {
        Ty *b = ty_of(t, n->a);
        if (tk(b) != TK_REC) return NULL;
        Sym *f = field_of(b->sym, AS_STR(n->val));
        return f ? f->ty : NULL;
    }
    default: return NULL;
    }
}

/* ---------- facts and sharing ---------- */

/* A fact is a proof in scope that a condition is true or false. `if c:` records
   one for each branch. They are found again by the Agda text of the condition,
   to fill in the proof that xs[i], a // b and d[k] need, and for known(c). A fact
   is only ever a candidate proof: Agda checks that it proves what it is used for. */
static void add_fact(T *t, const char *core, bool pol, const char *proof) {
    Var *v = arena_alloc(sizeof(Var));
    memset(v, 0, sizeof *v);
    v->term = proof; v->fact = core; v->fpol = pol; v->tp = -1;
    v->next = t->c.env;
    t->c.env = v;
}

static const char *fact_proof(T *t, const char *core, bool pol) {
    for (Var *v = t->c.env; v; v = v->next)
        if (v->fact && v->fpol == pol && strcmp(v->fact, core) == 0) {
            if (v->fid) t->fact_used[v->fid] = true;
            return v->term;
        }
    return NULL;
}

/* An `if` written as a with gets `in h` only when something in a branch asks for
   the proof. With it, Agda can no longer replace the test inside the types of
   other functions that were defined by testing the same thing, so it is left out
   when nothing needs it. The with line carries a mark that is filled in once the
   branches are written. */
static int new_fact_id(T *t) {
    if (t->nfacts + 2 > t->fcap) {
        int cap = t->fcap * 2 + 32;
        bool *nu = arena_alloc((size_t)cap);
        if (t->nfacts) memcpy(nu, t->fact_used, (size_t)t->nfacts + 1);
        t->fact_used = nu; t->fcap = cap;
    }
    return ++t->nfacts;
}

/* A list or dict the function made itself, which no other name can reach, can be
   changed in place: nobody else can see the change. Using the name as a plain
   value hands the object on, and from then on it counts as shared. */
static bool is_fresh(T *t, Var *v) {
    if (!v->fresh0) return false;
    for (Leak *l = t->c.leaks; l; l = l->next) if (l->id == v->id) return false;
    return true;
}

static void leak(T *t, Var *v) {
    if (!is_fresh(t, v)) return;
    Leak *l = arena_alloc(sizeof(Leak));
    l->id = v->id; l->next = t->c.leaks;
    t->c.leaks = l;
}

/* ---------- expressions ---------- */

static const char *EW(T *t, Node *n, Ty *want);
static const char *E(T *t, Node *n) { return EW(t, n, NULL); }
static const char *tr_type(T *t, Node *n);

/* An operand that is only read, not handed on: a plain name here stays unshared. */
static const char *ER(T *t, Node *n, Ty *want) {
    if (n->kind != E_NAME) return EW(t, n, want);
    t->c.noleak++;
    const char *r = EW(t, n, want);
    t->c.noleak--;
    return r;
}

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

/* An int literal: pos n, or negsuc n for -(n + 1). */
static const char *int_lit(T *t, Node *n, bool negate) {
    int64_t v = 0;
    if (!int_fits(n->val, &v) || v == INT64_MIN) terr(t, n->line, "this number is too large for an int literal in a checked file");
    if (negate) v = -v;
    return v < 0 ? af("(negsuc %lld)", (long long)(-(v + 1))) : af("(pos %lld)", (long long)v);
}

static const char *const_text(T *t, Node *n) {
    Obj *v = n->val;
    if (v == Py_True) return "true";
    if (v == Py_False) return "false";
    if (KIND(v) == K_INT) {
        if (int_sign(v) < 0) return int_lit(t, n, false);
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
    if (r.v) {
        if (!t->c.noleak) leak(t, r.v);
        return r.v->term;
    }
    Sym *s = r.s;
    switch (s->kind) {
    case SK_VALUE: return s->agda;
    case SK_FUNC: case SK_CTOR: return fun_wrap(t, s, n->line);
    case SK_DATA: case SK_TYPE: case SK_RECORD:
        if (s->arity) terr(t, n->line, "'%s' needs %d argument%s in square brackets", s->py->s, s->arity, s->arity == 1 ? "" : "s");
        return s->agda;
    default: terr(t, n->line, "'%s' cannot be used as a value", s->py->s);
    }
}

static const char *join_args(T *t, Node **l, int n, Ty **wants) {
    const char *r = "";
    for (int i = 0; i < n; i++) {
        if (l[i]->kind == E_KEYWORD) terr(t, l[i]->line, "keyword arguments are not available in a checked file");
        if (l[i]->kind == E_STARRED) terr(t, l[i]->line, "* arguments are not available in a checked file");
        r = af("%s %s", r, EW(t, l[i], wants ? wants[i] : NULL));
    }
    return r;
}

/* refl, standing in for a proof that no test in scope supplies. It is enough when
   Agda can work the condition out. The line is remembered so that a failure
   there can be explained. */
static const char *auto_refl(T *t, int line) {
    if (t->nauto == t->acap) {
        int cap = t->acap * 2 + 16;
        int *nl = arena_alloc((size_t)cap * sizeof(int));
        if (t->nauto) memcpy(nl, t->auto_lines, (size_t)t->nauto * sizeof(int));
        t->auto_lines = nl; t->acap = cap;
    }
    t->auto_lines[t->nauto++] = line;
    return "refl";
}

/* The proof that a divisor is not zero: a fact in scope, or refl, which works
   when Agda can compute the divisor. */
static const char *div_proof(T *t, const char *b, bool is_int, int line) {
    const char *p = fact_proof(t, is_int ? af("(%s == (pos 0))", b) : af("(%s == 0)", b), false);
    return p ? p : auto_refl(t, line);
}

static const char *binop_expr(T *t, Node *n, Ty *want) {
    Ty *lt = ty_of(t, n->a), *rt = ty_of(t, n->b);
    int lk = tk(lt), rk = tk(rt), op = n->op;
    int k = lk == TK_INT || rk == TK_INT ? TK_INT : lk == TK_STR || rk == TK_STR ? TK_STR : lk == TK_LIST || rk == TK_LIST ? TK_LIST : TK_NAT;
    /* a - b on two Nats is fine when an int is wanted: it is worked out as ints. */
    if (k == TK_NAT && tk(want) == TK_INT && op == OP_SUB) k = TK_INT;
    if (k == TK_NAT && tk(want) == TK_LIST && op == OP_ADD) k = TK_LIST;
    if (op == OP_TRUEDIV)
        terr(t, n->line, "'/' gives a float, and floats are not available in a checked file. Use // for whole-number division.");
    if (k == TK_INT) {
        const char *a = EW(t, n->a, &ty_int);
        if (op == OP_POW) {
            if (rk == TK_INT) terr(t, n->line, "the power in ** must be a Nat, so that the result is a whole number");
            return af("(powInt %s %s)", a, E(t, n->b));
        }
        const char *b = EW(t, n->b, &ty_int);
        switch (op) {
        case OP_ADD: return af("(%s +\xE1\xB5\xA2 %s)", a, b);
        case OP_SUB: return af("(%s -\xE1\xB5\xA2 %s)", a, b);
        case OP_MUL: return af("(%s *\xE1\xB5\xA2 %s)", a, b);
        case OP_FLOORDIV: return af("(divInt %s %s %s)", a, b, div_proof(t, b, true, n->line));
        case OP_MOD: return af("(modInt %s %s %s)", a, b, div_proof(t, b, true, n->line));
        default: terr(t, n->line, "this operator is not available in a checked file; int has + - * // %% and **");
        }
    }
    if (k == TK_STR) {
        if (op != OP_ADD) terr(t, n->line, "this operator is not available on strings in a checked file; strings have +");
        const char *a = E(t, n->a);
        return af("(concat %s %s)", a, E(t, n->b));
    }
    if (k == TK_LIST) {
        if (op != OP_ADD) terr(t, n->line, "this operator is not available on lists in a checked file; lists have +");
        Ty *lty = lk == TK_LIST ? lt : rk == TK_LIST ? rt : want;
        const char *a = ER(t, n->a, lty);
        return af("(%s ++ %s)", a, ER(t, n->b, lty));
    }
    if (op == OP_SUB) {
        /* Nat has no general subtraction: 0 - 1 would differ between Agda and Python. */
        if (n->a->kind == E_NAME && is_int_const(n->b) && int_cmp(n->b->val, int_new(1)) == 0) {
            Ref r = lookup(t, AS_STR(n->a->val), n->line);
            if (r.v && r.v->pred) return r.v->pred;
        }
        terr(t, n->line, "subtraction on Nat is only allowed as 'n - 1' where 'if n == 0' has ruled out zero. "
                         "Use monus(a, b) for subtraction that stops at 0, or work with int.");
    }
    const char *a = E(t, n->a);
    if (op == OP_ADD && is_int_const(n->b) && int_sign(n->b->val) > 0 && int_cmp(n->b->val, int_new(32)) <= 0) {
        /* n + 2 is written suc (suc n), the form Agda can match constructors against. */
        int64_t c = 0;
        int_fits(n->b->val, &c);
        for (int64_t i = 0; i < c; i++) a = af("(suc %s)", a);
        return a;
    }
    const char *b = E(t, n->b);
    switch (op) {
    case OP_ADD: return af("(%s + %s)", a, b);
    case OP_MUL: return af("(%s * %s)", a, b);
    case OP_FLOORDIV: return af("(divNat %s %s %s)", a, b, div_proof(t, b, false, n->line));
    case OP_MOD: return af("(modNat %s %s %s)", a, b, div_proof(t, b, false, n->line));
    case OP_POW: return af("(powNat %s %s)", a, b);
    default: terr(t, n->line, "this operator is not available in a checked file; Nat has + * // %% and **");
    }
}

static bool ty_has_int(Ty *ty) {
    if (!ty) return false;
    if (ty->k == TK_INT) return true;
    if (ty->k == TK_LIST) return ty_has_int(ty->a);
    if (ty->k == TK_DICT) return ty_has_int(ty->a) || ty_has_int(ty->b);
    if (ty->k == TK_TUPLE) for (int i = 0; i < ty->n; i++) if (ty_has_int(ty->items[i])) return true;
    return false;
}

/* The type to translate both sides of `x == y` at: when one side is known to hold
   ints, the literals on the other side are written as ints too. */
static Ty *both_want(T *t, Node *x, Node *y) {
    Ty *a = ty_of(t, x), *b = ty_of(t, y);
    return ty_has_int(a) ? a : ty_has_int(b) ? b : NULL;
}

/* One comparison `x op y`. With `core`, the result is the plain test that the
   comparison is built from, and *pol says whether the comparison is that test
   (true) or its opposite: a <= b is the opposite of b < a. */
static const char *cmp_text(T *t, int op, Node *x, Node *y, bool core, bool *pol) {
    *pol = true;
    if (op == CMP_IN || op == CMP_NOT_IN) {
        Ty *c = ty_of(t, y);
        const char *r = ER(t, y, NULL);
        const char *l = EW(t, x, c ? c->a : NULL);
        const char *in = tk(c) == TK_DICT ? af("(has %s %s)", r, l) : af("(elem %s %s)", l, r);
        if (op == CMP_IN) return in;
        if (core) { *pol = false; return in; }
        return af("(not %s)", in);
    }
    if (op != CMP_LT && op != CMP_LE && op != CMP_EQ && op != CMP_NE && op != CMP_GT && op != CMP_GE)
        terr(t, x->line, "'is' is not available in a checked file; use ==");
    Ty *w = both_want(t, x, y);
    bool is_int = tk(w) == TK_INT;
    const char *a = ER(t, x, w), *b = ER(t, y, w);
    const char *lt = is_int ? "<\xE1\xB5\xA2" : "<", *le = is_int ? "<=\xE1\xB5\xA2" : "<=";
    switch (op) {
    case CMP_EQ: return af("(%s == %s)", a, b);
    case CMP_NE: if (core) { *pol = false; return af("(%s == %s)", a, b); } return af("(not (%s == %s))", a, b);
    case CMP_LT: return af("(%s %s %s)", a, lt, b);
    case CMP_GT: return af("(%s %s %s)", b, lt, a);
    case CMP_LE: if (core) { *pol = false; return af("(%s %s %s)", b, lt, a); } return af("(%s %s %s)", a, le, b);
    default: if (core) { *pol = false; return af("(%s %s %s)", a, lt, b); } return af("(%s %s %s)", b, le, a);
    }
}

/* A condition as a plain test and whether the condition is that test or its opposite. */
static const char *cond_core(T *t, Node *c, bool *pol) {
    if (c->kind == E_NOT) {
        const char *r = cond_core(t, c->a, pol);
        *pol = !*pol;
        return r;
    }
    if (c->kind == E_COMPARE && c->n[0] == 1) return cmp_text(t, c->ops[0], c->a, c->l[0][0], true, pol);
    *pol = true;
    return E(t, c);
}

/* A proof that condition `c` is true, from a fact in scope. */
static const char *known_proof(T *t, Node *c, int line) {
    bool pol;
    const char *core = cond_core(t, c, &pol);
    const char *p = fact_proof(t, core, pol);
    if (!p)
        terr(t, line, "nothing here shows that this condition holds. known(c) gives the proof inside 'if c:' or 'while c:', "
                      "and known(not c) gives it in the else part, with c written the same way.");
    return pol ? p : af("(notFalse %s)", p);
}

static const char *target_pat(T *t, Node *target, Ty *ty);

/* An expression in a place that takes any run of items: after `in` of a loop or
   comprehension, and inside list(...), sum(...) and the like. range, enumerate,
   zip, reversed and the views of a dict are only available here, because as
   values they are not lists and would not compare equal to one. */
static const char *comp_expr(T *t, Node *n, Ty *want_elem, int gi);
static const char *EI(T *t, Node *n) {
    if (n->kind == E_LISTCOMP || n->kind == E_GENEXP) return comp_expr(t, n, NULL, 0);
    if (n->kind == E_CALL) {
        const char *b = plain_builtin(t, n->a);
        int argc = n->n[0];
        for (int i = 0; b && i < argc; i++)
            if (n->l[0][i]->kind == E_KEYWORD || n->l[0][i]->kind == E_STARRED) terr(t, n->line, "%s takes plain arguments in a checked file", b);
        if (b && !strcmp(b, "range")) {
            if (argc != 1 && argc != 2) terr(t, n->line, "range takes one or two arguments in a checked file");
            for (int i = 0; i < argc; i++)
                if (tk(ty_of(t, n->l[0][i])) == TK_INT) terr(t, n->l[0][i]->line, "range counts with Nat; turn an int into a Nat with to_nat(...)");
            const char *a = E(t, n->l[0][0]);
            return argc == 1 ? af("(range\xE2\x82\x81 %s)", a) : af("(range\xE2\x82\x82 %s %s)", a, E(t, n->l[0][1]));
        }
        if (b && !strcmp(b, "enumerate") && argc == 1) return af("(enumFrom 0 %s)", EI(t, n->l[0][0]));
        if (b && !strcmp(b, "reversed") && argc == 1) return af("(reverse %s)", EI(t, n->l[0][0]));
        if (b && !strcmp(b, "list") && argc == 1) return EI(t, n->l[0][0]);
        if (b && !strcmp(b, "zip") && argc == 2) {
            const char *a = EI(t, n->l[0][0]);
            return af("(zip %s %s)", a, EI(t, n->l[0][1]));
        }
        if (n->a->kind == E_ATTR && argc == 0 && tk(ty_of(t, n->a->a)) == TK_DICT) {
            Str *m = AS_STR(n->a->val);
            const char *d = ER(t, n->a->a, NULL);
            if (str_eqc(m, "items")) return af("(items %s)", d);
            if (str_eqc(m, "keys")) return af("(dkeys %s)", d);
            if (str_eqc(m, "values")) return af("(dvalues %s)", d);
        }
    }
    Ty *ty = ty_of(t, n);
    if (tk(ty) == TK_DICT) return af("(dkeys %s)", ER(t, n, NULL));
    if (tk(ty) == TK_STR) terr(t, n->line, "looping over a string is not available in a checked file");
    return ER(t, n, NULL);
}

/* [e for x in xs if c for y in ys] from generator gi on. */
static const char *comp_expr(T *t, Node *n, Ty *want_elem, int gi) {
    if (gi == n->n[0]) return af("(%s \xE2\x88\xB7 [])", EW(t, n->a, want_elem));
    Node *g = n->l[0][gi];
    Ty *elem = iter_elem(t, g->b);
    const char *iter = EI(t, g->b);
    Var *saved = t->c.env;
    t->c.lambda_depth++;
    const char *pat = target_pat(t, g->a, elem);
    const char *conds = NULL;
    for (int i = 0; i < g->n[0]; i++) {
        const char *c = E(t, g->l[0][i]);
        conds = conds ? af("(%s \xE2\x88\xA7 %s)", conds, c) : c;
    }
    const char *inner = comp_expr(t, n, want_elem, gi + 1);
    if (conds) inner = af("(if %s then %s else [])", conds, inner);
    t->c.lambda_depth--;
    t->c.env = saved;
    return af("(concatMap (\xCE\xBB { %s \xE2\x86\x92 %s }) %s)", pat, inner, iter);
}

static const char *builtin_call(T *t, Node *n, const char *b, Ty *want) {
    int argc = n->n[0];
    Node **a = n->l[0];
    for (int i = 0; i < argc; i++)
        if (a[i]->kind == E_KEYWORD || a[i]->kind == E_STARRED) terr(t, n->line, "%s takes plain arguments in a checked file", b);
    bool two = !strcmp(b, "min") || !strcmp(b, "max");
    if (!strcmp(b, "range") || !strcmp(b, "enumerate") || !strcmp(b, "zip") || !strcmp(b, "reversed"))
        terr(t, n->line, "%s(...) is available after 'in' of a for loop or a comprehension, and inside list(...). "
                         "As a value on its own it is not a list.", b);
    if (argc != (two ? 2 : 1)) terr(t, n->line, "%s takes %s in a checked file", b, two ? "two arguments" : "one argument");
    Ty *at = ty_of(t, a[0]);
    if (!strcmp(b, "len")) {
        if (tk(at) == TK_STR) return af("(strLen %s)", E(t, a[0]));
        if (tk(at) == TK_DICT) return af("(dlen %s)", ER(t, a[0], NULL));
        if (tk(at) == TK_TUPLE) return af("%d", at->n);
        return af("(len %s)", ER(t, a[0], NULL));
    }
    if (!strcmp(b, "int")) {
        if (tk(at) == TK_STR) terr(t, n->line, "int(...) of a string is not available in a checked file");
        return EW(t, a[0], &ty_int);
    }
    if (!strcmp(b, "abs")) return tk(at) == TK_INT ? af("(abs\xE1\xB5\xA2 %s)", E(t, a[0])) : E(t, a[0]);
    if (!strcmp(b, "str")) {
        if (tk(at) == TK_STR) return E(t, a[0]);
        if (tk(at) == TK_INT) return af("(showInt %s)", E(t, a[0]));
        if (tk(at) == TK_NAT) return af("(showNat %s)", E(t, a[0]));
        terr(t, n->line, "str(...) is available for Nat, int and str in a checked file");
    }
    if (two) {
        bool is_int = tk(at) == TK_INT || tk(ty_of(t, a[1])) == TK_INT || tk(want) == TK_INT;
        Ty *w = is_int ? &ty_int : NULL;
        const char *x = EW(t, a[0], w);
        return af("(%s%s %s %s)", b, is_int ? "Int" : "Nat", x, EW(t, a[1], w));
    }
    if (!strcmp(b, "list")) return EI(t, a[0]);
    Ty *elem = iter_elem(t, a[0]);
    if (!strcmp(b, "sum")) return af("(%s %s)", tk(elem) == TK_INT || (!elem && tk(want) == TK_INT) ? "sumInt" : "sumNat", EI(t, a[0]));
    if (!strcmp(b, "sorted")) return af("(%s %s)", tk(elem) == TK_INT ? "sortedInt" : "sortedNat", EI(t, a[0]));
    return af("(%s %s)", !strcmp(b, "any") ? "anyL" : "allL", EI(t, a[0]));
}

static const char *record_call(T *t, Sym *s, Node *n);
static const char *method_call(T *t, Node *n);

static const char *call_expr(T *t, Node *n, Ty *want) {
    static const char *const cn[] = {"call\xE2\x82\x80", "call\xE2\x82\x81", "call\xE2\x82\x82", "call\xE2\x82\x83", "call\xE2\x82\x84"};
    int argc = n->n[0];
    const char *callee;
    Ty **wants = NULL;
    if (n->a->kind == E_ATTR) return method_call(t, n);
    if (n->a->kind == E_NAME) {
        const char *b = plain_builtin(t, n->a);
        if (b) return builtin_call(t, n, b, want);
        Ref r = lookup(t, AS_STR(n->a->val), n->line);
        Sym *s = r.s;
        if (s && s->kind == SK_RECORD) return record_call(t, s, n);
        if (sym_is(s, "known")) {
            if (argc != 1 || n->l[0][0]->kind == E_KEYWORD) terr(t, n->line, "known takes one condition");
            return known_proof(t, n->l[0][0], n->line);
        }
        if (sym_is(s, "decreases"))
            terr(t, n->line, "decreases(measure, proof) is written as the last statement of a while loop");
        if (sym_is(s, "hole")) {
            if (argc) terr(t, n->line, "hole() takes no arguments");
            t->nholes++;
            return "{!!}";
        }
        if (s && (s->kind == SK_FUNC || s->kind == SK_CTOR)) {
            if (argc != s->arity)
                terr(t, n->line, "%s() takes %d argument%s but %d %s given", s->py->s, s->arity, s->arity == 1 ? "" : "s",
                     argc, argc == 1 ? "was" : "were");
            sym_tys(t, s);
            Ty *one[1] = {&ty_int};
            const char *args = join_args(t, n->l[0], argc, sym_is(s, "to_nat") ? one : s->p_tys);
            return argc ? af("(%s%s)", s->agda, args) : s->agda;
        }
        if (sym_is(s, "unreachable"))
            terr(t, n->line, "unreachable() is written as the whole body of 'case _:', as in: case _: return unreachable()");
        if (s && (s->kind == SK_DATA || s->kind == SK_TYPE || s->kind == SK_SPECIAL))
            terr(t, n->line, "'%s' cannot be called", s->py->s);
        callee = r.v ? r.v->term : s->agda;
        Ty *ft = r.v ? r.v->ty : s->ty;
        if (tk(ft) == TK_FUN && ft->n == argc) wants = ft->items;
    } else callee = E(t, n->a);
    if (argc > 4) terr(t, n->line, "a function held in a variable can take at most 4 arguments");
    return af("(%s %s%s)", cn[argc], callee, join_args(t, n->l[0], argc, wants));
}

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
        Ty *known = ty_ann(t, ty, NULL, NULL);
        const char *tt = tr_type(t, ty);
        text = i ? af("%s (\xCE\xBB%s \xE2\x86\x92 %s)", text, binders, tt) : af("%s %s", text, tt);
        const char *b = "_";
        if (name) {
            b = fresh(t, name->s);
            bind(t, name, b, false, false, a->line);
            t->c.env->erased = true;
            t->c.env->ty = known;
        }
        binders = af("%s %s", binders, b);
    }
    const char *rt = tr_type(t, res);
    t->c.env = saved;
    return np ? af("(%s (\xCE\xBB%s \xE2\x86\x92 %s))", text, binders, rt) : af("(%s %s)", text, rt);
}

/* xs[i], xs[a:b], t[0], d[k] */
static const char *index_expr(T *t, Node *n) {
    Ty *bt = ty_of(t, n->a);
    Node *i = n->b;
    if (i->kind == E_UNARY && i->op == '-')
        terr(t, n->line, "a negative index is not available in a checked file; count from the front, as in xs[len(xs) - 1] with ints");
    switch (tk(bt)) {
    case TK_LIST: {
        const char *base = ER(t, n->a, NULL);
        if (i->kind == E_SLICE) {
            if (i->c) terr(t, n->line, "a slice with a step is not available in a checked file");
            if (!i->a && !i->b) return base;
            if (!i->b) return af("(drop %s %s)", E(t, i->a), base);
            if (!i->a) return af("(take %s %s)", E(t, i->b), base);
            const char *a = E(t, i->a);
            return af("(slice %s %s %s)", a, E(t, i->b), base);
        }
        if (tk(ty_of(t, i)) == TK_INT) terr(t, n->line, "a list index is a Nat in a checked file; turn an int into a Nat with to_nat(...)");
        const char *ix = E(t, i);
        const char *p = fact_proof(t, af("(%s < (len %s))", ix, base), true);
        return af("(index %s %s %s)", base, ix, p ? p : auto_refl(t, n->line));
    }
    case TK_TUPLE: {
        int64_t k = -1;
        if (!is_int_const(i) || !int_fits(i->val, &k) || k < 0 || k >= bt->n)
            terr(t, n->line, "a tuple is indexed with a number written out, from 0 to %d here", bt->n - 1);
        const char *text = E(t, n->a);
        for (int64_t j = 0; j < k; j++) text = af("(snd %s)", text);
        return k < bt->n - 1 ? af("(fst %s)", text) : text;
    }
    case TK_DICT: {
        const char *base = ER(t, n->a, NULL);
        const char *key = EW(t, i, bt->a);
        const char *p = fact_proof(t, af("(has %s %s)", base, key), true);
        return af("(dindex %s %s %s)", base, key, p ? p : auto_refl(t, n->line));
    }
    case TK_STR: terr(t, n->line, "indexing a string is not available in a checked file");
    default:
        terr(t, n->line, "the checker cannot tell what kind of value is indexed here. Give it a type annotation "
                         "(list[...], tuple[...] or dict[...]). Square brackets after a type name give its arguments.");
    }
}

static const char *subscript_expr(T *t, Node *n) {
    Sym *s = NULL;
    if (n->a->kind == E_NAME && !find_var(t, AS_STR(n->a->val))) {
        s = find_sym(t, AS_STR(n->a->val));
        if (!s) s = prelude_sym(t, AS_STR(n->a->val));
    }
    if (!s || (s->kind != SK_DATA && s->kind != SK_TYPE && s->kind != SK_RECORD)) return index_expr(t, n);
    need(t, s, n->line);
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
    if (sym_is(s, "tuple")) {
        if (argc < 2) terr(t, n->line, "a tuple type in a checked file has two or more parts: tuple[A, B]");
        const char *text = tr_type(t, args[argc - 1]);
        for (int i = argc - 2; i >= 0; i--) text = af("(%s \xC3\x97 %s)", tr_type(t, args[i]), text);
        return text;
    }
    if (argc != s->arity || !s->arity)
        terr(t, n->line, "'%s' takes %d argument%s in square brackets but %d %s given", s->py->s, s->arity,
             s->arity == 1 ? "" : "s", argc, argc == 1 ? "was" : "were");
    if (sym_is(s, "Eq")) {
        Ty *w = both_want(t, args[0], args[1]);
        const char *a = EW(t, args[0], w);
        return af("(%s \xE2\x89\xA1 %s)", a, EW(t, args[1], w));
    }
    if (sym_is(s, "list")) return af("(List %s)", tr_type(t, args[0]));
    if (sym_is(s, "dict")) {
        const char *k = tr_type(t, args[0]);
        return af("(Dict %s %s)", k, tr_type(t, args[1]));
    }
    const char *text = s->agda;
    for (int i = 0; i < argc; i++) text = af("%s %s", text, E(t, args[i]));
    return af("(%s)", text);
}

static const char *attr_expr(T *t, Node *n);

static const char *lambda_expr(T *t, Node *n, Ty *want) {
    static const char *const w[] = {"fun\xE2\x82\x80", "fun\xE2\x82\x81", "fun\xE2\x82\x82", "fun\xE2\x82\x83", "fun\xE2\x82\x84"};
    Params *p = n->params;
    if (p->nkwonly || p->vararg || p->kwarg || p->nposonly) terr(t, n->line, "a lambda in a checked file takes plain parameters only");
    if (p->n > 4) terr(t, n->line, "a lambda can take at most 4 arguments");
    bool typed = tk(want) == TK_FUN && want->n == p->n;
    Var *saved = t->c.env;
    t->c.lambda_depth++;
    const char *names = "";
    for (int i = 0; i < p->n; i++) {
        if (p->defaults[i]) terr(t, n->line, "default values are not available in a checked file");
        const char *a = fresh(t, p->names[i]->s);
        bind(t, p->names[i], a, false, false, n->line);
        if (typed) t->c.env->ty = want->items[i];
        names = af("%s %s", names, a);
    }
    const char *body = EW(t, n->a, typed ? want->a : NULL);
    t->c.lambda_depth--;
    t->c.env = saved;
    if (!p->n) return af("(%s %s)", w[0], body);
    return af("(%s (\xCE\xBB%s \xE2\x86\x92 %s))", w[p->n], names, body);
}

static const char *EW(T *t, Node *n, Ty *want) {
    int wk = tk(want);
    if (wk == TK_INT) {
        if (is_int_const(n)) return int_lit(t, n, false);
        bool own = n->kind == E_IFEXP || n->kind == E_UNARY || (n->kind == E_BINOP && n->op == OP_SUB) ||
                   (n->kind == E_CALL && plain_builtin(t, n->a));
        /* A Nat where an int is wanted is turned into one. */
        if (!own && tk(ty_of(t, n)) == TK_NAT) return af("(pos %s)", EW(t, n, NULL));
        if (n->kind == E_CALL && plain_builtin(t, n->a) && strcmp(plain_builtin(t, n->a), "int") &&
            strcmp(plain_builtin(t, n->a), "min") && strcmp(plain_builtin(t, n->a), "max") &&
            strcmp(plain_builtin(t, n->a), "sum") && tk(ty_of(t, n)) == TK_NAT)
            return af("(pos %s)", EW(t, n, NULL));
    }
    switch (n->kind) {
    case E_CONST: return const_text(t, n);
    case E_NAME: return name_ref(t, n);
    case E_CALL: return call_expr(t, n, want);
    case E_SUBSCR: return subscript_expr(t, n);
    case E_ATTR: return attr_expr(t, n);
    case E_BINOP: return binop_expr(t, n, want);
    case E_UNARY:
        if (n->op != '-') terr(t, n->line, "this operator is not available in a checked file");
        if (is_int_const(n->a)) return int_lit(t, n->a, true);
        return af("(neg\xE1\xB5\xA2 %s)", EW(t, n->a, &ty_int));
    case E_COMPARE: {
        const char *all = NULL;
        Node *x = n->a;
        for (int i = 0; i < n->n[0]; i++) {
            bool pol;
            const char *one = cmp_text(t, n->ops[i], x, n->l[0][i], false, &pol);
            all = all ? af("(%s \xE2\x88\xA7 %s)", all, one) : one;
            x = n->l[0][i];
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
        const char *c = E(t, n->a), *a = EW(t, n->b, want);
        return af("(if %s then %s else %s)", c, a, EW(t, n->c, want));
    }
    case E_LAMBDA: return lambda_expr(t, n, want);
    case E_TUPLE: {
        int c = n->n[0];
        if (c < 2) terr(t, n->line, "a tuple in a checked file has two or more parts");
        bool typed = wk == TK_TUPLE && want->n == c;
        const char **parts = arena_alloc((size_t)c * sizeof(char *));
        for (int i = 0; i < c; i++) {
            if (n->l[0][i]->kind == E_STARRED) terr(t, n->line, "* is not available inside a tuple in a checked file");
            parts[i] = EW(t, n->l[0][i], typed ? want->items[i] : NULL);
        }
        const char *text = parts[c - 1];
        for (int i = c - 2; i >= 0; i--) text = af("(%s , %s)", parts[i], text);
        return text;
    }
    case E_LIST: {
        int c = n->n[0];
        const char **parts = arena_alloc((size_t)(c + 1) * sizeof(char *));
        for (int i = 0; i < c; i++) {
            if (n->l[0][i]->kind == E_STARRED) terr(t, n->line, "* is not available inside a list in a checked file; use +");
            parts[i] = EW(t, n->l[0][i], wk == TK_LIST ? want->a : NULL);
        }
        const char *text = "[]";
        for (int i = c - 1; i >= 0; i--) text = af("(%s \xE2\x88\xB7 %s)", parts[i], text);
        return text;
    }
    case E_DICT: {
        const char *text = "[]";
        int c = n->n[0];
        const char **parts = arena_alloc((size_t)(c + 1) * sizeof(char *));
        for (int i = 0; i < c; i++) {
            if (!n->l[0][i]) terr(t, n->line, "** is not available inside a dict in a checked file");
            const char *k = EW(t, n->l[0][i], wk == TK_DICT ? want->a : NULL);
            parts[i] = af("(%s , %s)", k, EW(t, n->l[1][i], wk == TK_DICT ? want->b : NULL));
        }
        for (int i = c - 1; i >= 0; i--) text = af("(%s \xE2\x88\xB7 %s)", parts[i], text);
        return af("(dictOf %s)", text);
    }
    case E_LISTCOMP: return comp_expr(t, n, wk == TK_LIST ? want->a : NULL, 0);
    case E_GENEXP:
        terr(t, n->line, "a generator expression is available inside sum(...), any(...), all(...), sorted(...) and list(...) "
                         "in a checked file; write a list comprehension for a list");
    case E_SLICE: terr(t, n->line, "a slice is written inside square brackets after a list");
    case E_SET: case E_SETCOMP: case E_DICTCOMP: terr(t, n->line, "sets and dict comprehensions are not available in a checked file");
    case E_NAMED: terr(t, n->line, "':=' is not available in a checked file");
    case E_FSTRING: terr(t, n->line, "f-strings are not available in a checked file");
    default: terr(t, n->line, "this expression is not available in a checked file");
    }
}

/* An annotation. At its top, `a == b` is the type of proofs that a equals b. */
static const char *tr_type(T *t, Node *n) {
    bool saved = t->c.in_type;
    t->c.in_type = true;
    const char *r;
    if (n->kind == E_COMPARE && n->n[0] == 1 && n->ops[0] == CMP_EQ) {
        Ty *w = both_want(t, n->a, n->l[0][0]);
        const char *a = EW(t, n->a, w);
        r = af("(%s \xE2\x89\xA1 %s)", a, EW(t, n->l[0][0], w));
    } else r = E(t, n);
    t->c.in_type = saved;
    return r;
}


/* ---------- signatures ---------- */

static void check_params(T *t, Node *def, bool method) {
    Params *p = def->params;
    if (p->vararg || p->kwarg || p->nkwonly || p->nposonly)
        terr(t, def->line, "a checked function takes plain parameters only: no *, ** or /");
    if (method && (!p->n || !str_eqc(p->names[0], "self")))
        terr(t, def->line, "a method's first parameter is self");
    for (int i = 0; i < p->n; i++) {
        if (p->defaults[i]) terr(t, def->line, "default values are not available in a checked file");
        if (!p->ann[i] && !(method && i == 0)) terr(t, def->line, "parameter '%s' needs a type annotation", p->names[i]->s);
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

static int tparams_of(Sym *s, Node ***out);
static Var *bindv(T *t, Str *py, const char *term, bool split, Ty *ty, int line);

static void emit_func_sig(T *t, Sym *s) {
    Node *def = s->node;
    if (s->busy) terr(t, def->line, "the annotations of '%s' depend on '%s' itself", s->py->s, s->py->s);
    if (s->owner && !s->owner->body_done) emit_record(t, s->owner);
    s->busy = true;
    check_params(t, def, s->owner != NULL);
    Ctx saved = t->c;
    t->c = (Ctx){0};
    Node **tps;
    int ntp = tparams_of(s, &tps), np = def->params->n;
    s->tp_names = arena_alloc((size_t)(ntp + 1) * sizeof(char *));
    s->p_names = arena_alloc((size_t)(np + 1) * sizeof(char *));
    const char *text = "";
    for (int i = 0; i < ntp; i++) {
        Node *tp = tps[i];
        const char *bound = tp->a ? tr_type(t, tp->a) : "Set";
        s->tp_names[i] = fresh(t, AS_STR(tp->val)->s);
        erased_var(t, AS_STR(tp->val), s->tp_names[i], i);
        text = af("%s{@0 %s : %s} ", text, s->tp_names[i], bound);
    }
    sym_tys(t, s);
    for (int i = 0; i < np; i++) {
        Node *ann = def->params->ann[i];
        const char *ty;
        Ty *pt;
        if (ann) { ty = tr_type(t, ann); pt = ty_ann(t, ann, NULL, NULL); }
        else {   /* self */
            ty = s->owner->agda;
            for (int j = 0; j < s->owner->arity; j++) ty = af("%s %s", ty, s->tp_names[j]);
            if (s->owner->arity) ty = af("(%s)", ty);
            pt = s->p_tys[0];
        }
        s->p_names[i] = fresh(t, def->params->names[i]->s);
        bindv(t, def->params->names[i], s->p_names[i], false, pt, def->line);
        text = af("%s(%s : %s) ", text, s->p_names[i], ty);
    }
    const char *ret = tr_type(t, def->b);
    t->c = saved;
    /* A @trusted def has no Agda body: its type is all Agda is told. */
    const char *kw = s->trusted ? "postulate " : "";
    if (s->trusted) t->unsafe = true;
    o_put(&t->out, def->line, ntp + np ? af("%s%s : %s\xE2\x86\x92 %s\n", kw, s->agda, text, ret) : af("%s%s : %s\n", kw, s->agda, ret));
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
        check_params(t, def, false);
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
            bindv(t, def->params->names[i], a, false, ty_ann(t, def->params->ann[i], NULL, NULL), def->line);
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

/* ---------- classes with fields ---------- */

static Sym *field_of(Sym *rec, Str *name) {
    for (int i = 0; i < rec->nfields; i++) if (str_eq(rec->fields[i]->py, name)) return rec->fields[i];
    return NULL;
}

static Sym *method_of(T *t, Sym *rec, Str *name) {
    (void)t;
    for (int i = 0; i < rec->nmethods; i++) if (str_eq(rec->methods[i]->mname, name)) return rec->methods[i];
    return NULL;
}

/* Whether == can be given to a class: every field has a type that has ==. */
static bool ty_has_eq(Ty *ty) {
    switch (tk(ty)) {
    case TK_NAT: case TK_INT: case TK_BOOL: case TK_STR: return true;
    case TK_LIST: return ty_has_eq(ty->a);
    case TK_TUPLE: for (int i = 0; i < ty->n; i++) if (!ty_has_eq(ty->items[i])) return false; return ty->n == 2;
    case TK_REC: return ty->sym->has_eq;
    default: return false;
    }
}

/* A @dataclass class becomes an Agda record. A field's type can mention the
   fields before it. */
static void emit_record(T *t, Sym *s) {
    Node *cls = s->node;
    if (s->busy) terr(t, cls->line, "the fields of '%s' depend on '%s' itself", s->py->s, s->py->s);
    s->busy = true;
    Out chunk = {0}, *outer = t->cur;
    Ctx saved = t->c;
    t->c = (Ctx){0};
    t->cur = &chunk;
    s->tp_names = arena_alloc((size_t)(s->arity + 1) * sizeof(char *));
    const char *params = "";
    for (int i = 0; i < s->arity; i++) {
        Node *tp = cls->l[3][i];
        if (tp->op || tp->c) terr(t, tp->line, "a name in square brackets cannot use *, ** or a default in a checked file");
        const char *bound = tp->a ? tr_type(t, tp->a) : "Set";
        s->tp_names[i] = fresh(t, AS_STR(tp->val)->s);
        erased_var(t, AS_STR(tp->val), s->tp_names[i], -1);
        params = af("%s (@0 %s : %s)", params, s->tp_names[i], bound);
    }
    o_put(&chunk, cls->line, af("record %s%s : Set where\n", s->agda, params));
    o_put(&chunk, cls->line, af("  constructor %s\n", s->ctor));
    if (s->nfields) o_put(&chunk, cls->line, "  field\n");
    bool eq = s->arity == 0;
    for (int i = 0; i < s->nfields; i++) {
        Sym *f = s->fields[i];
        f->ty = ty_ann(t, f->node, NULL, NULL);
        if (!ty_has_eq(f->ty)) eq = false;
        o_put(&chunk, f->line, af("    %s : %s\n", f->agda, tr_type(t, f->node)));
        mark_used(t, f->agda);
        bind(t, f->py, f->agda, false, false, f->line);
        t->c.env->ty = f->ty;
        t->c.env->erased = true;   /* a later field's type may mention it; nothing else here can */
    }
    if (eq && s->nfields) {
        /* == compares field by field, as a dataclass does. */
        const char *l = s->ctor, *r = s->ctor, *body = NULL;
        for (int i = 0; i < s->nfields; i++) {
            l = af("%s a%d", l, i); r = af("%s b%d", r, i);
            const char *one = af("(a%d == b%d)", i, i);
            body = body ? af("(%s \xE2\x88\xA7 %s)", body, one) : one;
        }
        o_put(&chunk, cls->line, af("instance\n  Eq-%s : Eq\xE2\x82\x80 %s\n", s->agda, s->agda));
        o_put(&chunk, cls->line, af("  Eq-%s = record { _==_ = \xCE\xBB { (%s) (%s) \xE2\x86\x92 %s } }\n", s->agda, l, r, body));
        s->has_eq = true;
    }
    t->c = saved;
    t->cur = outer;
    for (int i = 0, at = 0; i < chunk.nl; i++) {
        char *nl = strchr(chunk.s + at, '\n');
        *nl = 0;
        o_put(&t->out, chunk.lines[i], af("%s\n", chunk.s + at));
        at = (int)(nl - chunk.s) + 1;
    }
    s->sig_done = s->body_done = true;
    s->busy = false;
}

/* Point(1, 2) or Point(x=1, y=2) */
static const char *record_call(T *t, Sym *s, Node *n) {
    int argc = n->n[0];
    if (argc != s->nfields)
        terr(t, n->line, "%s() takes %d argument%s but %d %s given", s->py->s, s->nfields, s->nfields == 1 ? "" : "s", argc,
             argc == 1 ? "was" : "were");
    const char **parts = arena_alloc((size_t)(argc + 1) * sizeof(char *));
    for (int i = 0; i < argc; i++) {
        Node *a = n->l[0][i];
        int at = i;
        if (a->kind == E_STARRED) terr(t, a->line, "* arguments are not available in a checked file");
        if (a->kind == E_KEYWORD) {
            at = -1;
            for (int j = 0; a->val && j < s->nfields; j++) if (str_eq(s->fields[j]->py, AS_STR(a->val))) at = j;
            if (at < 0) terr(t, a->line, "'%s' has no field of that name", s->py->s);
            a = a->a;
        }
        if (parts[at]) terr(t, a->line, "the field '%s' is given twice", s->fields[at]->py->s);
        parts[at] = EW(t, a, s->fields[at]->ty);
    }
    const char *text = s->ctor;
    for (int i = 0; i < argc; i++) text = af("%s %s", text, parts[i]);
    return argc ? af("(%s)", text) : text;
}

static const char *attr_expr(T *t, Node *n) {
    Ty *b = ty_of(t, n->a);
    if (tk(b) != TK_REC)
        terr(t, n->line, "attribute access is available on a value whose class the checker knows. "
                         "Give the value a type annotation, or use match to take a Data value apart.");
    need(t, b->sym, n->line);
    Sym *f = field_of(b->sym, AS_STR(n->val));
    if (!f) {
        if (method_of(t, b->sym, AS_STR(n->val)))
            terr(t, n->line, "'%s' is a method; call it, as in x.%s(...)", AS_STR(n->val)->s, AS_STR(n->val)->s);
        terr(t, n->line, "'%s' has no field '%s'", b->sym->py->s, AS_STR(n->val)->s);
    }
    return af("(%s.%s %s)", b->sym->agda, f->agda, ER(t, n->a, NULL));
}

/* x.m(args): a method of a class, or one of the few methods of lists and dicts
   that give a value back. */
static const char *method_call(T *t, Node *n) {
    static const char *const cn[] = {"call\xE2\x82\x80", "call\xE2\x82\x81", "call\xE2\x82\x82", "call\xE2\x82\x83", "call\xE2\x82\x84"};
    Node *recv = n->a->a;
    Str *m = AS_STR(n->a->val);
    int argc = n->n[0];
    Ty *rt = ty_of(t, recv);
    if (tk(rt) == TK_REC) {
        need(t, rt->sym, n->line);
        Sym *ms = method_of(t, rt->sym, m);
        if (ms) {
            if (argc + 1 != ms->arity)
                terr(t, n->line, "%s() takes %d argument%s but %d %s given", m->s, ms->arity - 1, ms->arity == 2 ? "" : "s",
                     argc, argc == 1 ? "was" : "were");
            need(t, ms, n->line);
            sym_tys(t, ms);
            const char *self = ER(t, recv, NULL);
            return af("(%s %s%s)", ms->agda, self, join_args(t, n->l[0], argc, ms->p_tys + 1));
        }
        Sym *f = field_of(rt->sym, m);
        if (!f) terr(t, n->line, "'%s' has no method '%s'", rt->sym->py->s, m->s);
        if (argc > 4) terr(t, n->line, "a function held in a field can take at most 4 arguments");
        const char *fn = af("(%s.%s %s)", rt->sym->agda, f->agda, ER(t, recv, NULL));
        return af("(%s %s%s)", cn[argc], fn, join_args(t, n->l[0], argc, tk(f->ty) == TK_FUN && f->ty->n == argc ? f->ty->items : NULL));
    }
    if ((tk(rt) == TK_LIST || tk(rt) == TK_DICT) && str_eqc(m, "copy") && argc == 0) return ER(t, recv, NULL);
    if (tk(rt) == TK_DICT && str_eqc(m, "get")) {
        if (argc != 2) terr(t, n->line, "d.get takes the key and a value to give back when the key is missing");
        const char *d = ER(t, recv, NULL);
        const char *k = EW(t, n->l[0][0], rt->a);
        return af("(dget %s %s %s)", d, k, EW(t, n->l[0][1], rt->b));
    }
    if (tk(rt) == TK_DICT && (str_eqc(m, "items") || str_eqc(m, "keys") || str_eqc(m, "values")))
        terr(t, n->line, "d.%s() is available after 'in' of a for loop or a comprehension, and inside list(...)", m->s);
    if (str_eqc(m, "append") || str_eqc(m, "extend") || str_eqc(m, "sort") || str_eqc(m, "reverse") || str_eqc(m, "clear"))
        terr(t, n->line, "xs.%s(...) changes a list. It is written as a statement of its own, on a local name.", m->s);
    terr(t, n->line, "this method call is not available in a checked file. Methods are available on classes, "
                     "and lists and dicts have a few: append, extend, sort, reverse, clear, copy, get.");
}

/* ---------- function bodies ---------- */

typedef struct { Var *env; Leak *leaks; } Mark;
static Mark mark(T *t) { return (Mark){t->c.env, t->c.leaks}; }
static void reset(T *t, Mark m) { t->c.env = m.env; t->c.leaks = m.leaks; }

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

static Node *synth(int kind, int line) {
    Node *n = arena_alloc(sizeof(Node));
    n->kind = kind; n->line = line;
    return n;
}

static bool skippable(Node *s) {
    return s->kind == S_PASS || (s->kind == S_EXPR && s->a->kind == E_CONST);
}

/* `x == 0`, `x != 0`, `x > 0` or `0 < x`, where x is a name or `name - 1`. */
static Node *zero_test(Node *c, bool *then_is_zero) {
    if (c->kind != E_COMPARE || c->n[0] != 1) return NULL;
    Node *a = c->a, *b = c->l[0][0];
    int op = c->ops[0];
    bool a_zero = is_int_const(a) && int_sign(a->val) == 0;
    bool b_zero = is_int_const(b) && int_sign(b->val) == 0;
    Node *x;
    if (b_zero && (op == CMP_EQ || op == CMP_NE || op == CMP_GT)) { x = a; *then_is_zero = op == CMP_EQ; }
    else if (a_zero && (op == CMP_EQ || op == CMP_NE || op == CMP_LT)) { x = b; *then_is_zero = op == CMP_EQ; }
    else return NULL;
    if (x->kind == E_NAME) return x;
    if (x->kind == E_BINOP && x->op == OP_SUB && x->a->kind == E_NAME) return x;
    return NULL;
}

/* `if a and b: T else: E` is `if a: (if b: T else: E) else: E`, and the same for
   `or`. Each test then gets its own fact. */
static Node *split_boolop(Node *s) {
    Node *c = s->a;
    if (c->kind != E_BOOLOP) return s;
    Node *restc = c->l[0][1];
    if (c->n[0] > 2) {
        restc = synth(E_BOOLOP, c->line);
        restc->op = c->op; restc->l[0] = c->l[0] + 1; restc->n[0] = c->n[0] - 1;
    }
    Node *inner = synth(S_IF, s->line), *outer = synth(S_IF, s->line);
    inner->a = restc;
    inner->l[0] = s->l[0]; inner->n[0] = s->n[0]; inner->l[1] = s->l[1]; inner->n[1] = s->n[1];
    Node **one = arena_alloc(sizeof(Node *));
    one[0] = inner;
    outer->a = c->l[0][0];
    if (c->op == 0) { outer->l[0] = one; outer->n[0] = 1; outer->l[1] = s->l[1]; outer->n[1] = s->n[1]; }
    else { outer->l[0] = s->l[0]; outer->n[0] = s->n[0]; outer->l[1] = one; outer->n[1] = 1; }
    return outer;
}

/* Every if, match and loop is a split: each one adds `with` columns or patterns
   to the left-hand side. */
static bool stmt_splits(Node *s) {
    return s->kind == S_MATCH || s->kind == S_IF || s->kind == S_FOR || s->kind == S_WHILE;
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

/* --- which names a run of statements assigns, and which it changes in place --- */

typedef struct { Str **v; int n, cap; } Names;

static bool names_has(Names *ns, Str *s) {
    for (int i = 0; i < ns->n; i++) if (str_eq(ns->v[i], s)) return true;
    return false;
}
static void names_add(Names *ns, Str *s) {
    if (!s || names_has(ns, s)) return;
    if (ns->n == ns->cap) {
        int cap = ns->cap * 2 + 8;
        Str **nv = arena_alloc((size_t)cap * sizeof *nv);
        if (ns->n) memcpy(nv, ns->v, (size_t)ns->n * sizeof *nv);
        ns->v = nv; ns->cap = cap;
    }
    ns->v[ns->n++] = s;
}

static void target_names(Names *ns, Node *tg) {
    if (tg->kind == E_NAME) names_add(ns, AS_STR(tg->val));
    else if (tg->kind == E_TUPLE || tg->kind == E_LIST) for (int i = 0; i < tg->n[0]; i++) target_names(ns, tg->l[0][i]);
    else if (tg->kind == E_STARRED) target_names(ns, tg->a);
    else if (tg->kind == E_SUBSCR && tg->a->kind == E_NAME) names_add(ns, AS_STR(tg->a->val));
}

static void pattern_names(Names *ns, Node *p) {
    if (!p) return;
    if (p->kind == P_CAPTURE || p->kind == P_STAR || p->kind == P_AS || p->kind == P_MAP) names_add(ns, p->val ? AS_STR(p->val) : NULL);
    if (p->kind == P_AS) pattern_names(ns, p->a);
    for (int j = 0; j < 2; j++) for (int i = 0; i < p->n[j]; i++) if (p->kind != P_MAP || j == 1) pattern_names(ns, p->l[j][i]);
}

static bool changing_method(Str *m) {
    return str_eqc(m, "append") || str_eqc(m, "extend") || str_eqc(m, "sort") || str_eqc(m, "reverse") || str_eqc(m, "clear");
}

/* The name a statement changes in place, if it is one of: xs.append(e) and the
   like, xs[i] = e, xs[i] += e. (xs += e on a list is found by its type.) */
static Str *changed_name(Node *s) {
    if (s->kind == S_EXPR && s->a->kind == E_CALL && s->a->a->kind == E_ATTR && s->a->a->a->kind == E_NAME &&
        changing_method(AS_STR(s->a->a->val)))
        return AS_STR(s->a->a->a->val);
    if (s->kind == S_ASSIGN && s->n[0] == 1 && s->l[0][0]->kind == E_SUBSCR && s->l[0][0]->a->kind == E_NAME)
        return AS_STR(s->l[0][0]->a->val);
    if (s->kind == S_AUGASSIGN && s->a->kind == E_SUBSCR && s->a->a->kind == E_NAME) return AS_STR(s->a->a->val);
    return NULL;
}

/* Names assigned (all = true) or changed in place (all = false) by the statements. */
static void assigned(T *t, Names *ns, Node **l, int n, bool all) {
    for (int i = 0; i < n; i++) {
        Node *s = l[i];
        Str *ch = changed_name(s);
        if (ch) { names_add(ns, ch); continue; }
        switch (s->kind) {
        case S_ASSIGN: if (all) for (int j = 0; j < s->n[0]; j++) target_names(ns, s->l[0][j]); break;
        case S_ANNASSIGN: if (all) target_names(ns, s->a); break;
        case S_AUGASSIGN:
            if (s->a->kind != E_NAME) break;
            if (all) names_add(ns, AS_STR(s->a->val));
            else { Var *v = find_var(t, AS_STR(s->a->val)); if (v && tk(v->ty) == TK_LIST) names_add(ns, v->py); }
            break;
        case S_IF: assigned(t, ns, s->l[0], s->n[0], all); assigned(t, ns, s->l[1], s->n[1], all); break;
        case S_MATCH:
            for (int j = 0; j < s->n[0]; j++) {
                if (all) pattern_names(ns, s->l[0][j]->a);
                assigned(t, ns, s->l[0][j]->l[0], s->l[0][j]->n[0], all);
            }
            break;
        case S_FOR: if (all) target_names(ns, s->a); /* fall through */
        case S_WHILE: assigned(t, ns, s->l[0], s->n[0], all); assigned(t, ns, s->l[1], s->n[1], all); break;
        default: break;
        }
    }
}

static bool has_return(Node **l, int n) {
    for (int i = 0; i < n; i++) {
        Node *s = l[i];
        if (s->kind == S_RETURN) return true;
        if (s->kind == S_IF || s->kind == S_FOR || s->kind == S_WHILE) {
            if (has_return(s->l[0], s->n[0]) || has_return(s->l[1], s->n[1])) return true;
        } else if (s->kind == S_MATCH)
            for (int j = 0; j < s->n[0]; j++) if (has_return(s->l[0][j]->l[0], s->l[0][j]->n[0])) return true;
    }
    return false;
}

static void collect_locals(T *t, Node **l, int n) {
    Names ns = {0};
    assigned(t, &ns, l, n, true);
    t->c.locals = ns.v; t->c.nlocals = ns.n; t->c.lcap = ns.cap;
}

/* --- assignments --- */

typedef struct { Str *name; Node *tuple, *value, *ann; } Asg;

/* `total = 0` makes a Nat, and `total += x` with an int x then needs an int. So
   before a function is translated, this walks it once to find the names that are
   given an int anywhere. Such a name is an int from its first assignment on. The
   walk binds each name to the type of its value, in order, and goes round twice
   so that a name set late in a loop is known at the top of it. */
static void scan_ints(T *t, Names *ints, Node **l, int n) {
    for (int i = 0; i < n; i++) {
        Node *s = l[i];
        switch (s->kind) {
        case S_ASSIGN: case S_ANNASSIGN: case S_AUGASSIGN: {
            Node *tg = s->kind == S_ASSIGN ? (s->n[0] == 1 ? s->l[0][0] : NULL) : s->a;
            if (!tg || tg->kind != E_NAME) break;
            Node *value = s->kind == S_ASSIGN ? s->a : s->b;
            if (!value) break;
            Ty *ty;
            if (s->kind == S_ANNASSIGN) ty = ty_ann(t, s->c, NULL, NULL);
            else {
                ty = ty_of(t, value);
                Var *old = find_var(t, AS_STR(tg->val));
                if (s->kind == S_AUGASSIGN && old && tk(old->ty) != TK_NAT && tk(ty) != TK_INT) ty = old->ty;
                if (old && tk(old->ty) == TK_INT && tk(ty) == TK_NAT) ty = old->ty;
                if (tk(ty) == TK_INT) names_add(ints, AS_STR(tg->val));
            }
            bind(t, AS_STR(tg->val), "_", false, false, 0);
            t->c.env->ty = ty;
            break;
        }
        case S_FOR:
            if (s->a->kind == E_NAME) {
                Ty *elem = iter_elem(t, s->b);
                bind(t, AS_STR(s->a->val), "_", false, false, 0);
                t->c.env->ty = elem;
            } else if (s->a->kind == E_TUPLE) {
                Ty *elem = iter_elem(t, s->b);
                for (int j = 0; j < s->a->n[0]; j++)
                    if (s->a->l[0][j]->kind == E_NAME) {
                        bind(t, AS_STR(s->a->l[0][j]->val), "_", false, false, 0);
                        t->c.env->ty = tk(elem) == TK_TUPLE && elem->n == s->a->n[0] ? elem->items[j] : NULL;
                    }
            }
            scan_ints(t, ints, s->l[0], s->n[0]);
            break;
        case S_WHILE: scan_ints(t, ints, s->l[0], s->n[0]); break;
        case S_IF: scan_ints(t, ints, s->l[0], s->n[0]); scan_ints(t, ints, s->l[1], s->n[1]); break;
        case S_MATCH: for (int j = 0; j < s->n[0]; j++) scan_ints(t, ints, s->l[0][j]->l[0], s->l[0][j]->n[0]); break;
        default: break;
        }
    }
}

/* The type a plain assignment gives its name, and the type to translate the value at. */
static Ty *assign_ty(T *t, Asg *a, Ty **want) {
    *want = a->ann ? ty_ann(t, a->ann, NULL, NULL) : NULL;
    Ty *vt = *want ? *want : ty_of(t, a->value);
    if (!a->ann && a->name && tk(vt) == TK_NAT)
        for (int i = 0; i < t->c.nints; i++)
            if (str_eq(t->c.ints[i], a->name)) { *want = &ty_int; return &ty_int; }
    return vt;
}

/* x = e, x: T = e, x op= e, a, b = e */
static void plain_assign(T *t, Node *s, Asg *a) {
    memset(a, 0, sizeof *a);
    if (s->kind == S_ASSIGN) {
        Node *tg = s->l[0][0];
        if (s->n[0] != 1 || (tg->kind != E_NAME && tg->kind != E_TUPLE))
            terr(t, s->line, "an assignment in a checked file gives one plain name one value");
        if (tg->kind == E_NAME) a->name = AS_STR(tg->val); else a->tuple = tg;
        a->value = s->a;
    } else if (s->kind == S_ANNASSIGN) {
        if (s->a->kind != E_NAME) terr(t, s->line, "an assignment in a checked file gives one plain name one value");
        if (!s->b) terr(t, s->line, "an annotation without a value is not available here in a checked file");
        a->name = AS_STR(s->a->val); a->value = s->b; a->ann = s->c;
    } else {
        if (s->a->kind != E_NAME) terr(t, s->line, "an assignment in a checked file gives one plain name one value");
        Node *b = synth(E_BINOP, s->line);
        b->op = s->op; b->a = s->a; b->b = s->b;
        a->name = AS_STR(s->a->val); a->value = b;
    }
}

/* The name and value of a top-level `x = e` or `x: T = e`. */
static Str *assign_parts(T *t, Node *s, Node **value, Node **ann) {
    Asg a;
    plain_assign(t, s, &a);
    if (!a.name) terr(t, s->line, "an assignment at the top level of a checked file gives one plain name one value");
    *value = a.value; *ann = a.ann;
    return a.name;
}

/* A value that is a new object, which nothing else refers to yet. */
static bool is_fresh_expr(T *t, Node *v) {
    switch (v->kind) {
    case E_LIST: case E_DICT: case E_LISTCOMP: case E_BINOP: return true;
    case E_SUBSCR: return v->b->kind == E_SLICE;
    case E_CALL: {
        const char *b = plain_builtin(t, v->a);
        if (b) return !strcmp(b, "list") || !strcmp(b, "sorted");
        return v->a->kind == E_ATTR && str_eqc(AS_STR(v->a->val), "copy") && v->n[0] == 0;
    }
    default: return false;
    }
}

/* If `s` changes a list or dict in place, the new value as Agda text. In the Agda
   text the name simply gets a new value. That matches what Python does only when
   no other name can reach the object, so the change is refused otherwise. */
static const char *mutation(T *t, Node *s, Var **var) {
    Str *name = changed_name(s);
    bool extend = false;
    if (!name && s->kind == S_AUGASSIGN && s->a->kind == E_NAME) {
        Var *v = find_var(t, AS_STR(s->a->val));
        if (v && (tk(v->ty) == TK_LIST || tk(v->ty) == TK_DICT)) { name = v->py; extend = true; }
    }
    if (!name) return NULL;
    Var *v = find_var(t, name);
    if (!v || v->erased || v->dead)
        terr(t, s->line, "'%s' is not a local variable of this function, so it cannot be changed in place", name->s);
    int k = tk(v->ty);
    if (k != TK_LIST && k != TK_DICT)
        terr(t, s->line, "the checker does not know that '%s' is a list or a dict. Give it a type annotation where it "
                         "is first assigned, as in: %s: list[int] = []", name->s, name->s);
    if (!is_fresh(t, v))
        terr(t, s->line, "'%s' may be shared here: it is a parameter, or it came from a call, or it was given another "
                         "name, passed on or put inside another value. A shared list or dict cannot be changed in place, "
                         "because the other holder would see the change. Make a copy first: %s = %s", name->s, name->s,
             k == TK_LIST ? af("list(%s)", name->s) : af("%s.copy()", name->s));
    *var = v;
    const char *x = v->term;
    if (extend) {
        if (s->op != OP_ADD || k != TK_LIST) terr(t, s->line, "the only operator of this kind on a list is +=");
        return af("(%s ++ %s)", x, ER(t, s->b, v->ty));
    }
    if (s->kind == S_EXPR) {
        Str *m = AS_STR(s->a->a->val);
        int argc = s->a->n[0];
        Node **a = s->a->l[0];
        for (int i = 0; i < argc; i++)
            if (a[i]->kind == E_KEYWORD || a[i]->kind == E_STARRED) terr(t, s->line, "%s takes plain arguments in a checked file", m->s);
        bool one = str_eqc(m, "append") || str_eqc(m, "extend");
        if (argc != (one ? 1 : 0)) terr(t, s->line, "%s takes %s here", m->s, one ? "one argument" : "no arguments");
        if (k == TK_DICT) {
            if (!str_eqc(m, "clear")) terr(t, s->line, "a dict is changed with d[key] = value, or emptied with d.clear()");
            return "(dictOf [])";
        }
        if (str_eqc(m, "append")) return af("(%s ++ (%s \xE2\x88\xB7 []))", x, EW(t, a[0], v->ty->a));
        if (str_eqc(m, "extend")) return af("(%s ++ %s)", x, ER(t, a[0], v->ty));
        if (str_eqc(m, "reverse")) return af("(reverse %s)", x);
        if (str_eqc(m, "clear")) return "[]";
        return af("(%s %s)", tk(v->ty->a) == TK_INT ? "sortedInt" : "sortedNat", x);
    }
    Node *tg = s->kind == S_ASSIGN ? s->l[0][0] : s->a, *value = s->kind == S_ASSIGN ? s->a : NULL;
    if (!value) {
        value = synth(E_BINOP, s->line);
        value->op = s->op; value->a = tg; value->b = s->b;
    }
    if (tg->b->kind == E_SLICE) terr(t, s->line, "assigning to a slice is not available in a checked file");
    if (k == TK_DICT) {
        const char *key = EW(t, tg->b, v->ty->a);
        return af("(dset %s %s %s)", x, key, EW(t, value, v->ty->b));
    }
    if (tk(ty_of(t, tg->b)) == TK_INT) terr(t, s->line, "a list index is a Nat in a checked file; turn an int into a Nat with to_nat(...)");
    const char *ix = E(t, tg->b);
    const char *p = fact_proof(t, af("(%s < (len %s))", ix, x), true);
    const char *val = EW(t, value, v->ty->a);
    return af("(setAt %s %s %s %s)", x, ix, val, p ? p : auto_refl(t, s->line));
}

__attribute__((noreturn)) static void bad_stmt(T *t, Node *s, bool top) {
    const char *what = "this statement";
    switch (s->kind) {
    case S_WHILE: case S_FOR: what = "a loop"; break;
    case S_BREAK: case S_CONTINUE: what = "break or continue outside a loop"; break;
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
                         "classes, defs, assignments, print, assert, and for loops that print.", what);
    terr(t, s->line, "%s is not available inside a checked function. A checked function is built from "
                     "assignments, if, match, for, while and return.", what);
}

/* --- patterns --- */

static Var *bindv(T *t, Str *py, const char *term, bool split, Ty *ty, int line) {
    bind(t, py, term, split, tk(ty) == TK_NAT, line);
    t->c.env->ty = ty;
    return t->c.env;
}

/* A match pattern as Agda text. Captures are bound in the environment. `ty` is
   what is known of the type of the value being matched. */
static const char *pattern(T *t, Node *p, Ty *ty) {
    switch (p->kind) {
    case P_CAPTURE: {
        const char *a = lhs_var(t, fresh(t, p->val ? AS_STR(p->val)->s : "_"));
        if (p->val) bindv(t, AS_STR(p->val), a, true, ty, p->line);
        return a;
    }
    case P_VALUE:
        if (p->a->kind == E_UNARY && p->a->op == '-' && is_int_const(p->a->a)) return int_lit(t, p->a->a, true);
        if (p->a->kind != E_CONST) terr(t, p->line, "a dotted name is not available as a pattern in a checked file");
        if (is_int_const(p->a) && tk(ty) == TK_INT) return int_lit(t, p->a, false);
        return const_text(t, p->a);
    case P_AS: {
        const char *inner = pattern(t, p->a, ty);
        bind(t, AS_STR(p->val), inner, is_lhs_var(t, inner), tk(ty) == TK_NAT, p->line);
        t->c.env->ty = ty;
        return inner;
    }
    case P_CLASS: {
        if (p->a->kind != E_NAME) terr(t, p->line, "a pattern names a constructor directly");
        if (p->n[1]) terr(t, p->line, "keyword patterns are not available in a checked file");
        bool in_type = t->c.in_type;
        t->c.in_type = false;
        Ref r = lookup(t, AS_STR(p->a->val), p->line);
        t->c.in_type = in_type;
        if (!r.s || (r.s->kind != SK_CTOR && r.s->kind != SK_RECORD)) terr(t, p->line, "'%s' is not a constructor", AS_STR(p->a->val)->s);
        Sym *s = r.s;
        int arity = s->kind == SK_RECORD ? s->nfields : s->arity;
        if (p->n[0] != arity)
            terr(t, p->line, "the pattern %s(...) must list all %d field%s of the constructor", s->py->s, arity, arity == 1 ? "" : "s");
        const char *text = s->kind == SK_RECORD ? s->ctor : s->agda;
        if (!arity) return text;
        sym_tys(t, s);
        for (int i = 0; i < p->n[0]; i++) {
            Ty *ft = s->kind == SK_RECORD ? s->fields[i]->ty : s->p_tys ? s->p_tys[i] : NULL;
            text = af("%s %s", text, pattern(t, p->l[0][i], ft));
        }
        return af("(%s)", text);
    }
    case P_SEQ: {
        int c = p->n[0];
        if (tk(ty) == TK_TUPLE) {
            if (c != ty->n) terr(t, p->line, "this pattern has %d part%s and the tuple has %d", c, c == 1 ? "" : "s", ty->n);
            const char **parts = arena_alloc((size_t)(c + 1) * sizeof(char *));
            for (int i = 0; i < c; i++) {
                if (p->l[0][i]->kind == P_STAR) terr(t, p->line, "* is not available in a tuple pattern in a checked file");
                parts[i] = pattern(t, p->l[0][i], ty->items[i]);
            }
            const char *text = parts[c - 1];
            for (int i = c - 2; i >= 0; i--) text = af("(%s , %s)", parts[i], text);
            return text;
        }
        if (tk(ty) != TK_LIST)
            terr(t, p->line, "the checker cannot tell whether this pattern is for a list or a tuple. "
                             "Give the value being matched a type annotation.");
        const char **parts = arena_alloc((size_t)(c + 1) * sizeof(char *));
        const char *text = "[]";
        for (int i = 0; i < c; i++) {
            Node *q = p->l[0][i];
            if (q->kind == P_STAR) {
                if (i != c - 1) terr(t, p->line, "in a list pattern, * comes last in a checked file: [first, *rest]");
                text = lhs_var(t, fresh(t, q->val ? AS_STR(q->val)->s : "_"));
                if (q->val) bindv(t, AS_STR(q->val), text, true, ty, q->line);
                c--;
                break;
            }
            parts[i] = pattern(t, q, ty->a);
        }
        for (int i = c - 1; i >= 0; i--) text = af("(%s \xE2\x88\xB7 %s)", parts[i], text);
        return text;
    }
    case P_OR: terr(t, p->line, "'|' is only available at the top of a case in a checked file");
    default: terr(t, p->line, "mapping patterns are not available in a checked file");
    }
}

/* The target of a for loop, a comprehension or `a, b = e`: a name, or names in a tuple. */
static const char *target_pat(T *t, Node *target, Ty *ty) {
    if (target->kind == E_NAME) {
        const char *a = fresh(t, AS_STR(target->val)->s);
        bindv(t, AS_STR(target->val), a, false, ty, target->line);
        return a;
    }
    if (target->kind != E_TUPLE || target->n[0] < 2)
        terr(t, target->line, "this target is a name, or names in a tuple, in a checked file");
    int c = target->n[0];
    bool typed = tk(ty) == TK_TUPLE && ty->n == c;
    const char **parts = arena_alloc((size_t)c * sizeof(char *));
    for (int i = 0; i < c; i++) parts[i] = target_pat(t, target->l[0][i], typed ? ty->items[i] : NULL);
    const char *text = parts[c - 1];
    for (int i = c - 2; i >= 0; i--) text = af("(%s , %s)", parts[i], text);
    return text;
}

/* The same for a column of the left-hand side, whose names can be matched on later. */
static const char *target_lhs(T *t, Node *target, Ty *ty) {
    if (target->kind == E_NAME) {
        const char *a = lhs_var(t, fresh(t, AS_STR(target->val)->s));
        bindv(t, AS_STR(target->val), a, true, ty, target->line);
        return a;
    }
    if (target->kind != E_TUPLE || target->n[0] < 2)
        terr(t, target->line, "this target is a name, or names in a tuple, in a checked file");
    int c = target->n[0];
    bool typed = tk(ty) == TK_TUPLE && ty->n == c;
    const char **parts = arena_alloc((size_t)c * sizeof(char *));
    for (int i = 0; i < c; i++) parts[i] = target_lhs(t, target->l[0][i], typed ? ty->items[i] : NULL);
    const char *text = parts[c - 1];
    for (int i = c - 2; i >= 0; i--) text = af("(%s , %s)", parts[i], text);
    return text;
}

static void dead_targets(T *t, Node *target) {
    if (target->kind == E_NAME) {
        bind(t, AS_STR(target->val), "_", false, false, target->line);
        t->c.env->dead = true;
    } else if (target->kind == E_TUPLE) for (int i = 0; i < target->n[0]; i++) dead_targets(t, target->l[0][i]);
}

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

/* The statement `decreases(measure, proof)`. */
static bool is_decreases(T *t, Node *s) {
    if (s->kind != S_EXPR || s->a->kind != E_CALL || s->a->a->kind != E_NAME) return false;
    Str *nm = AS_STR(s->a->a->val);
    return str_eqc(nm, "decreases") && !find_var(t, nm) && !find_sym(t, nm) && sym_is(prelude_sym(t, nm), "decreases");
}

/* --- statements as one expression: loop bodies, and runs with no split --- */

/* What the end of the statements means, and return, break and continue. */
typedef struct {
    bool loop, is_while;
    Str **carried; int n;      /* loop: the names whose values travel round the loop */
    bool *stale;               /* loop: the name was shared by the end of some pass */
    Node *dec;                 /* while: its decreases(...) statement */
    Ty *ret;                   /* what the function returns */
    Leak *entry, *seen;        /* loop: what was shared on entry, and what any pass shared since */
} Tail;

static const char *state_text(T *t, Tail *tl, int line) {
    const char *text = NULL;
    /* The paths through the body meet again after the loop. A name that any one
       of them handed on is shared from then on. */
    for (Leak *l = t->c.leaks; l && l != tl->entry; l = l->next) {
        Leak *c = arena_alloc(sizeof(Leak));
        c->id = l->id; c->next = tl->seen;
        tl->seen = c;
    }
    for (int i = tl->n - 1; i >= 0; i--) {
        Var *v = find_var(t, tl->carried[i]);
        if (!v || v->dead)
            terr(t, line, "'%s' is assigned outside this loop and is also the variable of a for loop inside it. "
                          "Give one of them another name.", tl->carried[i]->s);
        if (!is_fresh(t, v)) tl->stale[i] = true;
        text = text ? af("(%s , %s)", v->term, text) : v->term;
    }
    return text ? text : "tt";
}

static const char *ret_text(Tail *tl, const char *e) {
    return !tl->loop ? e : af(tl->is_while ? "(wret %s)" : "(ret %s)", e);
}

static void no_more(T *t, K *k, const char *what) {
    for (int j = k->i + 1; j < k->n; j++)
        if (!skippable(k->l[j]))
            terr(t, k->l[j]->line, "this statement can never run: the %s above it always leaves the %s", what,
                 strcmp(what, "return") ? "loop or this pass of it" : "function");
}

typedef struct { Str **carried; int n; bool has_ret; bool *fresh_after; Ty **tys; } Loop;
static void loop_emit(T *t, Node *s, Tail *outer, Loop *L);
static void sx(T *t, K *k, Tail *tl, int line);

/* Tries the statements after a match as its last case. They may read a name that
   only the cases assign; then no last case is written, and Agda insists that the
   cases cover everything. Returns whether the case was written. */
static bool sx_try(T *t, const char *head, K *rest, Tail *tl, int line) {
    jmp_buf jb, *outer = t->try_jmp;
    Sym *fn = t->fn;
    Ctx before = t->c;
    Out *cur = t->cur;
    size_t n = cur->n;
    int nl = cur->nl;
    if (setjmp(jb) == 0) {
        t->try_jmp = &jb;
        o_put(t->cur, line, head);
        sx(t, rest, tl, line);
        t->try_jmp = outer;
        return true;
    }
    t->try_jmp = outer;
    t->c = before;
    t->cur = cur;
    cur->n = n; cur->nl = nl;
    if (cur->s) cur->s[n] = 0;
    t->fn = fn;
    for (int i = 0; i < t->nsyms; i++) { t->syms[i]->busy = false; t->syms[i]->in_body = t->syms[i] == fn; }
    return false;
}

static void sx(T *t, K *k, Tail *tl, int line) {
    for (;;) {
        k = k_norm(k);
        if (!k) {
            if (!tl->loop) terr(t, line, "this function can reach its end without a return");
            const char *st = state_text(t, tl, line);
            if (tl->is_while) o_put(t->cur, tl->dec->line, af("  (wnext %s %s)\n", st, E(t, tl->dec->a->l[0][1])));
            else o_put(t->cur, line, af("  (next %s)\n", st));
            return;
        }
        Node *s = k->l[k->i];
        K *rest = k_tail(k);
        if (skippable(s)) { k = rest; continue; }
        line = s->line;
        switch (s->kind) {
        case S_RETURN:
            if (!s->a) terr(t, s->line, "return needs a value in a checked function");
            no_more(t, k, "return");
            o_put(t->cur, s->line, af("  %s\n", ret_text(tl, EW(t, s->a, tl->ret))));
            return;
        case S_BREAK: case S_CONTINUE: {
            if (!tl->loop) bad_stmt(t, s, false);
            no_more(t, k, s->kind == S_BREAK ? "break" : "continue");
            if (tl->is_while && s->kind == S_CONTINUE)
                terr(t, s->line, "continue is not available in a while loop in a checked file: every pass must reach "
                                 "the decreases(...) at the end of the loop");
            const char *st = state_text(t, tl, s->line);
            o_put(t->cur, s->line, af("  (%s %s)\n", s->kind == S_CONTINUE ? "next" : tl->is_while ? "wstop" : "stop", st));
            return;
        }
        case S_EXPR: case S_ASSIGN: case S_ANNASSIGN: case S_AUGASSIGN: {
            Var *old = NULL;
            const char *mt = mutation(t, s, &old);
            if (mt) {
                const char *a = fresh(t, old->py->s);
                o_put(t->cur, s->line, af("  let %s = %s in\n", a, mt));
                bindv(t, old->py, a, false, old->ty, s->line)->fresh0 = true;
                k = rest;
                continue;
            }
            if (s->kind == S_EXPR) {
                if (is_decreases(t, s))
                    terr(t, s->line, "decreases(measure, proof) is written once, as the last statement of a while loop");
                bad_stmt(t, s, false);
            }
            Asg a;
            plain_assign(t, s, &a);
            Ty *want;
            Ty *vt = assign_ty(t, &a, &want);
            bool fr = is_fresh_expr(t, a.value);
            const char *e = EW(t, a.value, want);
            if (a.tuple) o_put(t->cur, s->line, af("  let %s = %s in\n", target_pat(t, a.tuple, vt), e));
            else if (a.value->kind == E_NAME && !a.ann && find_var(t, AS_STR(a.value->val)))
                bindv(t, a.name, e, false, vt, s->line);   /* another name for the same thing */
            else {
                const char *ty = a.ann ? tr_type(t, a.ann) : NULL;
                const char *x = fresh(t, a.name->s);
                o_put(t->cur, s->line, ty ? af("  let %s : %s ; %s = %s in\n", x, ty, x, e) : af("  let %s = %s in\n", x, e));
                bindv(t, a.name, x, false, vt, s->line)->fresh0 = fr;
            }
            k = rest;
            continue;
        }
        case S_IF: {
            s = split_boolop(s);
            bool pol;
            const char *core = cond_core(t, s->a, &pol);
            const char *h = fresh(t, "h");
            Mark m = mark(t);
            /* ifd c (λ h → when c is true) (λ h → when c is false) */
            o_put(t->cur, s->line, af("  (ifd %s (\xCE\xBB %s \xE2\x86\x92\n", core, h));
            add_fact(t, core, true, h);
            sx(t, k_push(s->l[pol ? 0 : 1], s->n[pol ? 0 : 1], rest), tl, s->line);
            reset(t, m);
            o_put(t->cur, s->line, af("  ) (\xCE\xBB %s \xE2\x86\x92\n", h));
            add_fact(t, core, false, h);
            sx(t, k_push(s->l[pol ? 1 : 0], s->n[pol ? 1 : 0], rest), tl, s->line);
            reset(t, m);
            o_put(t->cur, s->line, "  ))\n");
            return;
        }
        case S_MATCH: {
            Ty *ty = ty_of(t, s->a);
            const char *target = E(t, s->a);
            Mark m = mark(t);
            bool catch_all = false, first = true;
            o_put(t->cur, s->line, af("  (case\xE2\x82\x80 %s of \xCE\xBB {\n", target));
            for (int i = 0; i < s->n[0]; i++) {
                Node *c = s->l[0][i];
                if (c->b) terr(t, c->line, "'case ... if' is not available in a checked file; use an if inside the case");
                if (is_unreachable(t, c))
                    terr(t, c->line, "'case _: return unreachable()' is not available inside a loop; do this match in a function of its own");
                if (c->a->kind == P_CAPTURE) catch_all = true;
                Node *one[1] = {c->a}, **alts = one;
                int nalts = 1;
                if (c->a->kind == P_OR) { alts = c->a->l[0]; nalts = c->a->n[0]; }
                for (int j = 0; j < nalts; j++) {
                    reset(t, m);
                    const char *p = pattern(t, alts[j], ty);
                    o_put(t->cur, c->line, af("  %s%s \xE2\x86\x92\n", first ? "" : "; ", p));
                    first = false;
                    sx(t, k_push(c->l[0], c->n[0], rest), tl, c->line);
                }
            }
            reset(t, m);
            if (!catch_all) sx_try(t, first ? "  _ \xE2\x86\x92\n" : "  ; _ \xE2\x86\x92\n", rest, tl, s->line);
            reset(t, m);
            o_put(t->cur, s->line, "  })\n");
            return;
        }
        case S_FOR: case S_WHILE: {
            Loop L;
            bool has_ret = has_return(s->l[0], s->n[0]);
            Var *env = t->c.env;
            o_put(t->cur, s->line, has_ret ? "  (case\xE2\x82\x80 (\n" : "  (case\xE2\x82\x80 (runLoop (\n");
            loop_emit(t, s, tl, &L);
            t->c.env = env;
            if (has_ret) {
                const char *r = fresh(t, "r");
                o_put(t->cur, s->line, af("  ) of \xCE\xBB { (early %s) \xE2\x86\x92 %s ; (out \n", r, ret_text(tl, r)));
            } else o_put(t->cur, s->line, "  )) of \xCE\xBB { (\n");
            const char *pat = NULL;
            const char **names = arena_alloc((size_t)(L.n + 1) * sizeof(char *));
            for (int i = 0; i < L.n; i++) names[i] = fresh(t, L.carried[i]->s);
            for (int i = L.n - 1; i >= 0; i--) pat = pat ? af("(%s , %s)", names[i], pat) : names[i];
            for (int i = 0; i < L.n; i++) bindv(t, L.carried[i], names[i], false, L.tys[i], s->line)->fresh0 = L.fresh_after[i];
            if (s->kind == S_FOR) dead_targets(t, s->a);
            o_put(t->cur, s->line, af("  %s) \xE2\x86\x92\n", pat ? pat : "_"));
            sx(t, rest, tl, s->line);
            o_put(t->cur, s->line, "  })\n");
            return;
        }
        default: bad_stmt(t, s, false);
        }
    }
}

/* Names that a loop reads item by item while it runs, so the body must not change them. */
static void live_names(T *t, Names *ns, Node *it) {
    if (it->kind == E_NAME) names_add(ns, AS_STR(it->val));
    else if (it->kind == E_CALL) {
        const char *b = plain_builtin(t, it->a);
        if (b && (!strcmp(b, "enumerate") || !strcmp(b, "zip") || !strcmp(b, "reversed")))
            for (int i = 0; i < it->n[0]; i++) live_names(t, ns, it->l[0][i]);
        else if (it->a->kind == E_ATTR && it->a->a->kind == E_NAME) names_add(ns, AS_STR(it->a->a->val));
    }
}

/* Writes a loop as one expression of type Out R S. The state S holds the values
   of the names the body assigns; the body is a function from the state (and the
   item) to what happens next. */
static void loop_emit(T *t, Node *s, Tail *outer, Loop *L) {
    bool is_while = s->kind == S_WHILE;
    if (s->n[1]) terr(t, s->line, "'else' on a loop is not available in a checked file");
    Node **body = s->l[0], *dec = NULL;
    int nb = s->n[0];
    if (is_while) {
        int last = nb - 1;
        while (last >= 0 && skippable(body[last])) last--;
        if (last < 0 || !is_decreases(t, body[last]) || body[last]->a->n[0] != 2)
            terr(t, s->line, "a while loop in a checked file ends with decreases(measure, proof). The measure is a Nat "
                             "worked out from the loop's variables, and the proof shows that it is smaller at the end of "
                             "a pass than it was at the start: (new measure < old measure) == True. That is how the "
                             "checker knows the loop ends. A for loop needs none of this.");
        dec = body[last];
        nb = last;
    }
    Names as = {0}, mu = {0}, live = {0};
    assigned(t, &as, s->l[0], s->n[0], true);
    assigned(t, &mu, s->l[0], s->n[0], false);
    memset(L, 0, sizeof *L);
    L->carried = arena_alloc((size_t)(as.n + 1) * sizeof(Str *));
    for (int i = 0; i < as.n; i++) {
        Var *v = find_var(t, as.v[i]);
        if (v && !v->erased && !v->dead) L->carried[L->n++] = as.v[i];
    }
    L->has_ret = has_return(s->l[0], s->n[0]);
    L->tys = arena_alloc((size_t)(L->n + 1) * sizeof(Ty *));
    L->fresh_after = arena_alloc((size_t)L->n + 1);
    bool *fresh_in = arena_alloc((size_t)L->n + 1);
    const char *init = NULL, *spat = NULL, *head = NULL, *bound = NULL;
    for (int i = L->n - 1; i >= 0; i--) {
        Var *v = find_var(t, L->carried[i]);
        fresh_in[i] = is_fresh(t, v);
        L->tys[i] = v->ty;
        init = init ? af("(%s , %s)", v->term, init) : v->term;
    }
    if (!init) init = "tt";
    Ty *elem = NULL;
    if (!is_while) {
        live_names(t, &live, s->b);
        for (int i = 0; i < live.n; i++)
            if (names_has(&mu, live.v[i]))
                terr(t, s->line, "the loop changes '%s' while it is running over it. Loop over a copy: list(%s)",
                     live.v[i]->s, live.v[i]->s);
        const char *b = s->b->kind == E_CALL ? plain_builtin(t, s->b->a) : NULL;
        if (b && !strcmp(b, "range") && s->b->n[0] == 1 && s->b->l[0][0]->kind != E_KEYWORD && s->b->l[0][0]->kind != E_STARRED) {
            if (tk(ty_of(t, s->b->l[0][0])) == TK_INT)
                terr(t, s->line, "range counts with Nat; turn an int into a Nat with to_nat(...)");
            bound = E(t, s->b->l[0][0]);
        } else {
            elem = iter_elem(t, s->b);
            head = EI(t, s->b);
        }
    }
    Tail tl = {0};
    tl.loop = true; tl.is_while = is_while; tl.carried = L->carried; tl.n = L->n; tl.dec = dec; tl.ret = outer->ret;
    tl.stale = arena_alloc((size_t)L->n + 1);
    tl.entry = t->c.leaks;
    const char **names = arena_alloc((size_t)(L->n + 1) * sizeof(char *));
    for (int i = 0; i < L->n; i++) names[i] = fresh(t, L->carried[i]->s);
    for (int i = L->n - 1; i >= 0; i--) spat = spat ? af("(%s , %s)", names[i], spat) : names[i];
    if (!spat) spat = "_";
    for (int i = 0; i < L->n; i++) bindv(t, L->carried[i], names[i], false, L->tys[i], s->line)->fresh0 = fresh_in[i];
    if (is_while) {
        bool pol;
        const char *core = cond_core(t, s->a, &pol);
        if (tk(ty_of(t, dec->a->l[0][0])) == TK_INT)
            terr(t, dec->line, "the measure of a while loop is a Nat; turn an int into a Nat with to_nat(...)");
        const char *measure = E(t, dec->a->l[0][0]);
        const char *h = fresh(t, "h");
        o_put(t->cur, s->line, af("  (whileDo (\xCE\xBB { %s \xE2\x86\x92 %s%s%s }) (\xCE\xBB { %s \xE2\x86\x92 %s }) %s (\xCE\xBB { %s %s \xE2\x86\x92\n",
                                  spat, pol ? "" : "(not ", core, pol ? "" : ")", spat, measure, init, spat, h));
        add_fact(t, core, pol, pol ? h : af("(unNot %s)", h));
    } else if (bound) {
        if (s->a->kind != E_NAME) terr(t, s->line, "the variable of a loop over range(...) is a plain name");
        const char *i = target_pat(t, s->a, &ty_nat);
        const char *h = fresh(t, "h");
        o_put(t->cur, s->line, af("  (forRange %s %s (\xCE\xBB { %s %s %s \xE2\x86\x92\n", bound, init, i, h, spat));
        add_fact(t, af("(%s < %s)", i, bound), true, h);
    } else {
        const char *pat = target_pat(t, s->a, elem);
        o_put(t->cur, s->line, af("  (forEach %s %s (\xCE\xBB { %s %s \xE2\x86\x92\n", head, init, pat, spat));
    }
    Leak *entry = t->c.leaks;
    sx(t, k_push(body, nb, NULL), &tl, s->line);
    o_put(t->cur, s->line, "  }))\n");
    t->c.leaks = entry;
    for (Leak *l = tl.seen; l; l = l->next) {
        Leak *c = arena_alloc(sizeof(Leak));
        c->id = l->id; c->next = t->c.leaks;
        t->c.leaks = c;
    }
    for (int i = 0; i < L->n; i++) {
        if (tl.stale[i] && names_has(&mu, L->carried[i]))
            terr(t, s->line, "inside this loop '%s' is changed in place and is also shared (given another name, passed on, "
                             "or put inside another value). On the next pass the change would show through the other "
                             "holder. Share a copy instead: list(%s)", L->carried[i]->s, L->carried[i]->s);
        L->fresh_after[i] = fresh_in[i] && !tl.stale[i];
    }
}

/* --- statements as clauses --- */

static void clauses(T *t, const char *lhs, K *k, int line);

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
    Tail fn_tail = {0};
    fn_tail.ret = t->c.ret_ty;
    for (;;) {
        k = k_norm(k);
        if (!k) terr(t, line, "this function can reach its end without a return");
        Node *s = k->l[k->i];
        K *rest = k_tail(k);
        if (skippable(s)) { k = rest; continue; }
        if (!has_split(k)) {
            o_put(t->cur, s->line, af("%s =\n", lhs));
            sx(t, k, &fn_tail, line);
            return;
        }
        switch (s->kind) {
        case S_EXPR: case S_ASSIGN: case S_ANNASSIGN: case S_AUGASSIGN: {
            Var *old = NULL;
            const char *mt = mutation(t, s, &old);
            if (mt) {
                /* The name stands for its new value from here on. */
                bindv(t, old->py, mt, false, old->ty, s->line)->fresh0 = true;
                k = rest;
                continue;
            }
            if (s->kind == S_EXPR) {
                if (is_decreases(t, s))
                    terr(t, s->line, "decreases(measure, proof) is written once, as the last statement of a while loop");
                bad_stmt(t, s, false);
            }
            Asg a;
            plain_assign(t, s, &a);
            Ty *want;
            Ty *vt = assign_ty(t, &a, &want);
            bool fr = is_fresh_expr(t, a.value);
            const char *e = EW(t, a.value, want);
            bool nat = false;
            if (a.tuple) {
                o_put(t->cur, s->line, af("%s with %s\n", lhs, e));
                lhs = af("%s | %s", lhs, target_lhs(t, a.tuple, vt));
            } else if (a.value->kind == E_NAME && var_by_term(t, e, &nat)) {
                /* Another name for a variable that is already there. An annotation is
                   still checked, in a column that nothing reads. */
                if (a.ann) {
                    o_put(t->cur, s->line, af("%s with the %s %s\n", lhs, tr_type(t, a.ann), e));
                    lhs = af("%s | %s", lhs, fresh(t, "_"));
                }
                bind(t, a.name, e, true, nat, s->line);
                t->c.env->ty = vt;
            } else if (k_splits_on(rest, a.name)) {
                /* The name is taken apart later, so it has to be a variable of the
                   left-hand side: a column. */
                const char *x = lhs_var(t, fresh(t, a.name->s));
                o_put(t->cur, s->line, a.ann ? af("%s with the %s %s\n", lhs, tr_type(t, a.ann), e) : af("%s with %s\n", lhs, e));
                lhs = af("%s | %s", lhs, x);
                bindv(t, a.name, x, true, vt, s->line)->fresh0 = fr;
            } else {
                /* Otherwise the name stands for its expression, so Agda still sees that
                   the two are the same thing. */
                bindv(t, a.name, a.ann ? af("(the %s %s)", tr_type(t, a.ann), e) : e, false, vt, s->line)->fresh0 = fr;
            }
            k = rest;
            continue;
        }
        case S_IF: {
            s = split_boolop(s);
            K *kt = k_push(s->l[0], s->n[0], rest), *ke = k_push(s->l[1], s->n[1], rest);
            Mark m = mark(t);
            bool then_is_zero = false, nat = false;
            Node *x = zero_test(s->a, &then_is_zero);
            if (x && tk(ty_of(t, x->kind == E_BINOP ? x->a : x)) == TK_INT) x = NULL;
            const char *xt = x ? E(t, x) : NULL;
            if (x && var_by_term(t, xt, &nat)) {
                /* A Nat is zero or suc of something: two sets of clauses. */
                const char *p = lhs_var(t, fresh_agda(t, xt));
                const char *suc = af("(suc %s)", p);
                Var *env_zero = env_subst(t, xt, "zero", false, NULL), *env_suc = env_subst(t, xt, suc, false, p);
                const char *lhs_zero = subst_ident(lhs, xt, "zero"), *lhs_suc = subst_ident(lhs, xt, suc);
                t->c.env = then_is_zero ? env_zero : env_suc;
                clauses(t, then_is_zero ? lhs_zero : lhs_suc, kt, s->line);
                t->c.leaks = m.leaks;
                t->c.env = then_is_zero ? env_suc : env_zero;
                clauses(t, then_is_zero ? lhs_suc : lhs_zero, ke, s->line);
            } else {
                /* Any other test is a `with` on it. In each branch Agda has replaced the
                   test by true or false in the types, and h is the proof of which. */
                bool pol;
                const char *core = cond_core(t, s->a, &pol);
                const char *h = fresh(t, "h");
                int fid = new_fact_id(t);
                o_put(t->cur, s->line, af("%s with %s\x02%d %s\x02\n", lhs, core, fid, h));
                add_fact(t, core, pol, h);
                t->c.env->fid = fid;
                clauses(t, af("%s | %s", lhs, pol ? "true" : "false"), kt, s->line);
                reset(t, m);
                add_fact(t, core, !pol, h);
                t->c.env->fid = fid;
                clauses(t, af("%s | %s", lhs, pol ? "false" : "true"), ke, s->line);
            }
            reset(t, m);
            return;
        }
        case S_FOR: case S_WHILE: {
            Loop L;
            bool has_ret = has_return(s->l[0], s->n[0]);
            Var *env = t->c.env;
            o_put(t->cur, s->line, af(has_ret ? "%s with (\n" : "%s with (runLoop (\n", lhs));
            loop_emit(t, s, &fn_tail, &L);
            t->c.env = env;
            o_put(t->cur, s->line, has_ret ? "  )\n" : "  ))\n");
            if (has_ret) {
                const char *r = fresh(t, "r");
                o_put(t->cur, s->line, af("%s | early %s =\n  %s\n", lhs, r, r));
            }
            const char *pat = NULL;
            const char **names = arena_alloc((size_t)(L.n + 1) * sizeof(char *));
            for (int i = 0; i < L.n; i++) names[i] = lhs_var(t, fresh(t, L.carried[i]->s));
            for (int i = L.n - 1; i >= 0; i--) pat = pat ? af("(%s , %s)", names[i], pat) : names[i];
            if (!pat) pat = fresh(t, "_");
            for (int i = 0; i < L.n; i++) bindv(t, L.carried[i], names[i], true, L.tys[i], s->line)->fresh0 = L.fresh_after[i];
            if (s->kind == S_FOR) dead_targets(t, s->a);
            lhs = has_ret ? af("%s | out %s", lhs, pat) : af("%s | %s", lhs, pat);
            k = rest;
            continue;
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
                Mark m = mark(t);
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
                    reset(t, m);
                    if (p->l[0][0]->val) bind(t, AS_STR(p->l[0][0]->val), eq, false, false, c->line);
                    clauses(t, af("%s | %s", lhs, yes ? "true" : "false"), k_push(c->l[0], c->n[0], rest), c->line);
                }
                reset(t, m);
                if (!seen[0] || !seen[1])
                    terr(t, s->line, "'match decide(...)' needs both cases: 'case Yes(p):' and 'case No(p):'");
                return;
            }
            Ty *ty = ty_of(t, s->a);
            const char *target = E(t, s->a);
            bool nat = false;
            if (!var_by_term(t, target, &nat)) {
                const char *a = lhs_var(t, fresh(t, "m"));
                o_put(t->cur, s->line, af("%s with %s\n", lhs, target));
                lhs = af("%s | %s", lhs, a);
                target = a;
            }
            if (nat && !ty) ty = &ty_nat;
            Mark m = mark(t);
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
                    reset(t, m);
                    const char *p = pattern(t, alts[j], ty);
                    /* The new bindings must not be rewritten: they are the replacement. */
                    Var *bound = t->c.env;
                    t->c.env = m.env;
                    Var *env = env_subst(t, target, p, is_lhs_var(t, p), NULL);
                    Var *head = NULL, **tail = &head;
                    for (Var *v = bound; v != m.env; v = v->next) {
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
            reset(t, m);
            if (rest && !catch_all) fall_through(t, lhs, rest, s->line);
            return;
        }
        default: bad_stmt(t, s, false);
        }
    }
}

/* The square-bracket names in scope in a def: those of its class, then its own. */
static int tparams_of(Sym *s, Node ***out) {
    Node *def = s->node, *cls = s->owner ? s->owner->node : NULL;
    int nc = cls ? cls->n[3] : 0, nd = def->n[2];
    Node **all = arena_alloc((size_t)(nc + nd + 1) * sizeof(Node *));
    for (int i = 0; i < nc; i++) all[i] = cls->l[3][i];
    for (int i = 0; i < nd; i++) all[nc + i] = def->l[2][i];
    *out = all;
    return nc + nd;
}

static void emit_func_body(T *t, Sym *s) {
    Node *def = s->node;
    Sym *outer_fn = t->fn;
    s->in_body = true;
    if (!s->sig_done) emit_func_sig(t, s);
    if (s->trusted) { s->body_done = true; s->in_body = false; return; }
    t->fn = s;
    Out chunk = {0}, *outer = t->cur;
    Ctx saved = t->c;
    t->c = (Ctx){0};
    t->cur = &chunk;
    Node **tps;
    int ntp = tparams_of(s, &tps), np = def->params->n;
    t->c.tp_used = arena_alloc((size_t)ntp + 1);
    const char *lhs = af("%s\x01", s->agda);
    for (int i = 0; i < ntp; i++) { mark_used(t, s->tp_names[i]); erased_var(t, AS_STR(tps[i]->val), s->tp_names[i], i); }
    sym_tys(t, s);
    for (int i = 0; i < np; i++) {
        mark_used(t, s->p_names[i]);
        lhs_var(t, s->p_names[i]);
        Ty *pt = def->params->ann[i] ? ty_ann(t, def->params->ann[i], NULL, NULL) : s->p_tys[i];
        bindv(t, def->params->names[i], s->p_names[i], true, pt, def->line);
        lhs = af("%s %s", lhs, s->p_names[i]);
    }
    t->c.ret_ty = ty_ann(t, def->b, NULL, NULL);
    collect_locals(t, def->l[0], def->n[0]);
    {
        Names ints = {0};
        Var *env = t->c.env;
        scan_ints(t, &ints, def->l[0], def->n[0]);
        scan_ints(t, &ints, def->l[0], def->n[0]);
        t->c.env = env;
        t->c.ints = ints.v; t->c.nints = ints.n;
    }
    clauses(t, lhs, k_push(def->l[0], def->n[0], NULL), def->line);
    /* Bind the [] names the body mentioned, so its annotations can refer to them. */
    const char *binders = "";
    for (int i = 0; i < ntp; i++) if (t->c.tp_used[i]) binders = af("%s {%s = %s}", binders, s->tp_names[i], s->tp_names[i]);
    t->c = saved;
    t->cur = outer;
    for (int i = 0, at = 0; i < chunk.nl; i++) {
        char *nl = strchr(chunk.s + at, '\n');
        *nl = 0;
        const char *text = chunk.s + at;
        char *mark = strchr(text, '\x01');
        if (mark) { *mark = 0; text = af("%s%s%s", text, binders, mark + 1); }
        char *f1 = strchr(text, '\x02');
        if (f1) {
            /* "\x02<id> <name>\x02" becomes " in <name>" if the proof was asked for */
            char *f2 = strchr(f1 + 1, '\x02'), *sp = strchr(f1 + 1, ' ');
            int fid = atoi(f1 + 1);
            text = t->fact_used[fid] ? af("%.*s in %.*s%s", (int)(f1 - text), text, (int)(f2 - sp - 1), sp + 1, f2 + 1)
                                     : af("%.*s%s", (int)(f1 - text), text, f2 + 1);
        }
        o_put(&t->out, chunk.lines[i], af("%s\n", text));
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
        } else {
            /* another checked file: read once every import has been seen */
            Node **ni = arena_alloc((size_t)(t->nimport_stmts + 1) * sizeof *ni);
            if (t->nimport_stmts) memcpy(ni, t->import_stmts, (size_t)t->nimport_stmts * sizeof *ni);
            ni[t->nimport_stmts++] = s;
            t->import_stmts = ni;
        }
    }
}

/* @dataclass class Name[...]: fields (name: type) and methods. */
static void declare_record(T *t, Node *cls) {
    Str *name = AS_STR(cls->val);
    Sym *r = add_sym(t, name, SK_RECORD, cls->n[3], cls, cls->line);
    r->agda = top_name(name->s);
    r->ctor = af("new-%s", r->agda);
    r->fields = arena_alloc((size_t)(cls->n[0] + 1) * sizeof(Sym *));
    r->methods = arena_alloc((size_t)(cls->n[0] + 1) * sizeof(Sym *));
    for (int i = 0; i < cls->n[0]; i++) {
        Node *st = cls->l[0][i];
        if (st->kind == S_PASS || (st->kind == S_EXPR && st->a->kind == E_CONST)) continue;
        if (st->kind == S_ANNASSIGN && st->a->kind == E_NAME) {
            Str *fname = AS_STR(st->a->val);
            if (st->b) terr(t, st->line, "a field with a default value is not available in a checked file");
            for (int j = 0; j < r->nfields; j++)
                if (str_eq(r->fields[j]->py, fname)) terr(t, st->line, "the field '%s' is declared twice", fname->s);
            Sym *f = arena_alloc(sizeof(Sym));
            f->py = fname; f->kind = SK_FIELD; f->node = st->c; f->line = st->line; f->agda = top_name(fname->s);
            r->fields[r->nfields++] = f;
        } else if (st->kind == S_DEF) {
            Str *mname = AS_STR(st->val);
            if (st->n[1]) terr(t, st->line, "decorators on a method are not available in a checked file");
            for (int j = 0; j < r->nfields; j++)
                if (str_eq(r->fields[j]->py, mname)) terr(t, st->line, "'%s' is already a field of this class", mname->s);
            for (int j = 0; j < r->nmethods; j++)
                if (str_eq(r->methods[j]->mname, mname)) terr(t, st->line, "the method '%s' is defined twice", mname->s);
            Sym *m = add_sym(t, AS_STR(str_c(af("%s.%s", name->s, mname->s))), SK_FUNC, st->params->n, st, st->line);
            m->agda = af("%s-%s", r->agda, mangle(mname->s));
            m->owner = r; m->mname = mname;
            r->methods[r->nmethods++] = m;
        } else
            terr(t, st->line, "the body of a @dataclass class holds fields, written 'name: type', and methods");
    }
}

/* First pass: learn every top-level name, so later definitions can be referred to. */
static void prepass(T *t, Node **l, int n, bool guard) {
    for (int i = 0; i < n; i++) {
        Node *s = l[i];
        if (guard && s->kind != S_ASSIGN && s->kind != S_ANNASSIGN && s->kind != S_EXPR && s->kind != S_ASSERT && s->kind != S_PASS &&
            s->kind != S_FOR)
            terr(t, s->line, "the block under 'if __name__ == \"__main__\":' holds assignments, print, assert and for loops that print");
        switch (s->kind) {
        case S_CLASS: {
            Str *name = AS_STR(s->val);
            declare(t, name, s->line);
            bool ok = s->n[2] == 1 && s->l[2][0]->kind == E_NAME && !find_sym(t, AS_STR(s->l[2][0]->val)) &&
                      sym_is(prelude_sym(t, AS_STR(s->l[2][0]->val)), "Data") && !s->n[1];
            bool rec = !s->n[2] && s->n[1] == 1 && s->l[1][0]->kind == E_NAME && !find_sym(t, AS_STR(s->l[1][0]->val)) &&
                       sym_is(prelude_sym(t, AS_STR(s->l[1][0]->val)), "dataclass");
            if (rec) { declare_record(t, s); break; }
            if (!ok)
                terr(t, s->line, "a class in a checked file is written 'class %s[...](Data): ...' for a family of types, "
                                 "or with '@dataclass' on the line above 'class %s:' for a class with fields", name->s, name->s);
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
            bool plain = s->n[1] == 1 && s->l[1][0]->kind == E_NAME && !find_sym(t, AS_STR(s->l[1][0]->val));
            if (plain && sym_is(prelude_sym(t, AS_STR(s->l[1][0]->val)), "trusted")) {
                /* The body is ordinary Python that the checker does not read. */
                Sym *f = add_sym(t, name, SK_FUNC, s->params->n, s, s->line);
                f->agda = top_name(name->s);
                f->trusted = true;
                break;
            }
            bool ok = plain && sym_is(prelude_sym(t, AS_STR(s->l[1][0]->val)), "constructor");
            if (!ok) terr(t, s->line, "the decorators on a def in a checked file are @constructor and @trusted");
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

/* print(...) at the top level: each argument is checked. `pre` and `post` wrap it
   in the for loops it is inside. */
static void top_print(T *t, Node *s, const char *pre, const char *post) {
    Node *c = s->a;
    for (int j = 0; j < c->n[0]; j++) {
        Node *a = c->l[0][j];
        if (a->kind == E_STARRED) terr(t, a->line, "* arguments are not available in a checked file");
        if (a->kind == E_KEYWORD) {
            bool ok = a->val && (str_eqc(AS_STR(a->val), "sep") || str_eqc(AS_STR(a->val), "end"));
            if (!ok) terr(t, a->line, "print in a checked file takes sep= and end= only");
            o_put(&t->out, s->line, af("_ = %s(the String %s \xE2\x88\xB7 [])%s\n", pre, E(t, a->a), post));
        } else if (a->kind != E_CONST) o_put(&t->out, s->line, af("_ = %s(%s \xE2\x88\xB7 [])%s\n", pre, E(t, a), post));
    }
}

static bool is_print(T *t, Node *s) {
    Node *c = s->a;
    return s->kind == S_EXPR && c->kind == E_CALL && c->a->kind == E_NAME && str_eqc(AS_STR(c->a->val), "print") &&
           !find_sym(t, AS_STR(c->a->val));
}

/* A for loop at the top level holds prints and further for loops. Each printed
   expression is checked for every item. */
static void top_for(T *t, Node *s, const char *pre, const char *post) {
    if (s->n[1]) terr(t, s->line, "'else' on a loop is not available in a checked file");
    Ty *elem = iter_elem(t, s->b);
    const char *iter = EI(t, s->b);
    Var *saved = t->c.env;
    const char *pat = target_pat(t, s->a, elem);
    pre = af("%s(concatMap (\xCE\xBB { %s \xE2\x86\x92 ", pre, pat);
    post = af(" }) %s)%s", iter, post);
    for (int i = 0; i < s->n[0]; i++) {
        Node *b = s->l[0][i];
        if (skippable(b)) continue;
        if (b->kind == S_FOR) top_for(t, b, pre, post);
        else if (is_print(t, b)) top_print(t, b, pre, post);
        else
            terr(t, b->line, "a for loop at the top level of a checked file holds print statements and other for loops. "
                             "Put other work in a function.");
    }
    t->c.env = saved;
}

static void toplevel(T *t, Node **l, int n) {
    for (int i = 0; i < n; i++) {
        Node *s = l[i];
        t->cur = &t->out;
        switch (s->kind) {
        case S_IMPORTFROM: case S_PASS: break;
        case S_CLASS: {
            Sym *d = find_sym(t, AS_STR(s->val));
            if (d->kind == SK_RECORD) {
                if (!d->body_done) emit_record(t, d);
                for (int j = 0; j < d->nmethods; j++) if (!d->methods[j]->body_done) emit_func_body(t, d->methods[j]);
            } else if (!d->body_done) emit_data_body(t, d);
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
            Ty *want = ann ? ty_ann(t, ann, NULL, NULL) : NULL;
            v->ty = want ? want : ty_of(t, value);
            const char *e = EW(t, value, want);
            if (ty) o_put(&t->out, s->line, af("%s : %s\n", v->agda, ty));
            o_put(&t->out, s->line, af("%s = %s\n", v->agda, e));
            v->body_done = true;
            break;
        }
        case S_EXPR:
            if (s->a->kind == E_CONST) break;
            if (!is_print(t, s)) terr(t, s->line, "the only expression statement at the top level of a checked file is a call to print");
            begin_executed(t, s);
            top_print(t, s, "", "");
            break;
        case S_FOR:
            begin_executed(t, s);
            top_for(t, s, "", "");
            break;
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
static int run_agda(const char *dir, const char *file, const char *commands, char **output) {
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
        if (commands) {
            /* interaction mode: the commands are read from a file */
            if (!freopen(commands, "r", stdin)) _exit(126);
            execlp(agda, agda, "--interaction", "--no-libraries", (char *)NULL);
        } else execlp(agda, agda, "--no-libraries", file, (char *)NULL);
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

/* ---------- holes ---------- */

/* The text of the next Emacs-style string after `key` in Agda's interaction output. */
static const char *lisp_string_after(const char **at, const char *key) {
    const char *p = strstr(*at, key);
    if (!p) return NULL;
    p = strchr(p + strlen(key), '"');
    if (!p) return NULL;
    SB b;
    sb_init(&b);
    for (p++; *p && *p != '"'; p++) {
        if (*p == '\\' && p[1]) { p++; sb_putc(&b, *p == 'n' ? '\n' : *p); }
        else sb_putc(&b, *p);
    }
    sb_putc(&b, 0);
    const char *r = astr(b.s);
    sb_free(&b);
    *at = p;
    return r;
}

/* Agda accepted everything except the holes. Asks it, in interaction mode, what
   each hole needs and which names are in scope there, and reports that. */
__attribute__((noreturn)) static void report_holes(T *t, const char *dir, const char *module, const char *batch) {
    /* The batch report lists the holes by position, in the order Agda numbers them. */
    int lines[64], n = 0;
    size_t ml = strlen(module);
    const char *p = strstr(batch, "Unsolved interaction metas");
    while (p && n < 64 && (p = strstr(p, module))) {
        if (memcmp(p + ml, ".agda:", 6) == 0) {
            int l = atoi(p + ml + 6);
            lines[n++] = l >= 1 && l <= t->out.nl ? t->out.lines[l - 1] : 0;
        }
        p += ml;
    }
    SB cmd;
    sb_init(&cmd);
    sb_printf(&cmd, "IOTCM \"%s.agda\" None Indirect (Cmd_load \"%s.agda\" [\"--no-libraries\"])\n", module, module);
    for (int i = 0; i < n; i++)
        sb_printf(&cmd, "IOTCM \"%s.agda\" None Indirect (Cmd_goal_type_context Simplified %d noRange \"\")\n", module, i);
    sb_putc(&cmd, 0);
    const char *cmd_path = af("%s/%s.cmd", dir, module);
    write_file(cmd_path, cmd.s);
    sb_free(&cmd);
    char *out = NULL;
    run_agda(dir, NULL, af("%s.cmd", module), &out);
    remove(cmd_path);
    SB b;
    sb_init(&b);
    sb_printf(&b, "this file has %d hole%s, so it was not run.", n, n == 1 ? "" : "s");
    const char *at = out ? out : "";
    for (int i = 0; i < n; i++) {
        const char *g = lisp_string_after(&at, "\"*Goal type etc.*\"");
        sb_printf(&b, "\nline %d: hole() needs a value of type\n", lines[i]);
        if (!g) { sb_puts(&b, "  (Agda gave no answer for this hole)"); continue; }
        /* "Goal: T\n-----\nname : type\n..." */
        const char *rule = strstr(g, "\n\xE2\x80\x94");
        const char *goal = strncmp(g, "Goal: ", 6) == 0 ? g + 6 : g;
        size_t gl = rule ? (size_t)(rule - goal) : strlen(goal);
        sb_puts(&b, "  ");
        for (size_t j = 0; j < gl; j++) {
            if (memcmp(goal + j, USCORE, 2) == 0) { sb_putc(&b, '_'); j++; }
            else { sb_putc(&b, goal[j]); if (goal[j] == '\n') sb_puts(&b, "  "); }
        }
        const char *ctx = rule ? strchr(rule + 1, '\n') : NULL;
        if (ctx && ctx[1]) {
            sb_puts(&b, "\nwith these names to build it from:");
            for (const char *q = ctx; *q; q++) {
                if (memcmp(q, USCORE, 2) == 0) { sb_putc(&b, '_'); q++; }
                else { sb_putc(&b, *q); if (*q == '\n') sb_puts(&b, "  "); }
            }
        }
    }
    sb_putc(&b, 0);
    const char *r = astr(b.s);
    sb_free(&b);
    check_fail(t->filename, n ? lines[0] : 0, r);
}

/* ---------- checking a file, and the checked files it imports ---------- */

extern const char *const pylib_names[];
extern const char *const pylib_sources[];

typedef struct { const char *path; T *t; bool busy; } Mod;
static Mod mods[64];
static int nmods;

static T *check_module(Node *m, const char *filename, bool main);

/* Finds, translates and checks the checked file `mod`, once per run. It is looked
   for beside the importing file and then among the built-in modules, the same
   places the interpreter looks when the program runs. */
static T *load_checked(T *t, Str *mod, int line) {
    for (uint32_t i = 0; i < mod->len; i++)
        if (mod->s[i] == '.') terr(t, line, "a checked file imports other checked files by a plain name, with no dots");
    const char *slash = strrchr(t->filename, '/');
    const char *path = slash ? af("%.*s/%s.py", (int)(slash - t->filename), t->filename, mod->s) : af("%s.py", mod->s);
    const char *src = read_file(path);
    if (!src)
        for (int i = 0; pylib_names[i]; i++)
            if (str_eqc(mod, pylib_names[i])) { src = pylib_sources[i]; path = af("<builtin %s>", pylib_names[i]); }
    if (!src) terr(t, line, "cannot find '%s.py' beside this file", mod->s);
    for (int i = 0; i < nmods; i++)
        if (strcmp(mods[i].path, path) == 0) {
            if (mods[i].busy) terr(t, line, "'%s' and this file import each other, which is not available in checked files", mod->s);
            return mods[i].t;
        }
    if (nmods == 64) terr(t, line, "too many checked files in one program");
    int at = nmods++;
    mods[at].path = path; mods[at].busy = true; mods[at].t = NULL;
    Node *m = parse_source(src, path, MODE_FILE);
    if (!agda_wanted(m))
        terr(t, line, "'%s' is not a checked file: it does not import dependent. A checked file can import only checked "
                      "files. To call other Python, write a def marked @trusted that makes the call.", mod->s);
    mods[at].t = check_module(m, path, false);
    mods[at].busy = false;
    return mods[at].t;
}

/* from other import a, b as c */
static void load_imports(T *t) {
    t->imports = "";
    for (int k = 0; k < t->nimport_stmts; k++) {
        Node *s = t->import_stmts[k];
        Str *mod = AS_STR(s->val);
        if (s->op) terr(t, s->line, "'from %s import *' is not available in a checked file; name what is imported", mod->s);
        T *sub = load_checked(t, mod, s->line);
        if (sub->unsafe) t->unsafe = true;
        const char *using = NULL;
        for (int j = 0; j < s->n[0]; j++) {
            Node *a = s->l[0][j];
            Str *nm = AS_STR(a->val), *as = a->val2 ? AS_STR(a->val2) : nm;
            Sym *src = find_sym(sub, nm);
            if (src && src->external)
                terr(t, s->line, "'%s' is not defined in '%s'; import it from the file that defines it", nm->s, mod->s);
            if (!src || src->owner) terr(t, s->line, "'%s' has no top-level name '%s'", mod->s, nm->s);
            if (find_sym(t, as)) terr(t, s->line, "'%s' is imported twice", as->s);
            for (int i = 0; i < NPRELUDE; i++)
                if (str_eqc(as, prelude_names[i].py) && imported(t, prelude_names[i].py))
                    terr(t, s->line, "'%s' is already imported from dependent", as->s);
            sym_tys(sub, src);
            Sym *c = add_sym(t, as, src->kind, src->arity, src->node, 0);
            Str *py = c->py;
            *c = *src;
            c->py = py; c->external = true; c->line = 0; c->rt_ref = false;
            const char *names = c->agda;
            if (c->kind == SK_RECORD) {
                names = af("%s; module %s; %s", c->agda, c->agda, c->ctor);
                if (c->has_eq) names = af("%s; Eq-%s", names, c->agda);
                for (int i = 0; i < c->nmethods; i++) {
                    sym_tys(sub, c->methods[i]);
                    c->methods[i]->external = true;
                    names = af("%s; %s", names, c->methods[i]->agda);
                }
            }
            using = using ? af("%s; %s", using, names) : names;
        }
        t->imports = af("%sopen import %s using (%s)\n", t->imports, sub->module, using ? using : "");
        t->nimport_lines++;
    }
}

/* Translates a parsed module and has Agda check it. Returns when Agda accepts
   it; otherwise raises TypeCheckError through compile_jmp. */
static T *check_module(Node *m, const char *filename, bool main) {
    T *t = arena_alloc(sizeof(T));
    t->filename = filename;
    read_imports(t, m->l[0], m->n[0]);
    load_imports(t);
    prepass(t, m->l[0], m->n[0], false);
    if (!t->future_ann)
        terr(t, 1, "a checked file must start with: from __future__ import annotations");
    toplevel(t, m->l[0], m->n[0]);

    /* The module is named after a hash of its text, so the name is also the cache key.
       --safe rules out postulates, so it is left off when the file, or a file it
       imports, has a @trusted def. */
    const char *body = t->out.s ? t->out.s : "";
    const char *options = t->unsafe ? "--erasure" : "--erasure --safe";
    uint64_t h = fnv(fnv(fnv(fnv(1469598103934665603ULL, agda_prelude), options), t->imports), body);
    const char *module = af("C%016llx", (unsigned long long)h);
    t->module = module;
    const char *header = af("{-# OPTIONS %s #-}\nmodule %s where\nopen import PycPrelude\n%s", options, module, t->imports);
    const char *text = af("%s%s", header, body);
    /* The header lines come before the first generated line. */
    int hl = 3 + t->nimport_lines;
    int *lines = arena_alloc((size_t)(t->out.nl + hl) * sizeof(int));
    if (t->out.nl) memcpy(lines + hl, t->out.lines, (size_t)t->out.nl * sizeof(int));
    t->out.lines = lines;
    t->out.nl += hl;

    if (agda_mode == 1) {
        if (!main) return t;
        fputs(text, stdout);
        fflush(stdout);
        exit(0);
    }

    const char *dir = cache_dir();
    const char *prelude_path = af("%s/PycPrelude.agda", dir), *path = af("%s/%s.agda", dir, module);
    const char *ok_path = af("%s/%s.ok", dir, module);
    char *have = read_file(prelude_path);
    if ((!have || strcmp(have, agda_prelude) != 0) && !write_file(prelude_path, agda_prelude))
        terr(t, 0, "cannot write the type checker's files in %s", dir);
    /* A file that passed before, with the same text and prelude, is not checked again. */
    char *old = read_file(path), *mark = read_file(ok_path);
    if (old && mark && strcmp(old, text) == 0) return t;
    remove(ok_path);
    if (!write_file(path, text)) terr(t, 0, "cannot write the type checker's files in %s", dir);

    char *output = NULL;
    int status = run_agda(dir, af("%s.agda", module), NULL, &output);
    if (status < 0) remove(path);
    if (status < 0)
        terr(t, 0, "this file imports dependent, which asks for type checking, and the agda program could not be "
                   "started. Install Agda, or set PYC_AGDA to its path. The file was not run.");
    if (status == 0 && !t->nholes) {
        /* The .agdai file stays: a file that imports this one is checked against it. */
        write_file(ok_path, "ok\n");
        return t;
    }
    /* With holes and nothing else wrong, Agda reports the holes and nothing more. */
    const char *e1 = strstr(output, "error: ["), *e2 = e1 ? strstr(e1 + 1, "error: [") : NULL;
    bool only_holes = t->nholes && (!e1 || (!e2 && strncmp(e1, "error: [UnsolvedInteractionMetas]", 33) == 0));
    if (only_holes) report_holes(t, dir, module, output);
    int first = 0;
    const char *report = map_report(t, output, module, &first);
    remove(path);
    if (!*report) report = af("agda failed with exit status %d and no message", status);
    for (int i = 0; i < t->nauto; i++)
        if (t->auto_lines[i] == first && strstr(report, "when checking that the expression refl has type")) {
            report = af("%s\nxs[i] needs i < len(xs), d[k] needs k in d, and // and %% need a divisor that is not 0. "
                        "The checker cannot see that the one on this line holds. Put the code under an 'if' that tests it.", report);
            break;
        }
    check_fail(filename, first, report);
}

void agda_check(Node *m, const char *filename) {
    /* The file itself is in the table, so a file that imports it back is noticed. */
    nmods = 1;
    mods[0].path = filename; mods[0].busy = true; mods[0].t = NULL;
    check_module(m, filename, true);
}
