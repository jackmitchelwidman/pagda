/* The bytecode interpreter: frames, calls, exceptions, generators. */
#include "py.h"
#include "opcode.h"


/* ---------- source text for tracebacks ---------- */
static struct { char *name, *src; } *sources;
static int nsources;

void register_source(const char *filename, const char *src) {
    for (int i = 0; i < nsources; i++)
        if (strcmp(sources[i].name, filename) == 0) { free(sources[i].src); sources[i].src = strdup(src); return; }
    sources = realloc(sources, (size_t)(nsources + 1) * sizeof *sources);
    sources[nsources].name = strdup(filename);
    sources[nsources++].src = strdup(src);
}

static void print_source_line(const char *filename, int line) {
    for (int i = 0; i < nsources; i++) {
        if (strcmp(sources[i].name, filename) != 0) continue;
        const char *s = sources[i].src;
        for (int l = 1; l < line && s; l++) { s = strchr(s, '\n'); if (s) s++; }
        if (!s || !*s) return;
        while (*s == ' ' || *s == '\t') s++;
        const char *e = strchr(s, '\n');
        int n = e ? (int)(e - s) : (int)strlen(s);
        if (n > 0) fprintf(stderr, "    %.*s\n", n, s);
        return;
    }
}

static void print_exc_chain(Obj *exc, int depth) {
    Exc *e = (Exc *)exc;
    if (depth < 20) {
        if (e->cause) {
            print_exc_chain(e->cause, depth + 1);
            fprintf(stderr, "\nThe above exception was the direct cause of the following exception:\n\n");
        } else if (e->context && e->context != exc) {
            Obj *sup = e->dict ? dict_getc(e->dict, "__suppress_context__") : NULL;
            if (sup != Py_True) {
                print_exc_chain(e->context, depth + 1);
                fprintf(stderr, "\nDuring handling of the above exception, another exception occurred:\n\n");
            }
        }
    }
    if (isinstance(exc, E_SyntaxError) && e->dict && dict_getc(e->dict, "lineno")) {
        Obj *fn = dict_getc(e->dict, "filename"), *ln = dict_getc(e->dict, "lineno");
        int64_t line = 0;
        int_fits(ln, &line);
        fprintf(stderr, "  File \"%s\", line %lld\n", AS_STR(fn)->s, (long long)line);
        print_source_line(AS_STR(fn)->s, (int)line);
    } else if (e->tb && e->tb->n) {
        fprintf(stderr, "Traceback (most recent call last):\n");
        for (int64_t i = e->tb->n - 1; i >= 0; i--) {
            Tuple *t = (Tuple *)e->tb->items[i];
            int64_t line = 0;
            int_fits(t->items[1], &line);
            fprintf(stderr, "  File \"%s\", line %lld, in %s\n", AS_STR(t->items[0])->s, (long long)line, AS_STR(t->items[2])->s);
            print_source_line(AS_STR(t->items[0])->s, (int)line);
        }
    }
    Obj *saved = vm.exc;
    vm.exc = NULL;
    Obj *msg = obj_str(exc);
    if (!msg) { vm.exc = NULL; msg = str_c("<exception str() failed>"); }
    vm.exc = saved;
    const char *name = exc->type->name->s;
    Obj *mod = dict_gets(exc->type->dict, N_module);
    if (exc->type->user && mod && IS_STR(mod) && !str_eq(AS_STR(mod), N_main))
        fprintf(stderr, "%s.", AS_STR(mod)->s);
    if (AS_STR(msg)->len) fprintf(stderr, "%s: %s\n", name, AS_STR(msg)->s);
    else fprintf(stderr, "%s\n", name);
    Obj *notes = e->dict ? dict_getc(e->dict, "__notes__") : NULL;
    if (notes && KIND(notes) == K_LIST)
        for (int64_t i = 0; i < ((List *)notes)->n; i++)
            if (IS_STR(((List *)notes)->items[i])) fprintf(stderr, "%s\n", AS_STR(((List *)notes)->items[i])->s);
}

void print_exception(Obj *exc) {
    fflush(stdout);
    print_exc_chain(exc, 0);
    fflush(stderr);
}

static void tb_add(Frame *f, int line) {
    Exc *e = (Exc *)vm.exc;
    if (!e->tb) e->tb = (List *)list_new(0);
    if (e->tb->n > 2000) return;
    list_append(e->tb, tuple_pack(3, f->code->filename, int_new(line), f->code->name));
}

/* ---------- frames ---------- */
static Frame *frame_new(Code *co, Dict *globals, Dict *locals, Func *func) {
    int ncf = co->ncells + co->nfree;
    size_t nslots = (size_t)co->nlocals + (size_t)ncf + (size_t)co->stacksize;
    Frame *f = gc_alloc(T_frame, sizeof(Frame) + nslots * sizeof(Obj *) + (size_t)co->maxblocks * sizeof(Block));
    f->blocks = (Block *)(f->slots + nslots);
    f->fast = f->slots;
    f->cells = (Cell **)(f->slots + co->nlocals);
    f->stack = f->slots + co->nlocals + ncf;
    f->sp = f->stack;
    f->nstack = co->stacksize;
    f->code = co; f->globals = globals; f->locals = locals; f->func = func;
    return f;
}

static void frame_init_cells(Frame *f, Tuple *closure) {
    Code *co = f->code;
    for (int i = 0; i < co->ncells; i++)
        f->cells[i] = (Cell *)cell_new(co->cell2arg[i] >= 0 ? f->fast[co->cell2arg[i]] : NULL);
    for (int i = 0; i < co->nfree; i++) f->cells[co->ncells + i] = (Cell *)closure->items[i];
}

static Obj *run_frame(Frame *f, Obj *throw);

static int bind_args(Func *fn, Frame *f, int argc, Obj **argv, Obj *kwnames) {
    Code *co = fn->code;
    int n = co->argcount, nk = co->kwonly, total = n + nk;
    const char *name = fn->qualname->s;
    Obj **fast = f->fast;
    int ndef = fn->defaults ? (int)fn->defaults->n : 0;
    int ncopy = argc < n ? argc : n;
    for (int i = 0; i < ncopy; i++) fast[i] = argv[i];
    if (co->flags & CO_VARARGS) fast[total] = tuple_from(argv + ncopy, argc - ncopy);
    else if (argc > n) {
        if (ndef)
            err(E_TypeError, "%s() takes from %d to %d positional arguments but %d %s given", name, n - ndef, n, argc, argc == 1 ? "was" : "were");
        else
            err(E_TypeError, "%s() takes %d positional argument%s but %d %s given", name, n, n == 1 ? "" : "s", argc, argc == 1 ? "was" : "were");
        return -1;
    }
    Dict *kwdict = NULL;
    if (co->flags & CO_VARKW) {
        kwdict = (Dict *)dict_new();
        fast[total + ((co->flags & CO_VARARGS) ? 1 : 0)] = (Obj *)kwdict;
    }
    int nkw = kwnames ? (int)((Tuple *)kwnames)->n : 0;
    for (int j = 0; j < nkw; j++) {
        Str *kw = AS_STR(((Tuple *)kwnames)->items[j]);
        int i;
        for (i = co->posonly; i < total; i++) if (co->varnames->items[i] == (Obj *)kw) break;
        if (i == total) for (i = co->posonly; i < total; i++) if (str_eq(AS_STR(co->varnames->items[i]), kw)) break;
        if (i == total && !kwdict)
            for (int q = 0; q < co->posonly; q++)
                if (str_eq(AS_STR(co->varnames->items[q]), kw)) {
                    err(E_TypeError, "%s() got some positional-only arguments passed as keyword arguments: '%s'", name, kw->s);
                    return -1;
                }
        if (i < total) {
            if (fast[i]) { err(E_TypeError, "%s() got multiple values for argument '%s'", name, kw->s); return -1; }
            fast[i] = argv[argc + j];
        } else if (kwdict) dict_sets(kwdict, kw, argv[argc + j]);
        else { err(E_TypeError, "%s() got an unexpected keyword argument '%s'", name, kw->s); return -1; }
    }
    int missing = 0;
    for (int i = 0; i < n; i++) {
        if (fast[i]) continue;
        if (i >= n - ndef) fast[i] = fn->defaults->items[i - (n - ndef)];
        else missing++;
    }
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            missing = 0;
            for (int i = n; i < total; i++) {
                if (fast[i]) continue;
                Obj *d = fn->kwdefaults ? dict_gets(fn->kwdefaults, AS_STR(co->varnames->items[i])) : NULL;
                if (d) fast[i] = d; else missing++;
            }
        }
        if (!missing) continue;
        SB b;
        sb_init(&b);
        int lo = pass ? n : 0, hi = pass ? total : n, seen = 0;
        for (int i = lo; i < hi; i++) {
            if (fast[i]) continue;
            seen++;
            if (seen > 1) sb_puts(&b, seen == missing ? (missing == 2 ? " and " : ", and ") : ", ");
            sb_printf(&b, "'%s'", AS_STR(co->varnames->items[i])->s);
        }
        sb_putc(&b, 0);
        err(E_TypeError, "%s() missing %d required %s argument%s: %s", name, missing,
            pass ? "keyword-only" : "positional", missing == 1 ? "" : "s", b.s);
        sb_free(&b);
        return -1;
    }
    return 0;
}

static Obj *func_call(Func *fn, int argc, Obj **argv, Obj *kwnames) {
    Code *co = fn->code;
    Frame *f = frame_new(co, fn->globals, NULL, fn);
    if (bind_args(fn, f, argc, argv, kwnames) < 0) return NULL;
    if (co->ncells + co->nfree) frame_init_cells(f, fn->closure);
    if (co->flags & CO_GENERATOR) {
        Generator *g = gc_alloc(T_generator, sizeof(Generator));
        g->frame = f;
        g->name = fn->qualname;
        return (Obj *)g;
    }
    return run_frame(f, NULL);
}

Obj *run_code(Code *code, Dict *globals, Dict *locals) {
    Frame *f = frame_new(code, globals, locals, NULL);
    return run_frame(f, NULL);
}

static Obj *instance_new(Type *t) {
    if (t->kind == K_EXC) return exc_new(t, (Tuple *)tuple_new(0));
    Instance *in = gc_alloc(t, sizeof(Instance));
    in->dict = (Dict *)dict_new();
    return (Obj *)in;
}

static Obj *call_prepend(Obj *f, Obj *first, int argc, Obj **argv, Obj *kwnames) {
    int nkw = kwnames ? (int)((Tuple *)kwnames)->n : 0, total = argc + nkw;
    Obj *buf[10], **a = total + 1 <= 10 ? buf : malloc(((size_t)total + 1) * sizeof(Obj *));
    a[0] = first;
    if (total) memcpy(a + 1, argv, (size_t)total * sizeof(Obj *));
    Obj *r = obj_call(f, argc + 1, a, kwnames);
    if (a != buf) free(a);
    return r;
}

static Obj *type_call(Type *t, int argc, Obj **argv, Obj *kwnames) {
    int nkw = kwnames ? (int)((Tuple *)kwnames)->n : 0;
    if (t == T_type) {
        if (argc == 1 && !nkw) return (Obj *)argv[0]->type;
        if (argc == 3 && IS_STR(argv[0]) && KIND(argv[1]) == K_TUPLE && KIND(argv[2]) == K_DICT) {
            Dict *ns = (Dict *)dict_copy((Dict *)argv[2]);
            if (!dict_gets(ns, N_module)) dict_sets(ns, N_module, (Obj *)N_main);
            return (Obj *)class_new(AS_STR(argv[0]), (Tuple *)argv[1], ns);
        }
        return err(E_TypeError, "type() takes 1 or 3 arguments");
    }
    if (t->ctor) return t->ctor(argc, argv, kwnames);
    if (t->user && t->kind != K_INSTANCE && t->kind != K_EXC) {
        /* A class that extends a built-in type. */
        Type *bt = builtin_base(t);
        Obj *nw = type_lookup(t, N_new), *init = type_lookup(t, N_init), *inst;
        bool user_init = init && KIND(init) != K_BUILTIN;
        if (!bt) return err(E_TypeError, "cannot create '%s' instances", t->name->s);
        if (nw && KIND(nw) != K_BUILTIN) {
            if (KIND(nw) == K_WRAPPER) nw = ((Wrapper *)nw)->fn;
            inst = call_prepend(nw, (Obj *)t, argc, argv, kwnames);
            if (!inst || !isinstance(inst, t)) return inst;
        } else {
            bool mutable = t->kind == K_LIST || t->kind == K_DICT || (t->kind == K_SET && bt == T_set);
            Obj *base = (mutable && user_init) ? bt->ctor(0, NULL, NULL) : bt->ctor(argc, argv, kwnames);
            if (!base) return NULL;
            inst = subclass_instance(t, base);
            if (!inst) return NULL;
        }
        if (user_init) {
            Obj *r = call_prepend(init, inst, argc, argv, kwnames);
            if (!r) return NULL;
            if (r != Py_None) return err(E_TypeError, "__init__() should return None, not '%s'", tname(r));
        }
        return inst;
    }
    if (t->kind != K_INSTANCE && t->kind != K_EXC) return err(E_TypeError, "cannot create '%s' instances", t->name->s);
    Obj *inst, *nw = t->user ? type_lookup(t, N_new) : NULL;
    bool user_new = nw && KIND(nw) != K_BUILTIN;
    if (user_new) {
        if (KIND(nw) == K_WRAPPER) nw = ((Wrapper *)nw)->fn;
        inst = call_prepend(nw, (Obj *)t, argc, argv, kwnames);
        if (!inst || !isinstance(inst, t)) return inst;
    } else if (t->kind == K_EXC) inst = exc_new(t, (Tuple *)tuple_from(argv, argc));
    else inst = instance_new(t);
    Obj *init = type_lookup(t, N_init);
    if (init && KIND(init) != K_BUILTIN) {
        Obj *r = call_prepend(init, inst, argc, argv, kwnames);
        if (!r) return NULL;
        if (r != Py_None) return err(E_TypeError, "__init__() should return None, not '%s'", tname(r));
    } else if (!user_new && (nkw || (argc && t->kind == K_INSTANCE))) {
        if (t->kind == K_EXC) return err(E_TypeError, "%s() takes no keyword arguments", t->name->s);
        return err(E_TypeError, "%s() takes no arguments", t->name->s);
    }
    return inst;
}

Obj *obj_call(Obj *f, int argc, Obj **argv, Obj *kwnames) {
    switch (KIND(f)) {
    case K_FUNC: return func_call((Func *)f, argc, argv, kwnames);
    case K_BUILTIN: {
        Builtin *b = (Builtin *)f;
        if (b->owner && argc > 0 && argv[0]->type != b->owner && !isinstance(argv[0], b->owner)) {
            if (b->name[0] == '_' && b->name[1] == '_')
                return err(E_TypeError, "descriptor '%s' requires a '%s' object but received a '%s'", b->name, b->owner->name->s, tname(argv[0]));
            return err(E_TypeError, "descriptor '%s' for '%s' objects doesn't apply to a '%s' object", b->name, b->owner->name->s, tname(argv[0]));
        }
        if (kwnames && ((Tuple *)kwnames)->n && !(b->flags & BF_KW))
            return err(E_TypeError, "%s() takes no keyword arguments", b->name);
        if (argc < b->minargs || (b->maxargs >= 0 && argc > b->maxargs)) {
            int self = (b->flags & BF_METHOD) ? 1 : 0;
            if (self && argc == 0) return err(E_TypeError, "unbound method %s() needs an argument", b->name);
            int want = (argc < b->minargs ? b->minargs : b->maxargs) - self;
            if (b->minargs == b->maxargs && want == 1)
                return err(E_TypeError, "%s() takes exactly one argument (%d given)", b->name, argc - self);
            if (b->minargs == b->maxargs && want == 0)
                return err(E_TypeError, "%s() takes no arguments (%d given)", b->name, argc - self);
            if (b->minargs == b->maxargs)
                return err(E_TypeError, "%s expected %d arguments, got %d", b->name, want, argc - self);
            return err(E_TypeError, "%s expected at %s %d argument%s, got %d", b->name, argc < b->minargs ? "least" : "most",
                       want, want == 1 ? "" : "s", argc - self);
        }
        return b->fn(argc, argv, kwnames);
    }
    case K_METHOD: return call_prepend(((Method *)f)->func, ((Method *)f)->self, argc, argv, kwnames);
    case K_TYPE: return type_call((Type *)f, argc, argv, kwnames);
    default: {
        Obj *c = f->type->user ? type_lookup(f->type, N_call) : NULL;
        if (c) return call_prepend(c, f, argc, argv, kwnames);
        return err(E_TypeError, "'%s' object is not callable", tname(f));
    }
    }
}

Obj *call0(Obj *f) { return obj_call(f, 0, NULL, NULL); }
Obj *call1(Obj *f, Obj *a) { return obj_call(f, 1, &a, NULL); }
Obj *call2(Obj *f, Obj *a, Obj *b) { Obj *args[2] = {a, b}; return obj_call(f, 2, args, NULL); }

Obj *call_method(Obj *o, Str *name, int argc, Obj **argv, bool *found) {
    Obj *m = obj_getattr_opt(o, name);
    if (found) *found = m != NULL;
    if (!m) {
        if (!vm.exc && !found) err(E_AttributeError, "'%s' object has no attribute '%s'", tname(o), name->s);
        return NULL;
    }
    return obj_call(m, argc, argv, NULL);
}

/* ---------- generators ---------- */
Obj *gen_send(Generator *g, Obj *val, Obj **retval) {
    Frame *f = g->frame;
    *retval = Py_None;
    if (g->running) return err(E_ValueError, "generator already executing");
    if (f->state == FS_DONE) return NULL;
    if (vm.depth >= vm.recursion_limit) return err(E_RecursionError, "maximum recursion depth exceeded");
    if (f->state == FS_NEW) {
        if (val != Py_None) return err(E_TypeError, "can't send non-None value to a just-started generator");
    } else *f->sp++ = val;
    g->running = true;
    Obj *r = run_frame(f, NULL);
    g->running = false;
    if (f->state == FS_SUSPENDED) return r;
    if (!r) {
        if (err_matches(E_StopIteration)) {
            Obj *orig = vm.exc;
            vm.exc = NULL;
            err(E_RuntimeError, "generator raised StopIteration");
            ((Exc *)vm.exc)->cause = orig;
        }
        return NULL;
    }
    *retval = r;
    return NULL;
}

Obj *gen_throw(Generator *g, Obj *exc, Obj **retval) {
    Frame *f = g->frame;
    *retval = Py_None;
    if (g->running) return err(E_ValueError, "generator already executing");
    if (f->state == FS_NEW || f->state == FS_DONE) {
        f->state = FS_DONE;
        err_set(exc);
        return NULL;
    }
    g->running = true;
    Obj *r = run_frame(f, exc);
    g->running = false;
    if (f->state == FS_SUSPENDED) return r;
    if (!r) return NULL;
    *retval = r;
    return NULL;
}

/* ---------- finalizers ---------- */
/* Runs __del__ methods and closes abandoned generators that the collector queued.
   Called between bytecodes, never from inside the collector. */
void run_finalizers(void) {
    if (vm.finalizing || !vm.pending || vm.pending->n == 0 || vm.exc) return;
    vm.finalizing = true;
    Obj *saved_handled = vm.handled;
    while (vm.pending->n > 0) {
        Obj *o = vm.pending->items[--vm.pending->n];
        const char *what = "__del__";
        if (KIND(o) == K_GEN) {
            Generator *g = (Generator *)o;
            what = "generator cleanup";
            if (g->frame->state == FS_SUSPENDED && !g->running) {
                Obj *ret, *v = gen_throw(g, exc_new(E_GeneratorExit, (Tuple *)tuple_new(0)), &ret);
                if (v) { g->frame->state = FS_DONE; err(E_RuntimeError, "generator ignored GeneratorExit"); }
                else if (vm.exc && (err_matches(E_GeneratorExit) || err_matches(E_StopIteration))) err_clear();
            }
        } else if (!(o->mark & 2)) {
            o->mark |= 2;
            Obj *f = type_lookup(o->type, N_del);
            if (f) call1(f, o);
        }
        if (vm.exc) {
            Obj *e = vm.exc;
            vm.exc = NULL;
            fflush(stdout);
            fprintf(stderr, "Exception ignored in %s:\n", what);
            print_exception(e);
        }
        vm.handled = saved_handled;
    }
    vm.finalizing = false;
}

/* ---------- helpers for the main loop ---------- */
static Obj *make_exception(Obj *o) {
    if (KIND(o) == K_TYPE) {
        if (!type_issub((Type *)o, E_BaseException)) return err(E_TypeError, "exceptions must derive from BaseException");
        o = call0(o);
        if (!o) return NULL;
    }
    if (!isinstance(o, E_BaseException)) return err(E_TypeError, "exceptions must derive from BaseException");
    return o;
}

static int exc_match(Obj *exc, Obj *t) {
    if (KIND(t) == K_TUPLE) {
        Tuple *tu = (Tuple *)t;
        for (int64_t i = 0; i < tu->n; i++) {
            int r = exc_match(exc, tu->items[i]);
            if (r) return r;
        }
        return 0;
    }
    if (KIND(t) != K_TYPE || !type_issub((Type *)t, E_BaseException)) {
        err(E_TypeError, "catching classes that do not inherit from BaseException is not allowed");
        return -1;
    }
    return isinstance(exc, (Type *)t);
}

static Obj *unbound_local(Str *name) {
    return err(E_UnboundLocalError, "cannot access local variable '%s' where it is not associated with a value", name->s);
}

static bool is_mapping(Obj *o) {
    if (KIND(o) == K_DICT) return true;
    Obj *k = obj_getattr_opt(o, AS_STR(str_c("keys")));
    if (!k) { err_clear(); return false; }
    return true;
}

static Obj *do_call_ex(Obj *callee, Obj *self, Obj *args, Obj *kwargs) {
    List *a = (List *)list_new(0);
    if (self) list_append(a, self);
    if (list_extend(a, args) < 0) return NULL;
    int npos = (int)a->n;
    Tuple *names = NULL;
    if (kwargs) {
        Dict *d = (Dict *)kwargs;
        names = (Tuple *)tuple_new(d->used);
        int64_t pos = 0, i = 0;
        Obj *k, *v;
        while (dict_next(d, &pos, &k, &v)) {
            if (!IS_STR(k)) return err(E_TypeError, "keywords must be strings");
            names->items[i++] = k;
            list_append(a, v);
        }
    }
    return obj_call(callee, npos, a->items, (Obj *)names);
}

static Obj *build_class(Obj *bodyfn, Obj *name, Tuple *bases) {
    Func *bf = (Func *)bodyfn;
    Dict *ns = (Dict *)dict_new();
    Frame *cf = frame_new(bf->code, bf->globals, ns, bf);
    if (bf->code->ncells + bf->code->nfree) frame_init_cells(cf, bf->closure);
    Obj *r = run_frame(cf, NULL);
    if (!r) return NULL;
    for (int64_t i = 0; i < bases->n; i++)
        if (KIND(bases->items[i]) != K_TYPE) return err(E_TypeError, "bases must be types");
    Type *cls = class_new(AS_STR(name), bases, ns);
    if (!cls) return NULL;
    if (KIND(r) == K_CELL) ((Cell *)r)->value = (Obj *)cls;
    /* __set_name__ hooks on class attributes */
    for (int64_t i = 0; i < ns->n; i++) {
        Obj *k = ns->e[i].key, *v = ns->e[i].val;
        if (!k || !v->type->user) continue;
        Obj *hook = type_lookup(v->type, N_set_name);
        if (hook) { Obj *args[3] = {v, (Obj *)cls, k}; if (!obj_call(hook, 3, args, NULL)) return NULL; }
    }
    /* __init_subclass__ on the first base that defines it */
    Str *isc = AS_STR(str_c("__init_subclass__"));
    for (int64_t i = 1; i < cls->mro->n; i++) {
        Obj *h = dict_gets(((Type *)cls->mro->items[i])->dict, isc);
        if (!h) continue;
        if (KIND(h) == K_WRAPPER) h = ((Wrapper *)h)->fn;
        if (!call1(h, (Obj *)cls)) return NULL;
        break;
    }
    return (Obj *)cls;
}

static int unpack(Obj *v, int n, Obj **out) {
    if (n == 0 && (KIND(v) == K_TUPLE || KIND(v) == K_LIST)) {
        if ((KIND(v) == K_TUPLE ? ((Tuple *)v)->n : ((List *)v)->n) == 0) return 0;
    }
    if (KIND(v) == K_TUPLE && ((Tuple *)v)->n == n) { memcpy(out, ((Tuple *)v)->items, (size_t)n * sizeof(Obj *)); return 0; }
    if (KIND(v) == K_LIST && ((List *)v)->n == n) { memcpy(out, ((List *)v)->items, (size_t)n * sizeof(Obj *)); return 0; }
    Obj *it = obj_iter(v);
    if (!it) {
        if (err_matches(E_TypeError)) { err_clear(); err(E_TypeError, "cannot unpack non-iterable %s object", tname(v)); }
        return -1;
    }
    for (int i = 0; i < n; i++) {
        Obj *x = iter_next(it);
        if (!x) {
            if (!vm.exc) err(E_ValueError, "not enough values to unpack (expected %d, got %d)", n, i);
            return -1;
        }
        out[i] = x;
    }
    if (iter_next(it)) { err(E_ValueError, "too many values to unpack (expected %d)", n); return -1; }
    return vm.exc ? -1 : 0;
}

/* ---------- the interpreter loop ---------- */
static Obj *run_frame(Frame *f, Obj *throw) {
    if (vm.depth >= vm.recursion_limit) return err(E_RecursionError, "maximum recursion depth exceeded");
    vm.depth++;
    Code *co = f->code;
    uint32_t *code = co->code;
    Obj **fast = f->fast, **sp = f->sp;
    Obj **consts = co->consts->items, **names = co->names->items;
    int pc = f->pc;
    Obj *retval = NULL, *v, *w, *x;
    int t;
    f->back = vm.frame;
    vm.frame = f;
    if (f->state == FS_SUSPENDED) {
        /* Resuming inside an except block: make that exception the handled one again. */
        for (int i = 0; i < f->nblocks; i++)
            if (f->blocks[i].type == B_HANDLER) { f->blocks[i].prev = vm.handled; break; }
        for (int i = f->nblocks - 1; i >= 0; i--)
            if (f->blocks[i].type == B_HANDLER) { vm.handled = f->stack[f->blocks[i].level]; break; }
    }
    f->state = FS_RUNNING;
    if (throw) { err_set(throw); goto error; }
    if (vm.pending->n && !vm.finalizing) run_finalizers();

#define PUSH(o) (*sp++ = (o))
#define POP() (*--sp)
#define TOP() (sp[-1])
#define NAME(i) AS_STR(names[i])
    for (;;) {
        uint32_t ins = code[pc++];
        int arg = I_ARG(ins);
        switch (I_OP(ins)) {
        case OP_NOP_: continue;
        case OP_POP_TOP_: sp--; continue;
        case OP_DUP_TOP_: sp[0] = sp[-1]; sp++; continue;
        case OP_DUP_TOP_TWO_: sp[0] = sp[-2]; sp[1] = sp[-1]; sp += 2; continue;
        case OP_ROT_TWO_: v = sp[-1]; sp[-1] = sp[-2]; sp[-2] = v; continue;
        case OP_ROT_THREE_: v = sp[-1]; sp[-1] = sp[-2]; sp[-2] = sp[-3]; sp[-3] = v; continue;
        case OP_LOAD_CONST_: PUSH(consts[arg]); continue;
        case OP_LOAD_FAST_:
            v = fast[arg];
            if (!v) { unbound_local(AS_STR(co->varnames->items[arg])); goto error; }
            PUSH(v);
            continue;
        case OP_STORE_FAST_: fast[arg] = POP(); continue;
        case OP_DELETE_FAST_:
            if (!fast[arg]) { unbound_local(AS_STR(co->varnames->items[arg])); goto error; }
            fast[arg] = NULL;
            continue;
        case OP_LOAD_DEREF_:
            v = f->cells[arg]->value;
            if (!v) {
                Str *nm = AS_STR(co->cellnames->items[arg]);
                if (arg < co->ncells) unbound_local(nm);
                else err(E_NameError, "cannot access free variable '%s' where it is not associated with a value in enclosing scope", nm->s);
                goto error;
            }
            PUSH(v);
            continue;
        case OP_STORE_DEREF_: f->cells[arg]->value = POP(); continue;
        case OP_DELETE_DEREF_:
            if (!f->cells[arg]->value) { unbound_local(AS_STR(co->cellnames->items[arg])); goto error; }
            f->cells[arg]->value = NULL;
            continue;
        case OP_LOAD_CLOSURE_: PUSH((Obj *)f->cells[arg]); continue;
        case OP_LOAD_CLASSDEREF_: {
            Str *nm = NAME(arg >> 8);
            v = f->locals ? dict_gets(f->locals, nm) : NULL;
            if (!v) v = f->cells[arg & 0xFF]->value;
            if (!v) { err(E_NameError, "cannot access free variable '%s' where it is not associated with a value in enclosing scope", nm->s); goto error; }
            PUSH(v);
            continue;
        }
        case OP_LOAD_GLOBAL_:
            v = dict_gets(f->globals, NAME(arg));
            if (!v) {
                v = dict_gets(vm.builtins, NAME(arg));
                if (!v) { err(E_NameError, "name '%s' is not defined", NAME(arg)->s); goto error; }
            }
            PUSH(v);
            continue;
        case OP_STORE_GLOBAL_: dict_sets(f->globals, NAME(arg), POP()); continue;
        case OP_DELETE_GLOBAL_:
            if (!dict_dels(f->globals, NAME(arg))) { err(E_NameError, "name '%s' is not defined", NAME(arg)->s); goto error; }
            continue;
        case OP_LOAD_NAME_:
            v = f->locals ? dict_gets(f->locals, NAME(arg)) : NULL;
            if (!v) v = dict_gets(f->globals, NAME(arg));
            if (!v) v = dict_gets(vm.builtins, NAME(arg));
            if (!v) { err(E_NameError, "name '%s' is not defined", NAME(arg)->s); goto error; }
            PUSH(v);
            continue;
        case OP_STORE_NAME_: dict_sets(f->locals ? f->locals : f->globals, NAME(arg), POP()); continue;
        case OP_DELETE_NAME_:
            if (!dict_dels(f->locals ? f->locals : f->globals, NAME(arg))) { err(E_NameError, "name '%s' is not defined", NAME(arg)->s); goto error; }
            continue;
        case OP_LOAD_ATTR_:
            v = obj_getattr(TOP(), NAME(arg));
            if (!v) goto error;
            TOP() = v;
            continue;
        case OP_STORE_ATTR_:
            v = sp[-1]; w = sp[-2]; sp -= 2;
            if (obj_setattr(v, NAME(arg), w) < 0) goto error;
            continue;
        case OP_DELETE_ATTR_:
            v = POP();
            if (obj_setattr(v, NAME(arg), NULL) < 0) goto error;
            continue;
        case OP_LOAD_METHOD_: {
            Obj *o = TOP();
            Type *tp = o->type;
            int k = tp->kind;
            if (k != K_TYPE && k != K_MODULE && k != K_SUPER) {
                Obj *m = type_lookup(tp, NAME(arg));
                if (m && (KIND(m) == K_FUNC || (KIND(m) == K_BUILTIN && (((Builtin *)m)->flags & BF_METHOD)))) {
                    bool shadow = false;
                    if (k == K_INSTANCE || k == K_EXC || tp->user) {
                        Dict *d = obj_dict(o, false);
                        shadow = d && dict_gets(d, NAME(arg));
                    }
                    if (!shadow) { TOP() = m; PUSH(o); continue; }
                }
            }
            v = obj_getattr(o, NAME(arg));
            if (!v) goto error;
            TOP() = v;
            PUSH(NULL);
            continue;
        }
        case OP_PUSH_NULL_: PUSH(NULL); continue;
        case OP_BINARY_SUBSCR_:
            w = POP(); v = TOP();
            if (v->type == T_list && w->type == T_int && !((Int *)w)->d) {
                int64_t i = ((Int *)w)->v;
                List *l = (List *)v;
                if (i < 0) i += l->n;
                if (i >= 0 && i < l->n) { TOP() = l->items[i]; continue; }
            }
            x = obj_getitem(v, w);
            if (!x) goto error;
            TOP() = x;
            continue;
        case OP_STORE_SUBSCR_:
            w = sp[-1]; v = sp[-2]; x = sp[-3]; sp -= 3;
            if (obj_setitem(v, w, x) < 0) goto error;
            continue;
        case OP_DELETE_SUBSCR_:
            w = sp[-1]; v = sp[-2]; sp -= 2;
            if (obj_delitem(v, w) < 0) goto error;
            continue;
        case OP_BINARY_OP_: case OP_INPLACE_OP_:
            w = POP(); v = TOP();
            if (v->type == T_int && w->type == T_int && !((Int *)v)->d && !((Int *)w)->d) {
                int64_t a = ((Int *)v)->v, b = ((Int *)w)->v, r;
                if (arg == OP_ADD) { if (!__builtin_add_overflow(a, b, &r)) { TOP() = int_new(r); continue; } }
                else if (arg == OP_SUB) { if (!__builtin_sub_overflow(a, b, &r)) { TOP() = int_new(r); continue; } }
                else if (arg == OP_MUL) { if (!__builtin_mul_overflow(a, b, &r)) { TOP() = int_new(r); continue; } }
            }
            x = binary_op(arg, v, w, I_OP(ins) == OP_INPLACE_OP_);
            if (!x) goto error;
            TOP() = x;
            continue;
        case OP_UNARY_OP_:
            v = unary_op(arg, TOP());
            if (!v) goto error;
            TOP() = v;
            continue;
        case OP_UNARY_NOT_:
            t = obj_truthy(TOP());
            if (t < 0) goto error;
            TOP() = BOOL(!t);
            continue;
        case OP_COMPARE_OP_:
            w = POP(); v = TOP();
            if (v->type == T_int && w->type == T_int && !((Int *)v)->d && !((Int *)w)->d) {
                int64_t a = ((Int *)v)->v, b = ((Int *)w)->v;
                bool r = arg == CMP_LT ? a < b : arg == CMP_LE ? a <= b : arg == CMP_EQ ? a == b :
                         arg == CMP_NE ? a != b : arg == CMP_GT ? a > b : a >= b;
                TOP() = BOOL(r);
                continue;
            }
            x = obj_richcmp(v, w, arg);
            if (!x) goto error;
            TOP() = x;
            continue;
        case OP_IS_OP_:
            w = POP();
            TOP() = BOOL((TOP() == w) != (arg != 0));
            continue;
        case OP_CONTAINS_OP_:
            w = POP();
            t = obj_contains(w, TOP());
            if (t < 0) goto error;
            TOP() = BOOL((t != 0) != (arg != 0));
            continue;
        case OP_JUMP_:
            if (arg < pc && vm.interrupted) { vm.interrupted = 0; err(E_KeyboardInterrupt, "%s", ""); ((Exc *)vm.exc)->args = (Tuple *)tuple_new(0); goto error; }
            if (arg < pc && vm.pending->n && !vm.finalizing) { f->sp = sp; run_finalizers(); }
            pc = arg;
            continue;
        case OP_POP_JUMP_IF_FALSE_:
            v = POP();
            t = v == Py_True ? 1 : v == Py_False ? 0 : obj_truthy(v);
            if (t < 0) goto error;
            if (!t) pc = arg;
            continue;
        case OP_POP_JUMP_IF_TRUE_:
            v = POP();
            t = v == Py_True ? 1 : v == Py_False ? 0 : obj_truthy(v);
            if (t < 0) goto error;
            if (t) pc = arg;
            continue;
        case OP_JUMP_IF_FALSE_OR_POP_:
            t = obj_truthy(TOP());
            if (t < 0) goto error;
            if (!t) pc = arg; else sp--;
            continue;
        case OP_JUMP_IF_TRUE_OR_POP_:
            t = obj_truthy(TOP());
            if (t < 0) goto error;
            if (t) pc = arg; else sp--;
            continue;
        case OP_GET_ITER_:
            v = obj_iter(TOP());
            if (!v) goto error;
            TOP() = v;
            continue;
        case OP_FOR_ITER_:
            v = iter_next(TOP());
            if (v) { PUSH(v); continue; }
            if (vm.exc) goto error;
            sp--;
            pc = arg;
            continue;
        case OP_BUILD_TUPLE_: v = tuple_from(sp - arg, arg); sp -= arg; PUSH(v); continue;
        case OP_BUILD_LIST_: v = list_from(sp - arg, arg); sp -= arg; PUSH(v); continue;
        case OP_BUILD_SET_:
            v = set_new(T_set);
            for (int i = 0; i < arg; i++) if (set_add((Set *)v, sp[i - arg]) < 0) goto error;
            sp -= arg;
            PUSH(v);
            continue;
        case OP_BUILD_MAP_:
            v = dict_new();
            for (int i = 0; i < arg; i++)
                if (dict_set((Dict *)v, sp[2 * (i - arg)], sp[2 * (i - arg) + 1]) < 0) goto error;
            sp -= 2 * arg;
            PUSH(v);
            continue;
        case OP_BUILD_STRING_: {
            SB b;
            sb_init(&b);
            for (int i = 0; i < arg; i++) sb_putstr(&b, sp[i - arg]);
            sp -= arg;
            PUSH(sb_finish(&b));
            continue;
        }
        case OP_BUILD_SLICE_:
            if (arg == 3) { v = slice_new(sp[-3], sp[-2], sp[-1]); sp -= 3; }
            else { v = slice_new(sp[-2], sp[-1], Py_None); sp -= 2; }
            PUSH(v);
            continue;
        case OP_LIST_APPEND_: v = POP(); list_append((List *)sp[-arg], v); continue;
        case OP_SET_ADD_:
            v = POP();
            if (set_add((Set *)sp[-arg], v) < 0) goto error;
            continue;
        case OP_MAP_ADD_:
            w = sp[-1]; v = sp[-2]; sp -= 2;
            if (dict_set((Dict *)sp[-arg], v, w) < 0) goto error;
            continue;
        case OP_LIST_EXTEND_:
            v = POP();
            if (list_extend((List *)sp[-arg], v) < 0) {
                if (err_matches(E_TypeError) && KIND(v) != K_GEN && KIND(v) != K_ITER) {
                    err_clear();
                    err(E_TypeError, "Value after * must be an iterable, not %s", tname(v));
                }
                goto error;
            }
            continue;
        case OP_SET_UPDATE_:
            v = POP();
            if (set_update((Set *)sp[-arg], v) < 0) goto error;
            continue;
        case OP_DICT_UPDATE_:
            v = POP();
            if (!is_mapping(v)) { err(E_TypeError, "'%s' object is not a mapping", tname(v)); goto error; }
            if (dict_update((Dict *)sp[-arg], v) < 0) goto error;
            continue;
        case OP_DICT_MERGE_: {
            v = POP();
            Dict *d = (Dict *)sp[-arg], *src;
            if (!is_mapping(v)) { err(E_TypeError, "argument after ** must be a mapping, not %s", tname(v)); goto error; }
            if (KIND(v) != K_DICT) {
                src = (Dict *)dict_new();
                if (dict_update(src, v) < 0) goto error;
            } else src = (Dict *)v;
            int64_t pos = 0;
            Obj *k, *val;
            bool bad = false;
            while (dict_next(src, &pos, &k, &val)) {
                if (!IS_STR(k)) { err(E_TypeError, "keywords must be strings"); bad = true; break; }
                if (dict_gets(d, AS_STR(k))) { err(E_TypeError, "got multiple values for keyword argument '%s'", AS_STR(k)->s); bad = true; break; }
                dict_sets(d, AS_STR(k), val);
            }
            if (bad) goto error;
            continue;
        }
        case OP_LIST_TO_TUPLE_: v = TOP(); TOP() = tuple_from(((List *)v)->items, ((List *)v)->n); continue;
        case OP_UNPACK_SEQUENCE_: {
            v = POP();
            if (unpack(v, arg, sp) < 0) goto error;
            /* The first target is stored first, so it must end up on top. */
            for (int i = 0, j = arg - 1; i < j; i++, j--) { w = sp[i]; sp[i] = sp[j]; sp[j] = w; }
            sp += arg;
            continue;
        }
        case OP_UNPACK_EX_: {
            int before = arg & 0xFF, after = arg >> 8;
            v = POP();
            List *l = (List *)seq_to_list(v);
            if (!l) {
                if (err_matches(E_TypeError)) { err_clear(); err(E_TypeError, "cannot unpack non-iterable %s object", tname(v)); }
                goto error;
            }
            if (l->n < before + after) {
                err(E_ValueError, "not enough values to unpack (expected at least %d, got %lld)", before + after, (long long)l->n);
                goto error;
            }
            for (int i = after - 1; i >= 0; i--) PUSH(l->items[l->n - after + i]);
            PUSH(list_from(l->items + before, l->n - before - after));
            for (int i = before - 1; i >= 0; i--) PUSH(l->items[i]);
            continue;
        }
        case OP_FORMAT_VALUE_: {
            Obj *spec = (arg & 4) ? POP() : NULL;
            v = TOP();
            int conv = arg & 3;
            if (conv == 1) v = obj_str(v);
            else if (conv == 2 || conv == 3) v = obj_repr(v);
            if (!v) goto error;
            if (spec || !IS_STR(v)) v = obj_format(v, spec ? AS_STR(spec) : NULL);
            if (!v) goto error;
            TOP() = v;
            continue;
        }
        case OP_CALL_: {
            Obj **base = sp - arg - 2;
            v = base[1] ? obj_call(base[0], arg + 1, base + 1, NULL) : obj_call(base[0], arg, base + 2, NULL);
            while (sp > base) *--sp = NULL;   /* stale slots would keep these objects alive */
            if (!v) goto error;
            PUSH(v);
            continue;
        }
        case OP_CALL_KW_: {
            Obj *kwnames = POP();
            int nkw = (int)((Tuple *)kwnames)->n;
            Obj **base = sp - arg - 2;
            v = base[1] ? obj_call(base[0], arg - nkw + 1, base + 1, kwnames) : obj_call(base[0], arg - nkw, base + 2, kwnames);
            while (sp > base) *--sp = NULL;
            if (!v) goto error;
            PUSH(v);
            continue;
        }
        case OP_CALL_EX_: {
            Obj *kwargs = (arg & 1) ? POP() : NULL;
            w = POP();
            Obj *self = POP(), *callee = POP();
            v = do_call_ex(callee, self, w, kwargs);
            if (!v) goto error;
            PUSH(v);
            continue;
        }
        case OP_MAKE_FUNCTION_: {
            Code *c = (Code *)POP();
            Func *fn = gc_alloc(T_function, sizeof(Func));
            fn->code = c; fn->globals = f->globals;
            fn->name = c->name; fn->qualname = c->qualname; fn->doc = c->doc;
            if (arg & 8) fn->closure = (Tuple *)POP();
            if (arg & 4) fn->annotations = (Dict *)POP();
            if (arg & 2) fn->kwdefaults = (Dict *)POP();
            if (arg & 1) fn->defaults = (Tuple *)POP();
            PUSH((Obj *)fn);
            continue;
        }
        case OP_MATCH_SEQ_: {
            /* Sequence patterns match lists, tuples and ranges, never strings. */
            v = TOP();
            int k = KIND(v);
            t = 0;
            if (k == K_LIST || k == K_TUPLE || k == K_RANGE) {
                int64_t len = obj_len(v);
                if (len < 0) goto error;
                t = (arg & 1) ? len >= (arg >> 1) : len == (arg >> 1);
            }
            PUSH(BOOL(t));
            continue;
        }
        case OP_MATCH_MAP_:
            v = TOP();
            PUSH(BOOL(KIND(v) == K_DICT));
            continue;
        case OP_MATCH_KEYS_:
            v = match_keys(sp[-2], (Tuple *)TOP(), arg != 0);
            if (!v) goto error;
            TOP() = v;
            continue;
        case OP_MATCH_CLASS_:
            v = match_class(sp[-3], sp[-2], arg, (Tuple *)sp[-1]);
            if (!v) goto error;
            sp -= 2;
            TOP() = v;
            continue;
        case OP_RETURN_VALUE_:
            retval = POP();
            f->state = FS_DONE;
            goto leave;
        case OP_YIELD_VALUE_:
            retval = POP();
            goto suspend;
        case OP_GET_YIELD_FROM_ITER_:
            if (KIND(TOP()) != K_GEN) {
                v = obj_iter(TOP());
                if (!v) goto error;
                TOP() = v;
            }
            continue;
        case OP_YIELD_FROM_: {
            Obj *sent = POP(), *it = TOP(), *ret = Py_None;
            if (KIND(it) == K_GEN) v = gen_send((Generator *)it, sent, &ret);
            else if (sent == Py_None) v = iter_next(it);
            else {
                v = call_method(it, AS_STR(str_c("send")), 1, &sent, NULL);
                if (!v && err_matches(E_StopIteration)) {
                    Exc *e = (Exc *)vm.exc;
                    ret = e->args->n ? e->args->items[0] : Py_None;
                    err_clear();
                }
            }
            if (v) { pc--; retval = v; goto suspend; }
            if (vm.exc) goto error;
            TOP() = ret;
            continue;
        }
        case OP_SETUP_TRY_: {
            if (f->nblocks >= co->maxblocks) fatal("block stack overflow");
            Block *b = &f->blocks[f->nblocks++];
            b->type = B_TRY; b->handler = arg; b->level = (int)(sp - f->stack); b->prev = NULL;
            continue;
        }
        case OP_POP_BLOCK_: f->nblocks--; continue;
        case OP_POP_EXCEPT_:
            vm.handled = f->blocks[--f->nblocks].prev;
            sp--;
            continue;
        case OP_RERAISE_:
            vm.exc = POP();
            goto unwind;
        case OP_RAISE_: {
            if (arg == 0) {
                if (!vm.handled) { err(E_RuntimeError, "No active exception to reraise"); goto error; }
                vm.exc = vm.handled;
                goto unwind;
            }
            Obj *cause = arg == 2 ? POP() : NULL;
            v = make_exception(POP());
            if (!v) goto error;
            if (cause && cause != Py_None) {
                cause = make_exception(cause);
                if (!cause) goto error;
            }
            err_set(v);
            if (cause == Py_None) {
                Exc *e = (Exc *)v;
                if (!e->dict) e->dict = (Dict *)dict_new();
                dict_setc(e->dict, "__suppress_context__", Py_True);
            } else if (cause) ((Exc *)v)->cause = cause;
            goto error;
        }
        case OP_EXC_MATCH_:
            w = POP();
            t = exc_match(TOP(), w);
            if (t < 0) goto error;
            TOP() = BOOL(t);
            continue;
        case OP_BEFORE_WITH_: {
            Obj *mgr = TOP();
            Obj *ex = obj_getattr_opt(mgr, N_exit), *en = ex ? obj_getattr_opt(mgr, N_enter) : NULL;
            if (!ex || !en) {
                if (!vm.exc) err(E_TypeError, "'%s' object does not support the context manager protocol", tname(mgr));
                goto error;
            }
            TOP() = ex;
            v = call0(en);
            if (!v) goto error;
            PUSH(v);
            continue;
        }
        case OP_WITH_EXIT_: {
            Obj *args[3] = {Py_None, Py_None, Py_None};
            v = POP();
            if (!obj_call(v, 3, args, NULL)) goto error;
            continue;
        }
        case OP_WITH_EXCEPT_: {
            Obj *args[3] = {(Obj *)sp[-1]->type, sp[-1], Py_None};
            v = obj_call(sp[-2], 3, args, NULL);
            if (!v) goto error;
            t = obj_truthy(v);
            if (t < 0) goto error;
            PUSH(BOOL(t));
            continue;
        }
        case OP_BUILD_CLASS_: {
            Obj **base = sp - arg - 2;
            v = build_class(base[0], base[1], (Tuple *)tuple_from(base + 2, arg));
            sp = base;
            if (!v) goto error;
            PUSH(v);
            continue;
        }
        case OP_IMPORT_NAME_: {
            Str *full = NAME(arg >> 1);
            v = import_module(full);
            if (!v) goto error;
            if (!(arg & 1)) {
                const char *dot = memchr(full->s, '.', full->len);
                if (dot) {
                    v = import_module(AS_STR(str_new(full->s, (size_t)(dot - full->s))));
                    if (!v) goto error;
                }
            }
            PUSH(v);
            continue;
        }
        case OP_IMPORT_FROM_:
            v = obj_getattr_opt(TOP(), NAME(arg));
            if (!v) {
                if (!vm.exc) {
                    Obj *mn = KIND(TOP()) == K_MODULE ? (Obj *)((Module *)TOP())->name : str_c("?");
                    err(E_ImportError, "cannot import name '%s' from '%s'", NAME(arg)->s, AS_STR(mn)->s);
                }
                goto error;
            }
            PUSH(v);
            continue;
        case OP_IMPORT_STAR_: {
            v = POP();
            if (KIND(v) != K_MODULE) { err(E_ImportError, "import * needs a module"); goto error; }
            Dict *d = ((Module *)v)->dict;
            Obj *all = dict_gets(d, N_all), *k, *val;
            if (all) {
                Obj *it = obj_iter(all);
                if (!it) goto error;
                while ((k = iter_next(it))) {
                    val = IS_STR(k) ? dict_gets(d, AS_STR(k)) : NULL;
                    if (!val) { err(E_AttributeError, "module has no attribute named in __all__"); goto error; }
                    dict_sets(f->globals, AS_STR(k), val);
                }
                if (vm.exc) goto error;
            } else {
                int64_t pos = 0;
                while (dict_next(d, &pos, &k, &val))
                    if (IS_STR(k) && AS_STR(k)->s[0] != '_') dict_sets(f->globals, AS_STR(k), val);
            }
            continue;
        }
        case OP_PRINT_EXPR_:
            v = POP();
            if (v != Py_None) {
                w = obj_repr(v);
                if (!w) goto error;
                fwrite(AS_STR(w)->s, 1, AS_STR(w)->len, stdout);
                fputc('\n', stdout);
                dict_setc(vm.builtins, "_", v);
            }
            continue;
        default:
            fatal("bad opcode");
        }
    error:
        tb_add(f, co->lines[pc > 0 ? pc - 1 : 0]);
    unwind:
        while (f->nblocks > 0) {
            Block *b = &f->blocks[--f->nblocks];
            if (b->type == B_HANDLER) { vm.handled = b->prev; continue; }
            Obj *e = vm.exc;
            vm.exc = NULL;
            sp = f->stack + b->level;
            int handler = b->handler;
            Block *h = &f->blocks[f->nblocks++];
            h->type = B_HANDLER; h->prev = vm.handled; h->level = (int)(sp - f->stack); h->handler = 0;
            vm.handled = e;
            PUSH(e);
            pc = handler;
            break;
        }
        if (vm.exc) { retval = NULL; f->state = FS_DONE; goto leave; }
    }
suspend:
    f->sp = sp;
    f->pc = pc;
    f->state = FS_SUSPENDED;
    if (f->nblocks > 0) vm.fin_possible = true;   /* a try or with block is open: needs cleanup if abandoned */
    /* While suspended, this frame's except blocks do not apply to the caller. */
    for (int i = 0; i < f->nblocks; i++)
        if (f->blocks[i].type == B_HANDLER) { vm.handled = f->blocks[i].prev; break; }
leave:
    vm.frame = f->back;
    f->back = NULL;
    vm.depth--;
    return retval;
}

/* ---------- match statement support ---------- */

/* For a mapping pattern. With rest false: the values for keys as a tuple, or None when a
   key is missing. With rest true: a copy of the dict without those keys. */
Obj *match_keys(Obj *subject, Tuple *keys, bool rest) {
    Dict *d = (Dict *)subject;
    if (rest) {
        Dict *out = (Dict *)dict_copy(d);
        for (int64_t i = 0; i < keys->n; i++) {
            Obj *v;
            int r = dict_get(out, keys->items[i], &v);
            if (r < 0) return NULL;
            if (r > 0 && dict_del(out, keys->items[i]) < 0) return NULL;
        }
        return (Obj *)out;
    }
    for (int64_t i = 0; i < keys->n; i++)
        for (int64_t j = 0; j < i; j++) {
            int e = obj_eq(keys->items[i], keys->items[j]);
            if (e < 0) return NULL;
            if (e) {
                Obj *r = obj_repr(keys->items[i]);
                if (!r) return NULL;
                return err(E_ValueError, "mapping pattern checks duplicate key (%s)", AS_STR(r)->s);
            }
        }
    Tuple *vals = (Tuple *)tuple_new(keys->n);
    for (int64_t i = 0; i < keys->n; i++) {
        Obj *v;
        int r = dict_get(d, keys->items[i], &v);
        if (r < 0) return NULL;
        if (r == 0) return Py_None;
        vals->items[i] = v;
    }
    return (Obj *)vals;
}

/* For a class pattern: the attribute values to match the sub-patterns against, as a
   tuple, or None when the subject is not an instance or lacks an attribute. */
Obj *match_class(Obj *subject, Obj *cls, int npos, Tuple *kwnames) {
    if (KIND(cls) != K_TYPE) return err(E_TypeError, "called match pattern must be a class");
    Type *tp = (Type *)cls;
    if (!type_issub(subject->type, tp)) return Py_None;
    Tuple *out = (Tuple *)tuple_new(npos + kwnames->n);
    Str *seen[512];
    int nseen = 0;
    if (npos) {
        Obj *ma = type_lookup(tp, AS_STR(str_c("__match_args__")));
        if (!ma) {
            /* int(x), str(x) and the other basic types match the subject itself. */
            Type *b = builtin_base(tp);
            bool self_match = b == T_bool || b == T_int || b == T_float || b == T_str || b == T_list || b == T_tuple ||
                              b == T_dict || b == T_set || b == T_frozenset;
            if (!self_match || npos > 1)
                return err(E_TypeError, "%s() accepts %d positional sub-pattern%s (%d given)", tp->name->s,
                           self_match ? 1 : 0, self_match ? "" : "s", npos);
            out->items[0] = subject;
        } else {
            if (KIND(ma) != K_TUPLE) return err(E_TypeError, "%s.__match_args__ must be a tuple (got %s)", tp->name->s, tname(ma));
            Tuple *names = (Tuple *)ma;
            if (npos > names->n)
                return err(E_TypeError, "%s() accepts %d positional sub-pattern%s (%d given)", tp->name->s, (int)names->n,
                           names->n == 1 ? "" : "s", npos);
            for (int i = 0; i < npos; i++) {
                if (!IS_STR(names->items[i]))
                    return err(E_TypeError, "__match_args__ elements must be strings (got %s)", tname(names->items[i]));
                Obj *v = obj_getattr_opt(subject, AS_STR(names->items[i]));
                if (!v) { if (vm.exc) return NULL; return Py_None; }
                out->items[i] = v;
                if (nseen < 512) seen[nseen++] = AS_STR(names->items[i]);
            }
        }
    }
    for (int64_t i = 0; i < kwnames->n; i++) {
        Str *name = AS_STR(kwnames->items[i]);
        for (int j = 0; j < nseen; j++)
            if (str_eq(seen[j], name))
                return err(E_TypeError, "%s() got multiple sub-patterns for attribute '%s'", tp->name->s, name->s);
        Obj *v = obj_getattr_opt(subject, name);
        if (!v) { if (vm.exc) return NULL; return Py_None; }
        out->items[npos + i] = v;
    }
    return (Obj *)out;
}
