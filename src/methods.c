/* Methods of str, list, tuple, dict and set, plus all string formatting. */
#include "py.h"
#include <ctype.h>
#include <wctype.h>

#define ARGS int argc, Obj **argv, Obj *kwnames
#define UNUSED (void)argc; (void)argv; (void)kwnames
#define KW(name) (kw_find(kwnames, name) >= 0 ? argv[argc + kw_find(kwnames, name)] : NULL)

/* ================= format specs ================= */
typedef struct { uint32_t fill; char align, sign, grouping, type; bool alt, zero, has_prec; int width, prec; } Spec;

static int parse_spec(const char *s, size_t len, Spec *sp) {
    memset(sp, 0, sizeof *sp);
    const char *p = s, *e = s + len;
    if (p < e) {
        const char *q = p;
        uint32_t cp = utf8_next(&q);
        if (q < e && strchr("<>=^", *q)) { sp->fill = cp; sp->align = *q; p = q + 1; }
        else if (strchr("<>=^", *p)) sp->align = *p++;
    }
    if (p < e && (*p == '+' || *p == '-' || *p == ' ')) sp->sign = *p++;
    if (p < e && *p == 'z') p++;
    if (p < e && *p == '#') { sp->alt = true; p++; }
    if (p < e && *p == '0') { sp->zero = true; p++; }
    while (p < e && isdigit((unsigned char)*p)) { if (sp->width < 100000000) sp->width = sp->width * 10 + (*p - '0'); p++; }
    if (p < e && (*p == ',' || *p == '_')) sp->grouping = *p++;
    if (p < e && *p == '.') {
        p++;
        if (p >= e || !isdigit((unsigned char)*p)) { err(E_ValueError, "Format specifier missing precision"); return -1; }
        sp->has_prec = true;
        while (p < e && isdigit((unsigned char)*p)) { if (sp->prec < 100000000) sp->prec = sp->prec * 10 + (*p - '0'); p++; }
    }
    if (p < e) sp->type = *p++;
    if (p != e) { err(E_ValueError, "Invalid format specifier '%.*s'", (int)len, s); return -1; }
    return 0;
}

static size_t count_chars(const char *s, size_t n) {
    size_t c = 0;
    for (size_t i = 0; i < n; i++) c += ((unsigned char)s[i] & 0xC0) != 0x80;
    return c;
}

static void pad_out(SB *out, Spec *sp, const char *prefix, size_t plen, const char *body, size_t blen, char default_align) {
    size_t total = plen + count_chars(body, blen);
    char align = sp->align ? sp->align : default_align;
    uint32_t fill = sp->fill ? sp->fill : ' ';
    if (sp->zero && !sp->fill) fill = '0';
    if (sp->zero && !sp->align) align = '=';
    size_t padn = (size_t)sp->width > total ? (size_t)sp->width - total : 0;
    size_t left = align == '>' ? padn : align == '^' ? padn / 2 : 0;
    size_t mid = align == '=' ? padn : 0, right = padn - left - mid;
    for (size_t i = 0; i < left; i++) sb_putcp(out, fill);
    sb_put(out, prefix, plen);
    for (size_t i = 0; i < mid; i++) sb_putcp(out, fill);
    sb_put(out, body, blen);
    for (size_t i = 0; i < right; i++) sb_putcp(out, fill);
}

static void group_digits(SB *out, const char *d, size_t n, char sep, int group) {
    for (size_t i = 0; i < n; i++) {
        if (i && (n - i) % (size_t)group == 0) sb_putc(out, sep);
        sb_putc(out, d[i]);
    }
}

static int format_float_spec(SB *out, double x, Spec *sp) {
    char type = sp->type;
    int prec = sp->has_prec ? sp->prec : 6;
    bool neg = signbit(x) && !isnan(x);
    double ax = fabs(x);
    SB num;
    sb_init(&num);
    if (type && !strchr("eEfFgGn%", type)) {
        err(E_ValueError, "Unknown format code '%c' for object of type 'float'", type);
        return -1;
    }
    if (prec > 1000) { err(E_ValueError, "precision too big"); return -1; }
    bool upper = type == 'E' || type == 'F' || type == 'G';
    if (isnan(x) || isinf(x)) {
        sb_puts(&num, isnan(x) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf"));
        if (type == '%') sb_putc(&num, '%');
    } else switch (type) {
    case 0:
        if (!sp->has_prec) float_repr(&num, ax);
        else {
            sb_printf(&num, "%.*g", prec ? prec : 1, ax);
            bool plain = true;
            for (size_t i = 0; i < num.n; i++) if (num.s[i] == '.' || num.s[i] == 'e') plain = false;
            if (plain) sb_puts(&num, ".0");
        }
        break;
    case 'e': case 'E': sb_printf(&num, type == 'e' ? "%.*e" : "%.*E", prec, ax); break;
    case 'f': case 'F': sb_printf(&num, "%.*f", prec, ax); break;
    case 'g': case 'G': case 'n':
        if (sp->alt) sb_printf(&num, type == 'G' ? "%#.*G" : "%#.*g", prec ? prec : 1, ax);
        else sb_printf(&num, type == 'G' ? "%.*G" : "%.*g", prec ? prec : 1, ax);
        break;
    case '%': sb_printf(&num, "%.*f%%", prec, ax * 100.0); break;
    }
    SB body;
    sb_init(&body);
    if (sp->grouping && isfinite(x)) {
        size_t ip = 0;
        while (ip < num.n && isdigit((unsigned char)num.s[ip])) ip++;
        group_digits(&body, num.s, ip, sp->grouping, 3);
        sb_put(&body, num.s + ip, num.n - ip);
    } else sb_put(&body, num.s, num.n);
    char prefix[2];
    size_t plen = 0;
    if (neg) prefix[plen++] = '-';
    else if (sp->sign == '+' || sp->sign == ' ') prefix[plen++] = sp->sign;
    pad_out(out, sp, prefix, plen, body.s ? body.s : "", body.n, '>');
    sb_free(&num); sb_free(&body);
    return 0;
}

static int format_int_spec(SB *out, Obj *v, Spec *sp) {
    char type = sp->type ? sp->type : 'd';
    if (strchr("eEfFgG%", type)) {
        double d;
        if (obj_as_double(v, &d) < 0) return -1;
        return format_float_spec(out, d, sp);
    }
    if (type == 'c') {
        int64_t cp;
        if (!int_fits(v, &cp) || cp < 0 || cp > 0x10FFFF) { err(E_OverflowError, "%%c arg not in range(0x110000)"); return -1; }
        SB ch;
        sb_init(&ch);
        sb_putcp(&ch, (uint32_t)cp);
        pad_out(out, sp, "", 0, ch.s, ch.n, '<');
        sb_free(&ch);
        return 0;
    }
    int base = type == 'd' || type == 'n' ? 10 : type == 'b' ? 2 : type == 'o' ? 8 : (type == 'x' || type == 'X') ? 16 : 0;
    if (!base) { err(E_ValueError, "Unknown format code '%c' for object of type 'int'", type); return -1; }
    if (sp->has_prec) { err(E_ValueError, "Precision not allowed in integer format specifier"); return -1; }
    SB d, body;
    sb_init(&d); sb_init(&body);
    int_format(&d, v, base, type == 'X');
    bool neg = d.s[0] == '-';
    char prefix[4];
    size_t plen = 0;
    if (neg) prefix[plen++] = '-';
    else if (sp->sign == '+' || sp->sign == ' ') prefix[plen++] = sp->sign;
    if (sp->alt && base != 10) { prefix[plen++] = '0'; prefix[plen++] = type; }
    if (sp->grouping) group_digits(&body, d.s + neg, d.n - neg, sp->grouping, base == 10 ? 3 : 4);
    else sb_put(&body, d.s + neg, d.n - neg);
    pad_out(out, sp, prefix, plen, body.s, body.n, '>');
    sb_free(&d); sb_free(&body);
    return 0;
}

static int format_str_spec(SB *out, Str *s, Spec *sp) {
    if (sp->type && sp->type != 's') { err(E_ValueError, "Unknown format code '%c' for object of type 'str'", sp->type); return -1; }
    if (sp->sign) { err(E_ValueError, "Sign not allowed in string format specifier"); return -1; }
    size_t len = s->len;
    if (sp->has_prec && (uint32_t)sp->prec < s->nchars) len = str_offset(s, sp->prec);
    pad_out(out, sp, "", 0, s->s, len, '<');
    return 0;
}

Obj *obj_format(Obj *o, Str *spec) {
    if (o->type->user && o == raw_obj) raw_obj = NULL;
    else if (o->type->user) {
        Obj *f = type_lookup(o->type, N_format);
        if (f && KIND(f) != K_BUILTIN) {
            Obj *r = call2(f, o, spec ? (Obj *)spec : str_new("", 0));
            if (r && !IS_STR(r)) return err(E_TypeError, "__format__ must return a str, not %s", tname(r));
            return r;
        }
    }
    if (!spec || spec->len == 0) return obj_str(o);
    Spec sp;
    if (parse_spec(spec->s, spec->len, &sp) < 0) return NULL;
    SB out;
    sb_init(&out);
    int r;
    switch (KIND(o)) {
    case K_INT: case K_BOOL: r = format_int_spec(&out, o, &sp); break;
    case K_FLOAT:
        if (sp.type && strchr("dbcoxX", sp.type)) {
            sb_free(&out);
            return err(E_ValueError, "Unknown format code '%c' for object of type 'float'", sp.type);
        }
        r = format_float_spec(&out, ((Float *)o)->v, &sp);
        break;
    case K_STR: r = format_str_spec(&out, AS_STR(o), &sp); break;
    default:
        sb_free(&out);
        return err(E_TypeError, "unsupported format string passed to %s.__format__", tname(o));
    }
    if (r < 0) { sb_free(&out); return NULL; }
    return sb_finish(&out);
}

/* ---------- the % operator ---------- */
Obj *str_percent(Str *fmt, Obj *args) {
    Obj *single[1] = {args};
    Obj **av = single;
    int64_t an = 1, ai = 0;
    bool is_tuple = KIND(args) == K_TUPLE;
    if (is_tuple) { av = ((Tuple *)args)->items; an = ((Tuple *)args)->n; }
    bool used_map = false;
    SB out;
    sb_init(&out);
    const char *p = fmt->s, *e = fmt->s + fmt->len;
#define FAIL(...) do { sb_free(&out); return err(__VA_ARGS__); } while (0)
#define NEXTARG(dst) do { if (ai >= an) FAIL(E_TypeError, "not enough arguments for format string"); dst = av[ai++]; } while (0)
    while (p < e) {
        if (*p != '%') { sb_putc(&out, *p++); continue; }
        p++;
        if (p >= e) FAIL(E_ValueError, "incomplete format");
        if (*p == '%') { sb_putc(&out, '%'); p++; continue; }
        Obj *val = NULL;
        if (*p == '(') {
            const char *k = ++p;
            int depth = 1;
            while (p < e && depth) { if (*p == '(') depth++; else if (*p == ')') depth--; p++; }
            if (depth) FAIL(E_ValueError, "incomplete format key");
            if (KIND(args) != K_DICT && !(args->type->user && type_lookup(args->type, N_getitem)))
                FAIL(E_TypeError, "format requires a mapping");
            val = obj_getitem(args, str_new(k, (size_t)(p - 1 - k)));
            if (!val) { sb_free(&out); return NULL; }
            used_map = true;
        }
        Spec sp;
        memset(&sp, 0, sizeof sp);
        for (; p < e && strchr("-+ #0", *p); p++) {
            if (*p == '-') sp.align = '<';
            else if (*p == '+') sp.sign = '+';
            else if (*p == ' ') { if (!sp.sign) sp.sign = ' '; }
            else if (*p == '#') sp.alt = true;
            else sp.zero = true;
        }
        if (p < e && *p == '*') {
            Obj *w;
            int64_t wi;
            NEXTARG(w);
            if (!IS_INT(w) || !int_fits(w, &wi)) FAIL(E_TypeError, "* wants int");
            if (wi < 0) { sp.align = '<'; wi = -wi; }
            sp.width = (int)wi;
            p++;
        } else while (p < e && isdigit((unsigned char)*p)) sp.width = sp.width * 10 + (*p++ - '0');
        if (p < e && *p == '.') {
            p++;
            sp.has_prec = true;
            if (p < e && *p == '*') {
                Obj *w;
                int64_t wi;
                NEXTARG(w);
                if (!IS_INT(w) || !int_fits(w, &wi)) FAIL(E_TypeError, "* wants int");
                sp.prec = wi < 0 ? 0 : (int)wi;
                p++;
            } else while (p < e && isdigit((unsigned char)*p)) sp.prec = sp.prec * 10 + (*p++ - '0');
        }
        while (p < e && (*p == 'l' || *p == 'h' || *p == 'L')) p++;
        if (p >= e) FAIL(E_ValueError, "incomplete format");
        char c = *p++;
        if (!val) NEXTARG(val);
        if (sp.align == '<') sp.zero = false;
        int r = 0;
        switch (c) {
        case 's': case 'r': case 'a': {
            Obj *s = c == 's' ? obj_str(val) : obj_repr(val);
            if (!s) { sb_free(&out); return NULL; }
            sp.zero = false; sp.sign = 0;
            if (!sp.align) sp.align = '>';
            r = format_str_spec(&out, AS_STR(s), &sp);
            break;
        }
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
            Obj *iv = val;
            if (KIND(val) == K_FLOAT && (c == 'd' || c == 'i' || c == 'u')) {
                double d = ((Float *)val)->v;
                if (!isfinite(d)) FAIL(E_OverflowError, "cannot convert float infinity to integer");
                iv = int_from_double(d);
            } else if (!IS_INT(val)) {
                if (c == 'x' || c == 'X' || c == 'o') FAIL(E_TypeError, "%%%c format: an integer is required, not %s", c, tname(val));
                FAIL(E_TypeError, "%%%c format: a real number is required, not %s", c, tname(val));
            }
            if (sp.has_prec) {
                /* Precision means a minimum number of digits. */
                SB d;
                sb_init(&d);
                int_format(&d, iv, c == 'o' ? 8 : (c == 'x' || c == 'X') ? 16 : 10, c == 'X');
                bool neg = d.s[0] == '-';
                SB body;
                sb_init(&body);
                for (size_t k = d.n - neg; k < (size_t)sp.prec; k++) sb_putc(&body, '0');
                sb_put(&body, d.s + neg, d.n - neg);
                char prefix[4];
                size_t plen = 0;
                if (neg) prefix[plen++] = '-'; else if (sp.sign) prefix[plen++] = sp.sign;
                if (sp.alt && c != 'd' && c != 'i' && c != 'u') { prefix[plen++] = '0'; prefix[plen++] = c; }
                sp.has_prec = false; sp.zero = false;
                pad_out(&out, &sp, prefix, plen, body.s, body.n, '>');
                sb_free(&d); sb_free(&body);
            } else {
                sp.type = (c == 'i' || c == 'u') ? 'd' : c;
                r = format_int_spec(&out, iv, &sp);
            }
            break;
        }
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': {
            double d;
            if (obj_as_double(val, &d) < 0) {
                err_clear();
                FAIL(E_TypeError, "must be real number, not %s", tname(val));
            }
            sp.type = c;
            r = format_float_spec(&out, d, &sp);
            break;
        }
        case 'c': {
            if (IS_STR(val)) {
                if (AS_STR(val)->nchars != 1) FAIL(E_TypeError, "%%c requires an int or a unicode character, not a string of length %u", AS_STR(val)->nchars);
                sp.zero = false;
                if (!sp.align) sp.align = '>';
                r = format_str_spec(&out, AS_STR(val), &sp);
            } else if (IS_INT(val)) { sp.type = 'c'; if (!sp.align) sp.align = '>'; r = format_int_spec(&out, val, &sp); }
            else FAIL(E_TypeError, "%%c requires an int or a unicode character, not %s", tname(val));
            break;
        }
        default:
            FAIL(E_ValueError, "unsupported format character '%c' (0x%x) at index %d", c, c, (int)(p - 1 - fmt->s));
        }
        if (r < 0) { sb_free(&out); return NULL; }
    }
    if (!used_map && ai < an && !(KIND(args) == K_DICT && !is_tuple))
        FAIL(E_TypeError, "not all arguments converted during string formatting");
#undef FAIL
#undef NEXTARG
    return sb_finish(&out);
}

/* ---------- str.format ---------- */
typedef struct { int argc; Obj **argv; Obj *kwnames; int auto_idx; bool manual; } FmtArgs;

static int format_into(SB *out, const char *p, const char *e, FmtArgs *fa, int depth);

static Obj *format_field_value(const char *s, const char *e, FmtArgs *fa) {
    const char *p = s;
    while (p < e && *p != '.' && *p != '[') p++;
    Obj *v = NULL;
    if (p == s) {
        if (fa->manual) return err(E_ValueError, "cannot switch from manual field specification to automatic field numbering");
        if (fa->auto_idx >= fa->argc) return err(E_IndexError, "Replacement index %d out of range for positional args tuple", fa->auto_idx);
        v = fa->argv[fa->auto_idx++];
    } else if (isdigit((unsigned char)*s)) {
        int idx = 0;
        for (const char *q = s; q < p; q++) {
            if (!isdigit((unsigned char)*q)) return err(E_ValueError, "invalid field name in format string");
            idx = idx * 10 + (*q - '0');
        }
        if (fa->auto_idx > 0) return err(E_ValueError, "cannot switch from automatic field numbering to manual field specification");
        fa->manual = true;
        if (idx >= fa->argc) return err(E_IndexError, "Replacement index %d out of range for positional args tuple", idx);
        v = fa->argv[idx];
    } else {
        Tuple *kn = (Tuple *)fa->kwnames;
        for (int i = 0; kn && i < kn->n; i++) {
            Str *k = AS_STR(kn->items[i]);
            if (k->len == (size_t)(p - s) && memcmp(k->s, s, k->len) == 0) v = fa->argv[fa->argc + i];
        }
        if (!v) return err_obj(E_KeyError, str_new(s, (size_t)(p - s)));
    }
    while (p < e && v) {
        if (*p == '.') {
            const char *q = ++p;
            while (p < e && *p != '.' && *p != '[') p++;
            v = obj_getattr(v, AS_STR(str_new(q, (size_t)(p - q))));
        } else {
            const char *q = ++p;
            while (p < e && *p != ']') p++;
            if (p >= e) return err(E_ValueError, "Missing ']' in format string");
            bool digits = p > q;
            for (const char *d = q; d < p; d++) if (!isdigit((unsigned char)*d)) digits = false;
            Obj *key = digits ? int_from_digits(q, (size_t)(p - q), 10) : str_new(q, (size_t)(p - q));
            v = obj_getitem(v, key);
            p++;
        }
    }
    return v;
}

static int format_into(SB *out, const char *p, const char *e, FmtArgs *fa, int depth) {
    while (p < e) {
        char c = *p++;
        if (c == '{') {
            if (p < e && *p == '{') { sb_putc(out, '{'); p++; continue; }
            const char *start = p;
            int d = 1;
            while (p < e && d) { if (*p == '{') d++; else if (*p == '}') d--; p++; }
            if (d) { err(E_ValueError, "Single '{' encountered in format string"); return -1; }
            const char *end = p - 1, *name_end = start;
            int nest = 0;
            while (name_end < end && !((*name_end == '!' || *name_end == ':') && nest == 0)) {
                if (*name_end == '[') nest++; else if (*name_end == ']') nest--;
                name_end++;
            }
            if (depth > 1) { err(E_ValueError, "Max string recursion exceeded"); return -1; }
            Obj *v = format_field_value(start, name_end, fa);
            if (!v) return -1;
            const char *q = name_end;
            if (q < end && *q == '!') {
                char conv = q + 1 < end ? q[1] : 0;
                if (conv == 'r' || conv == 'a') v = obj_repr(v);
                else if (conv == 's') v = obj_str(v);
                else { err(E_ValueError, "Unknown conversion specifier %c", conv ? conv : ' '); return -1; }
                if (!v) return -1;
                q += 2;
            }
            Obj *spec = NULL;
            if (q < end && *q == ':') {
                SB sb;
                sb_init(&sb);
                if (format_into(&sb, q + 1, end, fa, depth + 1) < 0) { sb_free(&sb); return -1; }
                spec = sb_finish(&sb);
            } else if (q != end) { err(E_ValueError, "expected ':' after conversion specifier"); return -1; }
            Obj *s = obj_format(v, spec ? AS_STR(spec) : NULL);
            if (!s) return -1;
            sb_putstr(out, s);
        } else if (c == '}') {
            if (p < e && *p == '}') { sb_putc(out, '}'); p++; continue; }
            err(E_ValueError, "Single '}' encountered in format string");
            return -1;
        } else sb_putc(out, c);
    }
    return 0;
}

static Obj *s_format(ARGS) {
    Str *s = AS_STR(argv[0]);
    FmtArgs fa = {argc - 1, argv + 1, kwnames, 0, false};
    SB out;
    sb_init(&out);
    if (format_into(&out, s->s, s->s + s->len, &fa, 0) < 0) { sb_free(&out); return NULL; }
    return sb_finish(&out);
}

static Obj *s_format_map(ARGS) {
    UNUSED;
    if (KIND(argv[1]) != K_DICT) return err(E_TypeError, "format_map() argument must be a dict");
    Dict *d = (Dict *)argv[1];
    List *vals = (List *)list_new(0);
    Tuple *names = (Tuple *)tuple_new(d->used);
    int64_t pos = 0, i = 0;
    Obj *k, *v;
    while (dict_next(d, &pos, &k, &v)) {
        if (!IS_STR(k)) { names->n = i; continue; }
        names->items[i++] = k;
        list_append(vals, v);
    }
    names->n = i;
    Str *s = AS_STR(argv[0]);
    FmtArgs fa = {0, vals->items, (Obj *)names, 0, false};
    SB out;
    sb_init(&out);
    if (format_into(&out, s->s, s->s + s->len, &fa, 0) < 0) { sb_free(&out); return NULL; }
    return sb_finish(&out);
}

/* ================= str methods ================= */
#define SELF AS_STR(argv[0])
#define NEED_STR(i, fn) if (!IS_STR(argv[i])) return err(E_TypeError, fn "() argument must be str, not %s", tname(argv[i]))

static bool is_space(unsigned char c) { return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x1f); }

static Obj *map_case(Str *s, int mode) { /* 0 lower, 1 upper, 2 swap, 3 capitalize, 4 title */
    SB b;
    sb_init(&b);
    const char *p = s->s, *e = s->s + s->len;
    bool first = true, prev_alpha = false;
    while (p < e) {
        uint32_t cp = utf8_next(&p), r = cp;
        bool alpha = cp < 128 ? isalpha((int)cp) != 0 : iswalpha((wint_t)cp) != 0;
        bool up;
        switch (mode) {
        case 0: up = false; break;
        case 1: up = true; break;
        case 2: up = cp < 128 ? islower((int)cp) != 0 : iswlower((wint_t)cp) != 0; break;
        case 3: up = first; break;
        default: up = !prev_alpha; break;
        }
        if (cp < 128) r = up ? (uint32_t)toupper((int)cp) : (uint32_t)tolower((int)cp);
        else r = up ? (uint32_t)towupper((wint_t)cp) : (uint32_t)towlower((wint_t)cp);
        if (mode == 1 && cp == 0xDF) { sb_puts(&b, "SS"); first = false; prev_alpha = true; continue; }
        sb_putcp(&b, r);
        first = false;
        prev_alpha = alpha;
    }
    return sb_finish(&b);
}
static Obj *s_lower(ARGS) { UNUSED; return map_case(SELF, 0); }
static Obj *s_upper(ARGS) { UNUSED; return map_case(SELF, 1); }
static Obj *s_casefold(ARGS) {
    UNUSED;
    Str *low = AS_STR(map_case(SELF, 0));
    if (low->len == low->nchars) return (Obj *)low;
    SB b;
    sb_init(&b);
    for (const char *p = low->s, *e = low->s + low->len; p < e;) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0xDF) sb_puts(&b, "ss"); else sb_putcp(&b, cp);
    }
    return sb_finish(&b);
}
static Obj *s_swapcase(ARGS) { UNUSED; return map_case(SELF, 2); }
static Obj *s_capitalize(ARGS) { UNUSED; return map_case(SELF, 3); }
static Obj *s_title(ARGS) { UNUSED; return map_case(SELF, 4); }

/* The characters Python counts as white space (str.isspace). */
static bool uni_space(uint32_t c) {
    return (c >= 9 && c <= 13) || (c >= 28 && c <= 32) || c == 133 || c == 160 || c == 5760 || (c >= 8192 && c <= 8202) ||
           c == 8232 || c == 8233 || c == 8239 || c == 8287 || c == 12288;
}

static bool in_chars(uint32_t cp, Str *chars) {
    if (!chars) return uni_space(cp);
    const char *p = chars->s, *e = chars->s + chars->len;
    while (p < e) if (utf8_next(&p) == cp) return true;
    return false;
}
static Obj *strip_impl(int argc, Obj **argv, bool left, bool right, const char *fn) {
    Str *s = SELF, *chars = NULL;
    if (argc > 1 && argv[1] != Py_None) {
        if (!IS_STR(argv[1])) return err(E_TypeError, "%s arg must be None or str", fn);
        chars = AS_STR(argv[1]);
    }
    const char *a = s->s, *b = s->s + s->len;
    if (left) while (a < b) {
        const char *q = a;
        if (!in_chars(utf8_next(&q), chars)) break;
        a = q;
    }
    if (right) while (b > a) {
        const char *q = b - 1;
        while (q > a && ((unsigned char)*q & 0xC0) == 0x80) q--;
        const char *t = q;
        if (!in_chars(utf8_next(&t), chars)) break;
        b = q;
    }
    if (a == s->s && b == s->s + s->len) return (Obj *)s;
    return str_new(a, (size_t)(b - a));
}
static Obj *s_strip(ARGS) { (void)kwnames; return strip_impl(argc, argv, true, true, "strip"); }
static Obj *s_lstrip(ARGS) { (void)kwnames; return strip_impl(argc, argv, true, false, "lstrip"); }
static Obj *s_rstrip(ARGS) { (void)kwnames; return strip_impl(argc, argv, false, true, "rstrip"); }

static Obj *s_split_impl(ARGS, bool rev) {
    if (kw_check(kwnames, rev ? "rsplit" : "split", "sep,maxsplit") < 0) return NULL;
    Str *s = SELF;
    Obj *sepo = argc > 1 ? argv[1] : KW("sep"), *mo = argc > 2 ? argv[2] : KW("maxsplit");
    int64_t maxsplit = -1;
    if (mo && obj_index(mo, &maxsplit) < 0) return NULL;
    if (maxsplit < 0) maxsplit = INT64_MAX;
    List *out = (List *)list_new(0);
    const char *b = s->s, *e = s->s + s->len;
    if (!sepo || sepo == Py_None) {
        if (!rev) {
            const char *p = b;
            for (;;) {
                while (p < e && is_space((unsigned char)*p)) p++;
                if (p >= e) break;
                if (maxsplit-- == 0) {
                    const char *t = e;
                    while (t > p && is_space((unsigned char)t[-1])) t--;
                    list_append(out, str_new(p, (size_t)(t - p)));
                    break;
                }
                const char *q = p;
                while (q < e && !is_space((unsigned char)*q)) q++;
                list_append(out, str_new(p, (size_t)(q - p)));
                p = q;
            }
        } else {
            const char *p = e;
            for (;;) {
                while (p > b && is_space((unsigned char)p[-1])) p--;
                if (p <= b) break;
                if (maxsplit-- == 0) {
                    const char *t = b;
                    while (t < p && is_space((unsigned char)*t)) t++;
                    list_append(out, str_new(t, (size_t)(p - t)));
                    break;
                }
                const char *q = p;
                while (q > b && !is_space((unsigned char)q[-1])) q--;
                list_append(out, str_new(q, (size_t)(p - q)));
                p = q;
            }
            for (int64_t i = 0, j = out->n - 1; i < j; i++, j--) { Obj *t = out->items[i]; out->items[i] = out->items[j]; out->items[j] = t; }
        }
        return (Obj *)out;
    }
    if (!IS_STR(sepo)) return err(E_TypeError, "must be str or None, not %s", tname(sepo));
    Str *sep = AS_STR(sepo);
    if (sep->len == 0) return err(E_ValueError, "empty separator");
    if (!rev) {
        const char *p = b;
        while (maxsplit-- > 0) {
            const char *q = memmem(p, (size_t)(e - p), sep->s, sep->len);
            if (!q) break;
            list_append(out, str_new(p, (size_t)(q - p)));
            p = q + sep->len;
        }
        list_append(out, str_new(p, (size_t)(e - p)));
    } else {
        const char *end = e;
        while (maxsplit-- > 0) {
            const char *q = NULL;
            for (const char *t = end - sep->len; t >= b; t--)
                if (memcmp(t, sep->s, sep->len) == 0) { q = t; break; }
            if (!q) break;
            list_append(out, str_new(q + sep->len, (size_t)(end - q - sep->len)));
            end = q;
        }
        list_append(out, str_new(b, (size_t)(end - b)));
        for (int64_t i = 0, j = out->n - 1; i < j; i++, j--) { Obj *t = out->items[i]; out->items[i] = out->items[j]; out->items[j] = t; }
    }
    return (Obj *)out;
}
static Obj *s_split(ARGS) { return s_split_impl(argc, argv, kwnames, false); }
static Obj *s_rsplit(ARGS) { return s_split_impl(argc, argv, kwnames, true); }

static Obj *s_splitlines(ARGS) {
    if (kw_check(kwnames, "splitlines", "keepends") < 0) return NULL;
    Obj *ko = argc > 1 ? argv[1] : KW("keepends");
    bool keep = ko && obj_truthy(ko) > 0;
    Str *s = SELF;
    List *out = (List *)list_new(0);
    const char *p = s->s, *e = s->s + s->len;
    while (p < e) {
        const char *q = p;
        while (q < e && *q != '\n' && *q != '\r') q++;
        const char *eol = q;
        if (q < e) { if (*q == '\r' && q + 1 < e && q[1] == '\n') q += 2; else q++; }
        list_append(out, str_new(p, (size_t)((keep ? q : eol) - p)));
        p = q;
    }
    return (Obj *)out;
}

static Obj *s_join(ARGS) {
    UNUSED;
    Str *sep = SELF;
    List *l = (List *)seq_to_list(argv[1]);
    if (!l) return NULL;
    SB b;
    sb_init(&b);
    for (int64_t i = 0; i < l->n; i++) {
        if (!IS_STR(l->items[i])) {
            sb_free(&b);
            return err(E_TypeError, "sequence item %lld: expected str instance, %s found", (long long)i, tname(l->items[i]));
        }
        if (i) sb_put(&b, sep->s, sep->len);
        sb_putstr(&b, l->items[i]);
    }
    return sb_finish(&b);
}

static Obj *s_replace(ARGS) {
    UNUSED;
    Str *s = SELF;
    NEED_STR(1, "replace"); NEED_STR(2, "replace");
    Str *old = AS_STR(argv[1]), *new = AS_STR(argv[2]);
    int64_t count = -1;
    if (argc > 3 && obj_index(argv[3], &count) < 0) return NULL;
    if (count < 0) count = INT64_MAX;
    SB b;
    sb_init(&b);
    const char *p = s->s, *e = s->s + s->len;
    if (old->len == 0) {
        while (p < e && count-- > 0) {
            sb_put(&b, new->s, new->len);
            const char *q = p;
            utf8_next(&q);
            sb_put(&b, p, (size_t)(q - p));
            p = q;
        }
        if (p >= e && count > 0) sb_put(&b, new->s, new->len);
        sb_put(&b, p, (size_t)(e - p));
        return sb_finish(&b);
    }
    while (count-- > 0) {
        const char *q = memmem(p, (size_t)(e - p), old->s, old->len);
        if (!q) break;
        sb_put(&b, p, (size_t)(q - p));
        sb_put(&b, new->s, new->len);
        p = q + old->len;
    }
    sb_put(&b, p, (size_t)(e - p));
    return sb_finish(&b);
}

/* Converts optional start/end arguments (character indexes) to byte offsets. */
static int str_bounds(Str *s, int argc, Obj **argv, int at, size_t *bs, size_t *be, int64_t *cstart) {
    int64_t n = s->nchars, start = 0, end = n;
    if (argc > at && argv[at] != Py_None && obj_index(argv[at], &start) < 0) return -1;
    if (argc > at + 1 && argv[at + 1] != Py_None && obj_index(argv[at + 1], &end) < 0) return -1;
    if (start < 0) { start += n; if (start < 0) start = 0; }
    if (end < 0) { end += n; if (end < 0) end = 0; }
    if (end > n) end = n;
    *cstart = start;
    if (start > end) { *bs = *be = 0; return 1; }
    *bs = str_offset(s, start);
    *be = str_offset(s, end);
    return 0;
}

static Obj *find_impl(int argc, Obj **argv, bool rev, bool raise, const char *fn) {
    Str *s = SELF;
    if (!IS_STR(argv[1])) return err(E_TypeError, "must be str, not %s", tname(argv[1]));
    Str *sub = AS_STR(argv[1]);
    size_t bs, be;
    int64_t cs;
    int r = str_bounds(s, argc, argv, 2, &bs, &be, &cs);
    if (r < 0) return NULL;
    const char *hit = NULL;
    if (r == 0 && sub->len <= be - bs) {
        if (!rev) hit = sub->len ? memmem(s->s + bs, be - bs, sub->s, sub->len) : s->s + bs;
        else for (const char *t = s->s + be - sub->len; t >= s->s + bs; t--)
            if (memcmp(t, sub->s, sub->len) == 0) { hit = t; break; }
    }
    if (!hit) {
        if (raise) return err(E_ValueError, "substring not found");
        (void)fn;
        return int_new(-1);
    }
    return int_new((int64_t)count_chars(s->s, (size_t)(hit - s->s)));
}
static Obj *s_find(ARGS) { (void)kwnames; return find_impl(argc, argv, false, false, "find"); }
static Obj *s_rfind(ARGS) { (void)kwnames; return find_impl(argc, argv, true, false, "rfind"); }
static Obj *s_index(ARGS) { (void)kwnames; return find_impl(argc, argv, false, true, "index"); }
static Obj *s_rindex(ARGS) { (void)kwnames; return find_impl(argc, argv, true, true, "rindex"); }

static Obj *s_count(ARGS) {
    UNUSED;
    Str *s = SELF;
    if (!IS_STR(argv[1])) return err(E_TypeError, "must be str, not %s", tname(argv[1]));
    Str *sub = AS_STR(argv[1]);
    size_t bs, be;
    int64_t cs, n = 0;
    int r = str_bounds(s, argc, argv, 2, &bs, &be, &cs);
    if (r < 0) return NULL;
    if (r) return int_new(0);
    if (sub->len == 0) return int_new((int64_t)count_chars(s->s + bs, be - bs) + 1);
    const char *p = s->s + bs, *e = s->s + be;
    while ((p = memmem(p, (size_t)(e - p), sub->s, sub->len))) { n++; p += sub->len; }
    return int_new(n);
}

static Obj *affix_impl(int argc, Obj **argv, bool suffix, const char *fn) {
    Str *s = SELF;
    size_t bs, be;
    int64_t cs;
    int r = str_bounds(s, argc, argv, 2, &bs, &be, &cs);
    if (r < 0) return NULL;
    Obj *one[1] = {argv[1]}, **cands = one;
    int64_t nc = 1;
    if (KIND(argv[1]) == K_TUPLE) { cands = ((Tuple *)argv[1])->items; nc = ((Tuple *)argv[1])->n; }
    for (int64_t i = 0; i < nc; i++) {
        if (!IS_STR(cands[i]))
            return err(E_TypeError, "%s first arg must be str or a tuple of str, not %s", fn, tname(cands[i]));
        Str *a = AS_STR(cands[i]);
        if (r || a->len > be - bs) continue;
        if (memcmp(suffix ? s->s + be - a->len : s->s + bs, a->s, a->len) == 0) return Py_True;
    }
    return Py_False;
}
static Obj *s_startswith(ARGS) { (void)kwnames; return affix_impl(argc, argv, false, "startswith"); }
static Obj *s_endswith(ARGS) { (void)kwnames; return affix_impl(argc, argv, true, "endswith"); }

static Obj *s_removeprefix(ARGS) {
    UNUSED;
    NEED_STR(1, "removeprefix");
    Str *s = SELF, *a = AS_STR(argv[1]);
    if (a->len <= s->len && memcmp(s->s, a->s, a->len) == 0) return str_new(s->s + a->len, s->len - a->len);
    return (Obj *)s;
}
static Obj *s_removesuffix(ARGS) {
    UNUSED;
    NEED_STR(1, "removesuffix");
    Str *s = SELF, *a = AS_STR(argv[1]);
    if (a->len && a->len <= s->len && memcmp(s->s + s->len - a->len, a->s, a->len) == 0) return str_new(s->s, s->len - a->len);
    return (Obj *)s;
}

typedef int (*CharPred)(uint32_t);
static int p_digit(uint32_t c) { return c < 128 ? isdigit((int)c) : iswdigit((wint_t)c); }
static int p_alpha(uint32_t c) { return c < 128 ? isalpha((int)c) : iswalpha((wint_t)c); }
static int p_alnum(uint32_t c) { return c < 128 ? isalnum((int)c) : iswalnum((wint_t)c); }
static int p_space(uint32_t c) { return uni_space(c); }
static int p_ascii(uint32_t c) { return c < 128; }
static int p_print(uint32_t c) { return cp_printable(c); }
static Obj *all_chars(Str *s, CharPred f, bool empty_ok) {
    if (s->len == 0) return BOOL(empty_ok);
    const char *p = s->s, *e = s->s + s->len;
    while (p < e) if (!f(utf8_next(&p))) return Py_False;
    return Py_True;
}
static Obj *s_isdigit(ARGS) { UNUSED; return all_chars(SELF, p_digit, false); }
static Obj *s_isalpha(ARGS) { UNUSED; return all_chars(SELF, p_alpha, false); }
static Obj *s_isalnum(ARGS) { UNUSED; return all_chars(SELF, p_alnum, false); }
static Obj *s_isspace(ARGS) { UNUSED; return all_chars(SELF, p_space, false); }
static Obj *s_isascii(ARGS) { UNUSED; return all_chars(SELF, p_ascii, true); }
static Obj *s_isprintable(ARGS) { UNUSED; return all_chars(SELF, p_print, true); }
static Obj *case_check(Str *s, bool upper) {
    bool cased = false;
    const char *p = s->s, *e = s->s + s->len;
    while (p < e) {
        uint32_t c = utf8_next(&p);
        bool lo = c < 128 ? islower((int)c) != 0 : iswlower((wint_t)c) != 0;
        bool up = c < 128 ? isupper((int)c) != 0 : iswupper((wint_t)c) != 0;
        if (upper ? lo : up) return Py_False;
        if (lo || up) cased = true;
    }
    return BOOL(cased);
}
static Obj *s_isupper(ARGS) { UNUSED; return case_check(SELF, true); }
static Obj *s_islower(ARGS) { UNUSED; return case_check(SELF, false); }
static Obj *s_istitle(ARGS) {
    UNUSED;
    Str *s = SELF;
    bool cased = false, prev = false;
    for (uint32_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->s[i];
        if (isupper(c)) { if (prev) return Py_False; prev = true; cased = true; }
        else if (islower(c)) { if (!prev) return Py_False; cased = true; }
        else prev = false;
    }
    return BOOL(cased);
}
static Obj *s_isidentifier(ARGS) {
    UNUSED;
    Str *s = SELF;
    if (s->len == 0 || isdigit((unsigned char)s->s[0])) return Py_False;
    for (uint32_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->s[i];
        if (!(isalnum(c) || c == '_' || c >= 0x80)) return Py_False;
    }
    return Py_True;
}

static Obj *just_impl(int argc, Obj **argv, char align, const char *fn) {
    Str *s = SELF;
    int64_t width;
    if (obj_index(argv[1], &width) < 0) return NULL;
    Spec sp;
    memset(&sp, 0, sizeof sp);
    sp.fill = ' ';
    if (argc > 2) {
        if (!IS_STR(argv[2]) || AS_STR(argv[2])->nchars != 1)
            return err(E_TypeError, "%s: the fill character must be exactly one character long", fn);
        const char *p = AS_STR(argv[2])->s;
        sp.fill = utf8_next(&p);
    }
    if (width <= s->nchars) return (Obj *)s;
    if (width > 100000000) return err(E_OverflowError, "width too large");
    SB b;
    sb_init(&b);
    if (align == '^') {
        /* CPython puts the extra space on the left when both pad and width are odd. */
        int64_t pad = width - s->nchars, left = pad / 2 + (pad & width & 1);
        for (int64_t i = 0; i < left; i++) sb_putcp(&b, sp.fill);
        sb_put(&b, s->s, s->len);
        for (int64_t i = 0; i < pad - left; i++) sb_putcp(&b, sp.fill);
        return sb_finish(&b);
    }
    sp.align = align;
    sp.width = (int)width;
    pad_out(&b, &sp, "", 0, s->s, s->len, align);
    return sb_finish(&b);
}
static Obj *s_ljust(ARGS) { (void)kwnames; return just_impl(argc, argv, '<', "ljust"); }
static Obj *s_rjust(ARGS) { (void)kwnames; return just_impl(argc, argv, '>', "rjust"); }
static Obj *s_center(ARGS) { (void)kwnames; return just_impl(argc, argv, '^', "center"); }
static Obj *s_zfill(ARGS) {
    UNUSED;
    Str *s = SELF;
    int64_t width;
    if (obj_index(argv[1], &width) < 0) return NULL;
    if (width <= s->nchars) return (Obj *)s;
    if (width > 100000000) return err(E_OverflowError, "width too large");
    SB b;
    sb_init(&b);
    size_t sign = s->len && (s->s[0] == '+' || s->s[0] == '-') ? 1 : 0;
    sb_put(&b, s->s, sign);
    for (int64_t i = s->nchars; i < width; i++) sb_putc(&b, '0');
    sb_put(&b, s->s + sign, s->len - sign);
    return sb_finish(&b);
}

static Obj *partition_impl(Obj **argv, bool rev) {
    Str *s = SELF;
    if (!IS_STR(argv[1])) return err(E_TypeError, "must be str, not %s", tname(argv[1]));
    Str *sep = AS_STR(argv[1]);
    if (sep->len == 0) return err(E_ValueError, "empty separator");
    const char *hit = NULL;
    if (!rev) hit = memmem(s->s, s->len, sep->s, sep->len);
    else if (sep->len <= s->len)
        for (const char *t = s->s + s->len - sep->len; t >= s->s; t--)
            if (memcmp(t, sep->s, sep->len) == 0) { hit = t; break; }
    Obj *empty = str_new("", 0);
    if (!hit) return rev ? tuple_pack(3, empty, empty, s) : tuple_pack(3, s, empty, empty);
    return tuple_pack(3, str_new(s->s, (size_t)(hit - s->s)), sep,
                      str_new(hit + sep->len, (size_t)(s->s + s->len - hit - sep->len)));
}
static Obj *s_partition(ARGS) { UNUSED; return partition_impl(argv, false); }
static Obj *s_rpartition(ARGS) { UNUSED; return partition_impl(argv, true); }

static Obj *s_expandtabs(ARGS) {
    UNUSED;
    Str *s = SELF;
    int64_t tab = 8, col = 0;
    if (argc > 1 && obj_index(argv[1], &tab) < 0) return NULL;
    SB b;
    sb_init(&b);
    for (uint32_t i = 0; i < s->len; i++) {
        char c = s->s[i];
        if (c == '\t') {
            if (tab > 0) { int64_t n = tab - col % tab; for (int64_t k = 0; k < n; k++) sb_putc(&b, ' '); col += n; }
        } else {
            sb_putc(&b, c);
            if (c == '\n' || c == '\r') col = 0;
            else if (((unsigned char)c & 0xC0) != 0x80) col++;
        }
    }
    return sb_finish(&b);
}

static Obj *s_encode(ARGS) {
    UNUSED;
    return err(E_NotImplementedError, "str.encode() needs the bytes type, which is not supported");
}

/* ================= list methods ================= */
#define LSELF ((List *)argv[0])
static Obj *l_append(ARGS) { UNUSED; list_append(LSELF, argv[1]); return Py_None; }
static Obj *l_extend(ARGS) { UNUSED; return list_extend(LSELF, argv[1]) < 0 ? NULL : Py_None; }
static Obj *l_insert(ARGS) {
    UNUSED;
    List *l = LSELF;
    int64_t i;
    if (obj_index(argv[1], &i) < 0) return NULL;
    if (i < 0) { i += l->n; if (i < 0) i = 0; }
    if (i > l->n) i = l->n;
    list_insert(l, i, argv[2]);
    return Py_None;
}
static Obj *l_pop(ARGS) {
    UNUSED;
    List *l = LSELF;
    int64_t i = -1;
    if (argc > 1 && obj_index(argv[1], &i) < 0) return NULL;
    if (l->n == 0) return err(E_IndexError, "pop from empty list");
    if (i < 0) i += l->n;
    if (i < 0 || i >= l->n) return err(E_IndexError, "pop index out of range");
    Obj *v = l->items[i];
    memmove(l->items + i, l->items + i + 1, (size_t)(l->n - i - 1) * sizeof(Obj *));
    l->n--;
    return v;
}
static Obj *l_remove(ARGS) {
    UNUSED;
    List *l = LSELF;
    for (int64_t i = 0; i < l->n; i++) {
        int e = obj_eq(l->items[i], argv[1]);
        if (e < 0) return NULL;
        if (e) {
            memmove(l->items + i, l->items + i + 1, (size_t)(l->n - i - 1) * sizeof(Obj *));
            l->n--;
            return Py_None;
        }
    }
    return err(E_ValueError, "list.remove(x): x not in list");
}
static Obj *seq_index(Obj **items, int64_t *np, int argc, Obj **argv, const char *what) {
    int64_t n = *np, start = 0, end = n;
    if (argc > 2 && obj_index(argv[2], &start) < 0) return NULL;
    if (argc > 3 && obj_index(argv[3], &end) < 0) return NULL;
    if (start < 0) { start += n; if (start < 0) start = 0; }
    if (end < 0) { end += n; if (end < 0) end = 0; }
    for (int64_t i = start; i < end && i < *np; i++) {
        int e = obj_eq(items[i], argv[1]);
        if (e < 0) return NULL;
        if (e) return int_new(i);
    }
    if (strcmp(what, "list") == 0) {
        Obj *r = obj_repr(argv[1]);
        if (!r) return NULL;
        return err(E_ValueError, "%s is not in list", AS_STR(r)->s);
    }
    return err(E_ValueError, "tuple.index(x): x not in tuple");
}
static Obj *l_index(ARGS) { (void)kwnames; return seq_index(LSELF->items, &LSELF->n, argc, argv, "list"); }
static Obj *seq_count(Obj **items, int64_t *np, Obj *x) {
    int64_t c = 0;
    for (int64_t i = 0; i < *np; i++) {
        int e = obj_eq(items[i], x);
        if (e < 0) return NULL;
        c += e;
    }
    return int_new(c);
}
static Obj *l_count(ARGS) { UNUSED; return seq_count(LSELF->items, &LSELF->n, argv[1]); }
static Obj *l_sort(ARGS) {
    if (kw_check(kwnames, "sort", "key,reverse") < 0) return NULL;
    if (argc != 1) return err(E_TypeError, "sort() takes no positional arguments");
    Obj *rev = KW("reverse");
    int r = rev ? obj_truthy(rev) : 0;
    if (r < 0 || list_sort(LSELF, KW("key"), r) < 0) return NULL;
    return Py_None;
}
static Obj *l_reverse(ARGS) {
    UNUSED;
    List *l = LSELF;
    for (int64_t i = 0, j = l->n - 1; i < j; i++, j--) { Obj *t = l->items[i]; l->items[i] = l->items[j]; l->items[j] = t; }
    return Py_None;
}
static Obj *l_copy(ARGS) { UNUSED; return list_from(LSELF->items, LSELF->n); }
static Obj *l_clear(ARGS) { UNUSED; LSELF->n = 0; return Py_None; }
static Obj *t_index(ARGS) { (void)kwnames; Tuple *t = (Tuple *)argv[0]; return seq_index(t->items, &t->n, argc, argv, "tuple"); }
static Obj *t_count(ARGS) { UNUSED; Tuple *t = (Tuple *)argv[0]; return seq_count(t->items, &t->n, argv[1]); }

/* ================= dict methods ================= */
#define DSELF ((Dict *)argv[0])
static Obj *d_get(ARGS) {
    UNUSED;
    Obj *v;
    int r = dict_get(DSELF, argv[1], &v);
    if (r < 0) return NULL;
    return r ? v : argc > 2 ? argv[2] : Py_None;
}
static Obj *dict_view(Obj *d, Type *t, int kind) {
    DictView *v = gc_alloc(t, sizeof(DictView));
    v->d = (Dict *)d; v->kind = kind;
    return (Obj *)v;
}
static Obj *d_keys(ARGS) { UNUSED; return dict_view(argv[0], T_dict_keys, 0); }
static Obj *d_values(ARGS) { UNUSED; return dict_view(argv[0], T_dict_values, 1); }
static Obj *d_items(ARGS) { UNUSED; return dict_view(argv[0], T_dict_items, 2); }
static Obj *d_pop(ARGS) {
    UNUSED;
    Obj *v;
    int r = dict_get(DSELF, argv[1], &v);
    if (r < 0) return NULL;
    if (!r) {
        if (argc > 2) return argv[2];
        return err_obj(E_KeyError, argv[1]);
    }
    if (dict_del(DSELF, argv[1]) < 0) return NULL;
    return v;
}
static Obj *d_popitem(ARGS) {
    UNUSED;
    Dict *d = DSELF;
    for (int64_t i = d->n - 1; i >= 0; i--) {
        if (!d->e[i].key) continue;
        Obj *k = d->e[i].key, *v = d->e[i].val;
        if (dict_del(d, k) < 0) return NULL;
        return tuple_pack(2, k, v);
    }
    return err(E_KeyError, "popitem(): dictionary is empty");
}
static Obj *d_setdefault(ARGS) {
    UNUSED;
    Obj *v;
    int r = dict_get(DSELF, argv[1], &v);
    if (r < 0) return NULL;
    if (r) return v;
    v = argc > 2 ? argv[2] : Py_None;
    if (dict_set(DSELF, argv[1], v) < 0) return NULL;
    return v;
}
static Obj *d_update(ARGS) {
    if (argc > 2) return err(E_TypeError, "update expected at most 1 argument, got %d", argc - 1);
    if (argc == 2 && dict_update(DSELF, argv[1]) < 0) return NULL;
    Tuple *kn = (Tuple *)kwnames;
    for (int i = 0; kn && i < kn->n; i++) dict_sets(DSELF, AS_STR(kn->items[i]), argv[argc + i]);
    return Py_None;
}
static Obj *d_clear(ARGS) { UNUSED; dict_clear(DSELF); return Py_None; }
static Obj *d_copy(ARGS) { UNUSED; return dict_copy(DSELF); }
static Obj *d_fromkeys(ARGS) {
    UNUSED;
    Dict *d = (Dict *)dict_new();
    Obj *it = obj_iter(argv[0]), *k;
    if (!it) return NULL;
    while ((k = iter_next(it))) if (dict_set(d, k, argc > 1 ? argv[1] : Py_None) < 0) return NULL;
    return vm.exc ? NULL : (Obj *)d;
}

/* ================= set methods ================= */
#define SSELF ((Set *)argv[0])
static Obj *as_set(Obj *o) {
    if (KIND(o) == K_SET) return o;
    Set *s = (Set *)set_new(T_set);
    if (set_update(s, o) < 0) return NULL;
    return (Obj *)s;
}
static Obj *st_add(ARGS) { UNUSED; return set_add(SSELF, argv[1]) < 0 ? NULL : Py_None; }
static Obj *st_remove(ARGS) {
    UNUSED;
    int r = dict_del(SSELF->d, argv[1]);
    if (r < 0) return NULL;
    if (!r) return err_obj(E_KeyError, argv[1]);
    return Py_None;
}
static Obj *st_discard(ARGS) { UNUSED; return dict_del(SSELF->d, argv[1]) < 0 ? NULL : Py_None; }
static Obj *st_pop(ARGS) {
    UNUSED;
    Dict *d = SSELF->d;
    for (int64_t i = 0; i < d->n; i++) {
        if (!d->e[i].key) continue;
        Obj *k = d->e[i].key;
        if (dict_del(d, k) < 0) return NULL;
        return k;
    }
    return err(E_KeyError, "pop from an empty set");
}
static Obj *st_clear(ARGS) { UNUSED; dict_clear(SSELF->d); return Py_None; }
static Obj *st_copy(ARGS) {
    UNUSED;
    Set *s = (Set *)set_new(isinstance(argv[0], T_frozenset) ? T_frozenset : T_set);
    s->d = (Dict *)dict_copy(SSELF->d);
    return (Obj *)s;
}
static Obj *set_multi(int argc, Obj **argv, int op, bool inplace) {
    Obj *acc = inplace ? argv[0] : st_copy(1, argv, NULL);
    for (int i = 1; i < argc; i++) {
        Obj *o = as_set(argv[i]);
        if (!o) return NULL;
        Obj *r = binary_op(op, acc, o, false);
        if (!r) return NULL;
        if (inplace) ((Set *)acc)->d = ((Set *)r)->d; else acc = r;
    }
    return inplace ? Py_None : acc;
}
static Obj *st_union(ARGS) { (void)kwnames; return set_multi(argc, argv, OP_OR, false); }
static Obj *st_intersection(ARGS) { (void)kwnames; return set_multi(argc, argv, OP_AND, false); }
static Obj *st_difference(ARGS) { (void)kwnames; return set_multi(argc, argv, OP_SUB, false); }
static Obj *st_symdiff(ARGS) { (void)kwnames; return set_multi(argc, argv, OP_XOR, false); }
static Obj *st_update(ARGS) { (void)kwnames; return set_multi(argc, argv, OP_OR, true); }
static Obj *st_intersection_update(ARGS) { (void)kwnames; return set_multi(argc, argv, OP_AND, true); }
static Obj *st_difference_update(ARGS) { (void)kwnames; return set_multi(argc, argv, OP_SUB, true); }
static Obj *st_symdiff_update(ARGS) { (void)kwnames; return set_multi(argc, argv, OP_XOR, true); }
static Obj *st_issubset(ARGS) { UNUSED; Obj *o = as_set(argv[1]); return o ? obj_richcmp(argv[0], o, CMP_LE) : NULL; }
static Obj *st_issuperset(ARGS) { UNUSED; Obj *o = as_set(argv[1]); return o ? obj_richcmp(argv[0], o, CMP_GE) : NULL; }
static Obj *st_isdisjoint(ARGS) {
    UNUSED;
    Obj *o = as_set(argv[1]);
    if (!o) return NULL;
    Obj *r = binary_op(OP_AND, argv[0], o, false);
    return r ? BOOL(((Set *)r)->d->used == 0) : NULL;
}

void methods_init(void) {
#define M(t, name, fn, lo, hi, fl) type_add_method(T_##t, name, fn, lo, hi, fl)
    M(str, "format", s_format, 0, -1, BF_KW); M(str, "format_map", s_format_map, 1, 1, 0);
    M(str, "lower", s_lower, 0, 0, 0); M(str, "upper", s_upper, 0, 0, 0); M(str, "casefold", s_casefold, 0, 0, 0);
    M(str, "swapcase", s_swapcase, 0, 0, 0); M(str, "capitalize", s_capitalize, 0, 0, 0); M(str, "title", s_title, 0, 0, 0);
    M(str, "strip", s_strip, 0, 1, 0); M(str, "lstrip", s_lstrip, 0, 1, 0); M(str, "rstrip", s_rstrip, 0, 1, 0);
    M(str, "split", s_split, 0, 2, BF_KW); M(str, "rsplit", s_rsplit, 0, 2, BF_KW);
    M(str, "splitlines", s_splitlines, 0, 1, BF_KW); M(str, "join", s_join, 1, 1, 0);
    M(str, "replace", s_replace, 2, 3, 0); M(str, "find", s_find, 1, 3, 0); M(str, "rfind", s_rfind, 1, 3, 0);
    M(str, "index", s_index, 1, 3, 0); M(str, "rindex", s_rindex, 1, 3, 0); M(str, "count", s_count, 1, 3, 0);
    M(str, "startswith", s_startswith, 1, 3, 0); M(str, "endswith", s_endswith, 1, 3, 0);
    M(str, "removeprefix", s_removeprefix, 1, 1, 0); M(str, "removesuffix", s_removesuffix, 1, 1, 0);
    M(str, "isdigit", s_isdigit, 0, 0, 0); M(str, "isdecimal", s_isdigit, 0, 0, 0); M(str, "isnumeric", s_isdigit, 0, 0, 0);
    M(str, "isalpha", s_isalpha, 0, 0, 0); M(str, "isalnum", s_isalnum, 0, 0, 0); M(str, "isspace", s_isspace, 0, 0, 0);
    M(str, "isascii", s_isascii, 0, 0, 0); M(str, "isprintable", s_isprintable, 0, 0, 0);
    M(str, "isupper", s_isupper, 0, 0, 0); M(str, "islower", s_islower, 0, 0, 0); M(str, "istitle", s_istitle, 0, 0, 0);
    M(str, "isidentifier", s_isidentifier, 0, 0, 0); M(str, "ljust", s_ljust, 1, 2, 0); M(str, "rjust", s_rjust, 1, 2, 0);
    M(str, "center", s_center, 1, 2, 0); M(str, "zfill", s_zfill, 1, 1, 0); M(str, "partition", s_partition, 1, 1, 0);
    M(str, "rpartition", s_rpartition, 1, 1, 0); M(str, "expandtabs", s_expandtabs, 0, 1, 0);
    M(str, "encode", s_encode, 0, 2, BF_KW);

    M(list, "append", l_append, 1, 1, 0); M(list, "extend", l_extend, 1, 1, 0); M(list, "insert", l_insert, 2, 2, 0);
    M(list, "pop", l_pop, 0, 1, 0); M(list, "remove", l_remove, 1, 1, 0); M(list, "index", l_index, 1, 3, 0);
    M(list, "count", l_count, 1, 1, 0); M(list, "sort", l_sort, 0, 0, BF_KW); M(list, "reverse", l_reverse, 0, 0, 0);
    M(list, "copy", l_copy, 0, 0, 0); M(list, "clear", l_clear, 0, 0, 0);
    M(tuple, "index", t_index, 1, 3, 0); M(tuple, "count", t_count, 1, 1, 0);

    M(dict, "get", d_get, 1, 2, 0); M(dict, "keys", d_keys, 0, 0, 0); M(dict, "values", d_values, 0, 0, 0);
    M(dict, "items", d_items, 0, 0, 0); M(dict, "pop", d_pop, 1, 2, 0); M(dict, "popitem", d_popitem, 0, 0, 0);
    M(dict, "setdefault", d_setdefault, 1, 2, 0); M(dict, "update", d_update, 0, 1, BF_KW);
    M(dict, "clear", d_clear, 0, 0, 0); M(dict, "copy", d_copy, 0, 0, 0);
    type_add_static(T_dict, "fromkeys", d_fromkeys, 1, 2, 0);

    M(set, "add", st_add, 1, 1, 0); M(set, "remove", st_remove, 1, 1, 0); M(set, "discard", st_discard, 1, 1, 0);
    M(set, "pop", st_pop, 0, 0, 0); M(set, "clear", st_clear, 0, 0, 0); M(set, "update", st_update, 0, -1, 0);
    M(set, "intersection_update", st_intersection_update, 0, -1, 0);
    M(set, "difference_update", st_difference_update, 0, -1, 0);
    M(set, "symmetric_difference_update", st_symdiff_update, 1, 1, 0);
    Type *both[2] = {T_set, T_frozenset};
    for (int i = 0; i < 2; i++) {
        Type *t = both[i];
        type_add_method(t, "copy", st_copy, 0, 0, 0); type_add_method(t, "union", st_union, 0, -1, 0);
        type_add_method(t, "intersection", st_intersection, 0, -1, 0);
        type_add_method(t, "difference", st_difference, 0, -1, 0);
        type_add_method(t, "symmetric_difference", st_symdiff, 1, 1, 0);
        type_add_method(t, "issubset", st_issubset, 1, 1, 0); type_add_method(t, "issuperset", st_issuperset, 1, 1, 0);
        type_add_method(t, "isdisjoint", st_isdisjoint, 1, 1, 0);
    }
#undef M
}
