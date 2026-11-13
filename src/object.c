/* Core object types: creation, text form, hashing, equality, dict and set storage. */
#include "py.h"
#include <wctype.h>

Obj *Py_None, *Py_True, *Py_False, *Py_NotImplemented, *Py_Ellipsis;
#define X(n) Type *T_##n;
TYPES(X)
#undef X
#define X(n, b) Type *E_##n;
EXCS(X)
#undef X
#define X(n) Str *N_##n;
NAMES(X)
#undef X
Str *N_args;
static Obj *chr_cache[128];
Obj *raw_obj;

Dict *obj_dict(Obj *o, bool create) {
    Dict **slot;
    int k = KIND(o);
    if (k == K_INSTANCE || k == K_EXC) slot = &((Instance *)o)->dict;
    else if (o->type->user) slot = (Dict **)((char *)o + o->size - sizeof(Dict *));
    else return NULL;
    if (!*slot && create) { Dict *d = (Dict *)dict_new(); *slot = d; }
    return *slot;
}

Obj *user_override(Obj *o, Str *name) {
    Obj *f = type_lookup(o->type, name);
    return f && KIND(f) != K_BUILTIN ? f : NULL;
}

/* The built-in type a class is built on (int for class Celsius(int)). */
Type *builtin_base(Type *t) {
    for (int64_t i = 0; i < t->mro->n; i++) {
        Type *b = (Type *)t->mro->items[i];
        if (!b->user && b->ctor) return b;
    }
    return NULL;
}

/* Makes an instance of class t holding a copy of the built-in value base. */
#define SUBSIZE(n) ((((size_t)(n) + 7) & ~(size_t)7) + sizeof(Dict *))
Obj *subclass_instance(Type *t, Obj *base) {
    switch (t->kind) {
    case K_INT: {
        Int *s = (Int *)base, *n = gc_alloc(t, SUBSIZE(sizeof(Int)));
        n->v = s->v;
        if (s->d) {
            n->d = malloc(s->nd * sizeof(uint32_t));
            memcpy(n->d, s->d, s->nd * sizeof(uint32_t));
            n->nd = s->nd; n->neg = s->neg;
        }
        return (Obj *)n;
    }
    case K_FLOAT: {
        Float *n = gc_alloc(t, SUBSIZE(sizeof(Float)));
        n->v = ((Float *)base)->v;
        return (Obj *)n;
    }
    case K_STR: {
        Str *s = AS_STR(base), *n = gc_alloc(t, SUBSIZE(sizeof(Str) + s->len + 1));
        memcpy(n->s, s->s, (size_t)s->len + 1);
        n->len = s->len; n->nchars = s->nchars;
        return (Obj *)n;
    }
    case K_TUPLE: {
        Tuple *s = (Tuple *)base, *n = gc_alloc(t, SUBSIZE(sizeof(Tuple) + (size_t)s->n * sizeof(Obj *)));
        n->n = s->n;
        for (int64_t i = 0; i < s->n; i++) n->items[i] = s->items[i];
        return (Obj *)n;
    }
    case K_LIST: {
        List *s = (List *)base, *n = gc_alloc(t, SUBSIZE(sizeof(List)));
        for (int64_t i = 0; i < s->n; i++) list_append(n, s->items[i]);
        return (Obj *)n;
    }
    case K_DICT: {
        Dict *s = (Dict *)base, *n = gc_alloc(t, SUBSIZE(sizeof(Dict)));
        for (int64_t i = 0; i < s->n; i++)
            if (s->e[i].key) dict_set(n, s->e[i].key, s->e[i].val);
        return (Obj *)n;
    }
    case K_SET: {
        Set *n = gc_alloc(t, SUBSIZE(sizeof(Set)));
        Obj *d = dict_copy(((Set *)base)->d);
        n->d = (Dict *)d;
        return (Obj *)n;
    }
    default: return err(E_TypeError, "cannot create '%s' instances", t->name->s);
    }
}

/* ---------- types ---------- */
static void type_setup(Type *t, const char *name, Type *base, int kind) {
    t->name = AS_STR(str_c(name));
    t->kind = kind;
    t->dict = (Dict *)dict_new();
    if (base) {
        t->bases = (Tuple *)tuple_pack(1, base);
        t->mro = (Tuple *)tuple_new(1 + base->mro->n);
        t->mro->items[0] = (Obj *)t;
        for (int64_t i = 0; i < base->mro->n; i++) t->mro->items[i + 1] = base->mro->items[i];
    } else {
        t->bases = (Tuple *)tuple_new(0);
        t->mro = (Tuple *)tuple_pack(1, t);
    }
}

Type *type_new(const char *name, Type *base, int kind) {
    Type *t = gc_alloc(T_type, sizeof(Type));
    type_setup(t, name, base, kind);
    return t;
}

void objects_init(void) {
#define X(n) T_##n = gc_alloc(NULL, sizeof(Type)); gc_root((Obj **)&T_##n);
    TYPES(X)
#undef X
#define X(n) T_##n->h.type = T_type;
    TYPES(X)
#undef X
    type_setup(T_object, "object", NULL, K_INSTANCE);
#define S(n, name, kind) type_setup(T_##n, name, T_object, kind)
    S(type, "type", K_TYPE); S(NoneType, "NoneType", K_NONE);
    S(NotImplementedType, "NotImplementedType", K_NOTIMPL); S(ellipsis, "ellipsis", K_ELLIPSIS);
    S(int, "int", K_INT); S(float, "float", K_FLOAT); S(str, "str", K_STR); S(list, "list", K_LIST);
    S(tuple, "tuple", K_TUPLE); S(dict, "dict", K_DICT); S(set, "set", K_SET);
    S(frozenset, "frozenset", K_SET); S(range, "range", K_RANGE); S(slice, "slice", K_SLICE);
    S(function, "function", K_FUNC); S(builtin, "builtin_function_or_method", K_BUILTIN);
    S(method, "method", K_METHOD); S(module, "module", K_MODULE); S(code, "code", K_CODE);
    S(frame, "frame", K_FRAME); S(cell, "cell", K_CELL); S(generator, "generator", K_GEN);
    S(iterator, "iterator", K_ITER); S(property, "property", K_PROPERTY);
    S(staticmethod, "staticmethod", K_WRAPPER); S(classmethod, "classmethod", K_WRAPPER);
    S(super, "super", K_SUPER); S(dict_keys, "dict_keys", K_DICTVIEW);
    S(dict_values, "dict_values", K_DICTVIEW); S(dict_items, "dict_items", K_DICTVIEW);
    S(file, "TextIOWrapper", K_FILE);
#undef S
    type_setup(T_bool, "bool", T_int, K_BOOL);
    Type *E_0 = T_object;
#define X(n, b) E_##n = type_new(#n, E_##b, K_EXC); gc_root((Obj **)&E_##n);
    EXCS(X)
#undef X
#define X(n) N_##n = AS_STR(str_c("__" #n "__")); gc_root((Obj **)&N_##n);
    NAMES(X)
#undef X
    N_args = AS_STR(str_c("args")); gc_root((Obj **)&N_args);
    Py_None = gc_alloc(T_NoneType, sizeof(Obj)); gc_root(&Py_None);
    Py_NotImplemented = gc_alloc(T_NotImplementedType, sizeof(Obj)); gc_root(&Py_NotImplemented);
    Py_Ellipsis = gc_alloc(T_ellipsis, sizeof(Obj)); gc_root(&Py_Ellipsis);
    Py_True = gc_alloc(T_bool, sizeof(Int)); ((Int *)Py_True)->v = 1; gc_root(&Py_True);
    Py_False = gc_alloc(T_bool, sizeof(Int)); gc_root(&Py_False);
}

const char *tname(Obj *o) { return o->type->name->s; }

bool type_issub(Type *t, Type *base) {
    if (t == base) return true;
    for (int64_t i = 0; i < t->mro->n; i++)
        if (t->mro->items[i] == (Obj *)base) return true;
    return false;
}
bool isinstance(Obj *o, Type *t) { return type_issub(o->type, t); }

Obj *type_lookup(Type *t, Str *name) {
    for (int64_t i = 0; i < t->mro->n; i++) {
        Obj *v = dict_gets(((Type *)t->mro->items[i])->dict, name);
        if (v) return v;
    }
    return NULL;
}

/* ---------- errors ---------- */
Obj *exc_new(Type *t, Tuple *args) {
    Exc *e = gc_alloc(t, sizeof(Exc));
    e->args = args;
    return (Obj *)e;
}

void err_set(Obj *exc) {
    Exc *e = (Exc *)exc;
    if (vm.handled && vm.handled != exc && !e->context) e->context = vm.handled;
    vm.exc = exc;
}

Obj *err(Type *t, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    char probe[1];
    int n = vsnprintf(probe, 0, fmt, ap);
    va_end(ap);
    char *buf = malloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    Obj *msg = str_new(buf, (size_t)n);
    free(buf);
    err_set(exc_new(t, (Tuple *)tuple_pack(1, msg)));
    return NULL;
}

Obj *err_obj(Type *t, Obj *arg) {
    err_set(exc_new(t, (Tuple *)tuple_pack(1, arg)));
    return NULL;
}

bool err_matches(Type *t) { return vm.exc && isinstance(vm.exc, t); }
void err_clear(void) { vm.exc = NULL; }

/* ---------- float ---------- */
Obj *float_new(double v) {
    Float *f = gc_alloc(T_float, sizeof(Float));
    f->v = v;
    return (Obj *)f;
}

void float_repr(SB *b, double v) {
    if (isnan(v)) { sb_puts(b, "nan"); return; }
    if (isinf(v)) { sb_puts(b, v < 0 ? "-inf" : "inf"); return; }
    if (v == 0) { sb_puts(b, signbit(v) ? "-0.0" : "0.0"); return; }
    char buf[48];
    for (int prec = 1; prec <= 17; prec++) {
        snprintf(buf, sizeof buf, "%.*e", prec - 1, v);
        if (strtod(buf, NULL) == v) break;
    }
    char digits[24];
    int nd = 0;
    const char *p = buf;
    if (*p == '-') { sb_putc(b, '-'); p++; }
    for (; *p && *p != 'e'; p++) if (*p != '.') digits[nd++] = *p;
    int ex = atoi(p + 1);
    while (nd > 1 && digits[nd - 1] == '0') nd--;
    if (ex >= -4 && ex < 16) {
        if (ex >= 0) {
            for (int i = 0; i <= ex; i++) sb_putc(b, i < nd ? digits[i] : '0');
            sb_putc(b, '.');
            if (nd > ex + 1) sb_put(b, digits + ex + 1, (size_t)(nd - ex - 1));
            else sb_putc(b, '0');
        } else {
            sb_puts(b, "0.");
            for (int i = 0; i < -ex - 1; i++) sb_putc(b, '0');
            sb_put(b, digits, (size_t)nd);
        }
    } else {
        sb_putc(b, digits[0]);
        if (nd > 1) { sb_putc(b, '.'); sb_put(b, digits + 1, (size_t)(nd - 1)); }
        sb_printf(b, "e%c%02d", ex < 0 ? '-' : '+', abs(ex));
    }
}

/* ---------- str ---------- */
Obj *str_new(const char *s, size_t len) {
    if (len >= UINT32_MAX - sizeof(Str) - 1) fatal("string too large");
    Str *o = gc_alloc(T_str, sizeof(Str) + len + 1);
    if (len) memcpy(o->s, s, len);
    o->s[len] = 0;
    o->len = (uint32_t)len;
    uint32_t n = 0;
    for (size_t i = 0; i < len; i++) n += ((unsigned char)s[i] & 0xC0) != 0x80;
    o->nchars = n;
    return (Obj *)o;
}
Obj *str_c(const char *s) { return str_new(s, strlen(s)); }

Obj *str_fmt(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    char probe[1];
    int n = vsnprintf(probe, 0, fmt, ap);
    va_end(ap);
    char *buf = malloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    Obj *r = str_new(buf, (size_t)n);
    free(buf);
    return r;
}

Obj *str_chr(uint32_t cp) {
    if (cp < 128) {
        if (!chr_cache[cp]) {
            char c = (char)cp;
            chr_cache[cp] = str_new(&c, 1);
            gc_root(&chr_cache[cp]);
        }
        return chr_cache[cp];
    }
    SB b;
    sb_init(&b);
    sb_putcp(&b, cp);
    return sb_finish(&b);
}

Obj *str_concat(Str *a, Str *b) {
    Str *o = gc_alloc(T_str, sizeof(Str) + (size_t)a->len + b->len + 1);
    memcpy(o->s, a->s, a->len);
    memcpy(o->s + a->len, b->s, b->len);
    o->len = a->len + b->len;
    o->nchars = a->nchars + b->nchars;
    return (Obj *)o;
}

uint64_t str_hash(Str *s) {
    if (s->hashed) return s->hash;
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < s->len; i++) { h ^= (unsigned char)s->s[i]; h *= 1099511628211ull; }
    h ^= h >> 29;
    s->hash = h;
    s->hashed = true;
    return h;
}
bool str_eq(Str *a, Str *b) { return a == b || (a->len == b->len && memcmp(a->s, b->s, a->len) == 0); }
bool str_eqc(Str *a, const char *c) { size_t n = strlen(c); return a->len == n && memcmp(a->s, c, n) == 0; }

uint32_t utf8_next(const char **pp) {
    const unsigned char *p = (const unsigned char *)*pp;
    uint32_t c = *p++;
    if (c >= 0xF0) { c = (c & 7) << 18 | (p[0] & 0x3F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F); p += 3; }
    else if (c >= 0xE0) { c = (c & 15) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F); p += 2; }
    else if (c >= 0xC0) { c = (c & 31) << 6 | (p[0] & 0x3F); p += 1; }
    *pp = (const char *)p;
    return c;
}

/* Byte offset of character number idx (0 <= idx <= nchars). */
size_t str_offset(Str *s, int64_t idx) {
    if (s->len == s->nchars) return (size_t)idx;
    size_t i = 0;
    while (idx > 0 && i < s->len) {
        i++;
        while (i < s->len && ((unsigned char)s->s[i] & 0xC0) == 0x80) i++;
        idx--;
    }
    return i;
}

Obj *str_slice_chars(Str *s, int64_t start, int64_t stop) {
    if (stop <= start) return str_new("", 0);
    size_t a = str_offset(s, start), b = str_offset(s, stop);
    return str_new(s->s + a, b - a);
}

/* ---------- list / tuple ---------- */
Obj *list_new(int64_t n) {
    List *l = gc_alloc(T_list, sizeof(List));
    if (n > 0) {
        l->items = calloc((size_t)n, sizeof(Obj *));
        if (!l->items) fatal("out of memory");
        gc_account((size_t)n * sizeof(Obj *));
        l->cap = n;
        l->n = n;
    }
    return (Obj *)l;
}

static void list_reserve(List *l, int64_t need) {
    if (need <= l->cap) return;
    int64_t cap = l->cap ? l->cap * 2 : 4;
    while (cap < need) cap *= 2;
    l->items = realloc(l->items, (size_t)cap * sizeof(Obj *));
    if (!l->items) fatal("out of memory");
    gc_account((size_t)(cap - l->cap) * sizeof(Obj *));
    l->cap = cap;
}

void list_append(List *l, Obj *o) {
    list_reserve(l, l->n + 1);
    l->items[l->n++] = o;
}

void list_insert(List *l, int64_t i, Obj *o) {
    list_reserve(l, l->n + 1);
    memmove(l->items + i + 1, l->items + i, (size_t)(l->n - i) * sizeof(Obj *));
    l->items[i] = o;
    l->n++;
}

Obj *list_from(Obj **items, int64_t n) {
    List *l = (List *)list_new(n);
    for (int64_t i = 0; i < n; i++) l->items[i] = items[i];
    return (Obj *)l;
}

Obj *tuple_new(int64_t n) {
    Tuple *t = gc_alloc(T_tuple, sizeof(Tuple) + (size_t)n * sizeof(Obj *));
    t->n = n;
    return (Obj *)t;
}

Obj *tuple_from(Obj **items, int64_t n) {
    Tuple *t = (Tuple *)tuple_new(n);
    for (int64_t i = 0; i < n; i++) t->items[i] = items[i];
    return (Obj *)t;
}

Obj *tuple_pack(int n, ...) {
    Tuple *t = (Tuple *)tuple_new(n);
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) t->items[i] = va_arg(ap, Obj *);
    va_end(ap);
    return (Obj *)t;
}

/* ---------- dict ---------- */
/* Entries are kept in insertion order in e[]. idx[] is the hash table and holds
   positions in e[] (-1 empty, -2 deleted). */
Obj *dict_new(void) { return gc_alloc(T_dict, sizeof(Dict)); }

static void dict_resize(Dict *d) {
    int64_t isize = 8;
    while (isize * 2 / 3 < d->used * 2 + 1) isize *= 2;
    int64_t cap = isize * 2 / 3;
    DEntry *e = malloc((size_t)cap * sizeof *e);
    int32_t *idx = malloc((size_t)isize * sizeof *idx);
    if (!e || !idx) fatal("out of memory");
    gc_account((size_t)cap * sizeof *e + (size_t)isize * sizeof *idx);
    for (int64_t i = 0; i < isize; i++) idx[i] = -1;
    int64_t n = 0, mask = isize - 1;
    for (int64_t i = 0; i < d->n; i++) {
        if (!d->e[i].key) continue;
        e[n] = d->e[i];
        uint64_t h = e[n].hash, perturb = h;
        int64_t j = (int64_t)(h & (uint64_t)mask);
        while (idx[j] != -1) { perturb >>= 5; j = (int64_t)(((uint64_t)j * 5 + perturb + 1) & (uint64_t)mask); }
        idx[j] = (int32_t)n++;
    }
    free(d->e); free(d->idx);
    d->e = e; d->idx = idx; d->n = n; d->cap = cap; d->isize = isize;
}

/* Returns the entry position, -1 if missing, -2 on error. */
static int64_t dict_find(Dict *d, Obj *key, uint64_t hash, int64_t *slot) {
    if (!d->isize) { if (slot) *slot = -1; return -1; }
    uint64_t mask = (uint64_t)d->isize - 1, perturb = hash;
    int64_t j = (int64_t)(hash & mask);
    bool kstr = IS_STR(key);
    for (;;) {
        int32_t ix = d->idx[j];
        if (ix == -1) { if (slot) *slot = j; return -1; }
        if (ix >= 0 && d->e[ix].hash == hash) {
            Obj *k = d->e[ix].key;
            if (k == key) { if (slot) *slot = j; return ix; }
            if (kstr && IS_STR(k)) {
                if (str_eq(AS_STR(k), AS_STR(key))) { if (slot) *slot = j; return ix; }
            } else if (!kstr || !IS_STR(k)) {
                DEntry *before = d->e;
                int r = obj_eq(k, key);
                if (r < 0) return -2;
                if (d->e != before) return dict_find(d, key, hash, slot); /* table changed under us */
                if (r) { if (slot) *slot = j; return ix; }
            }
        }
        perturb >>= 5;
        j = (int64_t)(((uint64_t)j * 5 + perturb + 1) & mask);
    }
}

static int dict_insert(Dict *d, Obj *k, uint64_t h, Obj *v) {
    int64_t slot, ix = dict_find(d, k, h, &slot);
    if (ix == -2) return -1;
    if (ix >= 0) { d->e[ix].val = v; return 0; }
    if (d->n >= d->cap) {
        dict_resize(d);
        if (d->n >= INT32_MAX - 1) fatal("dict too large");
        ix = dict_find(d, k, h, &slot);
        if (ix == -2) return -1;
    }
    d->e[d->n].key = k; d->e[d->n].val = v; d->e[d->n].hash = h;
    d->idx[slot] = (int32_t)d->n++;
    d->used++;
    return 0;
}

int dict_get(Dict *d, Obj *k, Obj **out) {
    uint64_t h;
    if (obj_hash(k, &h) < 0) return -1;
    int64_t ix = dict_find(d, k, h, NULL);
    if (ix == -2) return -1;
    if (ix < 0) return 0;
    *out = d->e[ix].val;
    return 1;
}

int dict_set(Dict *d, Obj *k, Obj *v) {
    uint64_t h;
    if (obj_hash(k, &h) < 0) return -1;
    return dict_insert(d, k, h, v);
}

int dict_del(Dict *d, Obj *k) {
    uint64_t h;
    if (obj_hash(k, &h) < 0) return -1;
    int64_t slot, ix = dict_find(d, k, h, &slot);
    if (ix == -2) return -1;
    if (ix < 0) return 0;
    d->e[ix].key = NULL; d->e[ix].val = NULL;
    d->idx[slot] = -2;
    d->used--;
    return 1;
}

Obj *dict_gets(Dict *d, Str *k) {
    int64_t ix = dict_find(d, (Obj *)k, str_hash(k), NULL);
    return ix >= 0 ? d->e[ix].val : NULL;
}
void dict_sets(Dict *d, Str *k, Obj *v) { dict_insert(d, (Obj *)k, str_hash(k), v); }
bool dict_dels(Dict *d, Str *k) { return dict_del(d, (Obj *)k) == 1; }
Obj *dict_getc(Dict *d, const char *k) { return dict_gets(d, AS_STR(str_c(k))); }
void dict_setc(Dict *d, const char *k, Obj *v) { dict_sets(d, AS_STR(str_c(k)), v); }

bool dict_next(Dict *d, int64_t *pos, Obj **k, Obj **v) {
    while (*pos < d->n) {
        DEntry *e = &d->e[(*pos)++];
        if (e->key) { if (k) *k = e->key; if (v) *v = e->val; return true; }
    }
    return false;
}

void dict_clear(Dict *d) {
    free(d->e); free(d->idx);
    d->e = NULL; d->idx = NULL;
    d->used = d->n = d->cap = d->isize = 0;
}

Obj *dict_copy(Dict *d) {
    Dict *c = (Dict *)dict_new();
    for (int64_t i = 0; i < d->n; i++)
        if (d->e[i].key) dict_insert(c, d->e[i].key, d->e[i].hash, d->e[i].val);
    return (Obj *)c;
}

/* ---------- small constructors ---------- */
Obj *set_new(Type *t) {
    Set *s = gc_alloc(t, sizeof(Set));
    s->d = (Dict *)dict_new();
    return (Obj *)s;
}
int set_add(Set *s, Obj *k) { return dict_set(s->d, k, Py_None); }

Obj *cell_new(Obj *v) { Cell *c = gc_alloc(T_cell, sizeof(Cell)); c->value = v; return (Obj *)c; }
Obj *slice_new(Obj *a, Obj *b, Obj *c) {
    Slice *s = gc_alloc(T_slice, sizeof(Slice));
    s->start = a; s->stop = b; s->step = c;
    return (Obj *)s;
}
Obj *range_new(int64_t start, int64_t stop, int64_t step) {
    Range *r = gc_alloc(T_range, sizeof(Range));
    r->start = start; r->stop = stop; r->step = step;
    return (Obj *)r;
}
int64_t range_len(Range *r) {
    if (r->step > 0) return r->stop > r->start ? (int64_t)(((uint64_t)r->stop - (uint64_t)r->start - 1) / (uint64_t)r->step) + 1 : 0;
    return r->stop < r->start ? (int64_t)(((uint64_t)r->start - (uint64_t)r->stop - 1) / ((uint64_t)0 - (uint64_t)r->step)) + 1 : 0;
}
Obj *builtin_new(const char *name, CFunc fn, int minargs, int maxargs, int flags) {
    Builtin *b = gc_alloc(T_builtin, sizeof(Builtin));
    b->name = name; b->fn = fn; b->minargs = minargs; b->maxargs = maxargs; b->flags = flags;
    return (Obj *)b;
}
Obj *method_new(Obj *func, Obj *self) {
    Method *m = gc_alloc(T_method, sizeof(Method));
    m->func = func; m->self = self;
    return (Obj *)m;
}
Obj *module_new(const char *name) {
    Module *m = gc_alloc(T_module, sizeof(Module));
    m->name = AS_STR(str_c(name));
    m->dict = (Dict *)dict_new();
    dict_sets(m->dict, N_name, (Obj *)m->name);
    dict_sets(m->dict, N_doc, Py_None);
    return (Obj *)m;
}
Obj *iter_new(int kind, Obj *a, Obj *b) {
    Iter *it = gc_alloc(T_iterator, sizeof(Iter));
    it->kind = kind; it->a = a; it->b = b;
    return (Obj *)it;
}

/* ---------- repr / str ---------- */
Obj *repr_force_default; /* when set, repr of this object skips a Python-level __repr__ */
static Obj *repr_stack[256];
static int repr_depth;

/* Whether repr() shows a character as itself. Format, separator, private-use
   and unassigned characters are escaped, as in CPython. */
bool cp_printable(uint32_t cp) {
    if (cp < 0x80) return cp >= 0x20 && cp != 0x7f;
    if (cp == 0xA0 || cp == 0xAD || (cp >= 0x600 && cp <= 0x605) || cp == 0x61C || cp == 0x6DD || cp == 0x70F ||
        cp == 0x1680 || cp == 0x180E || (cp >= 0x2000 && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202F) ||
        (cp >= 0x205F && cp <= 0x2064) || (cp >= 0x2066 && cp <= 0x206F) || cp == 0x3000 || cp == 0xFEFF ||
        (cp >= 0xFFF9 && cp <= 0xFFFB) || (cp >= 0xD800 && cp <= 0xF8FF) || cp > 0x10FFFF)
        return false;
    return iswprint((wint_t)cp) != 0;
}

static void str_repr(SB *b, Str *s) {
    bool has_sq = memchr(s->s, '\'', s->len) != NULL, has_dq = memchr(s->s, '"', s->len) != NULL;
    char q = (has_sq && !has_dq) ? '"' : '\'';
    sb_putc(b, q);
    for (const char *p = s->s, *e = s->s + s->len; p < e;) {
        unsigned char c = (unsigned char)*p;
        if (c >= 0x80) {
            const char *start = p;
            uint32_t cp = utf8_next(&p);
            if (cp_printable(cp)) sb_put(b, start, (size_t)(p - start));
            else if (cp < 0x100) sb_printf(b, "\\x%02x", cp);
            else if (cp < 0x10000) sb_printf(b, "\\u%04x", cp);
            else sb_printf(b, "\\U%08x", cp);
            continue;
        }
        p++;
        if (c == (unsigned char)q || c == '\\') { sb_putc(b, '\\'); sb_putc(b, (char)c); }
        else if (c == '\n') sb_puts(b, "\\n");
        else if (c == '\r') sb_puts(b, "\\r");
        else if (c == '\t') sb_puts(b, "\\t");
        else if (c < 0x20 || c == 0x7f) sb_printf(b, "\\x%02x", c);
        else sb_putc(b, (char)c);
    }
    sb_putc(b, q);
}

static int repr_to(SB *b, Obj *o);

static int repr_items(SB *b, Obj *holder, int64_t (*count)(Obj *), Obj *(*get)(Obj *, int64_t)) {
    for (int64_t i = 0; i < count(holder); i++) {
        if (i) sb_puts(b, ", ");
        if (repr_to(b, get(holder, i)) < 0) return -1;
    }
    return 0;
}
static int64_t cnt_list(Obj *o) { return ((List *)o)->n; }
static Obj *get_list(Obj *o, int64_t i) { return ((List *)o)->items[i]; }
static int64_t cnt_tuple(Obj *o) { return ((Tuple *)o)->n; }
static Obj *get_tuple(Obj *o, int64_t i) { return ((Tuple *)o)->items[i]; }

static int repr_dict_keys(SB *b, Dict *d, int what) {
    bool first = true;
    for (int64_t i = 0; i < d->n; i++) {
        Obj *k = d->e[i].key, *v = d->e[i].val;
        if (!k) continue;
        if (!first) sb_puts(b, ", ");
        first = false;
        if (what == 2) sb_putc(b, '(');
        if (what != 1 && repr_to(b, k) < 0) return -1;
        if (what == 3) sb_puts(b, ": ");
        if (what == 2) sb_puts(b, ", ");
        if (what != 0 && repr_to(b, v) < 0) return -1;
        if (what == 2) sb_putc(b, ')');
    }
    return 0;
}

static void type_qualname(SB *b, Type *t) {
    Obj *mod = dict_gets(t->dict, N_module);
    if (t->user && mod && IS_STR(mod) && !str_eqc(AS_STR(mod), "builtins")) { sb_putstr(b, mod); sb_putc(b, '.'); }
    Obj *qn = dict_gets(t->dict, N_qualname);
    if (qn && IS_STR(qn)) sb_putstr(b, qn); else sb_putstr(b, (Obj *)t->name);
}

static int repr_to(SB *b, Obj *o) {
    int k = KIND(o);
    if (o->type->user && o != repr_force_default && overridable(o)) {
        Obj *f = user_override(o, N_repr);
        if (f) {
            Obj *s = call1(f, o);
            if (!s) return -1;
            if (!IS_STR(s)) { err(E_TypeError, "__repr__ returned non-string (type %s)", tname(s)); return -1; }
            sb_putstr(b, s);
            return 0;
        }
    }
    switch (k) {
    case K_NONE: sb_puts(b, "None"); return 0;
    case K_NOTIMPL: sb_puts(b, "NotImplemented"); return 0;
    case K_ELLIPSIS: sb_puts(b, "Ellipsis"); return 0;
    case K_BOOL: sb_puts(b, o == Py_True ? "True" : "False"); return 0;
    case K_INT: int_format(b, o, 10, false); return 0;
    case K_FLOAT: float_repr(b, ((Float *)o)->v); return 0;
    case K_STR: str_repr(b, AS_STR(o)); return 0;
    case K_RANGE: {
        Range *r = (Range *)o;
        if (r->step == 1) sb_printf(b, "range(%lld, %lld)", (long long)r->start, (long long)r->stop);
        else sb_printf(b, "range(%lld, %lld, %lld)", (long long)r->start, (long long)r->stop, (long long)r->step);
        return 0;
    }
    case K_FUNC: sb_printf(b, "<function %s at %p>", ((Func *)o)->qualname->s, (void *)o); return 0;
    case K_BUILTIN: sb_printf(b, "<built-in function %s>", ((Builtin *)o)->name); return 0;
    case K_TYPE: sb_puts(b, "<class '"); type_qualname(b, (Type *)o); sb_puts(b, "'>"); return 0;
    case K_MODULE: sb_printf(b, "<module '%s'>", ((Module *)o)->name->s); return 0;
    case K_GEN: sb_printf(b, "<generator object %s at %p>", ((Generator *)o)->name->s, (void *)o); return 0;
    default: break;
    }
    for (int i = 0; i < repr_depth; i++)
        if (repr_stack[i] == o) {
            sb_puts(b, k == K_LIST ? "[...]" : k == K_DICT ? "{...}" : k == K_TUPLE ? "(...)" : "...");
            return 0;
        }
    if (repr_depth >= 200) {
        err(E_RecursionError, "maximum recursion depth exceeded while getting the repr of an object");
        return -1;
    }
    repr_stack[repr_depth++] = o;
    int r = 0;
    switch (k) {
    case K_LIST:
        sb_putc(b, '[');
        r = repr_items(b, o, cnt_list, get_list);
        sb_putc(b, ']');
        break;
    case K_TUPLE:
        if (strcmp(o->type->name->s, "types.UnionType") == 0) {
            for (int64_t i = 0; i < ((Tuple *)o)->n && r == 0; i++) {
                Obj *m = ((Tuple *)o)->items[i];
                if (i) sb_puts(b, " | ");
                if (m == (Obj *)T_NoneType) sb_puts(b, "None");
                else if (KIND(m) == K_TYPE) type_qualname(b, (Type *)m);
                else r = repr_to(b, m);
            }
            break;
        }
        sb_putc(b, '(');
        r = repr_items(b, o, cnt_tuple, get_tuple);
        if (((Tuple *)o)->n == 1) sb_putc(b, ',');
        sb_putc(b, ')');
        break;
    case K_DICT:
        sb_putc(b, '{');
        r = repr_dict_keys(b, (Dict *)o, 3);
        sb_putc(b, '}');
        break;
    case K_SET: {
        Dict *d = ((Set *)o)->d;
        bool frozen = isinstance(o, T_frozenset);
        if (d->used == 0) { sb_puts(b, frozen ? "frozenset()" : "set()"); break; }
        sb_puts(b, frozen ? "frozenset({" : "{");
        r = repr_dict_keys(b, d, 0);
        sb_puts(b, frozen ? "})" : "}");
        break;
    }
    case K_DICTVIEW: {
        DictView *v = (DictView *)o;
        sb_printf(b, "%s([", tname(o));
        r = repr_dict_keys(b, v->d, v->kind);
        sb_puts(b, "])");
        break;
    }
    case K_SLICE: {
        Slice *s = (Slice *)o;
        sb_puts(b, "slice(");
        if (repr_to(b, s->start) < 0 || (sb_puts(b, ", "), repr_to(b, s->stop)) < 0 ||
            (sb_puts(b, ", "), repr_to(b, s->step)) < 0) r = -1;
        sb_putc(b, ')');
        break;
    }
    case K_METHOD: {
        Method *m = (Method *)o;
        if (KIND(m->func) == K_FUNC) {
            sb_printf(b, "<bound method %s of ", ((Func *)m->func)->qualname->s);
            r = repr_to(b, m->self);
            sb_putc(b, '>');
        } else if (KIND(m->func) == K_BUILTIN) {
            sb_printf(b, "<built-in method %s of %s object at %p>", ((Builtin *)m->func)->name, tname(m->self), (void *)m->self);
        } else sb_printf(b, "<bound method at %p>", (void *)o);
        break;
    }
    case K_INSTANCE: case K_EXC: {
        Obj *f = o->type->user && o != repr_force_default ? type_lookup(o->type, N_repr) : NULL;
        if (f && KIND(f) != K_BUILTIN) {
            Obj *s = call1(f, o);
            if (!s) r = -1;
            else if (!IS_STR(s)) { err(E_TypeError, "__repr__ returned non-string (type %s)", tname(s)); r = -1; }
            else sb_putstr(b, s);
        } else if (k == K_EXC) {
            Exc *e = (Exc *)o;
            sb_putstr(b, (Obj *)o->type->name);
            sb_putc(b, '(');
            r = repr_items(b, (Obj *)e->args, cnt_tuple, get_tuple);
            sb_putc(b, ')');
        } else {
            sb_putc(b, '<');
            type_qualname(b, o->type);
            sb_printf(b, " object at %p>", (void *)o);
        }
        break;
    }
    default:
        sb_printf(b, "<%s object at %p>", tname(o), (void *)o);
    }
    repr_depth--;
    return r;
}

Obj *obj_repr(Obj *o) {
    SB b;
    sb_init(&b);
    if (repr_to(&b, o) < 0) { sb_free(&b); return NULL; }
    return sb_finish(&b);
}

Obj *obj_str(Obj *o) {
    int k = KIND(o);
    if (o->type->user && overridable(o)) {
        Obj *f = user_override(o, N_str);
        if (f) {
            Obj *s = call1(f, o);
            if (s && !IS_STR(s)) return err(E_TypeError, "__str__ returned non-string (type %s)", tname(s));
            return s;
        }
        if (k == K_STR) return str_new(AS_STR(o)->s, AS_STR(o)->len);
        return obj_repr(o);
    }
    if (k == K_STR) return o;
    if (k == K_INSTANCE || k == K_EXC) {
        Obj *f = o->type->user ? type_lookup(o->type, N_str) : NULL;
        if (f && KIND(f) != K_BUILTIN) {
            Obj *s = call1(f, o);
            if (s && !IS_STR(s)) return err(E_TypeError, "__str__ returned non-string (type %s)", tname(s));
            return s;
        }
        if (k == K_EXC) {
            Exc *e = (Exc *)o;
            if (e->args->n == 0) return str_new("", 0);
            if (e->args->n == 1) {
                if (isinstance(o, E_KeyError)) return obj_repr(e->args->items[0]);
                return obj_str(e->args->items[0]);
            }
            return obj_repr((Obj *)e->args);
        }
    }
    return obj_repr(o);
}

/* ---------- truth, hash, equality ---------- */
int obj_truthy(Obj *o) {
    if (o->type->user && overridable(o)) {
        Obj *f = user_override(o, N_bool);
        if (f) {
            Obj *r = call1(f, o);
            if (!r) return -1;
            if (KIND(r) != K_BOOL) { err(E_TypeError, "__bool__ should return bool, returned %s", tname(r)); return -1; }
            return r == Py_True;
        }
        if (user_override(o, N_len)) { int64_t n = obj_len(o); return n < 0 ? -1 : n != 0; }
    }
    switch (KIND(o)) {
    case K_NONE: return 0;
    case K_BOOL: return o == Py_True;
    case K_INT: return int_sign(o) != 0;
    case K_FLOAT: return ((Float *)o)->v != 0;
    case K_STR: return AS_STR(o)->len != 0;
    case K_LIST: return ((List *)o)->n != 0;
    case K_TUPLE: return ((Tuple *)o)->n != 0;
    case K_DICT: return ((Dict *)o)->used != 0;
    case K_SET: return ((Set *)o)->d->used != 0;
    case K_DICTVIEW: return ((DictView *)o)->d->used != 0;
    case K_RANGE: return range_len((Range *)o) != 0;
    case K_INSTANCE: case K_EXC:
        if (o->type->user) {
            Obj *f = type_lookup(o->type, N_bool);
            if (f) {
                Obj *r = call1(f, o);
                if (!r) return -1;
                if (KIND(r) != K_BOOL) { err(E_TypeError, "__bool__ should return bool, returned %s", tname(r)); return -1; }
                return r == Py_True;
            }
            f = type_lookup(o->type, N_len);
            if (f) {
                int64_t n = obj_len(o);
                return n < 0 ? -1 : n != 0;
            }
        }
        return 1;
    default: return 1;
    }
}

int obj_hash(Obj *o, uint64_t *out) {
    if (o->type->user && overridable(o)) {
        Obj *f = type_lookup(o->type, N_hash);
        if (f == Py_None) { err(E_TypeError, "unhashable type: '%s'", tname(o)); return -1; }
        if (f && KIND(f) != K_BUILTIN) {
            Obj *r = call1(f, o);
            if (!r) return -1;
            if (!IS_INT(r)) { err(E_TypeError, "__hash__ method should return an integer"); return -1; }
            *out = int_hash(r);
            return 0;
        }
    }
    switch (KIND(o)) {
    case K_STR: *out = str_hash(AS_STR(o)); return 0;
    case K_INT: case K_BOOL: *out = int_hash(o); return 0;
    case K_NONE: *out = 0xFCA86420; return 0;
    case K_FLOAT: {
        double v = ((Float *)o)->v;
        if (v == trunc(v) && fabs(v) < 9.2e18) *out = int_hash(int_new((int64_t)v));
        else { uint64_t bits; memcpy(&bits, &v, 8); *out = bits ^ (bits >> 31); }
        return 0;
    }
    case K_TUPLE: {
        Tuple *t = (Tuple *)o;
        uint64_t h = 0x345678;
        for (int64_t i = 0; i < t->n; i++) {
            uint64_t eh;
            if (obj_hash(t->items[i], &eh) < 0) return -1;
            h = (h ^ eh) * 1000003ull + (uint64_t)i;
        }
        *out = h;
        return 0;
    }
    case K_SET:
        if (isinstance(o, T_frozenset)) {
            Dict *d = ((Set *)o)->d;
            uint64_t h = 1927868237ull;
            for (int64_t i = 0; i < d->n; i++)
                if (d->e[i].key) h ^= (d->e[i].hash ^ (d->e[i].hash << 16) ^ 89869747ull) * 3644798167ull;
            *out = h;
            return 0;
        }
        /* fallthrough */
    case K_LIST: case K_DICT: case K_SLICE:
        err(E_TypeError, "unhashable type: '%s'", tname(o));
        return -1;
    case K_INSTANCE: case K_EXC:
        if (o->type->user) {
            Obj *f = type_lookup(o->type, N_hash);
            if (f == Py_None) { err(E_TypeError, "unhashable type: '%s'", tname(o)); return -1; }
            if (f && KIND(f) != K_BUILTIN) {
                Obj *r = call1(f, o);
                if (!r) return -1;
                if (!IS_INT(r)) { err(E_TypeError, "__hash__ method should return an integer"); return -1; }
                *out = int_hash(r);
                return 0;
            }
        }
        /* fallthrough */
    default:
        *out = (uint64_t)((uintptr_t)o >> 4);
        return 0;
    }
}

int obj_eq(Obj *a, Obj *b) {
    if (a == b) return 1;
    if (IS_STR(a) && IS_STR(b)) return str_eq(AS_STR(a), AS_STR(b));
    Obj *r = obj_richcmp(a, b, CMP_EQ);
    if (!r) return -1;
    return obj_truthy(r);
}

int obj_cmp(Obj *a, Obj *b, int op) {
    Obj *r = obj_richcmp(a, b, op);
    if (!r) return -1;
    return obj_truthy(r);
}

static bool cmp_result(int c, int op) {
    switch (op) {
    case CMP_LT: return c < 0;
    case CMP_LE: return c <= 0;
    case CMP_EQ: return c == 0;
    case CMP_NE: return c != 0;
    case CMP_GT: return c > 0;
    default: return c >= 0;
    }
}

static Obj *seq_cmp(Obj **x, int64_t nx, Obj **y, int64_t ny, int op) {
    if (nx != ny && op == CMP_EQ) return Py_False;
    if (nx != ny && op == CMP_NE) return Py_True;
    int64_t n = nx < ny ? nx : ny;
    for (int64_t i = 0; i < n; i++) {
        int e = obj_eq(x[i], y[i]);
        if (e < 0) return NULL;
        if (!e) {
            if (op == CMP_EQ) return Py_False;
            if (op == CMP_NE) return Py_True;
            return obj_richcmp(x[i], y[i], op);
        }
    }
    return BOOL(cmp_result(nx < ny ? -1 : nx > ny, op));
}

/* 1 if every key of a is in b, 0 if not, -1 on error. */
static int dict_keys_subset(Dict *a, Dict *b) {
    for (int64_t i = 0; i < a->n; i++) {
        if (!a->e[i].key) continue;
        int64_t ix = dict_find(b, a->e[i].key, a->e[i].hash, NULL);
        if (ix == -2) return -1;
        if (ix < 0) return 0;
    }
    return 1;
}

static const char *cmp_sym[] = {"<", "<=", "==", "!=", ">", ">="};
static Str **cmp_names[] = {&N_lt, &N_le, &N_eq, &N_ne, &N_gt, &N_ge};
static const int cmp_swap[] = {CMP_GT, CMP_GE, CMP_EQ, CMP_NE, CMP_LT, CMP_LE};

/* Comparison methods defined by Python classes. Returns NotImplemented when
   neither side handles the comparison. */
static Obj *user_cmp(Obj *a, Obj *b, int op, bool ua, bool ub) {
    if (ua) {
        Obj *f = type_lookup(a->type, *cmp_names[op]);
        if (f && KIND(f) != K_BUILTIN && f != Py_None) {
            Obj *r = call2(f, a, b);
            if (!r || r != Py_NotImplemented) return r;
        } else if (op == CMP_NE) {
            f = type_lookup(a->type, N_eq);
            if (f && KIND(f) != K_BUILTIN && f != Py_None) {
                Obj *r = call2(f, a, b);
                if (!r) return NULL;
                if (r != Py_NotImplemented) { int t = obj_truthy(r); return t < 0 ? NULL : BOOL(!t); }
            }
        }
    }
    if (ub) {
        Obj *f = type_lookup(b->type, *cmp_names[cmp_swap[op]]);
        if (f && KIND(f) != K_BUILTIN && f != Py_None) {
            Obj *r = call2(f, b, a);
            if (!r || r != Py_NotImplemented) return r;
        } else if (op == CMP_NE) {
            f = type_lookup(b->type, N_eq);
            if (f && KIND(f) != K_BUILTIN && f != Py_None) {
                Obj *r = call2(f, b, a);
                if (!r) return NULL;
                if (r != Py_NotImplemented) { int t = obj_truthy(r); return t < 0 ? NULL : BOOL(!t); }
            }
        }
    }
    return Py_NotImplemented;
}

Obj *obj_richcmp(Obj *a, Obj *b, int op) {
    if (a->type->user || b->type->user) {
        bool ua = a->type->user && (KIND(a) == K_INSTANCE || KIND(a) == K_EXC || overridable(a));
        bool ub = b->type->user && (KIND(b) == K_INSTANCE || KIND(b) == K_EXC || overridable(b));
        Obj *r = user_cmp(a, b, op, ua, ub);
        if (r != Py_NotImplemented) return r;
    }
    int ka = KIND(a), kb = KIND(b);
    bool ia = ka == K_INT || ka == K_BOOL, ib = kb == K_INT || kb == K_BOOL;
    if (ia && ib) return BOOL(cmp_result(int_cmp(a, b), op));
    if ((ia || ka == K_FLOAT) && (ib || kb == K_FLOAT)) {
        if (ia != ib) {
            /* int against float: compare exactly, without rounding the int. */
            Obj *i = ia ? a : b;
            double d = ((Float *)(ia ? b : a))->v;
            int c;
            if (isnan(d)) return BOOL(op == CMP_NE);
            if (isinf(d)) c = d > 0 ? -1 : 1;
            else {
                double fl = floor(d);
                c = int_cmp(i, int_from_double(fl));
                if (c == 0 && d > fl) c = -1;
            }
            return BOOL(cmp_result(ia ? c : -c, op));
        }
        double x = ia ? int_to_double(a, NULL) : ((Float *)a)->v;
        double y = ib ? int_to_double(b, NULL) : ((Float *)b)->v;
        switch (op) {
        case CMP_LT: return BOOL(x < y);
        case CMP_LE: return BOOL(x <= y);
        case CMP_EQ: return BOOL(x == y);
        case CMP_NE: return BOOL(x != y);
        case CMP_GT: return BOOL(x > y);
        default: return BOOL(x >= y);
        }
    }
    if (ka == kb) {
        switch (ka) {
        case K_STR: {
            Str *x = AS_STR(a), *y = AS_STR(b);
            uint32_t n = x->len < y->len ? x->len : y->len;
            int c = memcmp(x->s, y->s, n);
            if (c == 0) c = x->len < y->len ? -1 : x->len > y->len;
            return BOOL(cmp_result(c, op));
        }
        case K_LIST: return seq_cmp(((List *)a)->items, ((List *)a)->n, ((List *)b)->items, ((List *)b)->n, op);
        case K_TUPLE: return seq_cmp(((Tuple *)a)->items, ((Tuple *)a)->n, ((Tuple *)b)->items, ((Tuple *)b)->n, op);
        case K_SET: {
            Dict *x = ((Set *)a)->d, *y = ((Set *)b)->d;
            int r;
            switch (op) {
            case CMP_EQ: case CMP_NE:
                r = x->used == y->used ? dict_keys_subset(x, y) : 0;
                if (r < 0) return NULL;
                return BOOL(op == CMP_EQ ? r : !r);
            case CMP_LE: r = dict_keys_subset(x, y); break;
            case CMP_LT: r = x->used < y->used ? dict_keys_subset(x, y) : 0; break;
            case CMP_GE: r = dict_keys_subset(y, x); break;
            default: r = y->used < x->used ? dict_keys_subset(y, x) : 0; break;
            }
            if (r < 0) return NULL;
            return BOOL(r);
        }
        case K_DICT:
            if (op == CMP_EQ || op == CMP_NE) {
                Dict *x = (Dict *)a, *y = (Dict *)b;
                bool eq = x->used == y->used;
                for (int64_t i = 0; eq && i < x->n; i++) {
                    if (!x->e[i].key) continue;
                    int64_t ix = dict_find(y, x->e[i].key, x->e[i].hash, NULL);
                    if (ix == -2) return NULL;
                    if (ix < 0) { eq = false; break; }
                    int e = obj_eq(x->e[i].val, y->e[ix].val);
                    if (e < 0) return NULL;
                    if (!e) eq = false;
                }
                return BOOL(op == CMP_EQ ? eq : !eq);
            }
            break;
        case K_METHOD:
            if (op == CMP_EQ || op == CMP_NE) {
                Method *x = (Method *)a, *y = (Method *)b;
                bool eq = x->func == y->func && x->self == y->self;
                return BOOL(op == CMP_EQ ? eq : !eq);
            }
            break;
        case K_RANGE:
            if (op == CMP_EQ || op == CMP_NE) {
                Range *x = (Range *)a, *y = (Range *)b;
                int64_t lx = range_len(x), ly = range_len(y);
                bool eq = lx == ly && (lx == 0 || (x->start == y->start && (lx == 1 || x->step == y->step)));
                return BOOL(op == CMP_EQ ? eq : !eq);
            }
            break;
        default: break;
        }
    }
    if (op == CMP_EQ) return BOOL(a == b);
    if (op == CMP_NE) return BOOL(a != b);
    return err(E_TypeError, "'%s' not supported between instances of '%s' and '%s'", cmp_sym[op], tname(a), tname(b));
}
