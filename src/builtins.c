/* Built-in functions, type constructors and the file object. */
#include "py.h"
#include <errno.h>
#include <ctype.h>

extern Obj *repr_force_default;
static Type *E_Unsupported;

#define ARGS int argc, Obj **argv, Obj *kwnames
#define UNUSED (void)argc; (void)argv; (void)kwnames
#define NKW (kwnames ? (int)((Tuple *)kwnames)->n : 0)

int kw_find(Obj *kwnames, const char *name) {
    if (!kwnames) return -1;
    Tuple *t = (Tuple *)kwnames;
    for (int i = 0; i < t->n; i++) if (str_eqc(AS_STR(t->items[i]), name)) return i;
    return -1;
}

/* allowed is a comma-separated list of accepted keyword names. */
int kw_check(Obj *kwnames, const char *fname, const char *allowed) {
    if (!kwnames) return 0;
    Tuple *t = (Tuple *)kwnames;
    for (int i = 0; i < t->n; i++) {
        Str *k = AS_STR(t->items[i]);
        const char *p = allowed;
        bool ok = false;
        while (*p) {
            const char *e = strchr(p, ',');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (n == k->len && memcmp(p, k->s, n) == 0) ok = true;
            p += n + (e ? 1 : 0);
        }
        if (!ok) {
            err(E_TypeError, "%s() got an unexpected keyword argument '%s'", fname, k->s);
            return -1;
        }
    }
    return 0;
}
#define KW(name) (kw_find(kwnames, name) >= 0 ? argv[argc + kw_find(kwnames, name)] : NULL)

/* ---------- files ---------- */
Obj *file_new(FILE *fp, const char *name, bool readable, bool writable, bool std) {
    File *f = gc_alloc(T_file, sizeof(File));
    f->fp = fp; f->name = AS_STR(str_c(name)); f->readable = readable; f->writable = writable; f->std = std;
    return (Obj *)f;
}

static File *file_check(Obj *o, bool reading, bool writing) {
    File *f = (File *)o;
    if (KIND(o) != K_FILE) { err(E_TypeError, "expected a file object, got %s", tname(o)); return NULL; }
    if (f->closed) { err(E_ValueError, "I/O operation on closed file."); return NULL; }
    if (reading && !f->readable) { err(E_Unsupported, "not readable"); return NULL; }
    if (writing && !f->writable) { err(E_Unsupported, "not writable"); return NULL; }
    return f;
}

static int file_getc(File *f) {
    int c = fgetc(f->fp);
    if (c == '\r' && f->universal) {
        int d = fgetc(f->fp);
        if (d != '\n' && d != EOF) ungetc(d, f->fp);
        return '\n';
    }
    return c;
}

/* Turns the bytes read from a file into a str, following its encoding. */
static Obj *file_decode(File *f, SB *raw) {
    const unsigned char *p = (const unsigned char *)raw->s;
    size_t n = raw->n, i = 0;
    bool plain = true;
    for (size_t k = 0; k < n; k++) if (p[k] >= 0x80) { plain = false; break; }
    if (plain) return sb_finish(raw);
    SB out;
    sb_init(&out);
    const char *why = NULL;
    while (i < n) {
        unsigned char c = p[i];
        if (c < 0x80) { sb_putc(&out, (char)c); i++; continue; }
        if (f->enc == ENC_LATIN1) { sb_putcp(&out, c); i++; continue; }
        size_t len = 0;
        if (f->enc == ENC_UTF8) {
            len = (c & 0xE0) == 0xC0 && c >= 0xC2 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 && c <= 0xF4 ? 4 : 0;
            if (!len) why = "invalid start byte";
            else if (i + len > n) { why = "unexpected end of data"; len = 0; }
            else for (size_t k = 1; k < len; k++) if ((p[i + k] & 0xC0) != 0x80) { why = "invalid continuation byte"; len = 0; break; }
        } else why = "ordinal not in range(128)";
        if (len) { sb_put(&out, (const char *)p + i, len); i += len; continue; }
        if (f->errors == ERR_UNKNOWN) {
            sb_free(&out); sb_free(raw);
            return err(E_LookupError, "unknown error handler name (supported: strict, replace, ignore)");
        }
        if (f->errors == ERR_STRICT) {
            sb_free(&out); sb_free(raw);
            return err(E_UnicodeDecodeError, "'%s' codec can't decode byte 0x%02x in position %zu: %s",
                       f->enc == ENC_UTF8 ? "utf-8" : "ascii", c, i, why);
        }
        if (f->errors == ERR_REPLACE) sb_putcp(&out, 0xFFFD);
        i++;
    }
    sb_free(raw);
    return sb_finish(&out);
}

Obj *file_readline(Obj *o) {
    File *f = file_check(o, true, false);
    if (!f) return NULL;
    if (f->fp == stdin) fflush(stdout);
    SB b;
    sb_init(&b);
    int c;
    while ((c = file_getc(f)) != EOF) {
        sb_putc(&b, (char)c);
        if (c == '\n') break;
    }
    return file_decode(f, &b);
}

int file_write_str(Obj *file, Str *s) {
    File *f = (File *)file;
    const char *p = s->s, *e = s->s + s->len;
    if (f->enc == ENC_UTF8 && !f->wnl) { fwrite(s->s, 1, s->len, f->fp); return 0; }
    SB out;
    sb_init(&out);
    for (int64_t pos = 0; p < e; pos++) {
        uint32_t cp = utf8_next(&p);
        if (cp == '\n' && f->wnl) { sb_puts(&out, f->wnl); continue; }
        if (f->enc == ENC_UTF8) { sb_putcp(&out, cp); continue; }
        uint32_t limit = f->enc == ENC_ASCII ? 128 : 256;
        if (cp >= limit) {
            if (f->errors == ERR_UNKNOWN) {
                sb_free(&out);
                err(E_LookupError, "unknown error handler name (supported: strict, replace, ignore)");
                return -1;
            }
            if (f->errors == ERR_STRICT) {
                sb_free(&out);
                err(E_UnicodeEncodeError, "'%s' codec can't encode character '\\u%04x' in position %lld: ordinal not in range(%u)",
                    f->enc == ENC_ASCII ? "ascii" : "latin-1", cp, (long long)pos, limit);
                return -1;
            }
            if (f->errors == ERR_REPLACE) sb_putc(&out, '?');
            continue;
        }
        sb_putc(&out, (char)cp);
    }
    fwrite(out.s, 1, out.n, f->fp);
    sb_free(&out);
    return 0;
}

static Obj *f_read(ARGS) {
    UNUSED;
    File *f = file_check(argv[0], true, false);
    if (!f) return NULL;
    int64_t n = -1;
    if (argc > 1 && argv[1] != Py_None && obj_index(argv[1], &n) < 0) return NULL;
    SB b;
    sb_init(&b);
    int c;
    while (n != 0 && (c = file_getc(f)) != EOF) {
        sb_putc(&b, (char)c);
        /* A count is in characters: finish the current UTF-8 sequence. */
        int more = f->enc != ENC_UTF8 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
        while (more-- > 0 && (c = fgetc(f->fp)) != EOF) sb_putc(&b, (char)c);
        if (n > 0) n--;
    }
    return file_decode(f, &b);
}
static Obj *f_readline(ARGS) { UNUSED; return file_readline(argv[0]); }
static Obj *f_readlines(ARGS) {
    UNUSED;
    List *l = (List *)list_new(0);
    for (;;) {
        Obj *s = file_readline(argv[0]);
        if (!s) return NULL;
        if (AS_STR(s)->len == 0) break;
        list_append(l, s);
    }
    return (Obj *)l;
}
static Obj *f_write(ARGS) {
    UNUSED;
    File *f = file_check(argv[0], false, true);
    if (!f) return NULL;
    if (!IS_STR(argv[1])) return err(E_TypeError, "write() argument must be str, not %s", tname(argv[1]));
    if (file_write_str(argv[0], AS_STR(argv[1])) < 0) return NULL;
    return int_new(AS_STR(argv[1])->nchars);
}
static Obj *f_writelines(ARGS) {
    UNUSED;
    File *f = file_check(argv[0], false, true);
    if (!f) return NULL;
    Obj *it = obj_iter(argv[1]), *s;
    if (!it) return NULL;
    while ((s = iter_next(it))) {
        if (!IS_STR(s)) return err(E_TypeError, "write() argument must be str, not %s", tname(s));
        if (file_write_str(argv[0], AS_STR(s)) < 0) return NULL;
    }
    return vm.exc ? NULL : Py_None;
}
static Obj *f_close(ARGS) {
    UNUSED;
    File *f = (File *)argv[0];
    if (!f->closed) {
        if (f->std) fflush(f->fp); else fclose(f->fp);
        if (!f->std) f->closed = true;
    }
    return Py_None;
}
static Obj *f_flush(ARGS) {
    UNUSED;
    File *f = file_check(argv[0], false, false);
    if (!f) return NULL;
    fflush(f->fp);
    return Py_None;
}
static Obj *f_enter(ARGS) { UNUSED; return file_check(argv[0], false, false) ? argv[0] : NULL; }
static Obj *f_exit(ARGS) { f_close(1, argv, NULL); (void)argc; (void)kwnames; return Py_None; }
static Obj *f_seek(ARGS) {
    UNUSED;
    File *f = file_check(argv[0], false, false);
    int64_t off, whence = 0;
    if (!f || obj_index(argv[1], &off) < 0 || (argc > 2 && obj_index(argv[2], &whence) < 0)) return NULL;
    fseek(f->fp, (long)off, whence == 1 ? SEEK_CUR : whence == 2 ? SEEK_END : SEEK_SET);
    return int_new(ftell(f->fp));
}
static Obj *f_tell(ARGS) {
    UNUSED;
    File *f = file_check(argv[0], false, false);
    return f ? int_new(ftell(f->fp)) : NULL;
}

static Obj *os_error(int e, const char *path) {
    Type *t = e == ENOENT ? E_FileNotFoundError : e == EACCES ? E_PermissionError : e == EISDIR ? E_IsADirectoryError :
              e == EEXIST ? E_FileExistsError : E_OSError;
    err(t, "[Errno %d] %s: '%s'", e, strerror(e), path);
    Exc *x = (Exc *)vm.exc;
    x->dict = (Dict *)dict_new();
    dict_setc(x->dict, "errno", int_new(e));
    dict_setc(x->dict, "strerror", str_c(strerror(e)));
    dict_setc(x->dict, "filename", str_c(path));
    return NULL;
}

static Obj *b_open(ARGS) {
    if (kw_check(kwnames, "open", "mode,buffering,encoding,errors,newline") < 0) return NULL;
    static const char *names[5] = {"mode", "buffering", "encoding", "errors", "newline"};
    Obj *opt[5];
    for (int i = 0; i < 5; i++) {
        opt[i] = argc > i + 1 ? argv[i + 1] : KW(names[i]);
        if (opt[i] == Py_None) opt[i] = NULL;
        if (opt[i] && i != 1 && !IS_STR(opt[i])) return err(E_TypeError, "open() argument '%s' must be str, not %s", names[i], tname(opt[i]));
    }
    if (!IS_STR(argv[0])) return err(E_TypeError, "open() path must be str, not %s", tname(argv[0]));
    const char *mode = opt[0] ? AS_STR(opt[0])->s : "r";
    bool r = false, w = false, a = false, x = false, plus = false;
    for (const char *p = mode; *p; p++) {
        if (*p == 'r') r = true; else if (*p == 'w') w = true; else if (*p == 'a') a = true;
        else if (*p == 'x') x = true; else if (*p == '+') plus = true;
        else if (*p == 'b') return err(E_ValueError, "binary mode is not supported");
        else if (*p != 't') return err(E_ValueError, "invalid mode: '%s'", mode);
    }
    if (r + w + a + x != 1) return err(E_ValueError, "must have exactly one of create/read/write/append mode");
    int64_t buffering = -1;
    if (opt[1] && obj_index(opt[1], &buffering) < 0) return NULL;
    if (buffering == 0) return err(E_ValueError, "can't have unbuffered text I/O");
    int enc = ENC_UTF8, errors = ERR_STRICT;
    if (opt[2]) {
        SB e;
        sb_init(&e);
        for (const char *p = AS_STR(opt[2])->s; *p; p++) if (*p != '-' && *p != '_') sb_putc(&e, (char)tolower((unsigned char)*p));
        sb_putc(&e, 0);
        if (!strcmp(e.s, "utf8")) enc = ENC_UTF8;
        else if (!strcmp(e.s, "ascii") || !strcmp(e.s, "usascii")) enc = ENC_ASCII;
        else if (!strcmp(e.s, "latin1") || !strcmp(e.s, "iso88591")) enc = ENC_LATIN1;
        else { sb_free(&e); return err(E_LookupError, "unknown encoding: %s (supported: utf-8, ascii, latin-1)", AS_STR(opt[2])->s); }
        sb_free(&e);
    }
    if (opt[3]) {
        const char *es = AS_STR(opt[3])->s;
        if (!strcmp(es, "strict")) errors = ERR_STRICT;
        else if (!strcmp(es, "replace")) errors = ERR_REPLACE;
        else if (!strcmp(es, "ignore")) errors = ERR_IGNORE;
        else errors = ERR_UNKNOWN; /* reported when a bad byte or character first needs it */
    }
    bool universal = true;
    const char *wnl = NULL;
    if (opt[4]) {
        const char *nl = AS_STR(opt[4])->s;
        universal = false;
        if (!strcmp(nl, "\r\n")) wnl = "\r\n";
        else if (!strcmp(nl, "\r")) return err(E_ValueError, "newline='\\r' is not supported");
        else if (strcmp(nl, "") && strcmp(nl, "\n")) return err(E_ValueError, "illegal newline value: %s", AS_STR(obj_repr(opt[4]))->s);
    }
    char cm[4] = {r ? 'r' : w ? 'w' : a ? 'a' : 'w', plus ? '+' : (x ? 'x' : 0), plus && x ? 'x' : 0, 0};
    FILE *fp = fopen(AS_STR(argv[0])->s, cm);
    if (!fp) return os_error(errno, AS_STR(argv[0])->s);
    if (buffering == 1) setvbuf(fp, NULL, _IOLBF, 0);
    else if (buffering > 1) setvbuf(fp, NULL, _IOFBF, (size_t)buffering);
    File *f = (File *)file_new(fp, AS_STR(argv[0])->s, r || plus, !r || plus, false);
    f->enc = (uint8_t)enc; f->errors = (uint8_t)errors; f->universal = universal; f->wnl = wnl;
    return (Obj *)f;
}

/* ---------- output and input ---------- */
static Obj *b_print(ARGS) {
    if (kw_check(kwnames, "print", "sep,end,file,flush") < 0) return NULL;
    Obj *sep = KW("sep"), *end = KW("end"), *file = KW("file"), *flush = KW("flush");
    if (sep == Py_None) sep = NULL;
    if (end == Py_None) end = NULL;
    if (file == Py_None) file = NULL;
    if (sep && !IS_STR(sep)) return err(E_TypeError, "sep must be None or a string, not %s", tname(sep));
    if (end && !IS_STR(end)) return err(E_TypeError, "end must be None or a string, not %s", tname(end));
    if (!file && sys_dict) {
        /* print goes to whatever sys.stdout currently is */
        static Str *name;
        if (!name) { name = AS_STR(str_c("stdout")); gc_root((Obj **)&name); }
        Obj *cur = dict_gets(sys_dict, name);
        if (cur && cur != sys_stdout_orig && cur != Py_None) file = cur;
    }
    if (file && KIND(file) != K_FILE) {
        /* Any object with write(): one call per piece, as CPython does. */
        Obj *w = obj_getattr(file, AS_STR(str_c("write")));
        if (!w) return NULL;
        for (int i = 0; i < argc; i++) {
            if (i && !call1(w, sep ? sep : str_c(" "))) return NULL;
            Obj *s = obj_str(argv[i]);
            if (!s || !call1(w, s)) return NULL;
        }
        if (!call1(w, end ? end : str_c("\n"))) return NULL;
        if (flush && obj_truthy(flush) > 0) {
            bool found;
            call_method(file, AS_STR(str_c("flush")), 0, NULL, &found);
            if (vm.exc) return NULL;
        }
        return Py_None;
    }
    SB b;
    sb_init(&b);
    for (int i = 0; i < argc; i++) {
        if (i) { if (sep) sb_putstr(&b, sep); else sb_putc(&b, ' '); }
        Obj *s = obj_str(argv[i]);
        if (!s) { sb_free(&b); return NULL; }
        sb_putstr(&b, s);
    }
    if (end) sb_putstr(&b, end); else sb_putc(&b, '\n');
    if (!file) {
        fwrite(b.s, 1, b.n, stdout);
        sb_free(&b);
        if (flush && obj_truthy(flush) > 0) fflush(stdout);
        return Py_None;
    }
    Obj *text = sb_finish(&b);
    File *f = file_check(file, false, true);
    if (!f) return NULL;
    if (file_write_str(file, AS_STR(text)) < 0) return NULL;
    if (flush && obj_truthy(flush) > 0) fflush(f->fp);
    return Py_None;
}

static Obj *b_input(ARGS) {
    UNUSED;
    if (argc > 0) {
        Obj *s = obj_str(argv[0]);
        if (!s) return NULL;
        fwrite(AS_STR(s)->s, 1, AS_STR(s)->len, stdout);
    }
    fflush(stdout);
    SB b;
    sb_init(&b);
    int c;
    bool any = false;
    while ((c = fgetc(stdin)) != EOF) {
        any = true;
        if (c == '\n') break;
        sb_putc(&b, (char)c);
    }
    if (!any) { sb_free(&b); return err(E_EOFError, "EOF when reading a line"); }
    if (b.n && b.s[b.n - 1] == '\r') b.n--;
    return sb_finish(&b);
}

/* ---------- constructors ---------- */
static Obj *parse_int(Str *s, int base) {
    const char *p = s->s, *e = s->s + s->len;
    while (p < e && isspace((unsigned char)*p)) p++;
    while (e > p && isspace((unsigned char)e[-1])) e--;
    bool neg = false;
    const char *start = p;
    if (p < e && (*p == '+' || *p == '-')) { neg = *p == '-'; p++; }
    if (e - p > 2 && p[0] == '0') {
        int pb = (p[1] | 32) == 'x' ? 16 : (p[1] | 32) == 'o' ? 8 : (p[1] | 32) == 'b' ? 2 : 0;
        if (pb && (base == 0 || base == pb)) { base = pb; p += 2; if (p < e && *p == '_') p++; }
    }
    if (base == 0) base = 10;
    SB b;
    sb_init(&b);
    bool ok = p < e, prev_us = true;
    for (; p < e && ok; p++) {
        char c = *p;
        if (c == '_') { if (prev_us) ok = false; prev_us = true; continue; }
        int d = isdigit((unsigned char)c) ? c - '0' : isalpha((unsigned char)c) ? (c | 32) - 'a' + 10 : 99;
        if (d >= base) ok = false;
        sb_putc(&b, c);
        prev_us = false;
    }
    if (!ok || prev_us || b.n == 0) {
        sb_free(&b);
        (void)start;
        Obj *r = obj_repr((Obj *)s);
        return err(E_ValueError, "invalid literal for int() with base %d: %s", base, AS_STR(r)->s);
    }
    Obj *v = int_from_digits(b.s, b.n, base);
    sb_free(&b);
    return neg ? int_neg(v) : v;
}

static Obj *c_int(ARGS) {
    if (kw_check(kwnames, "int", "base") < 0) return NULL;
    Obj *bo = argc > 1 ? argv[1] : KW("base");
    if (argc == 0) return int_new(0);
    Obj *x = argv[0];
    if (bo) {
        int64_t base;
        if (obj_index(bo, &base) < 0) return NULL;
        if (!IS_STR(x)) return err(E_TypeError, "int() can't convert non-string with explicit base");
        if (base != 0 && (base < 2 || base > 36)) return err(E_ValueError, "int() base must be >= 2 and <= 36, or 0");
        return parse_int(AS_STR(x), (int)base);
    }
    switch (KIND(x)) {
    case K_INT: return x->type == T_int ? x : int_add(x, int_new(0));
    case K_BOOL: return int_new(x == Py_True);
    case K_FLOAT: {
        double d = ((Float *)x)->v;
        if (isnan(d)) return err(E_ValueError, "cannot convert float NaN to integer");
        if (isinf(d)) return err(E_OverflowError, "cannot convert float infinity to integer");
        return int_from_double(d);
    }
    case K_STR: return parse_int(AS_STR(x), 10);
    default:
        if (x->type->user) {
            Obj *f = type_lookup(x->type, N_int);
            if (!f) f = type_lookup(x->type, N_index);
            if (f) {
                Obj *r = call1(f, x);
                if (r && !IS_INT(r)) return err(E_TypeError, "__int__ returned non-int (type %s)", tname(r));
                return r;
            }
        }
        return err(E_TypeError, "int() argument must be a string, a bytes-like object or a real number, not '%s'", tname(x));
    }
}

static Obj *c_float(ARGS) {
    UNUSED;
    if (argc == 0) return float_new(0.0);
    Obj *x = argv[0];
    if (KIND(x) == K_FLOAT) return x->type == T_float ? x : float_new(((Float *)x)->v);
    if (IS_STR(x)) {
        Str *s = AS_STR(x);
        SB b;
        sb_init(&b);
        const char *p = s->s, *e = s->s + s->len;
        while (p < e && isspace((unsigned char)*p)) p++;
        while (e > p && isspace((unsigned char)e[-1])) e--;
        bool ok = p < e;
        for (const char *q = p; q < e; q++) {
            if (*q == '_') { if (q == p || q + 1 == e || !isdigit((unsigned char)q[-1]) || !isdigit((unsigned char)q[1])) ok = false; continue; }
            if ((*q | 32) == 'x' || (*q | 32) == 'p') ok = false; /* no hex floats */
            sb_putc(&b, *q);
        }
        sb_putc(&b, 0);
        char *endp = b.s;
        double d = ok ? strtod(b.s, &endp) : 0;
        if (!ok || *endp || endp == b.s) {
            sb_free(&b);
            Obj *r = obj_repr(x);
            return err(E_ValueError, "could not convert string to float: %s", AS_STR(r)->s);
        }
        sb_free(&b);
        return float_new(d);
    }
    double d;
    if (obj_as_double(x, &d) < 0) {
        if (err_matches(E_TypeError)) {
            err_clear();
            return err(E_TypeError, "float() argument must be a string or a real number, not '%s'", tname(x));
        }
        return NULL;
    }
    return float_new(d);
}

static Obj *c_bool(ARGS) {
    UNUSED;
    if (argc == 0) return Py_False;
    int t = obj_truthy(argv[0]);
    return t < 0 ? NULL : BOOL(t);
}
static Obj *c_str(ARGS) { UNUSED; return argc == 0 ? str_new("", 0) : obj_str(argv[0]); }
static Obj *c_list(ARGS) { UNUSED; return argc == 0 ? list_new(0) : seq_to_list(argv[0]); }
static Obj *c_tuple(ARGS) {
    UNUSED;
    if (argc == 0) return tuple_new(0);
    if (KIND(argv[0]) == K_TUPLE && argv[0]->type != T_tuple) return tuple_from(((Tuple *)argv[0])->items, ((Tuple *)argv[0])->n);
    return seq_to_tuple(argv[0]);
}
static Obj *c_dict(ARGS) {
    Dict *d = (Dict *)dict_new();
    if (argc > 1) return err(E_TypeError, "dict expected at most 1 argument, got %d", argc);
    if (argc == 1 && dict_update(d, argv[0]) < 0) return NULL;
    for (int i = 0; i < NKW; i++) dict_sets(d, AS_STR(((Tuple *)kwnames)->items[i]), argv[argc + i]);
    return (Obj *)d;
}
static Obj *make_set(Type *t, int argc, Obj **argv) {
    Set *s = (Set *)set_new(t);
    if (argc == 1 && set_update(s, argv[0]) < 0) return NULL;
    return (Obj *)s;
}
static Obj *c_set(ARGS) { (void)kwnames; return make_set(T_set, argc, argv); }
static Obj *c_frozenset(ARGS) { (void)kwnames; return make_set(T_frozenset, argc, argv); }

static Obj *c_range(ARGS) {
    UNUSED;
    int64_t a = 0, b, c = 1;
    if (argc < 1 || argc > 3) return err(E_TypeError, "range expected 1 to 3 arguments, got %d", argc);
    for (int i = 0; i < argc; i++)
        if (!IS_INT(argv[i]) && !(argv[i]->type->user && type_lookup(argv[i]->type, N_index)))
            return err(E_TypeError, "'%s' object cannot be interpreted as an integer", tname(argv[i]));
    if (argc == 1) { if (obj_index(argv[0], &b) < 0) goto big; }
    else {
        if (obj_index(argv[0], &a) < 0 || obj_index(argv[1], &b) < 0) goto big;
        if (argc == 3 && obj_index(argv[2], &c) < 0) goto big;
    }
    if (c == 0) return err(E_ValueError, "range() arg 3 must not be zero");
    return range_new(a, b, c);
big:
    err_clear();
    return err(E_OverflowError, "range() bounds beyond 64 bits are not supported");
}

static Obj *c_slice(ARGS) {
    UNUSED;
    if (argc == 1) return slice_new(Py_None, argv[0], Py_None);
    if (argc == 2) return slice_new(argv[0], argv[1], Py_None);
    if (argc == 3) return slice_new(argv[0], argv[1], argv[2]);
    return err(E_TypeError, "slice expected 1 to 3 arguments, got %d", argc);
}

static Obj *c_property(ARGS) {
    if (kw_check(kwnames, "property", "fget,fset,fdel,doc") < 0) return NULL;
    Property *p = gc_alloc(T_property, sizeof(Property));
    p->fget = argc > 0 ? argv[0] : KW("fget") ? KW("fget") : Py_None;
    p->fset = argc > 1 ? argv[1] : KW("fset") ? KW("fset") : Py_None;
    p->fdel = argc > 2 ? argv[2] : KW("fdel") ? KW("fdel") : Py_None;
    p->doc = argc > 3 ? argv[3] : KW("doc");
    return (Obj *)p;
}
static Obj *prop_with(Obj *self, int which, Obj *fn) {
    Property *o = (Property *)self, *p = gc_alloc(T_property, sizeof(Property));
    *p = *o;
    p->h.mark = 0;
    if (which == 0) p->fget = fn; else if (which == 1) p->fset = fn; else p->fdel = fn;
    return (Obj *)p;
}
static Obj *prop_getter(ARGS) { UNUSED; return prop_with(argv[0], 0, argv[1]); }
static Obj *prop_setter(ARGS) { UNUSED; return prop_with(argv[0], 1, argv[1]); }
static Obj *prop_deleter(ARGS) { UNUSED; return prop_with(argv[0], 2, argv[1]); }

static Obj *make_wrapper(Type *t, int argc, Obj **argv) {
    if (argc != 1) return err(E_TypeError, "%s expected 1 argument, got %d", t->name->s, argc);
    Wrapper *w = gc_alloc(t, sizeof(Wrapper));
    w->fn = argv[0];
    return (Obj *)w;
}
static Obj *c_staticmethod(ARGS) { (void)kwnames; return make_wrapper(T_staticmethod, argc, argv); }
static Obj *c_classmethod(ARGS) { (void)kwnames; return make_wrapper(T_classmethod, argc, argv); }

static Obj *c_super(ARGS) {
    UNUSED;
    Super *s = gc_alloc(T_super, sizeof(Super));
    if (argc == 2) {
        if (KIND(argv[0]) != K_TYPE) return err(E_TypeError, "super() argument 1 must be a type, not %s", tname(argv[0]));
        s->cls = (Type *)argv[0]; s->self = argv[1];
        return (Obj *)s;
    }
    if (argc != 0) return err(E_TypeError, "super() takes 0 or 2 arguments");
    Frame *f = vm.frame;
    if (!f) return err(E_RuntimeError, "super(): no current frame");
    Code *co = f->code;
    Obj *cls = NULL;
    for (int i = 0; i < co->nfree; i++)
        if (str_eq(AS_STR(co->cellnames->items[co->ncells + i]), N_class)) cls = f->cells[co->ncells + i]->value;
    if (!cls || KIND(cls) != K_TYPE) return err(E_RuntimeError, "super(): __class__ cell not found");
    if (co->argcount == 0 || !f->fast[0]) return err(E_RuntimeError, "super(): no arguments");
    s->cls = (Type *)cls; s->self = f->fast[0];
    return (Obj *)s;
}

/* ---------- object and exception base methods ---------- */
static Obj *o_init(ARGS) { UNUSED; return Py_None; }
static Obj *o_new(ARGS) {
    UNUSED;
    if (argc < 1 || KIND(argv[0]) != K_TYPE) return err(E_TypeError, "object.__new__(X): X is not a type object");
    Type *t = (Type *)argv[0];
    if (t->kind == K_EXC) return exc_new(t, (Tuple *)tuple_from(argv + 1, argc - 1));
    if (t->kind != K_INSTANCE) return err(E_TypeError, "object.__new__(%s) is not safe", t->name->s);
    Instance *in = gc_alloc(t, sizeof(Instance));
    in->dict = (Dict *)dict_new();
    return (Obj *)in;
}
static Obj *default_repr(Obj *o) {
    Obj *saved = repr_force_default;
    repr_force_default = o;
    Obj *r = obj_repr(o);
    repr_force_default = saved;
    return r;
}
static Obj *o_repr(ARGS) { UNUSED; return default_repr(argv[0]); }
static Obj *o_str(ARGS) {
    UNUSED;
    Obj *o = argv[0];
    if (KIND(o) == K_EXC) {
        Exc *e = (Exc *)o;
        if (e->args->n == 0) return str_new("", 0);
        if (e->args->n == 1) return isinstance(o, E_KeyError) ? obj_repr(e->args->items[0]) : obj_str(e->args->items[0]);
        return obj_repr((Obj *)e->args);
    }
    if (KIND(o) == K_INSTANCE) return obj_repr(o);
    raw_obj = o;
    Obj *r = obj_str(o);
    raw_obj = NULL;
    return r;
}

/* ---------- plain built-in behavior, callable as methods ----------
   These run the built-in operation even when self is an instance of a Python
   subclass that overrides it, which is what super().__getitem__(k) needs. */
#define RAW(expr) (raw_obj = argv[0], r = (expr), raw_obj = NULL, r)
static Obj *not_impl_on_type_error(Obj *r) {
    if (!r && err_matches(E_TypeError)) { err_clear(); return Py_NotImplemented; }
    return r;
}
static Obj *r_len(ARGS) { UNUSED; int64_t r; (void)RAW(obj_len(argv[0])); return r < 0 ? NULL : int_new(r); }
static Obj *r_getitem(ARGS) { UNUSED; Obj *r; return RAW(obj_getitem(argv[0], argv[1])); }
static Obj *r_setitem(ARGS) { UNUSED; int r; return RAW(obj_setitem(argv[0], argv[1], argv[2])) < 0 ? NULL : Py_None; }
static Obj *r_delitem(ARGS) { UNUSED; int r; return RAW(obj_delitem(argv[0], argv[1])) < 0 ? NULL : Py_None; }
static Obj *r_contains(ARGS) { UNUSED; int r; (void)RAW(obj_contains(argv[0], argv[1])); return r < 0 ? NULL : BOOL(r); }
static Obj *r_iter(ARGS) { UNUSED; Obj *r; return RAW(obj_iter(argv[0])); }
static Obj *r_bool(ARGS) { UNUSED; int r; (void)RAW(obj_truthy(argv[0])); return r < 0 ? NULL : BOOL(r); }
static Obj *r_hash(ARGS) {
    UNUSED;
    uint64_t h;
    int r;
    if (RAW(obj_hash(argv[0], &h)) < 0) return NULL;
    return int_new((int64_t)h);
}
#define RAWCMP(name, OP) \
    static Obj *r_##name(ARGS) { UNUSED; Obj *r; return not_impl_on_type_error(RAW(obj_richcmp(argv[0], argv[1], OP))); }
RAWCMP(eq, CMP_EQ) RAWCMP(ne, CMP_NE) RAWCMP(lt, CMP_LT) RAWCMP(le, CMP_LE) RAWCMP(gt, CMP_GT) RAWCMP(ge, CMP_GE)
#define RAWBIN(name, OP) \
    static Obj *rb_##name(ARGS) { UNUSED; Obj *r; return not_impl_on_type_error(RAW(binary_op(OP, argv[0], argv[1], false))); } \
    static Obj *rr_##name(ARGS) { UNUSED; Obj *r; return not_impl_on_type_error(RAW(binary_op(OP, argv[1], argv[0], false))); } \
    static Obj *ri_##name(ARGS) { UNUSED; Obj *r; return not_impl_on_type_error(RAW(binary_op(OP, argv[0], argv[1], true))); }
#define BINOPS(X) X(add, OP_ADD) X(sub, OP_SUB) X(mul, OP_MUL) X(truediv, OP_TRUEDIV) X(floordiv, OP_FLOORDIV) \
    X(mod, OP_MOD) X(pow, OP_POW) X(lshift, OP_LSHIFT) X(rshift, OP_RSHIFT) X(and, OP_AND) X(or, OP_OR) X(xor, OP_XOR)
BINOPS(RAWBIN)
static Obj *r_neg(ARGS) { UNUSED; Obj *r; return RAW(unary_op('-', argv[0])); }
static Obj *r_pos(ARGS) { UNUSED; Obj *r; return RAW(unary_op('+', argv[0])); }
static Obj *r_invert(ARGS) { UNUSED; Obj *r; return RAW(unary_op('~', argv[0])); }

static Obj *type_new_static(ARGS) {
    if (argc < 1 || KIND(argv[0]) != K_TYPE) return err(E_TypeError, "__new__(X): X is not a type object");
    Type *t = (Type *)argv[0], *bt = builtin_base(t);
    if (!bt) return err(E_TypeError, "__new__(%s): not a subclass of a built-in type", t->name->s);
    bool mutable = bt == T_list || bt == T_dict || bt == T_set;
    Obj *base = mutable ? bt->ctor(0, NULL, NULL) : bt->ctor(argc - 1, argv + 1, kwnames);
    if (!base || !t->user) return base;
    return subclass_instance(t, base);
}
static Obj *list_init(ARGS) {
    UNUSED;
    if (KIND(argv[0]) != K_LIST) return err(E_TypeError, "list.__init__ needs a list");
    ((List *)argv[0])->n = 0;
    if (argc > 1 && list_extend((List *)argv[0], argv[1]) < 0) return NULL;
    return Py_None;
}
static Obj *dict_init(ARGS) {
    if (KIND(argv[0]) != K_DICT) return err(E_TypeError, "dict.__init__ needs a dict");
    if (argc > 2) return err(E_TypeError, "dict expected at most 1 argument, got %d", argc - 1);
    if (argc == 2 && dict_update((Dict *)argv[0], argv[1]) < 0) return NULL;
    for (int i = 0; i < NKW; i++)
        if (dict_set((Dict *)argv[0], ((Tuple *)kwnames)->items[i], argv[argc + i]) < 0) return NULL;
    return Py_None;
}
static Obj *set_init(ARGS) {
    UNUSED;
    if (KIND(argv[0]) != K_SET) return err(E_TypeError, "set.__init__ needs a set");
    dict_clear(((Set *)argv[0])->d);
    if (argc > 1 && set_update((Set *)argv[0], argv[1]) < 0) return NULL;
    return Py_None;
}
static Obj *o_eq(ARGS) { UNUSED; return argv[0] == argv[1] ? Py_True : Py_NotImplemented; }
static Obj *o_ne(ARGS) { UNUSED; return argv[0] != argv[1] ? Py_True : Py_NotImplemented; }
static Obj *o_hash(ARGS) { UNUSED; return int_new((int64_t)((uintptr_t)argv[0] >> 4)); }
static Obj *o_setattr(ARGS) {
    UNUSED;
    if (!IS_STR(argv[1])) return err(E_TypeError, "attribute name must be string");
    return obj_setattr_direct(argv[0], AS_STR(argv[1]), argv[2]) < 0 ? NULL : Py_None;
}
static Obj *o_delattr(ARGS) {
    UNUSED;
    if (!IS_STR(argv[1])) return err(E_TypeError, "attribute name must be string");
    return obj_setattr_direct(argv[0], AS_STR(argv[1]), NULL) < 0 ? NULL : Py_None;
}
static Obj *o_format(ARGS) {
    UNUSED;
    if (!IS_STR(argv[1])) return err(E_TypeError, "format spec must be str");
    if (KIND(argv[0]) != K_INSTANCE && KIND(argv[0]) != K_EXC) {
        raw_obj = argv[0];
        Obj *r = obj_format(argv[0], AS_STR(argv[1]));
        raw_obj = NULL;
        return r;
    }
    if (AS_STR(argv[1])->len) return err(E_TypeError, "unsupported format string passed to %s.__format__", tname(argv[0]));
    return obj_str(argv[0]);
}
static Obj *e_init(ARGS) {
    UNUSED;
    if (KIND(argv[0]) != K_EXC) return err(E_TypeError, "descriptor '__init__' requires an exception object");
    ((Exc *)argv[0])->args = (Tuple *)tuple_from(argv + 1, argc - 1);
    return Py_None;
}
static Obj *e_with_traceback(ARGS) { UNUSED; return argv[0]; }
static Obj *e_add_note(ARGS) {
    UNUSED;
    Exc *e = (Exc *)argv[0];
    if (!IS_STR(argv[1])) return err(E_TypeError, "note must be a str, not '%s'", tname(argv[1]));
    if (!e->dict) e->dict = (Dict *)dict_new();
    Obj *notes = dict_getc(e->dict, "__notes__");
    if (!notes || KIND(notes) != K_LIST) { notes = list_new(0); dict_setc(e->dict, "__notes__", notes); }
    list_append((List *)notes, argv[1]);
    return Py_None;
}

/* ---------- generators ---------- */
static Obj *stop_iteration(Obj *ret) {
    err_set(exc_new(E_StopIteration, (Tuple *)(ret == Py_None ? tuple_new(0) : tuple_pack(1, ret))));
    return NULL;
}
static Obj *g_send(ARGS) {
    UNUSED;
    Obj *ret, *v = gen_send((Generator *)argv[0], argv[1], &ret);
    if (!v && !vm.exc) return stop_iteration(ret);
    return v;
}
static Obj *g_next(ARGS) {
    UNUSED;
    Obj *ret, *v = gen_send((Generator *)argv[0], Py_None, &ret);
    if (!v && !vm.exc) return stop_iteration(ret);
    return v;
}
static Obj *g_iter(ARGS) { UNUSED; return argv[0]; }
static Obj *g_throw(ARGS) {
    UNUSED;
    Obj *e = argv[1];
    if (KIND(e) == K_TYPE) e = argc > 2 ? call1(e, argv[2]) : call0(e);
    if (!e) return NULL;
    if (!isinstance(e, E_BaseException)) return err(E_TypeError, "exceptions must be classes or instances deriving from BaseException");
    Obj *ret, *v = gen_throw((Generator *)argv[0], e, &ret);
    if (!v && !vm.exc) return stop_iteration(ret);
    return v;
}
static Obj *g_close(ARGS) {
    UNUSED;
    Generator *g = (Generator *)argv[0];
    if (g->frame->state == FS_NEW || g->frame->state == FS_DONE) { g->frame->state = FS_DONE; return Py_None; }
    Obj *ret, *v = gen_throw(g, exc_new(E_GeneratorExit, (Tuple *)tuple_new(0)), &ret);
    if (v) return err(E_RuntimeError, "generator ignored GeneratorExit");
    if (vm.exc && (err_matches(E_GeneratorExit) || err_matches(E_StopIteration))) err_clear();
    return vm.exc ? NULL : Py_None;
}

/* ---------- functions ---------- */
static Obj *b_len(ARGS) { UNUSED; int64_t n = obj_len(argv[0]); return n < 0 ? NULL : int_new(n); }
static Obj *b_repr(ARGS) { UNUSED; return obj_repr(argv[0]); }
static Obj *b_ascii(ARGS) {
    UNUSED;
    Obj *r = obj_repr(argv[0]);
    if (!r) return NULL;
    Str *s = AS_STR(r);
    if (s->len == s->nchars) return r;
    SB b;
    sb_init(&b);
    const char *p = s->s, *e = s->s + s->len;
    while (p < e) {
        uint32_t cp = utf8_next(&p);
        if (cp < 128) sb_putc(&b, (char)cp);
        else if (cp < 256) sb_printf(&b, "\\x%02x", cp);
        else if (cp < 65536) sb_printf(&b, "\\u%04x", cp);
        else sb_printf(&b, "\\U%08x", cp);
    }
    return sb_finish(&b);
}

static int class_check(Obj *o, Obj *cls, bool sub, const char *fn) {
    if (KIND(cls) == K_TUPLE) {
        Tuple *t = (Tuple *)cls;
        for (int64_t i = 0; i < t->n; i++) {
            int r = class_check(o, t->items[i], sub, fn);
            if (r) return r;
        }
        return 0;
    }
    if (KIND(cls) != K_TYPE) {
        err(E_TypeError, "%s() arg 2 must be a type, a tuple of types, or a union", fn);
        return -1;
    }
    return sub ? type_issub((Type *)o, (Type *)cls) : isinstance(o, (Type *)cls);
}
static Obj *b_isinstance(ARGS) {
    UNUSED;
    int r = class_check(argv[0], argv[1], false, "isinstance");
    return r < 0 ? NULL : BOOL(r);
}
static Obj *b_issubclass(ARGS) {
    UNUSED;
    if (KIND(argv[0]) != K_TYPE) return err(E_TypeError, "issubclass() arg 1 must be a class");
    int r = class_check(argv[0], argv[1], true, "issubclass");
    return r < 0 ? NULL : BOOL(r);
}

/* Hidden helpers for "def f[T]" and "class C[T]"; the names start with a dot so no
   program can spell them. The work is done in lib/typing.py. */
static Obj *typing_call(const char *fn, int argc, Obj **argv) {
    Obj *m = import_module(AS_STR(str_c("typing")));
    if (!m) return NULL;
    Obj *f = obj_getattr(m, AS_STR(str_c(fn)));
    if (!f) return NULL;
    return obj_call(f, argc, argv, NULL);
}
static Obj *b_type_param(ARGS) { UNUSED; return typing_call("_type_param", argc, argv); }
static Obj *b_generic_base(ARGS) { UNUSED; return typing_call("_generic_base", argc, argv); }

static Obj *b_abs(ARGS) {
    UNUSED;
    Obj *x = argv[0];
    if (IS_INT(x)) return int_sign(x) < 0 ? int_neg(x) : (KIND(x) == K_BOOL ? int_new(x == Py_True) : x);
    if (KIND(x) == K_FLOAT) return float_new(fabs(((Float *)x)->v));
    if (x->type->user) { Obj *f = type_lookup(x->type, N_abs); if (f) return call1(f, x); }
    return err(E_TypeError, "bad operand type for abs(): '%s'", tname(x));
}

static Obj *minmax(int argc, Obj **argv, Obj *kwnames, int op, const char *name) {
    if (kw_check(kwnames, name, "key,default") < 0) return NULL;
    Obj *key = KW("key"), *def = KW("default"), *best = NULL, *bestk = NULL, *it, *v;
    if (key == Py_None) key = NULL;
    if (argc == 0) return err(E_TypeError, "%s expected at least 1 argument, got 0", name);
    if (argc == 1) it = obj_iter(argv[0]);
    else it = obj_iter(tuple_from(argv, argc));
    if (!it) return NULL;
    while ((v = iter_next(it))) {
        Obj *k = key ? call1(key, v) : v;
        if (!k) return NULL;
        if (!best) { best = v; bestk = k; continue; }
        int c = obj_cmp(k, bestk, op);
        if (c < 0) return NULL;
        if (c) { best = v; bestk = k; }
    }
    if (vm.exc) return NULL;
    if (!best) {
        if (def) return def;
        return err(E_ValueError, "%s() iterable argument is empty", name);
    }
    return best;
}
static Obj *b_min(ARGS) { return minmax(argc, argv, kwnames, CMP_LT, "min"); }
static Obj *b_max(ARGS) { return minmax(argc, argv, kwnames, CMP_GT, "max"); }

static Obj *b_sum(ARGS) {
    if (kw_check(kwnames, "sum", "start") < 0) return NULL;
    Obj *acc = argc > 1 ? argv[1] : KW("start") ? KW("start") : int_new(0);
    if (IS_STR(acc)) return err(E_TypeError, "sum() can't sum strings [use ''.join(seq) instead]");
    Obj *it = obj_iter(argv[0]), *v;
    if (!it) return NULL;
    /* Floats are added with Neumaier compensation, as CPython does. */
    bool fmode = false;
    double f = 0, c = 0;
    while ((v = iter_next(it))) {
        if (!fmode && KIND(acc) == K_FLOAT && (KIND(v) == K_FLOAT || KIND(v) == K_INT)) { fmode = true; f = ((Float *)acc)->v; c = 0; }
        if (!fmode && IS_INT(acc) && KIND(v) == K_FLOAT) {
            bool of;
            f = int_to_double(acc, &of);
            if (!of) { fmode = true; c = 0; }
        }
        if (fmode) {
            if (KIND(v) == K_FLOAT) {
                double x = ((Float *)v)->v, t = f + x;
                c += fabs(f) >= fabs(x) ? (f - t) + x : (x - t) + f;
                f = t;
                continue;
            }
            int64_t iv;
            if (KIND(v) == K_INT && int_fits(v, &iv)) { f += (double)iv; continue; }
            if (c && isfinite(c)) f += c;
            acc = float_new(f);
            fmode = false;
        }
        acc = binary_op(OP_ADD, acc, v, false);
        if (!acc) return NULL;
    }
    if (vm.exc) return NULL;
    if (fmode) { if (c && isfinite(c)) f += c; return float_new(f); }
    return acc;
}

static Obj *b_sorted(ARGS) {
    if (kw_check(kwnames, "sorted", "key,reverse") < 0) return NULL;
    if (argc != 1) return err(E_TypeError, "sorted expected 1 argument, got %d", argc);
    List *l = (List *)seq_to_list(argv[0]);
    if (!l) return NULL;
    Obj *rev = KW("reverse");
    int r = rev ? obj_truthy(rev) : 0;
    if (r < 0 || list_sort(l, KW("key"), r) < 0) return NULL;
    return (Obj *)l;
}

static Obj *b_reversed(ARGS) {
    UNUSED;
    Obj *o = argv[0];
    Iter *it;
    switch (KIND(o)) {
    case K_LIST: it = (Iter *)iter_new(IT_REV, o, NULL); it->i = ((List *)o)->n - 1; return (Obj *)it;
    case K_TUPLE: it = (Iter *)iter_new(IT_REV, o, NULL); it->i = ((Tuple *)o)->n - 1; return (Obj *)it;
    case K_RANGE: {
        Range *r = (Range *)o;
        int64_t n = range_len(r);
        it = (Iter *)iter_new(IT_RANGE, NULL, NULL);
        it->i = r->start + (n - 1) * r->step; it->stop = r->start - r->step; it->step = -r->step;
        if (n == 0) { it->i = 0; it->stop = 0; it->step = 1; }
        return (Obj *)it;
    }
    case K_STR: case K_DICT: case K_DICTVIEW: {
        List *l = (List *)seq_to_list(o);
        if (!l) return NULL;
        it = (Iter *)iter_new(IT_REV, (Obj *)l, NULL);
        it->i = l->n - 1;
        return (Obj *)it;
    }
    default:
        if (o->type->user) {
            Obj *f = type_lookup(o->type, N_reversed);
            if (f) return call1(f, o);
            if (type_lookup(o->type, N_len) && type_lookup(o->type, N_getitem)) {
                int64_t n = obj_len(o);
                if (n < 0) return NULL;
                List *l = (List *)list_new(0);
                for (int64_t i = n - 1; i >= 0; i--) {
                    Obj *v = obj_getitem(o, int_new(i));
                    if (!v) return NULL;
                    list_append(l, v);
                }
                return obj_iter((Obj *)l);
            }
        }
        return err(E_TypeError, "'%s' object is not reversible", tname(o));
    }
}

static Obj *b_enumerate(ARGS) {
    if (kw_check(kwnames, "enumerate", "start") < 0) return NULL;
    Obj *it = obj_iter(argv[0]), *start = argc > 1 ? argv[1] : KW("start");
    if (!it) return NULL;
    if (start && !IS_INT(start)) return err(E_TypeError, "'%s' object cannot be interpreted as an integer", tname(start));
    return iter_new(IT_ENUM, it, start);
}

static Obj *iters_tuple(int argc, Obj **argv) {
    Tuple *t = (Tuple *)tuple_new(argc);
    for (int i = 0; i < argc; i++) {
        t->items[i] = obj_iter(argv[i]);
        if (!t->items[i]) { t->n = i; return NULL; }
    }
    return (Obj *)t;
}
static Obj *b_zip(ARGS) {
    if (kw_check(kwnames, "zip", "strict") < 0) return NULL;
    Obj *t = iters_tuple(argc, argv), *strict = KW("strict");
    if (!t) return NULL;
    Iter *it = (Iter *)iter_new(IT_ZIP, t, NULL);
    if (strict) {
        int st = obj_truthy(strict);
        if (st < 0) return NULL;
        it->sub = st;
    }
    return (Obj *)it;
}
static Obj *b_map(ARGS) {
    UNUSED;
    Obj *t = iters_tuple(argc - 1, argv + 1);
    return t ? iter_new(IT_MAP, argv[0], t) : NULL;
}
static Obj *b_filter(ARGS) {
    UNUSED;
    Obj *it = obj_iter(argv[1]);
    return it ? iter_new(IT_FILTER, argv[0], it) : NULL;
}

static Obj *anyall(Obj *iterable, bool want) {
    Obj *it = obj_iter(iterable), *v;
    if (!it) return NULL;
    while ((v = iter_next(it))) {
        int t = obj_truthy(v);
        if (t < 0) return NULL;
        if ((t != 0) == want) return BOOL(want);
    }
    return vm.exc ? NULL : BOOL(!want);
}
static Obj *b_any(ARGS) { UNUSED; return anyall(argv[0], true); }
static Obj *b_all(ARGS) { UNUSED; return anyall(argv[0], false); }

static Obj *b_ord(ARGS) {
    UNUSED;
    if (!IS_STR(argv[0])) return err(E_TypeError, "ord() expected string of length 1, but %s found", tname(argv[0]));
    Str *s = AS_STR(argv[0]);
    if (s->nchars != 1) return err(E_TypeError, "ord() expected a character, but string of length %u found", s->nchars);
    const char *p = s->s;
    return int_new(utf8_next(&p));
}
static Obj *b_chr(ARGS) {
    UNUSED;
    int64_t i;
    if (obj_index(argv[0], &i) < 0) return NULL;
    if (i < 0 || i > 0x10FFFF) return err(E_ValueError, "chr() arg not in range(0x110000)");
    return str_chr((uint32_t)i);
}

static Obj *int_base(Obj *x, int base, const char *prefix) {
    if (!IS_INT(x)) {
        int64_t i;
        if (obj_index(x, &i) < 0) return NULL;
        x = int_new(i);
    }
    SB b, d;
    sb_init(&b); sb_init(&d);
    int_format(&d, x, base, false);
    size_t skip = d.n && d.s[0] == '-' ? 1 : 0;
    if (skip) sb_putc(&b, '-');
    sb_puts(&b, prefix);
    sb_put(&b, d.s + skip, d.n - skip);
    sb_free(&d);
    return sb_finish(&b);
}
static Obj *b_hex(ARGS) { UNUSED; return int_base(argv[0], 16, "0x"); }
static Obj *b_bin(ARGS) { UNUSED; return int_base(argv[0], 2, "0b"); }
static Obj *b_oct(ARGS) { UNUSED; return int_base(argv[0], 8, "0o"); }

static Obj *b_round(ARGS) {
    if (kw_check(kwnames, "round", "ndigits") < 0) return NULL;
    Obj *x = argv[0], *nd = argc > 1 ? argv[1] : KW("ndigits");
    if (nd == Py_None) nd = NULL;
    int64_t n = 0;
    if (nd && obj_index(nd, &n) < 0) return NULL;
    if (IS_INT(x)) {
        if (KIND(x) == K_BOOL) x = int_new(x == Py_True);
        if (!nd || n >= 0) return x;
        Obj *p = int_pow(int_new(10), int_new(-n)), *q, *r;
        if (!p || int_divmod(x, p, &q, &r) < 0) return NULL;
        int c = int_cmp(int_mul(r, int_new(2)), p);
        Obj *odd;
        int_divmod(q, int_new(2), NULL, &odd);
        if (c > 0 || (c == 0 && int_sign(odd) != 0)) q = int_add(q, int_new(1));
        return int_mul(q, p);
    }
    if (KIND(x) == K_FLOAT) {
        double d = ((Float *)x)->v;
        if (!nd) {
            if (isnan(d)) return err(E_ValueError, "cannot convert float NaN to integer");
            if (isinf(d)) return err(E_OverflowError, "cannot convert float infinity to integer");
            return int_from_double(nearbyint(d));
        }
        if (!isfinite(d) || n > 320) return x;
        if (n < -320) return float_new(copysign(0.0, d));
        if (n >= 0) {
            char buf[512];
            snprintf(buf, sizeof buf, "%.*f", (int)n, d);
            return float_new(strtod(buf, NULL));
        }
        double p = pow(10.0, (double)-n);
        return float_new(nearbyint(d / p) * p);
    }
    if (x->type->user) {
        Obj *f = type_lookup(x->type, N_round);
        if (f) return nd ? call2(f, x, nd) : call1(f, x);
    }
    return err(E_TypeError, "type %s doesn't define __round__ method", tname(x));
}

static Obj *b_divmod(ARGS) {
    UNUSED;
    Obj *q = binary_op(OP_FLOORDIV, argv[0], argv[1], false);
    if (!q) {
        if (err_matches(E_ZeroDivisionError) && IS_INT(argv[0]) && IS_INT(argv[1])) {
            err_clear();
            return err(E_ZeroDivisionError, "integer division or modulo by zero");
        }
        if (err_matches(E_ZeroDivisionError)) { err_clear(); return err(E_ZeroDivisionError, "float divmod()"); }
        return NULL;
    }
    Obj *r = binary_op(OP_MOD, argv[0], argv[1], false);
    return r ? tuple_pack(2, q, r) : NULL;
}

static Obj *b_pow(ARGS) {
    UNUSED;
    if (argc == 2 || argv[2] == Py_None) return binary_op(OP_POW, argv[0], argv[1], false);
    Obj *b = argv[0], *e = argv[1], *m = argv[2];
    if (!IS_INT(b) || !IS_INT(e) || !IS_INT(m))
        return err(E_TypeError, "pow() 3rd argument not allowed unless all arguments are integers");
    if (int_sign(m) == 0) return err(E_ValueError, "pow() 3rd argument cannot be 0");
    if (int_sign(e) < 0) return err(E_ValueError, "pow() with a negative exponent and a modulus is not supported");
    Obj *result = int_new(1), *r;
    int_divmod(result, m, NULL, &result);
    int_divmod(b, m, NULL, &b);
    int64_t bits = int_bit_length(e);
    for (int64_t i = bits - 1; i >= 0; i--) {
        int_divmod(int_mul(result, result), m, NULL, &result);
        r = int_bitop('&', int_rshift(e, int_new(i)), int_new(1));
        if (int_sign(r)) int_divmod(int_mul(result, b), m, NULL, &result);
    }
    return result;
}

static Obj *b_hash(ARGS) {
    UNUSED;
    uint64_t h;
    if (obj_hash(argv[0], &h) < 0) return NULL;
    return int_new((int64_t)h);
}
static Obj *b_id(ARGS) { UNUSED; return int_new((int64_t)(uintptr_t)argv[0]); }
static Obj *b_iter(ARGS) { UNUSED; return obj_iter(argv[0]); }
static Obj *b_next(ARGS) {
    UNUSED;
    Obj *it = argv[0], *v, *ret = Py_None;
    if (KIND(it) == K_GEN) v = gen_send((Generator *)it, Py_None, &ret);
    else v = iter_next(it);
    if (v) return v;
    if (vm.exc) return NULL;
    if (argc > 1) return argv[1];
    return stop_iteration(ret);
}
static Obj *b_callable(ARGS) {
    UNUSED;
    switch (KIND(argv[0])) {
    case K_FUNC: case K_BUILTIN: case K_METHOD: case K_TYPE: return Py_True;
    default: return BOOL(argv[0]->type->user && type_lookup(argv[0]->type, N_call));
    }
}
static Obj *b_getattr(ARGS) {
    UNUSED;
    if (!IS_STR(argv[1])) return err(E_TypeError, "attribute name must be string, not '%s'", tname(argv[1]));
    if (argc < 3) return obj_getattr(argv[0], AS_STR(argv[1]));
    Obj *v = obj_getattr_opt(argv[0], AS_STR(argv[1]));
    if (v) return v;
    if (vm.exc && !err_matches(E_AttributeError)) return NULL;
    err_clear();
    return argv[2];
}
static Obj *b_hasattr(ARGS) {
    UNUSED;
    if (!IS_STR(argv[1])) return err(E_TypeError, "attribute name must be string, not '%s'", tname(argv[1]));
    Obj *v = obj_getattr_opt(argv[0], AS_STR(argv[1]));
    if (v) return Py_True;
    if (vm.exc && !err_matches(E_AttributeError)) return NULL;
    err_clear();
    return Py_False;
}
static Obj *b_setattr(ARGS) {
    UNUSED;
    if (!IS_STR(argv[1])) return err(E_TypeError, "attribute name must be string, not '%s'", tname(argv[1]));
    return obj_setattr(argv[0], AS_STR(argv[1]), argv[2]) < 0 ? NULL : Py_None;
}
static Obj *b_delattr(ARGS) {
    UNUSED;
    if (!IS_STR(argv[1])) return err(E_TypeError, "attribute name must be string, not '%s'", tname(argv[1]));
    return obj_setattr(argv[0], AS_STR(argv[1]), NULL) < 0 ? NULL : Py_None;
}
static Obj *b_format(ARGS) {
    UNUSED;
    if (argc > 1 && !IS_STR(argv[1])) return err(E_TypeError, "format() argument 2 must be str, not %s", tname(argv[1]));
    return obj_format(argv[0], argc > 1 ? AS_STR(argv[1]) : NULL);
}
static Obj *b_globals(ARGS) { UNUSED; return vm.frame ? (Obj *)vm.frame->globals : dict_new(); }
static Obj *b_locals(ARGS) {
    UNUSED;
    Frame *f = vm.frame;
    if (!f) return dict_new();
    if (f->locals) return (Obj *)f->locals;
    if (!f->func) return (Obj *)f->globals;
    Dict *d = (Dict *)dict_new();
    Code *co = f->code;
    for (int i = 0; i < co->nlocals; i++) if (f->fast[i]) dict_sets(d, AS_STR(co->varnames->items[i]), f->fast[i]);
    for (int i = 0; i < co->ncells + co->nfree; i++)
        if (f->cells[i]->value) dict_sets(d, AS_STR(co->cellnames->items[i]), f->cells[i]->value);
    return (Obj *)d;
}
static Obj *b_vars(ARGS) {
    UNUSED;
    if (argc == 0) return b_locals(0, NULL, NULL);
    Obj *d = obj_getattr_opt(argv[0], N_dict);
    if (!d && !vm.exc) return err(E_TypeError, "vars() argument must have __dict__ attribute");
    return d;
}
static Obj *b_dir(ARGS) {
    UNUSED;
    Set *names = (Set *)set_new(T_set);
    Obj *k;
    int64_t pos;
    if (argc == 0) {
        Dict *d = (Dict *)b_locals(0, NULL, NULL);
        for (pos = 0; dict_next(d, &pos, &k, NULL);) set_add(names, k);
    } else {
        Obj *o = argv[0];
        Type *t = KIND(o) == K_TYPE ? (Type *)o : o->type;
        Dict *own = KIND(o) == K_MODULE ? ((Module *)o)->dict :
                    (KIND(o) == K_INSTANCE || KIND(o) == K_EXC) ? ((Instance *)o)->dict : NULL;
        if (own) for (pos = 0; dict_next(own, &pos, &k, NULL);) set_add(names, k);
        if (KIND(o) != K_MODULE)
            for (int64_t i = 0; i < t->mro->n; i++) {
                Dict *d = ((Type *)t->mro->items[i])->dict;
                for (pos = 0; dict_next(d, &pos, &k, NULL);) set_add(names, k);
            }
    }
    List *l = (List *)seq_to_list((Obj *)names);
    if (!l || list_sort(l, NULL, false) < 0) return NULL;
    return (Obj *)l;
}

static Obj *run_string(Obj *src, Obj *globals, int mode) {
    if (!IS_STR(src)) return err(E_TypeError, "expected a string of source code, not %s", tname(src));
    if (globals && globals != Py_None && KIND(globals) != K_DICT) return err(E_TypeError, "globals must be a dict");
    Dict *g = globals && globals != Py_None ? (Dict *)globals : vm.frame ? vm.frame->globals : (Dict *)dict_new();
    Code *co = compile_source(AS_STR(src)->s, "<string>", mode);
    if (!co) return NULL;
    return run_code(co, g, NULL);
}
static bool locals_ok(int argc, Obj **argv, const char *fn) {
    if (argc > 2 && argv[2] != Py_None && argv[2] != argv[1]) {
        err(E_TypeError, "%s() with a locals mapping different from globals is not supported", fn);
        return false;
    }
    return true;
}
static Obj *b_exec(ARGS) {
    UNUSED;
    if (!locals_ok(argc, argv, "exec")) return NULL;
    return run_string(argv[0], argc > 1 ? argv[1] : NULL, MODE_FILE) ? Py_None : NULL;
}
static Obj *b_eval(ARGS) {
    UNUSED;
    if (!locals_ok(argc, argv, "eval")) return NULL;
    return run_string(argv[0], argc > 1 ? argv[1] : NULL, MODE_EVAL);
}

static Obj *b_exit(ARGS) {
    UNUSED;
    err_set(exc_new(E_SystemExit, (Tuple *)tuple_from(argv, argc)));
    return NULL;
}

/* ---------- int and float methods ---------- */
static Obj *i_bit_length(ARGS) { UNUSED; return int_new(int_bit_length(argv[0])); }
static Obj *i_bit_count(ARGS) {
    UNUSED;
    SB b;
    sb_init(&b);
    int_format(&b, argv[0], 2, false);
    int64_t n = 0;
    for (size_t i = 0; i < b.n; i++) n += b.s[i] == '1';
    sb_free(&b);
    return int_new(n);
}
static Obj *i_conjugate(ARGS) { UNUSED; return argv[0]; }
static Obj *fl_is_integer(ARGS) { UNUSED; double d = ((Float *)argv[0])->v; return BOOL(isfinite(d) && d == floor(d)); }
static Obj *t_mro(ARGS) { UNUSED; return seq_to_list((Obj *)((Type *)argv[0])->mro); }

void type_add_method(Type *t, const char *name, CFunc fn, int minargs, int maxargs, int flags) {
    Builtin *b = (Builtin *)builtin_new(name, fn, minargs + 1, maxargs < 0 ? -1 : maxargs + 1, flags | BF_METHOD);
    b->owner = t;
    dict_setc(t->dict, name, (Obj *)b);
}
void type_add_static(Type *t, const char *name, CFunc fn, int minargs, int maxargs, int flags) {
    dict_setc(t->dict, name, builtin_new(name, fn, minargs, maxargs, flags));
}

static void def(const char *name, CFunc fn, int minargs, int maxargs, int flags) {
    dict_setc(vm.builtins, name, builtin_new(name, fn, minargs, maxargs, flags));
}

void builtins_init(void) {
    vm.builtins = (Dict *)dict_new();
    vm.modules = (Dict *)dict_new();
    vm.pending = (List *)list_new(0);
    E_Unsupported = type_new("UnsupportedOperation", E_OSError, K_EXC);
    gc_root((Obj **)&E_Unsupported);
    Dict *b = vm.builtins;
#define X(n) dict_setc(b, T_##n->name->s, (Obj *)T_##n);
    X(object) X(type) X(bool) X(int) X(float) X(str) X(list) X(tuple) X(dict) X(set) X(frozenset)
    X(range) X(slice) X(property) X(staticmethod) X(classmethod) X(super)
#undef X
#define X(n, base) dict_setc(b, #n, (Obj *)E_##n);
    EXCS(X)
#undef X
    dict_setc(b, "IOError", (Obj *)E_OSError);
    dict_setc(b, "EnvironmentError", (Obj *)E_OSError);
    dict_setc(b, "None", Py_None);
    dict_setc(b, "NotImplemented", Py_NotImplemented);
    dict_setc(b, "Ellipsis", Py_Ellipsis);
    dict_setc(b, "__debug__", Py_True);

    T_int->ctor = c_int; T_float->ctor = c_float; T_bool->ctor = c_bool; T_str->ctor = c_str;
    T_list->ctor = c_list; T_tuple->ctor = c_tuple; T_dict->ctor = c_dict; T_set->ctor = c_set;
    T_frozenset->ctor = c_frozenset; T_range->ctor = c_range; T_slice->ctor = c_slice;
    T_property->ctor = c_property; T_staticmethod->ctor = c_staticmethod;
    T_classmethod->ctor = c_classmethod; T_super->ctor = c_super;

    def("print", b_print, 0, -1, BF_KW); def("input", b_input, 0, 1, 0); def("open", b_open, 1, 6, BF_KW);
    def("len", b_len, 1, 1, 0); def("repr", b_repr, 1, 1, 0); def("ascii", b_ascii, 1, 1, 0);
    def(".type_param", b_type_param, 2, 2, 0); def(".generic_base", b_generic_base, 0, -1, 0);
    def("isinstance", b_isinstance, 2, 2, 0); def("issubclass", b_issubclass, 2, 2, 0);
    def("abs", b_abs, 1, 1, 0); def("min", b_min, 0, -1, BF_KW); def("max", b_max, 0, -1, BF_KW);
    def("sum", b_sum, 1, 2, BF_KW); def("sorted", b_sorted, 0, -1, BF_KW); def("reversed", b_reversed, 1, 1, 0);
    def("enumerate", b_enumerate, 1, 2, BF_KW); def("zip", b_zip, 0, -1, BF_KW); def("map", b_map, 2, -1, 0);
    def("filter", b_filter, 2, 2, 0); def("any", b_any, 1, 1, 0); def("all", b_all, 1, 1, 0);
    def("ord", b_ord, 1, 1, 0); def("chr", b_chr, 1, 1, 0); def("hex", b_hex, 1, 1, 0);
    def("bin", b_bin, 1, 1, 0); def("oct", b_oct, 1, 1, 0); def("round", b_round, 1, 2, BF_KW);
    def("divmod", b_divmod, 2, 2, 0); def("pow", b_pow, 2, 3, 0); def("hash", b_hash, 1, 1, 0);
    def("id", b_id, 1, 1, 0); def("iter", b_iter, 1, 1, 0); def("next", b_next, 1, 2, 0);
    def("callable", b_callable, 1, 1, 0); def("getattr", b_getattr, 2, 3, 0); def("hasattr", b_hasattr, 2, 2, 0);
    def("setattr", b_setattr, 3, 3, 0); def("delattr", b_delattr, 2, 2, 0); def("format", b_format, 1, 2, 0);
    def("globals", b_globals, 0, 0, 0); def("locals", b_locals, 0, 0, 0); def("vars", b_vars, 0, 1, 0);
    def("dir", b_dir, 0, 1, 0); def("exec", b_exec, 1, 3, 0); def("eval", b_eval, 1, 3, 0);
    def("exit", b_exit, 0, 1, 0); def("quit", b_exit, 0, 1, 0);

    type_add_method(T_object, "__init__", o_init, 0, -1, BF_KW);
    type_add_static(T_object, "__new__", o_new, 1, -1, BF_KW);
    type_add_method(T_object, "__repr__", o_repr, 0, 0, 0);
    type_add_method(T_object, "__str__", o_str, 0, 0, 0);
    type_add_method(T_object, "__eq__", o_eq, 1, 1, 0);
    type_add_method(T_object, "__ne__", o_ne, 1, 1, 0);
    type_add_method(T_object, "__hash__", o_hash, 0, 0, 0);
    type_add_method(T_object, "__setattr__", o_setattr, 2, 2, 0);
    type_add_method(T_object, "__delattr__", o_delattr, 1, 1, 0);
    type_add_method(T_object, "__format__", o_format, 1, 1, 0);
    type_add_method(E_BaseException, "__init__", e_init, 0, -1, 0);
    type_add_method(E_BaseException, "with_traceback", e_with_traceback, 1, 1, 0);
    type_add_method(E_BaseException, "add_note", e_add_note, 1, 1, 0);
    type_add_method(T_type, "mro", t_mro, 0, 0, 0);

    Type *bts[8] = {T_int, T_float, T_str, T_tuple, T_list, T_dict, T_set, T_frozenset};
    for (int i = 0; i < 8; i++) {
        Type *t = bts[i];
        bool num = i < 2, cont = i >= 2;
        type_add_static(t, "__new__", type_new_static, 1, -1, BF_KW);
        type_add_method(t, "__eq__", r_eq, 1, 1, 0); type_add_method(t, "__ne__", r_ne, 1, 1, 0);
        type_add_method(t, "__lt__", r_lt, 1, 1, 0); type_add_method(t, "__le__", r_le, 1, 1, 0);
        type_add_method(t, "__gt__", r_gt, 1, 1, 0); type_add_method(t, "__ge__", r_ge, 1, 1, 0);
        type_add_method(t, "__hash__", r_hash, 0, 0, 0); type_add_method(t, "__bool__", r_bool, 0, 0, 0);
        if (cont) {
            type_add_method(t, "__len__", r_len, 0, 0, 0); type_add_method(t, "__contains__", r_contains, 1, 1, 0);
            type_add_method(t, "__iter__", r_iter, 0, 0, 0);
            if (i <= 5) type_add_method(t, "__getitem__", r_getitem, 1, 1, 0);
            if (i == 4 || i == 5) { type_add_method(t, "__setitem__", r_setitem, 2, 2, 0); type_add_method(t, "__delitem__", r_delitem, 1, 1, 0); }
        }
        if (num) {
            type_add_method(t, "__neg__", r_neg, 0, 0, 0); type_add_method(t, "__pos__", r_pos, 0, 0, 0);
            type_add_method(t, "__abs__", b_abs, 0, 0, 0);
            if (i == 0) type_add_method(t, "__invert__", r_invert, 0, 0, 0);
        }
#define X(n, OP) \
        type_add_method(t, "__" #n "__", rb_##n, 1, 1, 0); type_add_method(t, "__r" #n "__", rr_##n, 1, 1, 0); \
        if (i >= 4) type_add_method(t, "__i" #n "__", ri_##n, 1, 1, 0);
        BINOPS(X)
#undef X
    }
    type_add_method(T_list, "__init__", list_init, 0, 1, 0);
    type_add_method(T_dict, "__init__", dict_init, 0, 1, BF_KW);
    type_add_method(T_set, "__init__", set_init, 0, 1, 0);

    type_add_method(T_generator, "send", g_send, 1, 1, 0);
    type_add_method(T_generator, "throw", g_throw, 1, 3, 0);
    type_add_method(T_generator, "close", g_close, 0, 0, 0);
    type_add_method(T_generator, "__next__", g_next, 0, 0, 0);
    type_add_method(T_generator, "__iter__", g_iter, 0, 0, 0);
    type_add_method(T_property, "getter", prop_getter, 1, 1, 0);
    type_add_method(T_property, "setter", prop_setter, 1, 1, 0);
    type_add_method(T_property, "deleter", prop_deleter, 1, 1, 0);

    type_add_method(T_file, "read", f_read, 0, 1, 0); type_add_method(T_file, "readline", f_readline, 0, 0, 0);
    type_add_method(T_file, "readlines", f_readlines, 0, 0, 0); type_add_method(T_file, "write", f_write, 1, 1, 0);
    type_add_method(T_file, "writelines", f_writelines, 1, 1, 0); type_add_method(T_file, "close", f_close, 0, 0, 0);
    type_add_method(T_file, "flush", f_flush, 0, 0, 0); type_add_method(T_file, "__enter__", f_enter, 0, 0, 0);
    type_add_method(T_file, "__exit__", f_exit, 0, 3, 0); type_add_method(T_file, "seek", f_seek, 1, 2, 0);
    type_add_method(T_file, "tell", f_tell, 0, 0, 0);

    type_add_method(T_int, "bit_length", i_bit_length, 0, 0, 0);
    type_add_method(T_int, "bit_count", i_bit_count, 0, 0, 0);
    type_add_method(T_int, "conjugate", i_conjugate, 0, 0, 0);
    type_add_method(T_float, "is_integer", fl_is_integer, 0, 0, 0);
    type_add_method(T_float, "conjugate", i_conjugate, 0, 0, 0);
}
