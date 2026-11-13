/* Generic operations: attributes, indexing, iteration, arithmetic, class creation, sorting. */
#include "py.h"

Obj *file_readline(Obj *f);
static Str *bin_names[OP_NBINARY][3];
static const char *bin_sym[OP_NBINARY] = {"+", "-", "*", "/", "//", "%", "** or pow()", "<<", ">>", "&", "|", "^", "@"};
static const char *bin_isym[OP_NBINARY] = {"+=", "-=", "*=", "/=", "//=", "%=", "**=", "<<=", ">>=", "&=", "|=", "^=", "@="};

Type *T_union;   /* the type of "int | str" */

static bool typeish(Obj *o) { return KIND(o) == K_TYPE || o == Py_None || o->type == T_union; }

static Obj *make_union(Obj *a, Obj *b) {
    List *parts = (List *)list_new(0);
    Obj *both[2] = {a, b};
    for (int i = 0; i < 2; i++) {
        Obj *o = both[i] == Py_None ? (Obj *)T_NoneType : both[i];
        Obj **items = &o;
        int64_t n = 1;
        if (o->type == T_union) { items = ((Tuple *)o)->items; n = ((Tuple *)o)->n; }
        for (int64_t k = 0; k < n; k++) {
            bool seen = false;
            for (int64_t j = 0; j < parts->n; j++) if (parts->items[j] == items[k]) seen = true;
            if (!seen) list_append(parts, items[k]);
        }
    }
    if (parts->n == 1) return parts->items[0];
    Tuple *u = gc_alloc(T_union, sizeof(Tuple) + (size_t)parts->n * sizeof(Obj *));
    u->n = parts->n;
    for (int64_t i = 0; i < parts->n; i++) u->items[i] = parts->items[i];
    return (Obj *)u;
}

void ops_init(void) {
    T_union = type_new("types.UnionType", T_object, K_TUPLE);
    gc_root((Obj **)&T_union);
    static const char *base[OP_NBINARY] = {"add", "sub", "mul", "truediv", "floordiv", "mod", "pow",
                                           "lshift", "rshift", "and", "or", "xor", "matmul"};
    static const char *pre[3] = {"", "r", "i"};
    for (int i = 0; i < OP_NBINARY; i++)
        for (int j = 0; j < 3; j++) {
            bin_names[i][j] = AS_STR(str_fmt("__%s%s__", pre[j], base[i]));
            gc_root((Obj **)&bin_names[i][j]);
        }
}

/* ---------- numbers ---------- */
int obj_index(Obj *o, int64_t *out) {
    if (IS_INT(o)) {
        if (!int_fits(o, out)) { err(E_IndexError, "cannot fit 'int' into an index-sized integer"); return -1; }
        return 0;
    }
    if (o->type->user) {
        Obj *f = type_lookup(o->type, N_index);
        if (f) {
            Obj *r = call1(f, o);
            if (!r) return -1;
            if (IS_INT(r)) return obj_index(r, out);
        }
    }
    err(E_TypeError, "'%s' object cannot be interpreted as an integer", tname(o));
    return -1;
}

int obj_as_double(Obj *o, double *out) {
    if (KIND(o) == K_FLOAT) { *out = ((Float *)o)->v; return 0; }
    if (IS_INT(o)) {
        bool of;
        *out = int_to_double(o, &of);
        if (of) { err(E_OverflowError, "int too large to convert to float"); return -1; }
        return 0;
    }
    if (o->type->user) {
        Obj *f = type_lookup(o->type, N_float);
        if (f) {
            Obj *r = call1(f, o);
            if (!r) return -1;
            if (KIND(r) == K_FLOAT) { *out = ((Float *)r)->v; return 0; }
        }
    }
    err(E_TypeError, "must be real number, not %s", tname(o));
    return -1;
}

static int slice_val(Obj *o, int64_t *out) {
    if (IS_INT(o) && !int_fits(o, out)) { *out = int_sign(o) < 0 ? -INT64_MAX : INT64_MAX; return 0; }
    if (!IS_INT(o) && !(o->type->user && type_lookup(o->type, N_index))) {
        err(E_TypeError, "slice indices must be integers or None or have an __index__ method");
        return -1;
    }
    return obj_index(o, out);
}

int slice_indices(Obj *so, int64_t len, int64_t *start, int64_t *stop, int64_t *step, int64_t *count) {
    Slice *s = (Slice *)so;
    int64_t v;
    if (s->step == Py_None) *step = 1;
    else {
        if (slice_val(s->step, step) < 0) return -1;
        if (*step == 0) { err(E_ValueError, "slice step cannot be zero"); return -1; }
    }
    if (s->start == Py_None) *start = *step < 0 ? len - 1 : 0;
    else {
        if (slice_val(s->start, &v) < 0) return -1;
        if (v < 0) { v += len; if (v < 0) v = *step < 0 ? -1 : 0; }
        else if (v >= len) v = *step < 0 ? len - 1 : len;
        *start = v;
    }
    if (s->stop == Py_None) *stop = *step < 0 ? -1 : len;
    else {
        if (slice_val(s->stop, &v) < 0) return -1;
        if (v < 0) { v += len; if (v < 0) v = *step < 0 ? -1 : 0; }
        else if (v >= len) v = *step < 0 ? len - 1 : len;
        *stop = v;
    }
    if (*step > 0) *count = *start < *stop ? (*stop - *start - 1) / *step + 1 : 0;
    else *count = *stop < *start ? (*start - *stop - 1) / (-*step) + 1 : 0;
    return 0;
}

/* ---------- attributes ---------- */
static Obj *bind(Obj *attr, Obj *self, Type *cls) {
    switch (KIND(attr)) {
    case K_FUNC: return method_new(attr, self);
    case K_BUILTIN: return (((Builtin *)attr)->flags & BF_METHOD) ? method_new(attr, self) : attr;
    case K_WRAPPER:
        if (attr->type == T_staticmethod) return ((Wrapper *)attr)->fn;
        return method_new(((Wrapper *)attr)->fn, (Obj *)cls);
    case K_PROPERTY: {
        Property *p = (Property *)attr;
        if (p->fget == Py_None) return err(E_AttributeError, "property has no getter");
        return call1(p->fget, self);
    }
    default:
        if (attr->type->user) {
            /* A descriptor written in Python: an object whose class defines __get__. */
            Obj *g = type_lookup(attr->type, N_get);
            if (g) { Obj *args[3] = {attr, self ? self : Py_None, (Obj *)cls}; return obj_call(g, 3, args, NULL); }
        }
        return attr;
    }
}

static bool is_data_descriptor(Obj *t) {
    return KIND(t) == K_PROPERTY || (t->type->user && type_lookup(t->type, N_set));
}

static Obj *func_attr(Func *f, Str *name) {
    if (str_eq(name, N_name)) return (Obj *)f->name;
    if (str_eq(name, N_qualname)) return (Obj *)f->qualname;
    if (str_eq(name, N_doc)) return f->doc ? f->doc : Py_None;
    if (str_eq(name, N_dict)) { if (!f->dict) f->dict = (Dict *)dict_new(); return (Obj *)f->dict; }
    if (str_eq(name, N_defaults)) return f->defaults ? (Obj *)f->defaults : Py_None;
    if (str_eq(name, N_module)) { Obj *m = dict_gets(f->globals, N_name); return m ? m : Py_None; }
    if (str_eq(name, N_annotations)) { if (!f->annotations) f->annotations = (Dict *)dict_new(); return (Obj *)f->annotations; }
    if (str_eqc(name, "__globals__")) return (Obj *)f->globals;
    if (str_eqc(name, "__code__")) return (Obj *)f->code;
    if (f->dict) return dict_gets(f->dict, name);
    return NULL;
}

/* Returns NULL with no exception set when the attribute does not exist. */
Obj *obj_getattr_opt(Obj *o, Str *name) {
    Type *tp = o->type;
    bool sub = tp->user && tp->kind != K_INSTANCE && tp->kind != K_EXC;
    if (sub) {
        Obj *t = type_lookup(tp, name);
        if (t && KIND(t) == K_PROPERTY) return bind(t, o, tp);
        Dict *d = obj_dict(o, false);
        if (d) { Obj *v = dict_gets(d, name); if (v) return v; }
        if (t) return bind(t, o, tp);
        if (str_eq(name, N_dict)) return (Obj *)obj_dict(o, true);
    }
    switch (tp->kind) {
    case K_INSTANCE: case K_EXC: {
        Obj *t = type_lookup(tp, name);
        if (t && is_data_descriptor(t)) return bind(t, o, tp);
        Dict *d = ((Instance *)o)->dict;
        if (d) { Obj *v = dict_gets(d, name); if (v) return v; }
        if (t) return bind(t, o, tp);
        if (tp->kind == K_EXC) {
            Exc *e = (Exc *)o;
            if (str_eq(name, N_args)) return (Obj *)e->args;
            if (str_eq(name, N_cause)) return e->cause ? e->cause : Py_None;
            if (str_eq(name, N_context)) return e->context ? e->context : Py_None;
            if (str_eq(name, N_traceback)) return Py_None;
            if (str_eqc(name, "value") && isinstance(o, E_StopIteration)) return e->args->n ? e->args->items[0] : Py_None;
            if (str_eqc(name, "__suppress_context__")) return Py_False;
            if (str_eqc(name, "code") && isinstance(o, E_SystemExit)) return e->args->n ? e->args->items[0] : Py_None;
        }
        if (str_eq(name, N_class)) return (Obj *)tp;
        if (str_eq(name, N_dict)) {
            if (!d) d = ((Instance *)o)->dict = (Dict *)dict_new();
            return (Obj *)d;
        }
        if (tp->user) {
            Obj *hook = type_lookup(tp, N_getattr);
            if (hook) return call2(hook, o, (Obj *)name);
        }
        return NULL;
    }
    case K_TYPE: {
        Type *c = (Type *)o;
        Obj *v = type_lookup(c, name);
        if (v) {
            if (KIND(v) == K_WRAPPER) {
                if (v->type == T_staticmethod) return ((Wrapper *)v)->fn;
                return method_new(((Wrapper *)v)->fn, o);
            }
            if (v->type->user && type_lookup(v->type, N_get)) return bind(v, NULL, c);
            return v;
        }
        if (str_eq(name, N_name)) return (Obj *)c->name;
        if (str_eq(name, N_qualname)) return (Obj *)c->name;
        if (str_eq(name, N_bases)) return (Obj *)c->bases;
        if (str_eq(name, N_mro)) return (Obj *)c->mro;
        if (str_eq(name, N_dict)) return (Obj *)c->dict;
        if (str_eq(name, N_class)) return (Obj *)T_type;
        if (str_eq(name, N_doc)) return Py_None;
        if (str_eq(name, N_module)) return str_c("builtins");
        v = type_lookup(T_type, name);
        if (v) return bind(v, o, T_type);
        return NULL;
    }
    case K_MODULE: {
        Obj *v = dict_gets(((Module *)o)->dict, name);
        if (v) return v;
        if (str_eq(name, N_dict)) return (Obj *)((Module *)o)->dict;
        return NULL;
    }
    case K_FUNC: {
        Obj *v = func_attr((Func *)o, name);
        if (v) return v;
        break;
    }
    case K_METHOD: {
        Method *m = (Method *)o;
        if (str_eq(name, N_self)) return m->self;
        if (str_eq(name, N_func)) return m->func;
        if (KIND(m->func) == K_FUNC) { Obj *v = func_attr((Func *)m->func, name); if (v) return v; }
        if (KIND(m->func) == K_BUILTIN && str_eq(name, N_name)) return str_c(((Builtin *)m->func)->name);
        break;
    }
    case K_BUILTIN:
        if (str_eq(name, N_name) || str_eq(name, N_qualname)) return str_c(((Builtin *)o)->name);
        if (str_eq(name, N_doc)) return Py_None;
        break;
    case K_SUPER: {
        Super *s = (Super *)o;
        Type *st = (KIND(s->self) == K_TYPE && type_issub((Type *)s->self, s->cls)) ? (Type *)s->self : s->self->type;
        int64_t i = 0;
        while (i < st->mro->n && st->mro->items[i] != (Obj *)s->cls) i++;
        for (i++; i < st->mro->n; i++) {
            Obj *v = dict_gets(((Type *)st->mro->items[i])->dict, name);
            if (v) {
                if (KIND(s->self) == K_TYPE && st == (Type *)s->self && KIND(v) != K_WRAPPER) return v;
                return bind(v, s->self, st);
            }
        }
        return NULL;
    }
    case K_PROPERTY: {
        Property *p = (Property *)o;
        if (str_eqc(name, "fget")) return p->fget;
        if (str_eqc(name, "fset")) return p->fset;
        if (str_eqc(name, "fdel")) return p->fdel;
        if (str_eq(name, N_doc)) return p->doc ? p->doc : Py_None;
        break;
    }
    case K_WRAPPER:
        if (str_eq(name, N_func) || str_eq(name, N_wrapped)) return ((Wrapper *)o)->fn;
        break;
    case K_CODE:
        if (str_eqc(name, "co_name")) return (Obj *)((Code *)o)->name;
        if (str_eqc(name, "co_argcount")) return int_new(((Code *)o)->argcount);
        if (str_eqc(name, "co_varnames")) return (Obj *)((Code *)o)->varnames;
        break;
    case K_GEN:
        if (str_eq(name, N_name)) return (Obj *)((Generator *)o)->name;
        break;
    case K_SLICE:
        if (str_eqc(name, "start")) return ((Slice *)o)->start;
        if (str_eqc(name, "stop")) return ((Slice *)o)->stop;
        if (str_eqc(name, "step")) return ((Slice *)o)->step;
        break;
    case K_RANGE:
        if (str_eqc(name, "start")) return int_new(((Range *)o)->start);
        if (str_eqc(name, "stop")) return int_new(((Range *)o)->stop);
        if (str_eqc(name, "step")) return int_new(((Range *)o)->step);
        break;
    case K_INT: case K_BOOL:
        if (str_eqc(name, "real") || str_eqc(name, "numerator")) return KIND(o) == K_BOOL ? int_new(o == Py_True) : o;
        if (str_eqc(name, "imag")) return int_new(0);
        if (str_eqc(name, "denominator")) return int_new(1);
        break;
    case K_FLOAT:
        if (str_eqc(name, "real")) return o;
        if (str_eqc(name, "imag")) return float_new(0.0);
        break;
    case K_FILE:
        if (str_eqc(name, "closed")) return BOOL(((File *)o)->closed);
        if (str_eqc(name, "name")) return (Obj *)((File *)o)->name;
        break;
    default: break;
    }
    Obj *v = type_lookup(tp, name);
    if (v) return bind(v, o, tp);
    if (str_eq(name, N_class)) return (Obj *)tp;
    if (str_eq(name, N_doc)) return Py_None;
    if (sub) {
        Obj *hook = type_lookup(tp, N_getattr);
        if (hook) return call2(hook, o, (Obj *)name);
    }
    return NULL;
}

Obj *obj_getattr(Obj *o, Str *name) {
    Obj *r = obj_getattr_opt(o, name);
    if (!r && !vm.exc) {
        if (KIND(o) == K_TYPE) err(E_AttributeError, "type object '%s' has no attribute '%s'", ((Type *)o)->name->s, name->s);
        else if (KIND(o) == K_MODULE) err(E_AttributeError, "module '%s' has no attribute '%s'", ((Module *)o)->name->s, name->s);
        else err(E_AttributeError, "'%s' object has no attribute '%s'", tname(o), name->s);
    }
    return r;
}

static int dict_attr_store(Dict *d, Obj *o, Str *name, Obj *v) {
    if (v) { dict_sets(d, name, v); return 0; }
    if (!dict_dels(d, name)) {
        err(E_AttributeError, "'%s' object has no attribute '%s'", tname(o), name->s);
        return -1;
    }
    return 0;
}

/* Sets an attribute without calling a Python-level __setattr__. v == NULL deletes. */
int obj_setattr_direct(Obj *o, Str *name, Obj *v) {
    Type *tp = o->type;
    switch (tp->kind) {
    case K_INSTANCE: case K_EXC: {
        Obj *t = type_lookup(tp, name);
        if (t && KIND(t) == K_PROPERTY) {
            Property *p = (Property *)t;
            Obj *fn = v ? p->fset : p->fdel;
            if (fn == Py_None) {
                err(E_AttributeError, "property '%s' of '%s' object has no %s", name->s, tname(o), v ? "setter" : "deleter");
                return -1;
            }
            return (v ? call2(fn, o, v) : call1(fn, o)) ? 0 : -1;
        }
        if (t && t->type->user) {
            Obj *s = type_lookup(t->type, v ? N_set : N_delete);
            if (s) { Obj *args[3] = {t, o, v}; return obj_call(s, v ? 3 : 2, args, NULL) ? 0 : -1; }
        }
        if (tp->kind == K_EXC && v) {
            Exc *e = (Exc *)o;
            if (str_eq(name, N_args)) {
                Obj *tu = seq_to_tuple(v);
                if (!tu) return -1;
                e->args = (Tuple *)tu;
                return 0;
            }
            if (str_eq(name, N_cause) || str_eq(name, N_context)) {
                bool cause = str_eq(name, N_cause);
                if (v != Py_None && !isinstance(v, E_BaseException)) {
                    err(E_TypeError, "exception %s must be None or derive from BaseException", cause ? "cause" : "context");
                    return -1;
                }
                if (cause) e->cause = v == Py_None ? NULL : v; else e->context = v == Py_None ? NULL : v;
                return 0;
            }
            if (str_eq(name, N_traceback)) return 0;
        }
        Instance *in = (Instance *)o;
        if (tp->slots && v && !dict_gets(tp->slots, name)) {
            err(E_AttributeError, "'%s' object has no attribute '%s' and no __dict__ for setting new attributes", tname(o), name->s);
            return -1;
        }
        if (!in->dict) in->dict = (Dict *)dict_new();
        return dict_attr_store(in->dict, o, name, v);
    }
    case K_TYPE: {
        Type *c = (Type *)o;
        if (!c->user) {
            err(E_TypeError, "cannot set '%s' attribute of immutable type '%s'", name->s, c->name->s);
            return -1;
        }
        if (v && str_eq(name, N_del)) { c->has_del = true; vm.fin_possible = true; }
        if (v) { dict_sets(c->dict, name, v); return 0; }
        if (!dict_dels(c->dict, name)) {
            err(E_AttributeError, "type object '%s' has no attribute '%s'", c->name->s, name->s);
            return -1;
        }
        return 0;
    }
    case K_MODULE: return dict_attr_store(((Module *)o)->dict, o, name, v);
    case K_FUNC: {
        Func *f = (Func *)o;
        if (v && str_eq(name, N_name) && IS_STR(v)) { f->name = AS_STR(v); return 0; }
        if (v && str_eq(name, N_qualname) && IS_STR(v)) { f->qualname = AS_STR(v); return 0; }
        if (v && str_eq(name, N_doc)) { f->doc = v; return 0; }
        if (v && str_eq(name, N_annotations)) {
            if (KIND(v) != K_DICT) { err(E_TypeError, "__annotations__ must be set to a dict object"); return -1; }
            f->annotations = (Dict *)v;
            return 0;
        }
        if (!f->dict) f->dict = (Dict *)dict_new();
        return dict_attr_store(f->dict, o, name, v);
    }
    default:
        if (tp->user) {
            Obj *t = type_lookup(tp, name);
            if (t && KIND(t) == K_PROPERTY) {
                Property *p = (Property *)t;
                Obj *fn = v ? p->fset : p->fdel;
                if (fn == Py_None) {
                    err(E_AttributeError, "property '%s' of '%s' object has no %s", name->s, tname(o), v ? "setter" : "deleter");
                    return -1;
                }
                return (v ? call2(fn, o, v) : call1(fn, o)) ? 0 : -1;
            }
            return dict_attr_store(obj_dict(o, true), o, name, v);
        }
        err(E_AttributeError, v ? "'%s' object has no attribute '%s' and no __dict__ for setting new attributes" : "'%s' object has no attribute '%s'", tname(o), name->s);
        return -1;
    }
}

int obj_setattr(Obj *o, Str *name, Obj *v) {
    if (o->type->user && KIND(o) != K_TYPE) {
        Obj *hook = type_lookup(o->type, v ? N_setattr : N_delattr);
        if (hook && KIND(hook) != K_BUILTIN) {
            Obj *args[3] = {o, (Obj *)name, v};
            return obj_call(hook, v ? 3 : 2, args, NULL) ? 0 : -1;
        }
    }
    return obj_setattr_direct(o, name, v);
}

/* ---------- len / contains ---------- */
int64_t obj_len(Obj *o) {
    if (o->type->user && overridable(o)) {
        Obj *f = user_override(o, N_len);
        if (f) {
            Obj *r = call1(f, o);
            int64_t n;
            if (!r) return -1;
            if (!IS_INT(r) || !int_fits(r, &n) || n < 0) { err(E_ValueError, "__len__() should return >= 0"); return -1; }
            return n;
        }
    }
    switch (KIND(o)) {
    case K_STR: return AS_STR(o)->nchars;
    case K_LIST: return ((List *)o)->n;
    case K_TUPLE: return ((Tuple *)o)->n;
    case K_DICT: return ((Dict *)o)->used;
    case K_SET: return ((Set *)o)->d->used;
    case K_DICTVIEW: return ((DictView *)o)->d->used;
    case K_RANGE: return range_len((Range *)o);
    case K_INSTANCE: case K_EXC:
        if (o->type->user) {
            Obj *f = type_lookup(o->type, N_len);
            if (f) {
                Obj *r = call1(f, o);
                int64_t n;
                if (!r) return -1;
                if (!IS_INT(r)) { err(E_TypeError, "'%s' object cannot be interpreted as an integer", tname(r)); return -1; }
                if (!int_fits(r, &n) || n < 0) { err(E_ValueError, "__len__() should return >= 0"); return -1; }
                return n;
            }
        }
        /* fallthrough */
    default:
        err(E_TypeError, "object of type '%s' has no len()", tname(o));
        return -1;
    }
}

int obj_contains(Obj *c, Obj *item) {
    Obj *tmp;
    if (c->type->user && overridable(c)) {
        Obj *f = user_override(c, N_contains);
        if (f) { Obj *r = call2(f, c, item); return r ? obj_truthy(r) : -1; }
        if (user_override(c, N_iter)) {
            Obj *it = obj_iter(c), *x;
            if (!it) return -1;
            while ((x = iter_next(it))) { int e = obj_eq(x, item); if (e) return e; }
            return vm.exc ? -1 : 0;
        }
    }
    switch (KIND(c)) {
    case K_STR:
        if (!IS_STR(item)) {
            err(E_TypeError, "'in <string>' requires string as left operand, not %s", tname(item));
            return -1;
        }
        if (AS_STR(item)->len == 0) return 1;
        return memmem(AS_STR(c)->s, AS_STR(c)->len, AS_STR(item)->s, AS_STR(item)->len) != NULL;
    case K_LIST: {
        List *l = (List *)c;
        for (int64_t i = 0; i < l->n; i++) { int e = obj_eq(l->items[i], item); if (e) return e; }
        return 0;
    }
    case K_TUPLE: {
        Tuple *t = (Tuple *)c;
        for (int64_t i = 0; i < t->n; i++) { int e = obj_eq(t->items[i], item); if (e) return e; }
        return 0;
    }
    case K_DICT: return dict_get((Dict *)c, item, &tmp);
    case K_SET: return dict_get(((Set *)c)->d, item, &tmp);
    case K_DICTVIEW: {
        DictView *v = (DictView *)c;
        if (v->kind == 0) return dict_get(v->d, item, &tmp);
        break;
    }
    case K_RANGE: {
        Range *r = (Range *)c;
        int64_t v;
        if (KIND(item) == K_FLOAT) {
            double d = ((Float *)item)->v;
            if (d != trunc(d) || fabs(d) > 9e18) return 0;
            v = (int64_t)d;
        } else if (!IS_INT(item) || !int_fits(item, &v)) return 0;
        if (r->step > 0) return v >= r->start && v < r->stop && (v - r->start) % r->step == 0;
        return v <= r->start && v > r->stop && (r->start - v) % (-r->step) == 0;
    }
    case K_INSTANCE: case K_EXC:
        if (c->type->user) {
            Obj *f = type_lookup(c->type, N_contains);
            if (f) { Obj *r = call2(f, c, item); return r ? obj_truthy(r) : -1; }
        }
        break;
    default: break;
    }
    Obj *it = obj_iter(c);
    if (!it) {
        if (err_matches(E_TypeError)) {
            err_clear();
            err(E_TypeError, "argument of type '%s' is not iterable", tname(c));
        }
        return -1;
    }
    Obj *x;
    while ((x = iter_next(it))) { int e = obj_eq(x, item); if (e) return e; }
    return vm.exc ? -1 : 0;
}

/* ---------- items ---------- */
static Obj *index_err(const char *what) { return err(E_IndexError, "%s index out of range", what); }

static Obj *bad_index(Obj *o, Obj *k) {
    if (vm.exc && !IS_INT(k) && !(k->type->user && type_lookup(k->type, N_index))) {
        err_clear();
        const char *n = KIND(o) == K_STR ? "string" : tname(o);
        return err(E_TypeError, "%s indices must be integers or slices, not %s", n, tname(k));
    }
    return NULL;
}

Obj *obj_getitem(Obj *o, Obj *k) {
    int64_t i, start, stop, step, count;
    if (o->type->user && overridable(o)) {
        Obj *f = user_override(o, N_getitem);
        if (f) return call2(f, o, k);
    }
    switch (KIND(o)) {
    case K_LIST: {
        List *l = (List *)o;
        if (KIND(k) == K_SLICE) {
            if (slice_indices(k, l->n, &start, &stop, &step, &count) < 0) return NULL;
            List *r = (List *)list_new(count);
            for (int64_t j = 0; j < count; j++) r->items[j] = l->items[start + j * step];
            return (Obj *)r;
        }
        if (obj_index(k, &i) < 0) return bad_index(o, k);
        if (i < 0) i += l->n;
        if (i < 0 || i >= l->n) return index_err("list");
        return l->items[i];
    }
    case K_TUPLE: {
        Tuple *t = (Tuple *)o;
        if (KIND(k) == K_SLICE) {
            if (slice_indices(k, t->n, &start, &stop, &step, &count) < 0) return NULL;
            Tuple *r = (Tuple *)tuple_new(count);
            for (int64_t j = 0; j < count; j++) r->items[j] = t->items[start + j * step];
            return (Obj *)r;
        }
        if (obj_index(k, &i) < 0) return bad_index(o, k);
        if (i < 0) i += t->n;
        if (i < 0 || i >= t->n) return index_err("tuple");
        return t->items[i];
    }
    case K_STR: {
        Str *s = AS_STR(o);
        if (KIND(k) == K_SLICE) {
            if (slice_indices(k, s->nchars, &start, &stop, &step, &count) < 0) return NULL;
            if (step == 1) return str_slice_chars(s, start, start + count);
            SB b;
            sb_init(&b);
            if (s->len == s->nchars) {
                for (int64_t j = 0; j < count; j++) sb_putc(&b, s->s[start + j * step]);
            } else {
                /* Decode once, then pick characters. */
                uint32_t *cps = malloc(((size_t)s->nchars + 1) * sizeof *cps);
                const char *p = s->s;
                for (uint32_t j = 0; j < s->nchars; j++) cps[j] = utf8_next(&p);
                for (int64_t j = 0; j < count; j++) sb_putcp(&b, cps[start + j * step]);
                free(cps);
            }
            return sb_finish(&b);
        }
        if (obj_index(k, &i) < 0) return bad_index(o, k);
        if (i < 0) i += s->nchars;
        if (i < 0 || i >= s->nchars) return index_err("string");
        if (s->len == s->nchars) return str_chr((unsigned char)s->s[i]);
        const char *p = s->s + str_offset(s, i);
        return str_chr(utf8_next(&p));
    }
    case K_DICT: {
        Obj *v;
        int r = dict_get((Dict *)o, k, &v);
        if (r < 0) return NULL;
        if (!r) {
            if (o->type->user) {
                Obj *miss = user_override(o, N_missing);
                if (miss) return call2(miss, o, k);
            }
            return err_obj(E_KeyError, k);
        }
        return v;
    }
    case K_RANGE: {
        Range *r = (Range *)o;
        int64_t n = range_len(r);
        if (KIND(k) == K_SLICE) {
            if (slice_indices(k, n, &start, &stop, &step, &count) < 0) return NULL;
            return range_new(r->start + start * r->step, r->start + stop * r->step, step * r->step);
        }
        if (obj_index(k, &i) < 0) return bad_index(o, k);
        if (i < 0) i += n;
        if (i < 0 || i >= n) return err(E_IndexError, "range object index out of range");
        return int_new(r->start + i * r->step);
    }
    case K_INSTANCE: case K_EXC:
        if (o->type->user) {
            Obj *f = type_lookup(o->type, N_getitem);
            if (f) return call2(f, o, k);
        }
        break;
    case K_TYPE: {
        Obj *f = type_lookup((Type *)o, AS_STR(str_c("__class_getitem__")));
        if (f && KIND(f) == K_FUNC) return call2(f, o, k);   /* an implicit class method */
        if (f && KIND(f) == K_WRAPPER) return call2(((Wrapper *)f)->fn, o, k);
        return o; /* list[int] and friends: keep the plain type */
    }
    default: break;
    }
    return err(E_TypeError, "'%s' object is not subscriptable", tname(o));
}

static int list_set_slice(List *l, Obj *k, Obj *v) {
    int64_t start, stop, step, count;
    if (slice_indices(k, l->n, &start, &stop, &step, &count) < 0) return -1;
    List *src = NULL;
    if (v) {
        src = (List *)seq_to_list(v);
        if (!src) return -1;
        /* Indices may be stale if converting v ran user code that resized the list. */
        if (slice_indices(k, l->n, &start, &stop, &step, &count) < 0) return -1;
    }
    if (step == 1) {
        if (stop < start) stop = start;
        int64_t newn = src ? src->n : 0, oldn = stop - start, tail = l->n - stop;
        int64_t total = l->n - oldn + newn;
        if (total > l->cap) {
            l->items = realloc(l->items, (size_t)total * sizeof(Obj *));
            if (!l->items) fatal("out of memory");
            gc_account((size_t)(total - l->cap) * sizeof(Obj *));
            l->cap = total;
        }
        memmove(l->items + start + newn, l->items + stop, (size_t)tail * sizeof(Obj *));
        for (int64_t i = 0; i < newn; i++) l->items[start + i] = src->items[i];
        l->n = total;
        return 0;
    }
    if (src) {
        if (src->n != count) {
            err(E_ValueError, "attempt to assign sequence of size %lld to extended slice of size %lld",
                (long long)src->n, (long long)count);
            return -1;
        }
        for (int64_t i = 0; i < count; i++) l->items[start + i * step] = src->items[i];
        return 0;
    }
    /* Delete an extended slice: mark, then compact. */
    for (int64_t i = 0; i < count; i++) l->items[start + i * step] = NULL;
    int64_t w = 0;
    for (int64_t i = 0; i < l->n; i++) if (l->items[i]) l->items[w++] = l->items[i];
    l->n = w;
    return 0;
}

int obj_setitem(Obj *o, Obj *k, Obj *v) {
    if (o->type->user && overridable(o)) {
        Obj *f = user_override(o, N_setitem);
        if (f) { Obj *args[3] = {o, k, v}; return obj_call(f, 3, args, NULL) ? 0 : -1; }
    }
    switch (KIND(o)) {
    case K_LIST: {
        List *l = (List *)o;
        int64_t i;
        if (KIND(k) == K_SLICE) return list_set_slice(l, k, v);
        if (obj_index(k, &i) < 0) { bad_index(o, k); return -1; }
        if (i < 0) i += l->n;
        if (i < 0 || i >= l->n) { err(E_IndexError, "list assignment index out of range"); return -1; }
        l->items[i] = v;
        return 0;
    }
    case K_DICT: return dict_set((Dict *)o, k, v);
    case K_INSTANCE: case K_EXC:
        if (o->type->user) {
            Obj *f = type_lookup(o->type, N_setitem);
            if (f) { Obj *args[3] = {o, k, v}; return obj_call(f, 3, args, NULL) ? 0 : -1; }
        }
        break;
    default: break;
    }
    err(E_TypeError, "'%s' object does not support item assignment", tname(o));
    return -1;
}

int obj_delitem(Obj *o, Obj *k) {
    if (o->type->user && overridable(o)) {
        Obj *f = user_override(o, N_delitem);
        if (f) return call2(f, o, k) ? 0 : -1;
    }
    switch (KIND(o)) {
    case K_LIST: {
        List *l = (List *)o;
        int64_t i;
        if (KIND(k) == K_SLICE) return list_set_slice(l, k, NULL);
        if (obj_index(k, &i) < 0) { bad_index(o, k); return -1; }
        if (i < 0) i += l->n;
        if (i < 0 || i >= l->n) { err(E_IndexError, "list assignment index out of range"); return -1; }
        memmove(l->items + i, l->items + i + 1, (size_t)(l->n - i - 1) * sizeof(Obj *));
        l->n--;
        return 0;
    }
    case K_DICT: {
        int r = dict_del((Dict *)o, k);
        if (r < 0) return -1;
        if (!r) { err_obj(E_KeyError, k); return -1; }
        return 0;
    }
    case K_INSTANCE: case K_EXC:
        if (o->type->user) {
            Obj *f = type_lookup(o->type, N_delitem);
            if (f) return call2(f, o, k) ? 0 : -1;
        }
        break;
    default: break;
    }
    err(E_TypeError, "'%s' object doesn't support item deletion", tname(o));
    return -1;
}

/* ---------- iteration ---------- */
Obj *obj_iter(Obj *o) {
    Iter *it;
    if (o->type->user && overridable(o)) {
        Obj *f = user_override(o, N_iter);
        if (f) return call1(f, o);
    }
    switch (KIND(o)) {
    case K_LIST: return iter_new(IT_LIST, o, NULL);
    case K_TUPLE: return iter_new(IT_TUPLE, o, NULL);
    case K_STR: return iter_new(IT_STR, o, NULL);
    case K_RANGE:
        it = (Iter *)iter_new(IT_RANGE, NULL, NULL);
        it->i = ((Range *)o)->start; it->stop = ((Range *)o)->stop; it->step = ((Range *)o)->step;
        return (Obj *)it;
    case K_DICT:
        it = (Iter *)iter_new(IT_DICT, o, NULL);
        it->stop = ((Dict *)o)->used;
        return (Obj *)it;
    case K_SET:
        it = (Iter *)iter_new(IT_DICT, (Obj *)((Set *)o)->d, o);
        it->stop = ((Set *)o)->d->used;
        return (Obj *)it;
    case K_DICTVIEW:
        it = (Iter *)iter_new(IT_DICT, (Obj *)((DictView *)o)->d, NULL);
        it->sub = ((DictView *)o)->kind;
        it->stop = ((DictView *)o)->d->used;
        return (Obj *)it;
    case K_GEN: case K_ITER: case K_FILE: return o;
    case K_INSTANCE: case K_EXC:
        if (o->type->user) {
            Obj *f = type_lookup(o->type, N_iter);
            if (f && f != Py_None) {
                Obj *r = call1(f, o);
                if (r && !(KIND(r) == K_GEN || KIND(r) == K_ITER || KIND(r) == K_FILE ||
                           (r->type->user && type_lookup(r->type, N_next))))
                    return err(E_TypeError, "iter() returned non-iterator of type '%s'", tname(r));
                return r;
            }
            if (!f && type_lookup(o->type, N_getitem)) return iter_new(IT_SEQ, o, NULL);
        }
        break;
    default: break;
    }
    return err(E_TypeError, "'%s' object is not iterable", tname(o));
}

/* Returns NULL with no exception set when the iterator is finished. */
Obj *iter_next(Obj *o) {
    switch (KIND(o)) {
    case K_ITER: break;
    case K_GEN: { Obj *ret; return gen_send((Generator *)o, Py_None, &ret); }
    case K_FILE: {
        Obj *s = file_readline(o);
        if (!s || AS_STR(s)->len == 0) return NULL;
        return s;
    }
    default: {
        Obj *f = o->type->user ? type_lookup(o->type, N_next) : NULL;
        if (!f) return err(E_TypeError, "'%s' object is not an iterator", tname(o));
        Obj *r = call1(f, o);
        if (!r && err_matches(E_StopIteration)) err_clear();
        return r;
    }
    }
    Iter *it = (Iter *)o;
    switch (it->kind) {
    case IT_LIST: {
        List *l = (List *)it->a;
        return it->i < l->n ? l->items[it->i++] : NULL;
    }
    case IT_TUPLE: {
        Tuple *t = (Tuple *)it->a;
        return it->i < t->n ? t->items[it->i++] : NULL;
    }
    case IT_STR: {
        Str *s = AS_STR(it->a);
        if (it->i >= s->len) return NULL;
        const char *p = s->s + it->i;
        uint32_t cp = utf8_next(&p);
        it->i = p - s->s;
        return str_chr(cp);
    }
    case IT_RANGE: {
        if (it->step > 0 ? it->i >= it->stop : it->i <= it->stop) return NULL;
        int64_t v = it->i;
        if (__builtin_add_overflow(it->i, it->step, &it->i)) it->i = it->stop;
        return int_new(v);
    }
    case IT_DICT: {
        Dict *d = (Dict *)it->a;
        if (d->used != it->stop) {
            it->i = INT64_MAX;
            return err(E_RuntimeError, it->b ? "Set changed size during iteration" : "dictionary changed size during iteration");
        }
        Obj *k, *v;
        if (!dict_next(d, &it->i, &k, &v)) return NULL;
        return it->sub == 0 ? k : it->sub == 1 ? v : tuple_pack(2, k, v);
    }
    case IT_ENUM: {
        Obj *v = iter_next(it->a);
        if (!v) return NULL;
        Obj *idx = it->b ? it->b : int_new(0);
        it->b = int_add(idx, int_new(1));
        return tuple_pack(2, idx, v);
    }
    case IT_ZIP: {
        Tuple *its = (Tuple *)it->a;
        if (its->n == 0) return NULL;
        Tuple *r = (Tuple *)tuple_new(its->n);
        for (int64_t i = 0; i < its->n; i++) {
            Obj *v = iter_next(its->items[i]);
            if (!v) {
                if (vm.exc || !it->sub) return NULL;
                /* strict: every input must end on the same step */
                if (i > 0) {
                    if (i == 1) return err(E_ValueError, "zip() argument 2 is shorter than argument 1");
                    return err(E_ValueError, "zip() argument %lld is shorter than arguments 1-%lld", (long long)i + 1, (long long)i);
                }
                for (int64_t j = 1; j < its->n; j++) {
                    if (iter_next(its->items[j])) {
                        if (j == 1) return err(E_ValueError, "zip() argument 2 is longer than argument 1");
                        return err(E_ValueError, "zip() argument %lld is longer than arguments 1-%lld", (long long)j + 1, (long long)j);
                    }
                    if (vm.exc) return NULL;
                }
                return NULL;
            }
            r->items[i] = v;
        }
        return (Obj *)r;
    }
    case IT_MAP: {
        Tuple *its = (Tuple *)it->b;
        Tuple *args = (Tuple *)tuple_new(its->n);
        for (int64_t i = 0; i < its->n; i++) {
            Obj *v = iter_next(its->items[i]);
            if (!v) return NULL;
            args->items[i] = v;
        }
        return obj_call(it->a, (int)args->n, args->items, NULL);
    }
    case IT_FILTER:
        for (;;) {
            Obj *v = iter_next(it->b);
            if (!v) return NULL;
            Obj *t = it->a == Py_None ? v : call1(it->a, v);
            if (!t) return NULL;
            int r = obj_truthy(t);
            if (r < 0) return NULL;
            if (r) return v;
        }
    case IT_REV: {
        if (it->i < 0) return NULL;
        if (KIND(it->a) == K_LIST) {
            List *l = (List *)it->a;
            if (it->i >= l->n) it->i = l->n - 1;
            return it->i < 0 ? NULL : l->items[it->i--];
        }
        return ((Tuple *)it->a)->items[it->i--];
    }
    case IT_SEQ: {
        if (it->i < 0) return NULL;
        Obj *v = obj_getitem(it->a, int_new(it->i));
        if (!v) {
            if (err_matches(E_IndexError) || err_matches(E_StopIteration)) err_clear();
            it->i = -1;
            return NULL;
        }
        it->i++;
        return v;
    }
    }
    return NULL;
}

Obj *seq_to_list(Obj *o) {
    if (KIND(o) == K_LIST) return list_from(((List *)o)->items, ((List *)o)->n);
    if (KIND(o) == K_TUPLE) return list_from(((Tuple *)o)->items, ((Tuple *)o)->n);
    List *l = (List *)list_new(0);
    if (list_extend(l, o) < 0) return NULL;
    return (Obj *)l;
}

Obj *seq_to_tuple(Obj *o) {
    if (KIND(o) == K_TUPLE) return o;
    if (KIND(o) == K_LIST) return tuple_from(((List *)o)->items, ((List *)o)->n);
    List *l = (List *)seq_to_list(o);
    if (!l) return NULL;
    return tuple_from(l->items, l->n);
}

int list_extend(List *l, Obj *o) {
    if (KIND(o) == K_LIST || KIND(o) == K_TUPLE) {
        int64_t n = KIND(o) == K_LIST ? ((List *)o)->n : ((Tuple *)o)->n;
        for (int64_t i = 0; i < n; i++)
            list_append(l, KIND(o) == K_LIST ? ((List *)o)->items[i] : ((Tuple *)o)->items[i]);
        return 0;
    }
    Obj *it = obj_iter(o), *v;
    if (!it) return -1;
    while ((v = iter_next(it))) list_append(l, v);
    return vm.exc ? -1 : 0;
}

int set_update(Set *s, Obj *o) {
    Obj *it = obj_iter(o), *v;
    if (!it) return -1;
    while ((v = iter_next(it))) if (set_add(s, v) < 0) return -1;
    return vm.exc ? -1 : 0;
}

int dict_update(Dict *d, Obj *o) {
    if (KIND(o) == K_DICT) {
        Dict *s = (Dict *)o;
        if (s == d) return 0;
        for (int64_t i = 0; i < s->n; i++)
            if (s->e[i].key && dict_set(d, s->e[i].key, s->e[i].val) < 0) return -1;
        return 0;
    }
    Obj *keys = obj_getattr_opt(o, AS_STR(str_c("keys")));
    if (!keys && vm.exc) return -1;
    if (keys) {
        Obj *ks = call0(keys), *it, *k;
        if (!ks || !(it = obj_iter(ks))) return -1;
        while ((k = iter_next(it))) {
            Obj *v = obj_getitem(o, k);
            if (!v || dict_set(d, k, v) < 0) return -1;
        }
        return vm.exc ? -1 : 0;
    }
    Obj *it = obj_iter(o), *pair;
    if (!it) return -1;
    for (int64_t i = 0; (pair = iter_next(it)); i++) {
        Tuple *t = (Tuple *)seq_to_tuple(pair);
        if (!t) {
            if (err_matches(E_TypeError)) {
                err_clear();
                err(E_TypeError, "cannot convert dictionary update sequence element #%lld to a sequence", (long long)i);
            }
            return -1;
        }
        if (t->n != 2) {
            err(E_ValueError, "dictionary update sequence element #%lld has length %lld; 2 is required",
                (long long)i, (long long)t->n);
            return -1;
        }
        if (dict_set(d, t->items[0], t->items[1]) < 0) return -1;
    }
    return vm.exc ? -1 : 0;
}

/* ---------- arithmetic ---------- */
static Obj *int_binop(int op, Obj *a, Obj *b) {
    Obj *q, *r;
    switch (op) {
    case OP_ADD: return int_add(a, b);
    case OP_SUB: return int_sub(a, b);
    case OP_MUL: return int_mul(a, b);
    case OP_TRUEDIV: {
        if (int_sign(b) == 0) return err(E_ZeroDivisionError, "division by zero");
        bool o1, o2;
        double x = int_to_double(a, &o1), y = int_to_double(b, &o2);
        if (o1 || o2) {
            /* Too big for a float: divide as integers first. */
            if (int_divmod(a, b, &q, &r) < 0) return NULL;
            x = int_to_double(q, &o1);
            if (o1) return err(E_OverflowError, "integer division result too large for a float");
            return float_new(x);
        }
        return float_new(x / y);
    }
    case OP_FLOORDIV:
        if (int_divmod(a, b, &q, NULL) < 0) return err(E_ZeroDivisionError, "integer division or modulo by zero");
        return q;
    case OP_MOD:
        if (int_divmod(a, b, NULL, &r) < 0) return err(E_ZeroDivisionError, "integer modulo by zero");
        return r;
    case OP_POW:
        if (int_sign(b) < 0) {
            if (int_sign(a) == 0) return err(E_ZeroDivisionError, "0.0 cannot be raised to a negative power");
            return float_new(pow(int_to_double(a, NULL), int_to_double(b, NULL)));
        }
        return int_pow(a, b);
    case OP_LSHIFT: return int_lshift(a, b);
    case OP_RSHIFT: return int_rshift(a, b);
    case OP_AND: case OP_OR: case OP_XOR: {
        r = int_bitop(op == OP_AND ? '&' : op == OP_OR ? '|' : '^', a, b);
        if (KIND(a) == K_BOOL && KIND(b) == K_BOOL) return BOOL(int_sign(r) != 0);
        return r;
    }
    }
    return Py_NotImplemented;
}

static Obj *float_binop(int op, double x, double y) {
    switch (op) {
    case OP_ADD: return float_new(x + y);
    case OP_SUB: return float_new(x - y);
    case OP_MUL: return float_new(x * y);
    case OP_TRUEDIV:
        if (y == 0) return err(E_ZeroDivisionError, "float division by zero");
        return float_new(x / y);
    case OP_FLOORDIV: case OP_MOD: {
        if (y == 0) return err(E_ZeroDivisionError, op == OP_MOD ? "float modulo by zero" : "float floor division by zero");
        double mod = fmod(x, y), div = (x - mod) / y, fl;
        if (mod) { if ((y < 0) != (mod < 0)) { mod += y; div -= 1.0; } }
        else mod = copysign(0.0, y);
        if (div) { fl = floor(div); if (div - fl > 0.5) fl += 1.0; }
        else fl = copysign(0.0, x / y);
        return float_new(op == OP_MOD ? mod : fl);
    }
    case OP_POW: {
        if (x == 0 && y < 0) return err(E_ZeroDivisionError, "0.0 cannot be raised to a negative power");
        if (x < 0 && y != floor(y) && isfinite(y))
            return err(E_ValueError, "negative number cannot be raised to a fractional power (complex numbers are not supported)");
        double r = pow(x, y);
        if (isinf(r) && isfinite(x) && isfinite(y)) return err(E_OverflowError, "(34, 'Numerical result out of range')");
        return float_new(r);
    }
    }
    return Py_NotImplemented;
}

static Obj *seq_repeat(Obj *seq, Obj *count) {
    int64_t n;
    if (!int_fits(count, &n)) return err(E_OverflowError, "cannot fit 'int' into an index-sized integer");
    if (n < 0) n = 0;
    switch (KIND(seq)) {
    case K_STR: {
        Str *s = AS_STR(seq);
        if (n == 1) return seq;
        if ((uint64_t)s->len * (uint64_t)n > (1ull << 31)) return err(E_MemoryError, "repeated string is too long");
        SB b;
        sb_init(&b);
        for (int64_t i = 0; i < n; i++) sb_put(&b, s->s, s->len);
        return sb_finish(&b);
    }
    case K_LIST: {
        List *l = (List *)seq;
        if (l->n && n > (INT64_C(1) << 40) / l->n) return err(E_MemoryError, "repeated list is too long");
        List *r = (List *)list_new(l->n * n);
        for (int64_t i = 0; i < n; i++)
            for (int64_t j = 0; j < l->n; j++) r->items[i * l->n + j] = l->items[j];
        return (Obj *)r;
    }
    default: {
        Tuple *t = (Tuple *)seq;
        if (t->n && n > (INT64_C(1) << 40) / t->n) return err(E_MemoryError, "repeated tuple is too long");
        Tuple *r = (Tuple *)tuple_new(t->n * n);
        for (int64_t i = 0; i < n; i++)
            for (int64_t j = 0; j < t->n; j++) r->items[i * t->n + j] = t->items[j];
        return (Obj *)r;
    }
    }
}

static Obj *set_binop(int op, Set *a, Set *b) {
    Set *r = (Set *)set_new(isinstance((Obj *)a, T_frozenset) ? T_frozenset : T_set);
    Obj *tmp;
    Dict *x = a->d, *y = b->d;
    if (op == OP_OR || op == OP_SUB || op == OP_XOR)
        for (int64_t i = 0; i < x->n; i++) {
            if (!x->e[i].key) continue;
            int in = op == OP_OR ? 0 : dict_get(y, x->e[i].key, &tmp);
            if (in < 0) return NULL;
            if (!in) set_add(r, x->e[i].key);
        }
    if (op == OP_AND)
        for (int64_t i = 0; i < x->n; i++) {
            if (!x->e[i].key) continue;
            int in = dict_get(y, x->e[i].key, &tmp);
            if (in < 0) return NULL;
            if (in) set_add(r, x->e[i].key);
        }
    if (op == OP_OR || op == OP_XOR)
        for (int64_t i = 0; i < y->n; i++) {
            if (!y->e[i].key) continue;
            int in = op == OP_OR ? 0 : dict_get(x, y->e[i].key, &tmp);
            if (in < 0) return NULL;
            if (!in) set_add(r, y->e[i].key);
        }
    return (Obj *)r;
}

static Obj *user_binop(Obj *self, Str *name, Obj *a, Obj *b);
static Obj *user_binop_impl(Obj *self, Str *name, Obj *a, Obj *b) {
    if (!self->type->user) return Py_NotImplemented;
    Obj *f = type_lookup(self->type, name);
    if (!f || KIND(f) == K_BUILTIN) return Py_NotImplemented;
    return call2(f, a, b);
}

static Obj *user_binop(Obj *self, Str *name, Obj *a, Obj *b) { return user_binop_impl(self, name, a, b); }

Obj *binary_op(int op, Obj *a, Obj *b, bool inplace) {
    int ka = KIND(a), kb = KIND(b);
    bool ia = ka == K_INT || ka == K_BOOL, ib = kb == K_INT || kb == K_BOOL;
    Obj *r = Py_NotImplemented;
    if (a->type->user || b->type->user) {
        /* Classes that extend a built-in type: their own methods come first. */
        bool ua = overridable(a), ub = overridable(b);
        if (ua && inplace) { r = user_binop(a, bin_names[op][2], a, b); if (r != Py_NotImplemented) return r; }
        if (ua) { r = user_binop(a, bin_names[op][0], a, b); if (r != Py_NotImplemented) return r; }
        if (ub) { r = user_binop(b, bin_names[op][1], b, a); if (r != Py_NotImplemented) return r; }
    }
    if (ia && ib) r = int_binop(op, a, b);
    else if (op == OP_OR && typeish(a) && typeish(b)) r = make_union(a, b);
    else if ((ia || ka == K_FLOAT) && (ib || kb == K_FLOAT)) {
        double x, y;
        if (obj_as_double(a, &x) < 0 || obj_as_double(b, &y) < 0) return NULL;
        r = float_binop(op, x, y);
    } else switch (ka) {
    case K_STR:
        if (op == OP_ADD) {
            if (kb != K_STR) return err(E_TypeError, "can only concatenate str (not \"%s\") to str", tname(b));
            r = str_concat(AS_STR(a), AS_STR(b));
        } else if (op == OP_MUL && ib) r = seq_repeat(a, b);
        else if (op == OP_MOD) r = str_percent(AS_STR(a), b);
        break;
    case K_LIST:
        if (op == OP_ADD && inplace) { if (list_extend((List *)a, b) < 0) return NULL; r = a; }
        else if (op == OP_ADD) {
            if (kb != K_LIST) return err(E_TypeError, "can only concatenate list (not \"%s\") to list", tname(b));
            List *x = (List *)a, *y = (List *)b, *l = (List *)list_new(x->n + y->n);
            for (int64_t i = 0; i < x->n; i++) l->items[i] = x->items[i];
            for (int64_t i = 0; i < y->n; i++) l->items[x->n + i] = y->items[i];
            r = (Obj *)l;
        } else if (op == OP_MUL && ib) {
            r = seq_repeat(a, b);
            if (r && inplace) {
                List *l = (List *)a, *n = (List *)r;
                Obj **ti = l->items; l->items = n->items; n->items = ti;
                int64_t t = l->n; l->n = n->n; n->n = t;
                t = l->cap; l->cap = n->cap; n->cap = t;
                r = a;
            }
        }
        break;
    case K_TUPLE:
        if (op == OP_ADD) {
            if (kb != K_TUPLE) return err(E_TypeError, "can only concatenate tuple (not \"%s\") to tuple", tname(b));
            Tuple *x = (Tuple *)a, *y = (Tuple *)b, *t = (Tuple *)tuple_new(x->n + y->n);
            for (int64_t i = 0; i < x->n; i++) t->items[i] = x->items[i];
            for (int64_t i = 0; i < y->n; i++) t->items[x->n + i] = y->items[i];
            r = (Obj *)t;
        } else if (op == OP_MUL && ib) r = seq_repeat(a, b);
        break;
    case K_SET:
        if (kb == K_SET && (op == OP_OR || op == OP_AND || op == OP_SUB || op == OP_XOR)) {
            r = set_binop(op, (Set *)a, (Set *)b);
            if (r && inplace && !isinstance(a, T_frozenset)) { ((Set *)a)->d = ((Set *)r)->d; r = a; }
        }
        break;
    case K_DICT:
        if (op == OP_OR && inplace) { if (dict_update((Dict *)a, b) < 0) return NULL; r = a; }
        else if (op == OP_OR && kb == K_DICT) {
            r = dict_copy((Dict *)a);
            if (dict_update((Dict *)r, b) < 0) return NULL;
        }
        break;
    default:
        if (ia && op == OP_MUL && (kb == K_STR || kb == K_LIST || kb == K_TUPLE)) r = seq_repeat(b, a);
        break;
    }
    if (r != Py_NotImplemented) return r;
    if (inplace) {
        r = user_binop(a, bin_names[op][2], a, b);
        if (r != Py_NotImplemented) return r;
    }
    r = user_binop(a, bin_names[op][0], a, b);
    if (r != Py_NotImplemented) return r;
    r = user_binop(b, bin_names[op][1], b, a);
    if (r != Py_NotImplemented) return r;
    if (op == OP_MUL && (ka == K_STR || ka == K_LIST || ka == K_TUPLE) && !b->type->user)
        return err(E_TypeError, "can't multiply sequence by non-int of type '%s'", tname(b));
    if (op == OP_MUL && (kb == K_STR || kb == K_LIST || kb == K_TUPLE) && !a->type->user)
        return err(E_TypeError, "can't multiply sequence by non-int of type '%s'", tname(a));
    return err(E_TypeError, "unsupported operand type(s) for %s: '%s' and '%s'",
               inplace ? bin_isym[op] : bin_sym[op], tname(a), tname(b));
}

Obj *unary_op(int op, Obj *a) {
    int k = KIND(a);
    if (a->type->user && overridable(a)) {
        Obj *f = user_override(a, op == '-' ? N_neg : op == '+' ? N_pos : N_invert);
        if (f) return call1(f, a);
    }
    if (k == K_INT || k == K_BOOL) {
        if (op == '-') return int_neg(a);
        if (op == '+') return k == K_BOOL ? int_new(a == Py_True) : a;
        return int_sub(int_neg(a), int_new(1));
    }
    if (k == K_FLOAT && op != '~') return op == '-' ? float_new(-((Float *)a)->v) : a;
    if (a->type->user) {
        Obj *f = type_lookup(a->type, op == '-' ? N_neg : op == '+' ? N_pos : N_invert);
        if (f) return call1(f, a);
    }
    return err(E_TypeError, "bad operand type for unary %c: '%s'", op, tname(a));
}

/* ---------- classes ---------- */
/* C3 linearization of the base classes. */
static Tuple *compute_mro(Type *cls, Tuple *bases) {
    int nb = (int)bases->n, nseq = nb + 1;
    Obj ***seq = malloc((size_t)nseq * sizeof *seq);
    int64_t *len = malloc((size_t)nseq * sizeof *len), *pos = calloc((size_t)nseq, sizeof *pos);
    int64_t total = 1;
    for (int i = 0; i < nb; i++) {
        Tuple *m = ((Type *)bases->items[i])->mro;
        seq[i] = m->items; len[i] = m->n; total += m->n;
    }
    seq[nb] = bases->items; len[nb] = nb;
    List *out = (List *)list_new(0);
    list_append(out, (Obj *)cls);
    bool ok = true;
    for (;;) {
        bool done = true;
        Obj *pick = NULL;
        for (int i = 0; i < nseq && !pick; i++) {
            if (pos[i] >= len[i]) continue;
            done = false;
            Obj *cand = seq[i][pos[i]];
            bool in_tail = false;
            for (int j = 0; j < nseq && !in_tail; j++)
                for (int64_t k = pos[j] + 1; k < len[j]; k++)
                    if (seq[j][k] == cand) { in_tail = true; break; }
            if (!in_tail) pick = cand;
        }
        if (done) break;
        if (!pick) { ok = false; break; }
        list_append(out, pick);
        for (int i = 0; i < nseq; i++)
            if (pos[i] < len[i] && seq[i][pos[i]] == pick) pos[i]++;
    }
    (void)total;
    free(seq); free(len); free(pos);
    if (!ok) {
        err(E_TypeError, "Cannot create a consistent method resolution order (MRO) for bases");
        return NULL;
    }
    return (Tuple *)tuple_from(out->items, out->n);
}

Type *class_new(Str *name, Tuple *bases, Dict *ns) {
    if (bases->n == 0) bases = (Tuple *)tuple_pack(1, T_object);
    int kind = K_INSTANCE;
    for (int64_t i = 0; i < bases->n; i++) {
        Obj *b = bases->items[i];
        if (KIND(b) != K_TYPE) { err(E_TypeError, "bases must be types"); return NULL; }
        Type *bt = (Type *)b;
        int bk = bt->kind;
        bool ok = bk == K_INSTANCE || bk == K_EXC || bk == K_INT || bk == K_FLOAT || bk == K_STR ||
                  bk == K_TUPLE || bk == K_LIST || bk == K_DICT || bk == K_SET;
        if (!ok) {
            err(E_TypeError, "type '%s' is not an acceptable base type", bt->name->s);
            return NULL;
        }
        if (bk != K_INSTANCE) {
            if (kind != K_INSTANCE && kind != bk) {
                err(E_TypeError, "multiple bases have instance lay-out conflict");
                return NULL;
            }
            kind = bk;
        }
        for (int64_t j = 0; j < i; j++)
            if (bases->items[j] == b) { err(E_TypeError, "duplicate base class %s", bt->name->s); return NULL; }
    }
    Type *t = gc_alloc(T_type, sizeof(Type));
    t->name = name;
    t->kind = kind;
    t->user = true;
    t->bases = bases;
    t->dict = ns;
    t->mro = compute_mro(t, bases);
    if (!t->mro) return NULL;
    if (dict_gets(ns, N_eq) && !dict_gets(ns, N_hash)) dict_sets(ns, N_hash, Py_None);
    if (type_lookup(t, N_del)) { t->has_del = true; vm.fin_possible = true; }
    /* __slots__ limits attributes only if every class up to object declares it. */
    if (kind == K_INSTANCE) {
        Dict *allowed = (Dict *)dict_new();
        bool all = true;
        for (int64_t i = 0; i + 1 < t->mro->n && all; i++) {
            Obj *sl = dict_gets(((Type *)t->mro->items[i])->dict, N_slots);
            if (!sl) { all = false; break; }
            if (IS_STR(sl)) dict_sets(allowed, AS_STR(sl), Py_True);
            else {
                Obj *it = obj_iter(sl), *nm;
                if (!it) return NULL;
                while ((nm = iter_next(it))) if (IS_STR(nm)) dict_sets(allowed, AS_STR(nm), Py_True);
                if (vm.exc) return NULL;
            }
        }
        if (all) t->slots = allowed;
    }
    return t;
}

/* ---------- sorting ---------- */
typedef struct { Obj **keys; bool reverse, failed; } SortCtx;

/* True when the element at index b must come before the element at index a. */
static bool sort_before(SortCtx *c, int64_t b, int64_t a) {
    if (c->failed) return false;
    int r = c->reverse ? obj_cmp(c->keys[a], c->keys[b], CMP_LT) : obj_cmp(c->keys[b], c->keys[a], CMP_LT);
    if (r < 0) { c->failed = true; return false; }
    return r;
}

static void merge_sort(SortCtx *c, int64_t *idx, int64_t *tmp, int64_t n) {
    if (n < 2) return;
    if (n <= 8) {
        for (int64_t i = 1; i < n; i++) {
            int64_t x = idx[i], j = i;
            while (j > 0 && sort_before(c, x, idx[j - 1])) { idx[j] = idx[j - 1]; j--; }
            idx[j] = x;
        }
        return;
    }
    int64_t mid = n / 2;
    merge_sort(c, idx, tmp, mid);
    merge_sort(c, idx + mid, tmp, n - mid);
    if (c->failed) return;
    if (!sort_before(c, idx[mid], idx[mid - 1])) return; /* already in order */
    memcpy(tmp, idx, (size_t)n * sizeof *idx);
    int64_t i = 0, j = mid, k = 0;
    while (i < mid && j < n) idx[k++] = sort_before(c, tmp[j], tmp[i]) ? tmp[j++] : tmp[i++];
    while (i < mid) idx[k++] = tmp[i++];
    while (j < n) idx[k++] = tmp[j++];
}

int list_sort(List *l, Obj *key, bool reverse) {
    int64_t n = l->n;
    List *vals = (List *)list_from(l->items, n);
    List *keys = vals;
    if (key && key != Py_None) {
        keys = (List *)list_new(n);
        for (int64_t i = 0; i < n; i++) {
            Obj *k = call1(key, vals->items[i]);
            if (!k) return -1;
            keys->items[i] = k;
        }
    }
    if (n < 2) return 0;
    int64_t *idx = malloc((size_t)n * sizeof *idx), *tmp = malloc((size_t)n * sizeof *tmp);
    for (int64_t i = 0; i < n; i++) idx[i] = i;
    SortCtx c = {keys->items, reverse, false};
    merge_sort(&c, idx, tmp, n);
    int rc = 0;
    if (c.failed) rc = -1;
    else if (l->n != n) { err(E_ValueError, "list modified during sort"); rc = -1; }
    else for (int64_t i = 0; i < n; i++) l->items[i] = vals->items[idx[i]];
    free(idx); free(tmp);
    /* Keep vals and keys visible to the collector until here. */
    if (vals->n < 0 || keys->n < 0) abort();
    return rc;
}
