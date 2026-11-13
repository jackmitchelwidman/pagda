/* Lexer and recursive-descent parser. Produces the syntax tree in ast.h. */
#include "ast.h"
#include <ctype.h>

jmp_buf compile_jmp;
bool compile_incomplete;

/* ---------- arena ---------- */
typedef struct ABlock { struct ABlock *next; size_t used, cap; char data[]; } ABlock;
static ABlock *arena;

void *arena_alloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (!arena || arena->used + n > arena->cap) {
        size_t cap = n > 65536 ? n : 65536;
        ABlock *b = malloc(sizeof(ABlock) + cap);
        if (!b) fatal("out of memory");
        b->next = arena; b->used = 0; b->cap = cap;
        arena = b;
    }
    void *p = arena->data + arena->used;
    arena->used += n;
    memset(p, 0, n);
    return p;
}

void arena_free_all(void) {
    while (arena) { ABlock *n = arena->next; free(arena); arena = n; }
}

void syntax_error(const char *filename, int line, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    bool indent = strstr(buf, "indent") != NULL;
    Exc *e = (Exc *)exc_new(indent ? E_IndentationError : E_SyntaxError, (Tuple *)tuple_pack(1, str_c(buf)));
    e->dict = (Dict *)dict_new();
    dict_setc(e->dict, "lineno", int_new(line));
    dict_setc(e->dict, "filename", str_c(filename));
    dict_setc(e->dict, "msg", str_c(buf));
    vm.exc = (Obj *)e;
    longjmp(compile_jmp, 1);
}

/* ---------- tokens ---------- */
enum { TK_EOF, TK_NEWLINE, TK_INDENT, TK_DEDENT, TK_NAME, TK_KW, TK_NUMBER, TK_STRING, TK_FSTRING, TK_OP };
enum { KW_False = 1, KW_None, KW_True, KW_and, KW_as, KW_assert, KW_async, KW_await, KW_break, KW_class,
       KW_continue, KW_def, KW_del, KW_elif, KW_else, KW_except, KW_finally, KW_for, KW_from, KW_global,
       KW_if, KW_import, KW_in, KW_is, KW_lambda, KW_nonlocal, KW_not, KW_or, KW_pass, KW_raise,
       KW_return, KW_try, KW_while, KW_with, KW_yield };
static const char *kw_names[] = {"", "False", "None", "True", "and", "as", "assert", "async", "await", "break",
    "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if",
    "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while",
    "with", "yield", NULL};
enum { TOP_POWEQ = 256, TOP_FLOOREQ, TOP_RSHEQ, TOP_LSHEQ, TOP_ELLIPSIS, TOP_NE, TOP_EQ, TOP_LE, TOP_GE,
       TOP_ARROW, TOP_WALRUS, TOP_PLUSEQ, TOP_MINUSEQ, TOP_STAREQ, TOP_SLASHEQ, TOP_PCTEQ, TOP_ANDEQ,
       TOP_OREQ, TOP_XOREQ, TOP_ATEQ, TOP_POW, TOP_FLOOR, TOP_LSH, TOP_RSH };
static const char *multi_ops[] = {"**=", "//=", ">>=", "<<=", "...", "!=", "==", "<=", ">=", "->", ":=", "+=",
    "-=", "*=", "/=", "%=", "&=", "|=", "^=", "@=", "**", "//", "<<", ">>", NULL};

typedef struct { int type, op, kw, line; const char *start; int len; Obj *val; bool raw; } Token;
typedef struct { const char *src, *filename; Token *toks; int ntok, cap, pos, depth, line0; Str *cls; } P;
typedef struct { Node **v; int n, cap; } Vec;

static void vpush(Vec *v, Node *x) {
    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 4;
        Node **nv = arena_alloc((size_t)cap * sizeof *nv);
        if (v->n) memcpy(nv, v->v, (size_t)v->n * sizeof *nv);
        v->v = nv; v->cap = cap;
    }
    v->v[v->n++] = x;
}

static void push_tok(P *p, Token t) {
    if (p->ntok == p->cap) {
        int cap = p->cap ? p->cap * 2 : 256;
        Token *nt = arena_alloc((size_t)cap * sizeof *nt);
        if (p->ntok) memcpy(nt, p->toks, (size_t)p->ntok * sizeof *nt);
        p->toks = nt; p->cap = cap;
    }
    p->toks[p->ntok++] = t;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* *ps points just past a backslash. */
static void decode_escape(P *p, const char **ps, SB *b, int line) {
    const char *s = *ps;
    char c = *s++;
    int n = 0;
    switch (c) {
    case '\n': break;
    case '\\': case '\'': case '"': sb_putc(b, c); break;
    case 'n': sb_putc(b, '\n'); break;
    case 't': sb_putc(b, '\t'); break;
    case 'r': sb_putc(b, '\r'); break;
    case 'a': sb_putc(b, '\a'); break;
    case 'b': sb_putc(b, '\b'); break;
    case 'f': sb_putc(b, '\f'); break;
    case 'v': sb_putc(b, '\v'); break;
    case 'x': n = 2; break;
    case 'u': n = 4; break;
    case 'U': n = 8; break;
    case 'N': sb_free(b); syntax_error(p->filename, line, "\\N{...} escapes are not supported");
    default:
        if (c >= '0' && c <= '7') {
            uint32_t v = (uint32_t)(c - '0');
            for (int i = 0; i < 2 && *s >= '0' && *s <= '7'; i++) v = v * 8 + (uint32_t)(*s++ - '0');
            sb_putcp(b, v);
        } else { sb_putc(b, '\\'); s--; }
    }
    if (n) {
        uint32_t v = 0;
        for (int i = 0; i < n; i++) {
            int h = hexval(*s);
            if (h < 0) { sb_free(b); syntax_error(p->filename, line, "invalid \\%c escape in string literal", c); }
            v = v * 16 + (uint32_t)h;
            s++;
        }
        sb_putcp(b, v);
    }
    *ps = s;
}

static void unterminated(P *p, int line, bool triple) {
    if (triple) compile_incomplete = true;
    syntax_error(p->filename, line, triple ? "unterminated triple-quoted string literal" : "unterminated string literal");
}

/* Skips a quoted string nested in an f-string field. s points at the opening quote. */
static const char *skip_nested_string(const char *s, const char *end, int *line) {
    char q = *s;
    bool t = s + 2 < end && s[1] == q && s[2] == q;
    s += t ? 3 : 1;
    while (s < end && *s && !(*s == q && (!t || (s[1] == q && s[2] == q)))) {
        if (*s == '\\' && s[1]) s++;
        if (*s == '\n' && line) (*line)++;
        s++;
    }
    if (s >= end || !*s) return NULL;
    return s + (t ? 3 : 1);
}

static const char *lex_string(P *p, const char *s, int *line, bool raw, bool fstr, Token *t) {
    char q = *s;
    bool triple = s[1] == q && s[2] == q;
    int line0 = *line;
    s += triple ? 3 : 1;
    t->line = line0;
    t->raw = raw;
    if (fstr) {
        const char *body = s;
        int depth = 0;
        bool spec = false;
        for (;;) {
            char c = *s;
            if (!c) unterminated(p, line0, triple);
            if (c == '\n') { if (!triple) unterminated(p, line0, false); (*line)++; s++; continue; }
            if (c == '\\' && s[1]) { if (s[1] == '\n') (*line)++; s += 2; continue; }
            if (depth == 0) {
                if (c == '{') { if (s[1] == '{') s += 2; else { depth = 1; spec = false; s++; } continue; }
                if (c == q && (!triple || (s[1] == q && s[2] == q))) break;
                s++;
                continue;
            }
            if (c == '{' || c == '[' || c == '(') depth++;
            else if (c == '}' || c == ']' || c == ')') depth--;
            else if (c == ':' && depth == 1) spec = true;
            else if ((c == '\'' || c == '"') && !(spec && depth == 1)) {
                const char *e = skip_nested_string(s, s + strlen(s), line);
                if (!e) unterminated(p, line0, triple);
                s = e;
                continue;
            }
            s++;
        }
        t->type = TK_FSTRING;
        t->start = body;
        t->len = (int)(s - body);
        return s + (triple ? 3 : 1);
    }
    SB b;
    sb_init(&b);
    for (;;) {
        char c = *s;
        if (!c) { sb_free(&b); unterminated(p, line0, triple); }
        if (c == q && (!triple || (s[1] == q && s[2] == q))) break;
        if (c == '\n') {
            if (!triple) { sb_free(&b); unterminated(p, line0, false); }
            (*line)++;
        }
        if (c == '\\') {
            if (raw) {
                sb_putc(&b, '\\');
                s++;
                if (*s) { if (*s == '\n') (*line)++; sb_putc(&b, *s++); }
            } else {
                s++;
                if (*s == '\n') (*line)++;
                if (!*s) { sb_free(&b); unterminated(p, line0, triple); }
                decode_escape(p, &s, &b, *line);
            }
            continue;
        }
        sb_putc(&b, c);
        s++;
    }
    t->type = TK_STRING;
    t->val = sb_finish(&b);
    return s + (triple ? 3 : 1);
}

/* An underscore in a number must sit between two digits (after 0x it may come first). */
static bool underscores_ok(const char *from, const char *to, bool prefixed) {
    for (const char *q = from; q < to; q++) {
        if (*q != '_') continue;
        /* With a 0x, 0o or 0b prefix every character is a digit; otherwise e and . are not. */
        bool before = q > from ? (prefixed ? isalnum((unsigned char)q[-1]) : isdigit((unsigned char)q[-1])) : prefixed;
        bool after = q + 1 < to && (prefixed ? isalnum((unsigned char)q[1]) : isdigit((unsigned char)q[1]));
        if (!before || !after) return false;
    }
    return true;
}

static const char *lex_number(P *p, const char *s, int line, Token *t) {
    SB b;
    sb_init(&b);
    const char *start = s;
    t->type = TK_NUMBER;
    if (s[0] == '0' && s[1] && strchr("xXoObB", s[1])) {
        int base = (s[1] | 32) == 'x' ? 16 : (s[1] | 32) == 'o' ? 8 : 2;
        s += 2;
        while (*s == '_' || (hexval(*s) >= 0 && hexval(*s) < base)) { if (*s != '_') sb_putc(&b, *s); s++; }
        if (b.n == 0 || isalnum((unsigned char)*s) || !underscores_ok(start + 2, s, true)) {
            sb_free(&b);
            syntax_error(p->filename, line, "invalid number literal");
        }
        t->val = int_from_digits(b.s, b.n, base);
        sb_free(&b);
        return s;
    }
    bool isfloat = false;
    while (isdigit((unsigned char)*s) || *s == '_') { if (*s != '_') sb_putc(&b, *s); s++; }
    if (*s == '.') {
        isfloat = true;
        sb_putc(&b, *s++);
        while (isdigit((unsigned char)*s) || *s == '_') { if (*s != '_') sb_putc(&b, *s); s++; }
    }
    if ((*s == 'e' || *s == 'E') && (isdigit((unsigned char)s[1]) || ((s[1] == '+' || s[1] == '-') && isdigit((unsigned char)s[2])))) {
        isfloat = true;
        sb_putc(&b, *s++);
        if (*s == '+' || *s == '-') sb_putc(&b, *s++);
        while (isdigit((unsigned char)*s) || *s == '_') { if (*s != '_') sb_putc(&b, *s); s++; }
    }
    if (*s == 'j' || *s == 'J') { sb_free(&b); syntax_error(p->filename, line, "complex numbers are not supported"); }
    if (isalpha((unsigned char)*s) || *s == '_' || !underscores_ok(start, s, false)) {
        sb_free(&b);
        syntax_error(p->filename, line, "invalid decimal literal");
    }
    if (!isfloat && b.n > 1 && b.s[0] == '0' && strspn(b.s, "0") != b.n) {
        /* b holds the digits only, not yet terminated */
        bool nonzero = false;
        for (size_t i = 0; i < b.n; i++) if (b.s[i] != '0') nonzero = true;
        if (nonzero) {
            sb_free(&b);
            syntax_error(p->filename, line, "leading zeros in decimal integer literals are not permitted; use an 0o prefix for octal integers");
        }
    }
    sb_putc(&b, 0);
    if (isfloat) t->val = float_new(strtod(b.s, NULL));
    else t->val = int_from_digits(b.s, b.n - 1, 10);
    sb_free(&b);
    return s;
}

static void lex(P *p) {
    const char *s = p->src;
    int line = p->line0, paren = 0;
    int indents[128], nind = 1;
    indents[0] = 0;
    bool bol = true;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
    for (;;) {
        Token t = {0};
        if (bol && paren == 0) {
            int col = 0;
            while (*s == ' ' || *s == '\t' || *s == '\f') { col = *s == '\t' ? (col / 8 + 1) * 8 : col + 1; s++; }
            if (*s == '#') while (*s && *s != '\n') s++;
            if (*s == '\r') s++;
            if (*s == '\n') { s++; line++; continue; }
            if (!*s) break;
            t.line = line;
            if (col > indents[nind - 1]) {
                if (nind >= 127) syntax_error(p->filename, line, "too many levels of indentation");
                indents[nind++] = col;
                t.type = TK_INDENT;
                push_tok(p, t);
            } else {
                while (col < indents[nind - 1]) { nind--; t.type = TK_DEDENT; push_tok(p, t); }
                if (col != indents[nind - 1])
                    syntax_error(p->filename, line, "unindent does not match any outer indentation level");
            }
            bol = false;
        }
        while (*s == ' ' || *s == '\t' || *s == '\f' || *s == '\r') s++;
        char c = *s;
        t.line = line;
        t.start = s;
        if (!c) break;
        if (c == '#') { while (*s && *s != '\n') s++; continue; }
        if (c == '\\' && (s[1] == '\n' || (s[1] == '\r' && s[2] == '\n'))) { s += s[1] == '\n' ? 2 : 3; line++; continue; }
        if (c == '\n') {
            s++; line++;
            if (paren == 0) { t.type = TK_NEWLINE; push_tok(p, t); bol = true; }
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80) {
            const char *e = s;
            while (isalnum((unsigned char)*e) || *e == '_' || (unsigned char)*e >= 0x80) e++;
            int len = (int)(e - s);
            if ((*e == '"' || *e == '\'') && len <= 2) {
                bool raw = false, f = false, b = false, ok = true;
                for (int i = 0; i < len; i++) {
                    char pc = (char)(s[i] | 32);
                    if (pc == 'r') raw = true; else if (pc == 'f') f = true; else if (pc == 'b') b = true;
                    else if (pc != 'u') ok = false;
                }
                if (ok) {
                    if (b) syntax_error(p->filename, line, "bytes literals are not supported");
                    s = lex_string(p, e, &line, raw, f, &t);
                    push_tok(p, t);
                    continue;
                }
            }
            t.type = TK_NAME;
            t.len = len;
            for (int i = 1; kw_names[i]; i++)
                if ((int)strlen(kw_names[i]) == len && memcmp(kw_names[i], s, (size_t)len) == 0) { t.type = TK_KW; t.kw = i; break; }
            if (t.type == TK_NAME) t.val = str_new(s, (size_t)len);
            push_tok(p, t);
            s = e;
            continue;
        }
        if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)s[1]))) {
            s = lex_number(p, s, line, &t);
            push_tok(p, t);
            continue;
        }
        if (c == '"' || c == '\'') {
            s = lex_string(p, s, &line, false, false, &t);
            push_tok(p, t);
            continue;
        }
        t.type = TK_OP;
        t.op = 0;
        for (int i = 0; multi_ops[i]; i++) {
            size_t n = strlen(multi_ops[i]);
            if (strncmp(s, multi_ops[i], n) == 0) { t.op = 256 + i; s += n; break; }
        }
        if (!t.op) {
            if (!strchr("+-*/%@&|^~<>()[]{},:.;=!", c) || c == '!')
                syntax_error(p->filename, line, "invalid character '%c'", c);
            t.op = c;
            s++;
        }
        if (t.op == '(' || t.op == '[' || t.op == '{') paren++;
        if ((t.op == ')' || t.op == ']' || t.op == '}') && paren > 0) paren--;
        push_tok(p, t);
    }
    Token t = {0};
    t.line = line;
    if (paren > 0) compile_incomplete = true;
    if (!bol && paren == 0) { t.type = TK_NEWLINE; push_tok(p, t); }
    while (nind > 1) { nind--; t.type = TK_DEDENT; push_tok(p, t); }
    t.type = TK_EOF;
    push_tok(p, t);
}

/* ---------- parser helpers ---------- */
static Token *tk(P *p) { return &p->toks[p->pos]; }
static bool is_op(P *p, int op) { return tk(p)->type == TK_OP && tk(p)->op == op; }
static bool is_kw(P *p, int kw) { return tk(p)->type == TK_KW && tk(p)->kw == kw; }
static bool accept_op(P *p, int op) { if (is_op(p, op)) { p->pos++; return true; } return false; }
static bool accept_kw(P *p, int kw) { if (is_kw(p, kw)) { p->pos++; return true; } return false; }

__attribute__((noreturn)) static void fail(P *p, const char *msg) {
    Token *t = tk(p);
    if (t->type == TK_EOF) compile_incomplete = true;
    syntax_error(p->filename, t->line, "%s", msg);
}
static void expect_op(P *p, int op) {
    if (!accept_op(p, op)) {
        char buf[64];
        if (op < 256) snprintf(buf, sizeof buf, "invalid syntax (expected '%c')", op);
        else snprintf(buf, sizeof buf, "invalid syntax (expected '%s')", multi_ops[op - 256]);
        fail(p, buf);
    }
}
static void expect_kw(P *p, int kw) { if (!accept_kw(p, kw)) fail(p, "invalid syntax"); }
/* Inside a class body, __name becomes _Class__name (private name mangling). */
static Str *mangle(P *p, Str *name) {
    if (!p->cls || name->len < 3 || name->s[0] != '_' || name->s[1] != '_') return name;
    if (name->s[name->len - 1] == '_' && name->s[name->len - 2] == '_') return name;
    if (memchr(name->s, '.', name->len)) return name;
    const char *c = p->cls->s;
    while (*c == '_') c++;
    if (!*c) return name;
    return AS_STR(str_fmt("_%s%s", c, name->s));
}
static Str *expect_name(P *p) {
    if (tk(p)->type != TK_NAME) fail(p, "invalid syntax");
    return mangle(p, AS_STR(p->toks[p->pos++].val));
}
static Node *mk(P *p, int kind, int line) {
    (void)p;
    Node *n = arena_alloc(sizeof(Node));
    n->kind = kind; n->line = line;
    return n;
}
static Node *mk_const(P *p, Obj *v, int line) { Node *n = mk(p, E_CONST, line); n->val = v; return n; }
static void set_list(Node *n, int i, Vec *v) { n->l[i] = v->v; n->n[i] = v->n; }

static Node *p_test(P *p);
static Node *p_namedexpr(P *p);
static Node *p_bitor(P *p);
static Node *p_star_or_test(P *p);
static Node *p_testlist(P *p, bool allow_star);
static Node *p_yield_or_testlist(P *p);
static void p_block(P *p, Vec *out);
static void p_stmt(P *p, Vec *out);
static Node *p_or(P *p);

static void enter(P *p) { if (++p->depth > 400) fail(p, "too many nested parentheses"); }

/* ---------- f-strings ---------- */
static Node *parse_sub_expr(P *p, const char *text, int len, int line) {
    char *buf = arena_alloc((size_t)len + 3);
    buf[0] = '(';
    memcpy(buf + 1, text, (size_t)len);
    buf[len + 1] = ')';
    P sub = {0};
    sub.src = buf; sub.filename = p->filename; sub.line0 = line; sub.depth = p->depth; sub.cls = p->cls;
    lex(&sub);
    expect_op(&sub, '(');
    Node *n = p_yield_or_testlist(&sub);
    expect_op(&sub, ')');
    if (tk(&sub)->type != TK_NEWLINE && tk(&sub)->type != TK_EOF) fail(&sub, "f-string: invalid syntax");
    return n;
}

static void fstring_flush(P *p, SB *lit, Vec *parts, int line) {
    if (lit->n == 0) return;
    vpush(parts, mk_const(p, sb_finish(lit), line));
}

static void parse_fstring_body(P *p, const char *s, const char *end, bool raw, int line, Vec *parts) {
    SB lit;
    sb_init(&lit);
    while (s < end) {
        char c = *s;
        if (c == '{') {
            if (s + 1 < end && s[1] == '{') { sb_putc(&lit, '{'); s += 2; continue; }
            fstring_flush(p, &lit, parts, line);
            s++;
            const char *e = s;
            int depth = 0;
            bool debug = false;
            for (;;) {
                if (e >= end) syntax_error(p->filename, line, "f-string: expecting '}'");
                char ch = *e;
                if (ch == '\'' || ch == '"') {
                    e = skip_nested_string(e, end, NULL);
                    if (!e) syntax_error(p->filename, line, "f-string: unterminated string");
                    continue;
                }
                if (ch == '(' || ch == '[' || ch == '{') depth++;
                else if (ch == ')' || ch == ']' || ch == '}') {
                    if (depth == 0) { if (ch == '}') break; syntax_error(p->filename, line, "f-string: unmatched '%c'", ch); }
                    depth--;
                } else if (depth == 0) {
                    if (ch == '!' && e[1] != '=') break;
                    if (ch == ':') break;
                    if (ch == '=' && e > s && !strchr("=!<>", e[-1]) && e[1] != '=') {
                        const char *f = e + 1;
                        while (f < end && *f == ' ') f++;
                        if (f < end && (*f == '}' || *f == '!' || *f == ':')) { debug = true; break; }
                    }
                }
                e++;
            }
            const char *expr_end = e;
            if (debug) {
                e++;
                while (*e == ' ') e++;
                vpush(parts, mk_const(p, str_new(s, (size_t)(e - s)), line));
            }
            bool blank = true;
            for (const char *q = s; q < expr_end; q++) if (!isspace((unsigned char)*q)) blank = false;
            if (blank) syntax_error(p->filename, line, "f-string: valid expression required before '}'");
            Node *fv = mk(p, E_FORMATTED, line);
            fv->a = parse_sub_expr(p, s, (int)(expr_end - s), line);
            if (*e == '!') {
                fv->op = e[1];
                if (!fv->op || !strchr("rsa", fv->op)) syntax_error(p->filename, line, "f-string: invalid conversion character");
                e += 2;
            }
            if (e < end && *e == ':') {
                e++;
                const char *sp = e;
                int d = 0;
                while (e < end && !(d == 0 && *e == '}')) { if (*e == '{') d++; else if (*e == '}') d--; e++; }
                Vec sparts = {0};
                parse_fstring_body(p, sp, e, raw, line, &sparts);
                Node *spec = mk(p, E_FSTRING, line);
                set_list(spec, 0, &sparts);
                fv->b = spec;
            } else if (debug && !fv->op) fv->op = 'r';
            if (e >= end || *e != '}') syntax_error(p->filename, line, "f-string: expecting '}'");
            vpush(parts, fv);
            s = e + 1;
            continue;
        }
        if (c == '}') {
            if (s + 1 < end && s[1] == '}') { sb_putc(&lit, '}'); s += 2; continue; }
            sb_free(&lit);
            syntax_error(p->filename, line, "f-string: single '}' is not allowed");
        }
        if (c == '\\' && !raw) { s++; decode_escape(p, &s, &lit, line); continue; }
        sb_putc(&lit, c);
        s++;
    }
    fstring_flush(p, &lit, parts, line);
}

static Node *p_strings(P *p) {
    int line = tk(p)->line;
    Vec parts = {0};
    bool any_f = false;
    while (tk(p)->type == TK_STRING || tk(p)->type == TK_FSTRING) {
        Token *t = &p->toks[p->pos++];
        if (t->type == TK_STRING) {
            if (parts.n && parts.v[parts.n - 1]->kind == E_CONST) {
                Node *prev = parts.v[parts.n - 1];
                prev->val = str_concat(AS_STR(prev->val), AS_STR(t->val));
            } else vpush(&parts, mk_const(p, t->val, t->line));
        } else {
            any_f = true;
            Vec sub = {0};
            parse_fstring_body(p, t->start, t->start + t->len, t->raw, t->line, &sub);
            for (int i = 0; i < sub.n; i++) {
                if (sub.v[i]->kind == E_CONST && parts.n && parts.v[parts.n - 1]->kind == E_CONST) {
                    Node *prev = parts.v[parts.n - 1];
                    prev->val = str_concat(AS_STR(prev->val), AS_STR(sub.v[i]->val));
                } else vpush(&parts, sub.v[i]);
            }
        }
    }
    if (!any_f) return parts.v[0];
    if (parts.n == 0) return mk_const(p, str_new("", 0), line);
    if (parts.n == 1 && parts.v[0]->kind == E_CONST) return parts.v[0];
    Node *n = mk(p, E_FSTRING, line);
    set_list(n, 0, &parts);
    return n;
}

/* ---------- expressions ---------- */
static void p_comp_clauses(P *p, Vec *gens) {
    while (is_kw(p, KW_for) || is_kw(p, KW_async)) {
        if (is_kw(p, KW_async)) fail(p, "async comprehensions are not supported");
        int line = tk(p)->line;
        p->pos++;
        Node *g = mk(p, N_COMP, line);
        Vec targets = {0};
        bool comma = false;
        for (;;) {
            Node *t;
            if (is_op(p, '*')) { int l = tk(p)->line; p->pos++; t = mk(p, E_STARRED, l); t->a = p_bitor(p); }
            else t = p_bitor(p);
            vpush(&targets, t);
            if (!accept_op(p, ',')) break;
            comma = true;
            if (is_kw(p, KW_in)) break;
        }
        if (comma || targets.n > 1) { g->a = mk(p, E_TUPLE, line); set_list(g->a, 0, &targets); }
        else g->a = targets.v[0];
        expect_kw(p, KW_in);
        g->b = p_or(p);
        Vec ifs = {0};
        while (accept_kw(p, KW_if)) vpush(&ifs, p_or(p));
        set_list(g, 0, &ifs);
        vpush(gens, g);
    }
}

static Node *p_slice_item(P *p) {
    int line = tk(p)->line;
    Node *first = NULL;
    if (!is_op(p, ':')) {
        first = p_star_or_test(p);
        if (!is_op(p, ':')) return first;
    }
    expect_op(p, ':');
    Node *n = mk(p, E_SLICE, line);
    n->a = first;
    if (!is_op(p, ':') && !is_op(p, ']') && !is_op(p, ',')) n->b = p_test(p);
    if (accept_op(p, ':'))
        if (!is_op(p, ']') && !is_op(p, ',')) n->c = p_test(p);
    return n;
}

static void p_call_args(P *p, Vec *args) {
    bool seen_kw = false;
    while (!is_op(p, ')')) {
        int line = tk(p)->line;
        Node *a;
        if (accept_op(p, '*')) { a = mk(p, E_STARRED, line); a->a = p_test(p); }
        else if (accept_op(p, TOP_POW)) { a = mk(p, E_KEYWORD, line); a->a = p_test(p); seen_kw = true; }
        else if (tk(p)->type == TK_NAME && p->toks[p->pos + 1].type == TK_OP && p->toks[p->pos + 1].op == '=') {
            a = mk(p, E_KEYWORD, line);
            a->val = (Obj *)expect_name(p);
            p->pos++;
            a->a = p_test(p);
            seen_kw = true;
        } else {
            a = p_namedexpr(p);
            if (seen_kw) syntax_error(p->filename, line, "positional argument follows keyword argument");
            if (is_kw(p, KW_for)) {
                Node *g = mk(p, E_GENEXP, line);
                g->a = a;
                Vec gens = {0};
                p_comp_clauses(p, &gens);
                set_list(g, 0, &gens);
                a = g;
            }
        }
        vpush(args, a);
        if (!accept_op(p, ',')) break;
    }
    expect_op(p, ')');
}

static Node *p_atom(P *p) {
    Token *t = tk(p);
    int line = t->line;
    enter(p);
    Node *n = NULL;
    switch (t->type) {
    case TK_NAME: p->pos++; n = mk(p, E_NAME, line); n->val = (Obj *)mangle(p, AS_STR(t->val)); break;
    case TK_NUMBER: p->pos++; n = mk_const(p, t->val, line); break;
    case TK_STRING: case TK_FSTRING: n = p_strings(p); break;
    case TK_KW:
        p->pos++;
        if (t->kw == KW_None) n = mk_const(p, Py_None, line);
        else if (t->kw == KW_True) n = mk_const(p, Py_True, line);
        else if (t->kw == KW_False) n = mk_const(p, Py_False, line);
        else if (t->kw == KW_await) syntax_error(p->filename, line, "async and await are not supported");
        else { p->pos--; fail(p, "invalid syntax"); }
        break;
    case TK_OP:
        p->pos++;
        if (t->op == TOP_ELLIPSIS) { n = mk_const(p, Py_Ellipsis, line); break; }
        if (t->op == '(') {
            if (accept_op(p, ')')) { n = mk(p, E_TUPLE, line); break; }
            if (is_kw(p, KW_yield)) { n = p_yield_or_testlist(p); expect_op(p, ')'); break; }
            Node *first = is_op(p, '*') ? p_star_or_test(p) : p_namedexpr(p);
            if (is_kw(p, KW_for)) {
                n = mk(p, E_GENEXP, line);
                n->a = first;
                Vec gens = {0};
                p_comp_clauses(p, &gens);
                set_list(n, 0, &gens);
                expect_op(p, ')');
                break;
            }
            if (is_op(p, ',')) {
                Vec items = {0};
                vpush(&items, first);
                while (accept_op(p, ',')) {
                    if (is_op(p, ')')) break;
                    vpush(&items, is_op(p, '*') ? p_star_or_test(p) : p_namedexpr(p));
                }
                n = mk(p, E_TUPLE, line);
                set_list(n, 0, &items);
            } else {
                if (first->kind == E_STARRED) syntax_error(p->filename, line, "cannot use starred expression here");
                n = first;
                if (n->kind == E_TUPLE || n->kind == E_NAME) { /* (x) stays x */ }
            }
            expect_op(p, ')');
            break;
        }
        if (t->op == '[') {
            n = mk(p, E_LIST, line);
            if (accept_op(p, ']')) break;
            Node *first = is_op(p, '*') ? p_star_or_test(p) : p_namedexpr(p);
            if (is_kw(p, KW_for)) {
                n->kind = E_LISTCOMP;
                n->a = first;
                Vec gens = {0};
                p_comp_clauses(p, &gens);
                set_list(n, 0, &gens);
                expect_op(p, ']');
                break;
            }
            Vec items = {0};
            vpush(&items, first);
            while (accept_op(p, ',')) {
                if (is_op(p, ']')) break;
                vpush(&items, is_op(p, '*') ? p_star_or_test(p) : p_namedexpr(p));
            }
            expect_op(p, ']');
            set_list(n, 0, &items);
            break;
        }
        if (t->op == '{') {
            if (accept_op(p, '}')) { n = mk(p, E_DICT, line); break; }
            Vec keys = {0}, vals = {0};
            if (is_op(p, TOP_POW)) {
                n = mk(p, E_DICT, line);
            } else {
                Node *first = is_op(p, '*') ? p_star_or_test(p) : p_namedexpr(p);
                if (is_op(p, ':')) {
                    p->pos++;
                    Node *v = p_test(p);
                    if (is_kw(p, KW_for)) {
                        n = mk(p, E_DICTCOMP, line);
                        n->a = first; n->b = v;
                        Vec gens = {0};
                        p_comp_clauses(p, &gens);
                        set_list(n, 0, &gens);
                        expect_op(p, '}');
                        break;
                    }
                    n = mk(p, E_DICT, line);
                    vpush(&keys, first); vpush(&vals, v);
                    if (!accept_op(p, ',')) { expect_op(p, '}'); set_list(n, 0, &keys); set_list(n, 1, &vals); break; }
                } else {
                    if (is_kw(p, KW_for)) {
                        n = mk(p, E_SETCOMP, line);
                        n->a = first;
                        Vec gens = {0};
                        p_comp_clauses(p, &gens);
                        set_list(n, 0, &gens);
                        expect_op(p, '}');
                        break;
                    }
                    n = mk(p, E_SET, line);
                    vpush(&keys, first);
                    while (accept_op(p, ',')) {
                        if (is_op(p, '}')) break;
                        vpush(&keys, is_op(p, '*') ? p_star_or_test(p) : p_namedexpr(p));
                    }
                    expect_op(p, '}');
                    set_list(n, 0, &keys);
                    break;
                }
            }
            /* rest of a dict display */
            while (!is_op(p, '}')) {
                if (accept_op(p, TOP_POW)) { vpush(&keys, NULL); vpush(&vals, p_bitor(p)); }
                else { Node *k = p_test(p); expect_op(p, ':'); vpush(&keys, k); vpush(&vals, p_test(p)); }
                if (!accept_op(p, ',')) break;
            }
            expect_op(p, '}');
            set_list(n, 0, &keys); set_list(n, 1, &vals);
            break;
        }
        p->pos--;
        fail(p, "invalid syntax");
    default:
        fail(p, "invalid syntax");
    }
    p->depth--;
    return n;
}

static Node *p_primary(P *p) {
    Node *n = p_atom(p);
    for (;;) {
        int line = tk(p)->line;
        if (accept_op(p, '(')) {
            Node *c = mk(p, E_CALL, line);
            c->a = n;
            Vec args = {0};
            p_call_args(p, &args);
            set_list(c, 0, &args);
            n = c;
        } else if (accept_op(p, '[')) {
            Node *s = mk(p, E_SUBSCR, line);
            s->a = n;
            Node *first = p_slice_item(p);
            if (is_op(p, ',')) {
                Vec items = {0};
                vpush(&items, first);
                while (accept_op(p, ',')) { if (is_op(p, ']')) break; vpush(&items, p_slice_item(p)); }
                Node *tu = mk(p, E_TUPLE, line);
                set_list(tu, 0, &items);
                first = tu;
            }
            expect_op(p, ']');
            s->b = first;
            n = s;
        } else if (accept_op(p, '.')) {
            Node *a = mk(p, E_ATTR, line);
            a->a = n;
            a->val = (Obj *)expect_name(p);
            n = a;
        } else break;
    }
    return n;
}

static Node *p_factor(P *p);
static Node *p_power(P *p) {
    if (is_kw(p, KW_await)) fail(p, "async and await are not supported");
    Node *n = p_primary(p);
    if (is_op(p, TOP_POW)) {
        int line = tk(p)->line;
        p->pos++;
        Node *b = mk(p, E_BINOP, line);
        b->op = OP_POW; b->a = n; b->b = p_factor(p);
        return b;
    }
    return n;
}

static Node *p_factor(P *p) {
    Token *t = tk(p);
    if (t->type == TK_OP && (t->op == '-' || t->op == '+' || t->op == '~')) {
        enter(p);
        p->pos++;
        Node *operand = p_factor(p);
        p->depth--;
        if (t->op == '-' && operand->kind == E_CONST && (KIND(operand->val) == K_INT || KIND(operand->val) == K_FLOAT)) {
            operand->val = KIND(operand->val) == K_INT ? int_neg(operand->val) : float_new(-((Float *)operand->val)->v);
            return operand;
        }
        Node *n = mk(p, E_UNARY, t->line);
        n->op = t->op; n->a = operand;
        return n;
    }
    return p_power(p);
}

#define BINARY_LEVEL(name, next, cond, opexpr)                         \
    static Node *name(P *p) {                                          \
        Node *n = next(p);                                             \
        for (;;) {                                                     \
            Token *t = tk(p);                                          \
            if (t->type != TK_OP || !(cond)) return n;                 \
            p->pos++;                                                  \
            Node *b = mk(p, E_BINOP, t->line);                         \
            b->op = (opexpr); b->a = n; b->b = next(p);                \
            n = b;                                                     \
        }                                                              \
    }
BINARY_LEVEL(p_term, p_factor, t->op == '*' || t->op == '/' || t->op == TOP_FLOOR || t->op == '%' || t->op == '@',
             t->op == '*' ? OP_MUL : t->op == '/' ? OP_TRUEDIV : t->op == TOP_FLOOR ? OP_FLOORDIV : t->op == '%' ? OP_MOD : OP_MATMUL)
BINARY_LEVEL(p_arith, p_term, t->op == '+' || t->op == '-', t->op == '+' ? OP_ADD : OP_SUB)
BINARY_LEVEL(p_shift, p_arith, t->op == TOP_LSH || t->op == TOP_RSH, t->op == TOP_LSH ? OP_LSHIFT : OP_RSHIFT)
BINARY_LEVEL(p_bitand, p_shift, t->op == '&', OP_AND)
BINARY_LEVEL(p_bitxor, p_bitand, t->op == '^', OP_XOR)
BINARY_LEVEL(p_bitor, p_bitxor, t->op == '|', OP_OR)

static int cmp_op_at(P *p, int *consumed) {
    Token *t = tk(p);
    *consumed = 1;
    if (t->type == TK_OP) {
        switch (t->op) {
        case '<': return CMP_LT;
        case '>': return CMP_GT;
        case TOP_EQ: return CMP_EQ;
        case TOP_NE: return CMP_NE;
        case TOP_LE: return CMP_LE;
        case TOP_GE: return CMP_GE;
        }
    } else if (t->type == TK_KW) {
        Token *t2 = &p->toks[p->pos + 1];
        if (t->kw == KW_in) return CMP_IN;
        if (t->kw == KW_not && t2->type == TK_KW && t2->kw == KW_in) { *consumed = 2; return CMP_NOT_IN; }
        if (t->kw == KW_is) {
            if (t2->type == TK_KW && t2->kw == KW_not) { *consumed = 2; return CMP_IS_NOT; }
            return CMP_IS;
        }
    }
    return -1;
}

static Node *p_comparison(P *p) {
    Node *left = p_bitor(p);
    int consumed, op = cmp_op_at(p, &consumed);
    if (op < 0) return left;
    Node *n = mk(p, E_COMPARE, tk(p)->line);
    n->a = left;
    Vec rights = {0};
    int ops[64], nops = 0;
    while (op >= 0) {
        if (nops >= 64) fail(p, "comparison chain too long");
        p->pos += consumed;
        ops[nops++] = op;
        vpush(&rights, p_bitor(p));
        op = cmp_op_at(p, &consumed);
    }
    set_list(n, 0, &rights);
    n->ops = arena_alloc((size_t)nops * sizeof(int));
    memcpy(n->ops, ops, (size_t)nops * sizeof(int));
    return n;
}

static Node *p_not(P *p) {
    if (is_kw(p, KW_not)) {
        int line = tk(p)->line;
        enter(p);
        p->pos++;
        Node *n = mk(p, E_NOT, line);
        n->a = p_not(p);
        p->depth--;
        return n;
    }
    return p_comparison(p);
}

static Node *p_and(P *p) {
    Node *n = p_not(p);
    if (!is_kw(p, KW_and)) return n;
    Node *b = mk(p, E_BOOLOP, tk(p)->line);
    Vec v = {0};
    vpush(&v, n);
    while (accept_kw(p, KW_and)) vpush(&v, p_not(p));
    set_list(b, 0, &v);
    b->op = 0;
    return b;
}

static Node *p_or(P *p) {
    Node *n = p_and(p);
    if (!is_kw(p, KW_or)) return n;
    Node *b = mk(p, E_BOOLOP, tk(p)->line);
    Vec v = {0};
    vpush(&v, n);
    while (accept_kw(p, KW_or)) vpush(&v, p_and(p));
    set_list(b, 0, &v);
    b->op = 1;
    return b;
}

static Params *p_params(P *p, bool lambda) {
    Params *pr = arena_alloc(sizeof(Params));
    int end_op = lambda ? ':' : ')';
    Str *names[256], *kwonly[256];
    Node *defs[256], *kwdefs[256], *anns[256], *kwanns[256];
    bool star = false, seen_default = false;
    while (!is_op(p, end_op)) {
        if (pr->n >= 255 || pr->nkwonly >= 255) fail(p, "too many parameters");
        if (accept_op(p, '/')) {
            if (star || pr->nposonly) fail(p, "invalid syntax");
            pr->nposonly = pr->n;
        } else if (accept_op(p, '*')) {
            if (star) fail(p, "invalid syntax");
            star = true;
            if (tk(p)->type == TK_NAME) {
                pr->vararg = expect_name(p);
                if (!lambda && accept_op(p, ':')) pr->vararg_ann = p_test(p);
            }
        } else if (accept_op(p, TOP_POW)) {
            pr->kwarg = expect_name(p);
            if (!lambda && accept_op(p, ':')) pr->kwarg_ann = p_test(p);
        } else {
            Str *name = expect_name(p);
            Node *ann = !lambda && accept_op(p, ':') ? p_test(p) : NULL;
            Node *def = accept_op(p, '=') ? p_test(p) : NULL;
            if (star) { kwonly[pr->nkwonly] = name; kwanns[pr->nkwonly] = ann; kwdefs[pr->nkwonly++] = def; }
            else {
                if (!def && seen_default) fail(p, "parameter without a default follows parameter with a default");
                if (def) seen_default = true;
                names[pr->n] = name; anns[pr->n] = ann; defs[pr->n++] = def;
            }
        }
        if (!accept_op(p, ',')) break;
    }
    pr->names = arena_alloc((size_t)(pr->n + 1) * sizeof(Str *));
    pr->defaults = arena_alloc((size_t)(pr->n + 1) * sizeof(Node *));
    pr->kwonly = arena_alloc((size_t)(pr->nkwonly + 1) * sizeof(Str *));
    pr->kwdefaults = arena_alloc((size_t)(pr->nkwonly + 1) * sizeof(Node *));
    memcpy(pr->names, names, (size_t)pr->n * sizeof(Str *));
    memcpy(pr->defaults, defs, (size_t)pr->n * sizeof(Node *));
    memcpy(pr->kwonly, kwonly, (size_t)pr->nkwonly * sizeof(Str *));
    memcpy(pr->kwdefaults, kwdefs, (size_t)pr->nkwonly * sizeof(Node *));
    pr->ann = arena_alloc((size_t)(pr->n + 1) * sizeof(Node *));
    pr->kwann = arena_alloc((size_t)(pr->nkwonly + 1) * sizeof(Node *));
    memcpy(pr->ann, anns, (size_t)pr->n * sizeof(Node *));
    memcpy(pr->kwann, kwanns, (size_t)pr->nkwonly * sizeof(Node *));
    /* duplicate check */
    Str *all[520];
    int na = 0;
    for (int i = 0; i < pr->n; i++) all[na++] = pr->names[i];
    for (int i = 0; i < pr->nkwonly; i++) all[na++] = pr->kwonly[i];
    if (pr->vararg) all[na++] = pr->vararg;
    if (pr->kwarg) all[na++] = pr->kwarg;
    for (int i = 0; i < na; i++)
        for (int j = 0; j < i; j++)
            if (str_eq(all[i], all[j]))
                syntax_error(p->filename, tk(p)->line, "duplicate argument '%s' in function definition", all[i]->s);
    return pr;
}

static Node *p_test(P *p) {
    enter(p);
    Node *n;
    if (is_kw(p, KW_lambda)) {
        int line = tk(p)->line;
        p->pos++;
        n = mk(p, E_LAMBDA, line);
        n->params = p_params(p, true);
        expect_op(p, ':');
        n->a = p_test(p);
    } else {
        n = p_or(p);
        if (is_kw(p, KW_if)) {
            int line = tk(p)->line;
            p->pos++;
            Node *c = mk(p, E_IFEXP, line);
            c->b = n;
            c->a = p_or(p);
            expect_kw(p, KW_else);
            c->c = p_test(p);
            n = c;
        }
    }
    p->depth--;
    return n;
}

static Node *p_namedexpr(P *p) {
    if (tk(p)->type == TK_NAME && p->toks[p->pos + 1].type == TK_OP && p->toks[p->pos + 1].op == TOP_WALRUS) {
        int line = tk(p)->line;
        Node *n = mk(p, E_NAMED, line);
        n->a = mk(p, E_NAME, line);
        n->a->val = (Obj *)expect_name(p);
        p->pos++;
        n->b = p_test(p);
        return n;
    }
    return p_test(p);
}

static Node *p_star_or_test(P *p) {
    if (is_op(p, '*')) {
        int line = tk(p)->line;
        p->pos++;
        Node *n = mk(p, E_STARRED, line);
        n->a = p_bitor(p);
        return n;
    }
    return p_namedexpr(p);
}

static bool at_expr_end(P *p) {
    Token *t = tk(p);
    if (t->type == TK_NEWLINE || t->type == TK_EOF) return true;
    if (t->type == TK_OP) return t->op == ')' || t->op == ']' || t->op == '}' || t->op == '=' || t->op == ';' || t->op == ':';
    return false;
}

static Node *p_testlist(P *p, bool allow_star) {
    int line = tk(p)->line;
    Node *first = allow_star ? p_star_or_test(p) : p_test(p);
    if (!is_op(p, ',')) {
        if (first->kind == E_STARRED) syntax_error(p->filename, line, "can't use starred expression here");
        return first;
    }
    Vec items = {0};
    vpush(&items, first);
    while (accept_op(p, ',')) {
        if (at_expr_end(p)) break;
        vpush(&items, allow_star ? p_star_or_test(p) : p_test(p));
    }
    Node *n = mk(p, E_TUPLE, line);
    set_list(n, 0, &items);
    return n;
}

static Node *p_yield_or_testlist(P *p) {
    if (is_kw(p, KW_yield)) {
        int line = tk(p)->line;
        p->pos++;
        if (accept_kw(p, KW_from)) {
            Node *n = mk(p, E_YIELDFROM, line);
            n->a = p_test(p);
            return n;
        }
        Node *n = mk(p, E_YIELD, line);
        if (!at_expr_end(p)) n->a = p_testlist(p, true);
        return n;
    }
    return p_testlist(p, true);
}

/* ---------- statements ---------- */
static void check_target(P *p, Node *n, int line, bool del) {
    switch (n->kind) {
    case E_NAME: case E_ATTR: case E_SUBSCR: return;
    case E_TUPLE: case E_LIST: {
        int stars = 0;
        for (int i = 0; i < n->n[0]; i++) {
            Node *e = n->l[0][i];
            if (e->kind == E_STARRED) {
                if (del) break;
                stars++;
                check_target(p, e->a, line, del);
            } else check_target(p, e, line, del);
        }
        if (stars > 1) syntax_error(p->filename, line, "multiple starred expressions in assignment");
        return;
    }
    case E_STARRED:
        syntax_error(p->filename, line, "starred assignment target must be in a list or tuple");
    case E_CONST: syntax_error(p->filename, line, "cannot %s literal", del ? "delete" : "assign to");
    case E_CALL: syntax_error(p->filename, line, "cannot %s function call", del ? "delete" : "assign to");
    default: syntax_error(p->filename, line, "cannot %s expression", del ? "delete" : "assign to expression here. Maybe you meant '==' instead of '='?");
    }
}

static int augassign_op(P *p) {
    Token *t = tk(p);
    if (t->type != TK_OP) return -1;
    switch (t->op) {
    case TOP_PLUSEQ: return OP_ADD;
    case TOP_MINUSEQ: return OP_SUB;
    case TOP_STAREQ: return OP_MUL;
    case TOP_SLASHEQ: return OP_TRUEDIV;
    case TOP_FLOOREQ: return OP_FLOORDIV;
    case TOP_PCTEQ: return OP_MOD;
    case TOP_POWEQ: return OP_POW;
    case TOP_LSHEQ: return OP_LSHIFT;
    case TOP_RSHEQ: return OP_RSHIFT;
    case TOP_ANDEQ: return OP_AND;
    case TOP_OREQ: return OP_OR;
    case TOP_XOREQ: return OP_XOR;
    case TOP_ATEQ: return OP_MATMUL;
    }
    return -1;
}

static Str *p_dotted_name(P *p) {
    SB b;
    sb_init(&b);
    while (accept_op(p, '.')) { }
    while (accept_op(p, TOP_ELLIPSIS)) { }
    Str *saved = p->cls;
    p->cls = NULL; /* module names are never mangled */
    if (tk(p)->type == TK_NAME) {
        sb_putstr(&b, (Obj *)expect_name(p));
        while (accept_op(p, '.')) { sb_putc(&b, '.'); sb_putstr(&b, (Obj *)expect_name(p)); }
    }
    p->cls = saved;
    return AS_STR(sb_finish(&b));
}

static void p_simple(P *p, Vec *out) {
    Token *t = tk(p);
    int line = t->line;
    Node *n;
    if (t->type == TK_KW) {
        switch (t->kw) {
        case KW_pass: p->pos++; vpush(out, mk(p, S_PASS, line)); return;
        case KW_break: p->pos++; vpush(out, mk(p, S_BREAK, line)); return;
        case KW_continue: p->pos++; vpush(out, mk(p, S_CONTINUE, line)); return;
        case KW_return:
            p->pos++;
            n = mk(p, S_RETURN, line);
            if (!at_expr_end(p)) n->a = p_testlist(p, true);
            vpush(out, n);
            return;
        case KW_raise:
            p->pos++;
            n = mk(p, S_RAISE, line);
            if (!at_expr_end(p)) {
                n->a = p_test(p);
                if (accept_kw(p, KW_from)) n->b = p_test(p);
            }
            vpush(out, n);
            return;
        case KW_global: case KW_nonlocal: {
            p->pos++;
            n = mk(p, t->kw == KW_global ? S_GLOBAL : S_NONLOCAL, line);
            Vec names = {0};
            do {
                Node *nm = mk(p, E_NAME, line);
                nm->val = (Obj *)expect_name(p);
                vpush(&names, nm);
            } while (accept_op(p, ','));
            set_list(n, 0, &names);
            vpush(out, n);
            return;
        }
        case KW_del: {
            p->pos++;
            n = mk(p, S_DEL, line);
            Vec targets = {0};
            do {
                if (at_expr_end(p)) break;
                Node *e = p_bitor(p);
                check_target(p, e, line, true);
                vpush(&targets, e);
            } while (accept_op(p, ','));
            set_list(n, 0, &targets);
            vpush(out, n);
            return;
        }
        case KW_assert:
            p->pos++;
            n = mk(p, S_ASSERT, line);
            n->a = p_test(p);
            if (accept_op(p, ',')) n->b = p_test(p);
            vpush(out, n);
            return;
        case KW_import: {
            p->pos++;
            n = mk(p, S_IMPORT, line);
            Vec al = {0};
            do {
                Node *a = mk(p, N_ALIAS, line);
                a->val = (Obj *)p_dotted_name(p);
                if (AS_STR(a->val)->len == 0) fail(p, "invalid syntax");
                if (accept_kw(p, KW_as)) a->val2 = (Obj *)expect_name(p);
                vpush(&al, a);
            } while (accept_op(p, ','));
            set_list(n, 0, &al);
            vpush(out, n);
            return;
        }
        case KW_from: {
            p->pos++;
            n = mk(p, S_IMPORTFROM, line);
            n->val = (Obj *)p_dotted_name(p);
            expect_kw(p, KW_import);
            Vec al = {0};
            if (accept_op(p, '*')) n->op = 1;
            else {
                bool paren = accept_op(p, '(');
                do {
                    if (paren && is_op(p, ')')) break;
                    Node *a = mk(p, N_ALIAS, line);
                    a->val = (Obj *)expect_name(p);
                    if (accept_kw(p, KW_as)) a->val2 = (Obj *)expect_name(p);
                    vpush(&al, a);
                } while (accept_op(p, ','));
                if (paren) expect_op(p, ')');
            }
            set_list(n, 0, &al);
            vpush(out, n);
            return;
        }
        default: break;
        }
    }
    Node *e = p_yield_or_testlist(p);
    int aug = augassign_op(p);
    if (aug >= 0) {
        if (e->kind != E_NAME && e->kind != E_ATTR && e->kind != E_SUBSCR)
            syntax_error(p->filename, line, "illegal expression for augmented assignment");
        p->pos++;
        n = mk(p, S_AUGASSIGN, line);
        n->a = e; n->op = aug;
        n->b = p_yield_or_testlist(p);
        vpush(out, n);
        return;
    }
    if (is_op(p, ':')) {
        if (e->kind != E_NAME && e->kind != E_ATTR && e->kind != E_SUBSCR)
            syntax_error(p->filename, line, "illegal target for annotation");
        p->pos++;
        Node *ann = p_test(p);
        n = mk(p, S_ANNASSIGN, line);
        n->a = e;
        n->c = ann;
        if (accept_op(p, '=')) n->b = p_yield_or_testlist(p);
        vpush(out, n);
        return;
    }
    if (is_op(p, '=')) {
        n = mk(p, S_ASSIGN, line);
        Vec targets = {0};
        while (accept_op(p, '=')) {
            check_target(p, e, line, false);
            vpush(&targets, e);
            e = p_yield_or_testlist(p);
        }
        set_list(n, 0, &targets);
        n->a = e;
        vpush(out, n);
        return;
    }
    n = mk(p, S_EXPR, line);
    n->a = e;
    vpush(out, n);
}

static void p_simple_line(P *p, Vec *out) {
    p_simple(p, out);
    while (accept_op(p, ';')) {
        if (tk(p)->type == TK_NEWLINE || tk(p)->type == TK_EOF) break;
        p_simple(p, out);
    }
    if (tk(p)->type == TK_EOF) return;
    if (tk(p)->type != TK_NEWLINE) fail(p, "invalid syntax");
    p->pos++;
}

static void p_block(P *p, Vec *out) {
    expect_op(p, ':');
    if (tk(p)->type != TK_NEWLINE) { p_simple_line(p, out); return; }
    p->pos++;
    if (tk(p)->type != TK_INDENT) fail(p, "expected an indented block");
    p->pos++;
    while (tk(p)->type != TK_DEDENT && tk(p)->type != TK_EOF) p_stmt(p, out);
    if (tk(p)->type == TK_DEDENT) p->pos++;
}

static Node *p_target_list(P *p) {
    int line = tk(p)->line;
    Vec targets = {0};
    bool comma = false;
    for (;;) {
        Node *t;
        if (is_op(p, '*')) { p->pos++; t = mk(p, E_STARRED, line); t->a = p_bitor(p); }
        else t = p_bitor(p);
        vpush(&targets, t);
        if (!accept_op(p, ',')) break;
        comma = true;
        if (is_kw(p, KW_in)) break;
    }
    Node *n;
    if (comma || targets.n > 1) { n = mk(p, E_TUPLE, line); set_list(n, 0, &targets); }
    else n = targets.v[0];
    check_target(p, n, line, false);
    return n;
}

/* The bracketed list in "def f[T, *Ts, **P]" and "class C[T: bound]". The '[' is already consumed. */
static void p_type_params(P *p, Vec *out) {
    if (is_op(p, ']')) fail(p, "Type parameter list cannot be empty");
    do {
        if (is_op(p, ']')) break;
        Node *t = mk(p, N_TYPEPARAM, tk(p)->line);
        if (accept_op(p, '*')) t->op = 1;
        else if (accept_op(p, TOP_POW)) t->op = 2;
        t->val = (Obj *)expect_name(p);
        if (!t->op && accept_op(p, ':')) t->a = p_test(p);
        if (accept_op(p, '=')) t->c = p_test(p);
        for (int i = 0; i < out->n; i++)
            if (str_eq(AS_STR(out->v[i]->val), AS_STR(t->val)))
                syntax_error(p->filename, t->line, "duplicate type parameter '%s'", AS_STR(t->val)->s);
        vpush(out, t);
    } while (accept_op(p, ','));
    expect_op(p, ']');
}

/* ---------- match statement ---------- */

static bool at_word(P *p, int pos, const char *word) {
    Token *t = &p->toks[pos];
    return t->type == TK_NAME && str_eqc(AS_STR(t->val), word);
}

/* "match" is an ordinary name everywhere except at the start of a statement
   whose line ends in ':'. No other statement that starts with a name ends that way. */
static bool at_match_stmt(P *p) {
    if (!at_word(p, p->pos, "match")) return false;
    int i = p->pos + 1;
    if (p->toks[i].type == TK_NEWLINE || p->toks[i].type == TK_EOF) return false;
    while (p->toks[i].type != TK_NEWLINE && p->toks[i].type != TK_EOF) i++;
    Token *last = &p->toks[i - 1];
    if (last->type != TK_OP || last->op != ':') return false;
    if (p->toks[i].type == TK_EOF) return true;
    if (p->toks[i + 1].type == TK_EOF) return true;
    return p->toks[i + 1].type == TK_INDENT && at_word(p, i + 2, "case");
}

static Node *p_pattern(P *p);

static Node *p_star_pattern(P *p) {
    if (is_op(p, '*')) {
        Node *s = mk(p, P_STAR, tk(p)->line);
        p->pos++;
        Str *name = expect_name(p);
        s->val = str_eqc(name, "_") ? NULL : (Obj *)name;
        return s;
    }
    return p_pattern(p);
}

/* Items of a sequence pattern up to (not including) the closing token. With close == 0
   the list is the open form "case a, b:" and ends at ':' or 'if'. */
static Node *p_seq_pattern(P *p, Node *first, int line, int close) {
    Node *n = mk(p, P_SEQ, line);
    Vec items = {0};
    bool more = true;
    if (first) {
        vpush(&items, first);
        more = accept_op(p, ',');
        if (!more && first->kind == P_STAR) fail(p, "invalid syntax");
    }
    while (more) {
        if (close ? is_op(p, close) : (is_op(p, ':') || is_kw(p, KW_if))) break;
        vpush(&items, p_star_pattern(p));
        more = accept_op(p, ',');
    }
    int stars = 0;
    for (int i = 0; i < items.n; i++) if (items.v[i]->kind == P_STAR) stars++;
    if (stars > 1) syntax_error(p->filename, line, "multiple starred names in sequence pattern");
    set_list(n, 0, &items);
    return n;
}

/* A literal or a dotted name, used as a value pattern or a mapping key. Returns NULL
   for a bare name. */
static Node *p_value_pattern(P *p, bool *bare_name) {
    Token *t = tk(p);
    int line = t->line;
    Node *n = mk(p, P_VALUE, line);
    *bare_name = false;
    if (t->type == TK_OP && t->op == '-' && p->toks[p->pos + 1].type == TK_NUMBER) { n->a = p_factor(p); return n; }
    if (t->type == TK_NUMBER) { p->pos++; n->a = mk_const(p, t->val, line); return n; }
    if (t->type == TK_STRING) { n->a = p_strings(p); return n; }
    if (t->type == TK_FSTRING) fail(p, "patterns may only match literals and attribute lookups");
    if (t->type == TK_KW && (t->kw == KW_None || t->kw == KW_True || t->kw == KW_False)) {
        p->pos++;
        n->a = mk_const(p, t->kw == KW_None ? Py_None : t->kw == KW_True ? Py_True : Py_False, line);
        n->op = 1;
        return n;
    }
    if (t->type == TK_NAME) {
        p->pos++;
        Node *e = mk(p, E_NAME, line);
        e->val = (Obj *)mangle(p, AS_STR(t->val));
        if (!is_op(p, '.')) { *bare_name = true; n->a = e; return n; }
        while (accept_op(p, '.')) {
            Node *at = mk(p, E_ATTR, line);
            at->a = e; at->val = (Obj *)expect_name(p);
            e = at;
        }
        n->a = e;
        return n;
    }
    fail(p, "invalid pattern");
}

static Node *p_closed_pattern(P *p) {
    Token *t = tk(p);
    int line = t->line;
    enter(p);
    Node *n;
    if (t->type == TK_OP && t->op == '(') {
        p->pos++;
        if (accept_op(p, ')')) n = mk(p, P_SEQ, line);
        else {
            Node *first = p_star_pattern(p);
            if (is_op(p, ')') && first->kind != P_STAR) n = first;
            else n = p_seq_pattern(p, first, line, ')');
            expect_op(p, ')');
        }
    } else if (t->type == TK_OP && t->op == '[') {
        p->pos++;
        n = p_seq_pattern(p, NULL, line, ']');
        expect_op(p, ']');
    } else if (t->type == TK_OP && t->op == '{') {
        p->pos++;
        n = mk(p, P_MAP, line);
        Vec keys = {0}, pats = {0};
        while (!is_op(p, '}')) {
            if (accept_op(p, TOP_POW)) {
                if (n->val) fail(p, "invalid syntax");
                Str *name = expect_name(p);
                if (str_eqc(name, "_")) fail(p, "invalid syntax");
                n->val = (Obj *)name;
            } else {
                if (n->val) fail(p, "double star pattern must be the last (right-most) subpattern in the mapping pattern");
                bool bare;
                Node *k = p_value_pattern(p, &bare);
                if (bare) fail(p, "invalid syntax");
                expect_op(p, ':');
                vpush(&keys, k->a);
                vpush(&pats, p_pattern(p));
            }
            if (!accept_op(p, ',')) break;
        }
        expect_op(p, '}');
        set_list(n, 0, &keys); set_list(n, 1, &pats);
    } else {
        bool bare;
        n = p_value_pattern(p, &bare);
        if (is_op(p, '(') && n->a->kind != E_CONST) {
            Node *c = mk(p, P_CLASS, line);
            c->a = n->a;
            p->pos++;
            Vec pos = {0}, kws = {0}, names = {0};
            while (!is_op(p, ')')) {
                if (tk(p)->type == TK_NAME && p->toks[p->pos + 1].type == TK_OP && p->toks[p->pos + 1].op == '=') {
                    Str *name = expect_name(p);
                    p->pos++;
                    for (int i = 0; i < names.n; i++)
                        if (str_eq(AS_STR(names.v[i]->val), name))
                            syntax_error(p->filename, line, "attribute name repeated in class pattern: %s", name->s);
                    vpush(&names, mk_const(p, (Obj *)name, line));
                    vpush(&kws, p_pattern(p));
                } else {
                    if (kws.n) fail(p, "positional patterns follow keyword patterns");
                    vpush(&pos, p_pattern(p));
                }
                if (!accept_op(p, ',')) break;
            }
            expect_op(p, ')');
            set_list(c, 0, &pos); set_list(c, 1, &kws); set_list(c, 2, &names);
            n = c;
        } else if (bare) {
            Str *name = AS_STR(n->a->val);
            n = mk(p, P_CAPTURE, line);
            n->val = str_eqc(name, "_") ? NULL : (Obj *)name;
        }
    }
    p->depth--;
    return n;
}

static Node *p_pattern(P *p) {
    int line = tk(p)->line;
    Node *n = p_closed_pattern(p);
    if (is_op(p, '|')) {
        Node *o = mk(p, P_OR, line);
        Vec alts = {0};
        vpush(&alts, n);
        while (accept_op(p, '|')) vpush(&alts, p_closed_pattern(p));
        set_list(o, 0, &alts);
        n = o;
    }
    if (accept_kw(p, KW_as)) {
        Node *a = mk(p, P_AS, line);
        a->a = n;
        Str *name = expect_name(p);
        if (str_eqc(name, "_")) syntax_error(p->filename, line, "cannot use '_' as a target");
        a->val = (Obj *)name;
        n = a;
    }
    return n;
}

static Node *p_match(P *p) {
    int line = tk(p)->line;
    p->pos++;
    Node *n = mk(p, S_MATCH, line);
    n->a = p_testlist(p, true);
    expect_op(p, ':');
    if (tk(p)->type != TK_NEWLINE) fail(p, "invalid syntax");
    p->pos++;
    if (tk(p)->type != TK_INDENT) fail(p, "expected an indented block");
    p->pos++;
    Vec cases = {0};
    while (tk(p)->type != TK_DEDENT && tk(p)->type != TK_EOF) {
        if (tk(p)->type == TK_NEWLINE) { p->pos++; continue; }
        if (!at_word(p, p->pos, "case")) fail(p, "invalid syntax");
        Node *c = mk(p, N_CASE, tk(p)->line);
        p->pos++;
        Node *first = p_star_pattern(p);
        if (is_op(p, ',')) first = p_seq_pattern(p, first, c->line, 0);
        else if (first->kind == P_STAR) fail(p, "invalid syntax");
        c->a = first;
        if (accept_kw(p, KW_if)) c->b = p_namedexpr(p);
        Vec body = {0};
        p_block(p, &body);
        set_list(c, 0, &body);
        vpush(&cases, c);
    }
    if (tk(p)->type == TK_DEDENT) p->pos++;
    if (!cases.n) fail(p, "invalid syntax");
    set_list(n, 0, &cases);
    return n;
}

static Node *p_funcdef(P *p, Vec *decorators) {
    int line = tk(p)->line;
    expect_kw(p, KW_def);
    Node *n = mk(p, S_DEF, line);
    n->val = (Obj *)expect_name(p);
    Vec tparams = {0};
    if (accept_op(p, '[')) p_type_params(p, &tparams);
    expect_op(p, '(');
    n->params = p_params(p, false);
    expect_op(p, ')');
    if (accept_op(p, TOP_ARROW)) n->b = p_test(p);
    Vec body = {0};
    p_block(p, &body);
    set_list(n, 0, &body);
    if (decorators) set_list(n, 1, decorators);
    set_list(n, 2, &tparams);
    return n;
}

static Node *p_classdef(P *p, Vec *decorators) {
    int line = tk(p)->line;
    expect_kw(p, KW_class);
    Node *n = mk(p, S_CLASS, line);
    n->val = (Obj *)expect_name(p);
    Vec bases = {0}, tparams = {0};
    if (accept_op(p, '[')) p_type_params(p, &tparams);
    set_list(n, 3, &tparams);
    if (accept_op(p, '(')) p_call_args(p, &bases);
    Vec body = {0};
    Str *outer_cls = p->cls;
    p->cls = AS_STR(n->val);
    p_block(p, &body);
    p->cls = outer_cls;
    set_list(n, 0, &body);
    if (decorators) set_list(n, 1, decorators);
    set_list(n, 2, &bases);
    return n;
}

static Node *p_if(P *p) {
    int line = tk(p)->line;
    p->pos++;
    Node *n = mk(p, S_IF, line);
    n->a = p_namedexpr(p);
    Vec body = {0}, orelse = {0};
    p_block(p, &body);
    if (is_kw(p, KW_elif)) vpush(&orelse, p_if(p));
    else if (accept_kw(p, KW_else)) p_block(p, &orelse);
    set_list(n, 0, &body);
    set_list(n, 1, &orelse);
    return n;
}

static void p_stmt(P *p, Vec *out) {
    Token *t = tk(p);
    int line = t->line;
    Node *n;
    if (t->type == TK_INDENT) fail(p, "unexpected indent");
    if (t->type == TK_NEWLINE) { p->pos++; return; }
    if (t->type == TK_OP && t->op == '@') {
        Vec decos = {0};
        while (accept_op(p, '@')) {
            vpush(&decos, p_namedexpr(p));
            if (tk(p)->type != TK_NEWLINE) fail(p, "invalid syntax");
            p->pos++;
        }
        if (is_kw(p, KW_def)) vpush(out, p_funcdef(p, &decos));
        else if (is_kw(p, KW_class)) vpush(out, p_classdef(p, &decos));
        else fail(p, "invalid syntax");
        return;
    }
    if (t->type != TK_KW) {
        if (at_match_stmt(p)) { vpush(out, p_match(p)); return; }
        p_simple_line(p, out);
        return;
    }
    switch (t->kw) {
    case KW_if: vpush(out, p_if(p)); return;
    case KW_while: {
        p->pos++;
        n = mk(p, S_WHILE, line);
        n->a = p_namedexpr(p);
        Vec body = {0}, orelse = {0};
        p_block(p, &body);
        if (accept_kw(p, KW_else)) p_block(p, &orelse);
        set_list(n, 0, &body); set_list(n, 1, &orelse);
        vpush(out, n);
        return;
    }
    case KW_for: {
        p->pos++;
        n = mk(p, S_FOR, line);
        n->a = p_target_list(p);
        expect_kw(p, KW_in);
        n->b = p_testlist(p, true);
        Vec body = {0}, orelse = {0};
        p_block(p, &body);
        if (accept_kw(p, KW_else)) p_block(p, &orelse);
        set_list(n, 0, &body); set_list(n, 1, &orelse);
        vpush(out, n);
        return;
    }
    case KW_try: {
        p->pos++;
        n = mk(p, S_TRY, line);
        Vec body = {0}, handlers = {0}, orelse = {0}, fin = {0};
        p_block(p, &body);
        while (is_kw(p, KW_except)) {
            Node *h = mk(p, N_HANDLER, tk(p)->line);
            p->pos++;
            if (is_op(p, '*')) fail(p, "except* is not supported");
            if (!is_op(p, ':')) {
                h->a = p_test(p);
                if (is_op(p, ',')) {
                    /* except A, B: (tuple without parentheses) */
                    Vec items = {0};
                    vpush(&items, h->a);
                    while (accept_op(p, ',')) vpush(&items, p_test(p));
                    h->a = mk(p, E_TUPLE, h->line);
                    set_list(h->a, 0, &items);
                }
                if (accept_kw(p, KW_as)) h->val = (Obj *)expect_name(p);
            }
            Vec hb = {0};
            p_block(p, &hb);
            set_list(h, 0, &hb);
            vpush(&handlers, h);
        }
        if (handlers.n && accept_kw(p, KW_else)) p_block(p, &orelse);
        bool has_fin = false;
        if (accept_kw(p, KW_finally)) { has_fin = true; p_block(p, &fin); }
        if (!handlers.n && !has_fin) fail(p, "expected 'except' or 'finally' block");
        for (int i = 0; i + 1 < handlers.n; i++)
            if (!handlers.v[i]->a) syntax_error(p->filename, handlers.v[i]->line, "default 'except:' must be last");
        set_list(n, 0, &body); set_list(n, 1, &handlers); set_list(n, 2, &orelse); set_list(n, 3, &fin);
        n->op = has_fin;
        vpush(out, n);
        return;
    }
    case KW_with: {
        p->pos++;
        n = mk(p, S_WITH, line);
        Vec items = {0};
        bool paren = false;
        if (is_op(p, '(')) {
            /* Parenthesized item list: only if the matching ')' is followed by ':'. */
            int depth = 0, i = p->pos;
            bool as_seen = false;
            for (;; i++) {
                Token *q = &p->toks[i];
                if (q->type == TK_EOF) break;
                if (q->type == TK_OP && (q->op == '(' || q->op == '[' || q->op == '{')) depth++;
                else if (q->type == TK_OP && (q->op == ')' || q->op == ']' || q->op == '}')) { if (--depth == 0) break; }
                else if (q->type == TK_KW && q->kw == KW_as && depth == 1) as_seen = true;
            }
            Token *after = &p->toks[i + 1];
            if (as_seen && p->toks[i].type != TK_EOF && after->type == TK_OP && after->op == ':') { paren = true; p->pos++; }
        }
        do {
            if (paren && is_op(p, ')')) break;
            Node *it = mk(p, N_WITHITEM, tk(p)->line);
            it->a = p_test(p);
            if (accept_kw(p, KW_as)) {
                it->b = p_bitor(p);
                check_target(p, it->b, it->line, false);
            }
            vpush(&items, it);
        } while (accept_op(p, ','));
        if (paren) expect_op(p, ')');
        Vec body = {0};
        p_block(p, &body);
        set_list(n, 0, &items); set_list(n, 1, &body);
        vpush(out, n);
        return;
    }
    case KW_def: vpush(out, p_funcdef(p, NULL)); return;
    case KW_class: vpush(out, p_classdef(p, NULL)); return;
    case KW_async: fail(p, "async and await are not supported");
    default: p_simple_line(p, out); return;
    }
}

Node *parse_source(const char *src, const char *filename, int mode) {
    P p = {0};
    p.src = src; p.filename = filename; p.line0 = 1;
    lex(&p);
    Node *m = mk(&p, S_MODULE, 1);
    Vec body = {0};
    if (mode == MODE_EVAL) {
        while (tk(&p)->type == TK_NEWLINE) p.pos++;
        Node *r = mk(&p, S_RETURN, 1);
        r->a = p_testlist(&p, true);
        while (tk(&p)->type == TK_NEWLINE) p.pos++;
        if (tk(&p)->type != TK_EOF) fail(&p, "invalid syntax");
        vpush(&body, r);
    } else {
        while (tk(&p)->type != TK_EOF) {
            if (tk(&p)->type == TK_DEDENT) fail(&p, "invalid syntax");
            p_stmt(&p, &body);
        }
    }
    set_list(m, 0, &body);
    return m;
}

/* ---------- expression to source text ----------
   Used for annotations under "from __future__ import annotations", which are
   stored as strings. The spacing and parentheses follow CPython's ast.unparse. */

enum { PR_TUPLE, PR_NAMED, PR_TEST, PR_OR, PR_AND, PR_NOT, PR_CMP, PR_BOR, PR_BXOR, PR_BAND, PR_SHIFT,
       PR_ARITH, PR_TERM, PR_FACTOR, PR_POWER, PR_ATOM };

static void unp(SB *b, Node *n, int prec);
static const char *unp_filename;

static void unp_list(SB *b, Node **l, int n, int prec) {
    for (int i = 0; i < n; i++) { if (i) sb_puts(b, ", "); unp(b, l[i], prec); }
}

static void unp_params(SB *b, Params *p) {
    bool first = true;
#define SEP() do { if (!first) sb_puts(b, ", "); first = false; } while (0)
    for (int i = 0; i < p->n; i++) {
        SEP();
        sb_puts(b, p->names[i]->s);
        if (p->defaults[i]) { sb_putc(b, '='); unp(b, p->defaults[i], PR_TEST); }
        if (p->nposonly && i + 1 == p->nposonly) sb_puts(b, ", /");
    }
    if (p->vararg) { SEP(); sb_putc(b, '*'); sb_puts(b, p->vararg->s); }
    else if (p->nkwonly) { SEP(); sb_putc(b, '*'); }
    for (int i = 0; i < p->nkwonly; i++) {
        SEP();
        sb_puts(b, p->kwonly[i]->s);
        if (p->kwdefaults[i]) { sb_putc(b, '='); unp(b, p->kwdefaults[i], PR_TEST); }
    }
    if (p->kwarg) { SEP(); sb_puts(b, "**"); sb_puts(b, p->kwarg->s); }
#undef SEP
}

static void unp_comp(SB *b, Node *n) {
    for (int i = 0; i < n->n[0]; i++) {
        Node *g = n->l[0][i];
        sb_puts(b, " for ");
        if (g->a->kind == E_TUPLE) unp_list(b, g->a->l[0], g->a->n[0], PR_BOR); else unp(b, g->a, PR_BOR);
        sb_puts(b, " in ");
        unp(b, g->b, PR_OR);
        for (int j = 0; j < g->n[0]; j++) { sb_puts(b, " if "); unp(b, g->l[0][j], PR_OR); }
    }
}

static void unp(SB *b, Node *n, int prec) {
    static const char *binop[] = {"+", "-", "*", "/", "//", "%", "**", "<<", ">>", "&", "|", "^", "@"};
    static const int binprec[] = {PR_ARITH, PR_ARITH, PR_TERM, PR_TERM, PR_TERM, PR_TERM, PR_POWER, PR_SHIFT, PR_SHIFT,
                                  PR_BAND, PR_BOR, PR_BXOR, PR_TERM};
    static const char *cmpop[] = {"<", "<=", "==", "!=", ">", ">=", "in", "not in", "is", "is not"};
    int mine = PR_ATOM;
    switch (n->kind) {
    case E_BINOP: mine = binprec[n->op]; break;
    case E_UNARY: mine = PR_FACTOR; break;
    case E_NOT: mine = PR_NOT; break;
    case E_BOOLOP: mine = n->op ? PR_OR : PR_AND; break;
    case E_COMPARE: mine = PR_CMP; break;
    case E_IFEXP: case E_LAMBDA: mine = PR_TEST; break;
    case E_NAMED: mine = PR_NAMED; break;
    case E_YIELD: case E_YIELDFROM: mine = PR_TUPLE - 1; break;
    case E_STARRED: mine = PR_TEST; break;
    case E_CONST: {
        /* The parser folds "-1" into one constant; it still binds like a unary minus. */
        Obj *v = n->val;
        if ((KIND(v) == K_INT && int_sign(v) < 0) || (KIND(v) == K_FLOAT && signbit(((Float *)v)->v))) mine = PR_FACTOR;
        break;
    }
    default: break;
    }
    bool paren = mine < prec;
    if (paren) sb_putc(b, '(');
    switch (n->kind) {
    case E_CONST:
        if (n->val == Py_Ellipsis) sb_puts(b, "...");
        else sb_putstr(b, obj_repr(n->val));
        break;
    case E_NAME: sb_puts(b, AS_STR(n->val)->s); break;
    case E_BINOP:
        /* ** binds to the right; everything else to the left. */
        unp(b, n->a, n->op == OP_POW ? mine + 1 : mine);
        sb_printf(b, " %s ", binop[n->op]);
        unp(b, n->b, n->op == OP_POW ? mine : mine + 1);
        break;
    case E_UNARY: sb_putc(b, (char)n->op); unp(b, n->a, PR_FACTOR); break;
    case E_NOT: sb_puts(b, "not "); unp(b, n->a, PR_NOT); break;
    case E_BOOLOP:
        for (int i = 0; i < n->n[0]; i++) {
            if (i) sb_puts(b, n->op ? " or " : " and ");
            unp(b, n->l[0][i], mine + 1);
        }
        break;
    case E_COMPARE:
        unp(b, n->a, PR_CMP + 1);
        for (int i = 0; i < n->n[0]; i++) { sb_printf(b, " %s ", cmpop[n->ops[i]]); unp(b, n->l[0][i], PR_CMP + 1); }
        break;
    case E_CALL:
        unp(b, n->a, PR_ATOM);
        sb_putc(b, '(');
        unp_list(b, n->l[0], n->n[0], PR_TEST);
        sb_putc(b, ')');
        break;
    case E_KEYWORD:
        if (n->val) sb_printf(b, "%s=", AS_STR(n->val)->s); else sb_puts(b, "**");
        unp(b, n->a, PR_TEST);
        break;
    case E_STARRED: sb_putc(b, '*'); unp(b, n->a, PR_BOR); break;
    case E_ATTR:
        unp(b, n->a, PR_ATOM);
        /* "1 .real": a space keeps the dot from joining the number. */
        if (n->a->kind == E_CONST && KIND(n->a->val) == K_INT) sb_putc(b, ' ');
        sb_printf(b, ".%s", AS_STR(n->val)->s);
        break;
    case E_SUBSCR:
        unp(b, n->a, PR_ATOM);
        sb_putc(b, '[');
        if (n->b->kind == E_TUPLE && n->b->n[0] > 0) {
            unp_list(b, n->b->l[0], n->b->n[0], PR_TEST);
            if (n->b->n[0] == 1) sb_putc(b, ',');
        } else unp(b, n->b, PR_TUPLE);
        sb_putc(b, ']');
        break;
    case E_SLICE:
        if (n->a) unp(b, n->a, PR_TEST);
        sb_putc(b, ':');
        if (n->b) unp(b, n->b, PR_TEST);
        if (n->c) { sb_putc(b, ':'); unp(b, n->c, PR_TEST); }
        break;
    case E_LIST: sb_putc(b, '['); unp_list(b, n->l[0], n->n[0], PR_TEST); sb_putc(b, ']'); break;
    case E_SET: sb_putc(b, '{'); unp_list(b, n->l[0], n->n[0], PR_TEST); sb_putc(b, '}'); break;
    case E_TUPLE:
        sb_putc(b, '(');
        unp_list(b, n->l[0], n->n[0], PR_TEST);
        if (n->n[0] == 1) sb_putc(b, ',');
        sb_putc(b, ')');
        break;
    case E_DICT:
        sb_putc(b, '{');
        for (int i = 0; i < n->n[0]; i++) {
            if (i) sb_puts(b, ", ");
            if (!n->l[0][i]) { sb_puts(b, "**"); unp(b, n->l[1][i], PR_BOR); }
            else { unp(b, n->l[0][i], PR_TEST); sb_puts(b, ": "); unp(b, n->l[1][i], PR_TEST); }
        }
        sb_putc(b, '}');
        break;
    case E_IFEXP:
        unp(b, n->b, PR_TEST + 1); sb_puts(b, " if "); unp(b, n->a, PR_TEST + 1);
        sb_puts(b, " else "); unp(b, n->c, PR_TEST);
        break;
    case E_LAMBDA:
        sb_puts(b, "lambda");
        if (n->params->n || n->params->nkwonly || n->params->vararg || n->params->kwarg) sb_putc(b, ' ');
        unp_params(b, n->params);
        sb_puts(b, ": ");
        unp(b, n->a, PR_TEST);
        break;
    case E_NAMED: unp(b, n->a, PR_ATOM); sb_puts(b, " := "); unp(b, n->b, PR_TEST); break;
    case E_LISTCOMP: sb_putc(b, '['); unp(b, n->a, PR_TEST); unp_comp(b, n); sb_putc(b, ']'); break;
    case E_SETCOMP: sb_putc(b, '{'); unp(b, n->a, PR_TEST); unp_comp(b, n); sb_putc(b, '}'); break;
    case E_GENEXP: sb_putc(b, '('); unp(b, n->a, PR_TEST); unp_comp(b, n); sb_putc(b, ')'); break;
    case E_DICTCOMP:
        sb_putc(b, '{'); unp(b, n->a, PR_TEST); sb_puts(b, ": "); unp(b, n->b, PR_TEST); unp_comp(b, n); sb_putc(b, '}');
        break;
    case E_YIELD: sb_puts(b, "yield"); if (n->a) { sb_putc(b, ' '); unp(b, n->a, PR_TUPLE); } break;
    case E_YIELDFROM: sb_puts(b, "yield from "); unp(b, n->a, PR_TEST); break;
    default:
        sb_free(b);
        syntax_error(unp_filename, n->line, "f-strings in annotations are not supported with 'from __future__ import annotations'");
    }
    if (paren) sb_putc(b, ')');
}

Str *unparse_expr(Node *n, const char *filename) {
    SB b;
    sb_init(&b);
    unp_filename = filename;
    unp(&b, n, PR_TUPLE);
    return AS_STR(sb_finish(&b));
}
