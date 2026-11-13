/* Shared declarations for the interpreter. */
#ifndef PY_H
#define PY_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

typedef struct Obj Obj;
typedef struct Type Type;
typedef struct Str Str;
typedef struct Dict Dict;
typedef struct Tuple Tuple;
typedef struct List List;
typedef struct Frame Frame;
typedef struct Code Code;

/* Every heap value starts with this header. */
struct Obj { Type *type; uint32_t size; uint8_t mark; };

enum {
    K_INSTANCE, K_EXC, K_NONE, K_NOTIMPL, K_ELLIPSIS, K_BOOL, K_INT, K_FLOAT, K_STR,
    K_LIST, K_TUPLE, K_DICT, K_SET, K_RANGE, K_SLICE, K_FUNC, K_BUILTIN, K_METHOD,
    K_TYPE, K_MODULE, K_CODE, K_FRAME, K_CELL, K_GEN, K_ITER, K_PROPERTY, K_WRAPPER,
    K_SUPER, K_DICTVIEW, K_FILE
};

/* C function callable from Python. argv holds argc positional values, then one
   value per name in kwnames (a tuple of str, or NULL). */
typedef Obj *(*CFunc)(int argc, Obj **argv, Obj *kwnames);

struct Type {
    Obj h; Str *name; Tuple *bases; Tuple *mro; Dict *dict;
    int kind; bool user, has_del; CFunc ctor;
    Dict *slots;   /* allowed attribute names when every class in the MRO defines __slots__ */
};

typedef struct { Obj h; int64_t v; uint32_t *d; uint32_t nd; bool neg; } Int; /* d != NULL: big */
typedef struct { Obj h; double v; } Float;
struct Str { Obj h; uint64_t hash; uint32_t len; uint32_t nchars; bool hashed; char s[]; };
struct List { Obj h; int64_t n, cap; Obj **items; };
struct Tuple { Obj h; int64_t n; Obj *items[]; };
typedef struct { Obj *key, *val; uint64_t hash; } DEntry;
struct Dict { Obj h; int64_t used, n, cap; DEntry *e; int32_t *idx; int64_t isize; };
typedef struct { Obj h; Dict *d; } Set;
typedef struct { Obj h; int64_t start, stop, step; } Range;
typedef struct { Obj h; Obj *start, *stop, *step; } Slice;
typedef struct { Obj h; Obj *value; } Cell;

enum { CO_VARARGS = 1, CO_VARKW = 2, CO_GENERATOR = 4, CO_CLASSBODY = 8 };
struct Code {
    Obj h; Str *name, *qualname, *filename; Obj *doc;
    uint32_t *code; int *lines; int ncode;
    Tuple *consts, *names, *varnames, *cellnames;
    int argcount, posonly, kwonly, flags, nlocals, ncells, nfree, stacksize, firstline, maxblocks;
    int *cell2arg;
};

typedef struct {
    Obj h; Code *code; Dict *globals; Tuple *defaults; Dict *kwdefaults; Tuple *closure;
    Str *name, *qualname; Obj *doc; Dict *dict; Dict *annotations;
} Func;

enum { BF_KW = 1, BF_METHOD = 2 };
typedef struct { Obj h; const char *name; CFunc fn; int minargs, maxargs, flags; Type *owner; } Builtin;
typedef struct { Obj h; Obj *func, *self; } Method;
typedef struct { Obj h; Dict *dict; } Instance;
typedef struct { Obj h; Dict *dict; Tuple *args; Obj *cause, *context; List *tb; } Exc;
typedef struct { Obj h; Str *name; Dict *dict; } Module;
typedef struct { Obj h; Obj *fget, *fset, *fdel, *doc; } Property;
typedef struct { Obj h; Obj *fn; } Wrapper;            /* staticmethod, classmethod */
typedef struct { Obj h; Type *cls; Obj *self; } Super;
typedef struct { Obj h; Dict *d; int kind; } DictView;  /* 0 keys, 1 values, 2 items */
enum { ENC_UTF8, ENC_ASCII, ENC_LATIN1 };
enum { ERR_STRICT, ERR_REPLACE, ERR_IGNORE, ERR_UNKNOWN };
typedef struct {
    Obj h; FILE *fp; Str *name; bool closed, readable, writable, std;
    uint8_t enc, errors;   /* text encoding and what to do with bad bytes */
    bool universal;        /* reading turns \r\n and \r into \n */
    const char *wnl;       /* what "\n" is written as, or NULL for no change */
} File;

enum { IT_LIST, IT_TUPLE, IT_STR, IT_RANGE, IT_DICT, IT_ENUM, IT_ZIP, IT_MAP, IT_FILTER,
       IT_REV, IT_SEQ };
typedef struct { Obj h; int kind, sub; Obj *a, *b; int64_t i, stop, step; } Iter;

#define MAXBLOCKS 20   /* deepest nesting of try/with blocks in one function */
enum { B_TRY, B_HANDLER };
typedef struct { int type, handler, level; Obj *prev; } Block;
enum { FS_NEW, FS_RUNNING, FS_SUSPENDED, FS_DONE };
struct Frame {
    Obj h; Code *code; Dict *globals; Dict *locals; Func *func; Frame *back;
    Obj **fast; Cell **cells; Obj **stack; Obj **sp;
    int pc, nblocks, state, nstack;
    Block *blocks;
    Obj *slots[];
};
typedef struct { Obj h; Frame *frame; Str *name; bool running; } Generator;

typedef struct {
    Obj *exc;        /* pending exception, or NULL */
    Obj *handled;    /* exception being handled by an except block */
    Frame *frame;    /* innermost running frame */
    int depth, recursion_limit, max_recursion;
    Dict *builtins, *modules;
    Obj *argv;
    volatile int interrupted;
    List *pending;        /* objects waiting for __del__ or generator cleanup */
    bool fin_possible;    /* some object may need finalizing; makes the collector look for them */
    bool finalizing;
    const char *script_dir;
} VM;
extern VM vm;

/* ---- singletons and types ---- */
extern Obj *Py_None, *Py_True, *Py_False, *Py_NotImplemented, *Py_Ellipsis;
#define TYPES(X) X(object) X(type) X(NoneType) X(NotImplementedType) X(ellipsis) X(bool) X(int) \
    X(float) X(str) X(list) X(tuple) X(dict) X(set) X(frozenset) X(range) X(slice) X(function) \
    X(builtin) X(method) X(module) X(code) X(frame) X(cell) X(generator) X(iterator) X(property) \
    X(staticmethod) X(classmethod) X(super) X(dict_keys) X(dict_values) X(dict_items) X(file)
#define X(n) extern Type *T_##n;
TYPES(X)
#undef X
#define EXCS(X) X(BaseException, 0) X(Exception, BaseException) X(ArithmeticError, Exception) \
    X(ZeroDivisionError, ArithmeticError) X(OverflowError, ArithmeticError) \
    X(LookupError, Exception) X(IndexError, LookupError) X(KeyError, LookupError) \
    X(ValueError, Exception) X(UnicodeError, ValueError) X(UnicodeDecodeError, UnicodeError) \
    X(UnicodeEncodeError, UnicodeError) X(TypeError, Exception) \
    X(NameError, Exception) X(UnboundLocalError, NameError) X(AttributeError, Exception) \
    X(RuntimeError, Exception) X(RecursionError, RuntimeError) X(NotImplementedError, RuntimeError) \
    X(StopIteration, Exception) X(AssertionError, Exception) X(ImportError, Exception) \
    X(ModuleNotFoundError, ImportError) X(OSError, Exception) X(FileNotFoundError, OSError) \
    X(PermissionError, OSError) X(IsADirectoryError, OSError) X(FileExistsError, OSError) \
    X(EOFError, Exception) X(MemoryError, Exception) \
    X(SyntaxError, Exception) X(IndentationError, SyntaxError) X(TypeCheckError, SyntaxError) \
    X(SystemExit, BaseException) \
    X(KeyboardInterrupt, BaseException) X(GeneratorExit, BaseException)
#define X(n, b) extern Type *E_##n;
EXCS(X)
#undef X

/* Names the runtime looks up often. */
#define NAMES(X) X(init) X(new) X(repr) X(str) X(eq) X(ne) X(lt) X(le) X(gt) X(ge) X(hash) \
    X(bool) X(len) X(getitem) X(setitem) X(delitem) X(contains) X(iter) X(next) X(call) \
    X(enter) X(exit) X(getattr) X(setattr) X(delattr) X(name) X(qualname) X(doc) X(class) \
    X(dict) X(module) X(main) X(neg) X(pos) X(invert) X(abs) X(int) X(float) X(index) \
    X(format) X(file) X(bases) X(mro) X(cause) X(context) X(missing) X(reversed) X(builtins) \
    X(traceback) X(self) X(func) X(defaults) X(wrapped) X(all) X(slots) X(round) X(set_name) \
    X(get) X(set) X(delete) X(annotations) X(del)
#define X(n) extern Str *N_##n;
NAMES(X)
#undef X
extern Str *N_args;

#define KIND(o) (((Obj *)(o))->type->kind)
#define IS(o, t) (((Obj *)(o))->type == T_##t)
#define IS_INT(o) (KIND(o) == K_INT || KIND(o) == K_BOOL)
#define IS_STR(o) (KIND(o) == K_STR)
#define AS_STR(o) ((Str *)(o))
#define BOOL(c) ((c) ? Py_True : Py_False)

/* Instances of a Python class that extends a built-in type (class Stack(list))
   use the built-in layout plus one trailing slot for the instance dict.
   overridable(o) is true for such an instance, unless a built-in method asked
   for the plain built-in behavior for this one call (raw_obj). */
extern Obj *raw_obj;
static inline bool overridable(Obj *o) {
    if (!o->type->user || o->type->kind == K_INSTANCE || o->type->kind == K_EXC) return false;
    if (o == raw_obj) { raw_obj = NULL; return false; }
    return true;
}

/* ---- gc.c ---- */
extern int gc_paused;
extern void *gc_stack_bottom;
void *gc_alloc(Type *t, size_t size);
void gc_collect(void);
void gc_find_all_finalizable(void);
void run_finalizers(void);
void gc_account(size_t bytes);   /* report memory held outside the object itself */
void gc_root(Obj **p);
void gc_init(void);
void fatal(const char *msg);

/* ---- string builder ---- */
typedef struct { char *s; size_t n, cap; } SB;
void sb_init(SB *b);
void sb_putc(SB *b, char c);
void sb_put(SB *b, const char *s, size_t n);
void sb_puts(SB *b, const char *s);
void sb_printf(SB *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void sb_putcp(SB *b, uint32_t cp);
void sb_putstr(SB *b, Obj *s);
Obj *sb_finish(SB *b);
void sb_free(SB *b);

/* ---- int.c ---- */
Obj *int_new(int64_t v);
Obj *int_from_u64(uint64_t v);
Obj *int_from_digits(const char *s, size_t n, int base);
Obj *int_from_double(double v);
bool int_fits(Obj *o, int64_t *out);
double int_to_double(Obj *o, bool *overflow);
int int_sign(Obj *o);
int int_cmp(Obj *a, Obj *b);
Obj *int_add(Obj *a, Obj *b);
Obj *int_sub(Obj *a, Obj *b);
Obj *int_mul(Obj *a, Obj *b);
int int_divmod(Obj *a, Obj *b, Obj **q, Obj **r);
Obj *int_pow(Obj *a, Obj *b);
Obj *int_neg(Obj *a);
Obj *int_lshift(Obj *a, Obj *b);
Obj *int_rshift(Obj *a, Obj *b);
Obj *int_bitop(int op, Obj *a, Obj *b);
void int_format(SB *b, Obj *o, int base, bool upper);
uint64_t int_hash(Obj *o);
int64_t int_bit_length(Obj *o);

/* ---- object.c ---- */
void objects_init(void);
Type *type_new(const char *name, Type *base, int kind);
Obj *err(Type *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
Obj *err_obj(Type *t, Obj *arg);
void err_set(Obj *exc);
bool err_matches(Type *t);
void err_clear(void);
Obj *exc_new(Type *t, Tuple *args);
bool type_issub(Type *t, Type *base);
bool isinstance(Obj *o, Type *t);
const char *tname(Obj *o);

Obj *float_new(double v);
void float_repr(SB *b, double v);
Obj *str_new(const char *s, size_t len);
Obj *str_c(const char *s);
Obj *str_fmt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
Obj *str_chr(uint32_t cp);
Obj *str_concat(Str *a, Str *b);
uint64_t str_hash(Str *s);
bool str_eq(Str *a, Str *b);
bool str_eqc(Str *a, const char *c);
size_t str_offset(Str *s, int64_t idx);
uint32_t utf8_next(const char **p);
bool cp_printable(uint32_t cp);
Obj *str_slice_chars(Str *s, int64_t start, int64_t stop);
Obj *list_new(int64_t n);
void list_append(List *l, Obj *o);
void list_insert(List *l, int64_t i, Obj *o);
Obj *tuple_new(int64_t n);
Obj *tuple_pack(int n, ...);
Obj *tuple_from(Obj **items, int64_t n);
Obj *list_from(Obj **items, int64_t n);
Obj *dict_new(void);
int dict_get(Dict *d, Obj *k, Obj **out);
int dict_set(Dict *d, Obj *k, Obj *v);
int dict_del(Dict *d, Obj *k);
Obj *dict_gets(Dict *d, Str *k);
void dict_sets(Dict *d, Str *k, Obj *v);
bool dict_dels(Dict *d, Str *k);
Obj *dict_getc(Dict *d, const char *k);
void dict_setc(Dict *d, const char *k, Obj *v);
bool dict_next(Dict *d, int64_t *pos, Obj **k, Obj **v);
void dict_clear(Dict *d);
Obj *dict_copy(Dict *d);
Obj *set_new(Type *t);
Obj *cell_new(Obj *v);
Obj *slice_new(Obj *a, Obj *b, Obj *c);
Obj *range_new(int64_t start, int64_t stop, int64_t step);
int64_t range_len(Range *r);
Obj *builtin_new(const char *name, CFunc fn, int minargs, int maxargs, int flags);
Obj *method_new(Obj *func, Obj *self);
Obj *module_new(const char *name);
Obj *iter_new(int kind, Obj *a, Obj *b);
int slice_indices(Obj *slice, int64_t len, int64_t *start, int64_t *stop, int64_t *step, int64_t *count);

Obj *obj_repr(Obj *o);
Obj *obj_str(Obj *o);
int obj_truthy(Obj *o);
int obj_eq(Obj *a, Obj *b);
Obj *obj_richcmp(Obj *a, Obj *b, int op);
int obj_cmp(Obj *a, Obj *b, int op);
int obj_hash(Obj *o, uint64_t *out);
Obj *obj_getattr(Obj *o, Str *name);
Obj *obj_getattr_opt(Obj *o, Str *name);
int obj_setattr(Obj *o, Str *name, Obj *v);
Obj *obj_getitem(Obj *o, Obj *k);
int obj_setitem(Obj *o, Obj *k, Obj *v);
int obj_delitem(Obj *o, Obj *k);
Obj *obj_iter(Obj *o);
Obj *iter_next(Obj *it);
int64_t obj_len(Obj *o);
int obj_contains(Obj *c, Obj *item);
int obj_index(Obj *o, int64_t *out);
int obj_as_double(Obj *o, double *out);
Obj *type_lookup(Type *t, Str *name);
Obj *seq_to_list(Obj *iterable);
Obj *seq_to_tuple(Obj *iterable);
int list_extend(List *l, Obj *iterable);
int list_sort(List *l, Obj *key, bool reverse);
int set_add(Set *s, Obj *k);
int set_update(Set *s, Obj *iterable);
int dict_update(Dict *d, Obj *other);
Type *class_new(Str *name, Tuple *bases, Dict *ns);
Dict *obj_dict(Obj *o, bool create);
Obj *subclass_instance(Type *t, Obj *base);
Obj *user_override(Obj *o, Str *name);
Type *builtin_base(Type *t);

enum { OP_ADD, OP_SUB, OP_MUL, OP_TRUEDIV, OP_FLOORDIV, OP_MOD, OP_POW, OP_LSHIFT, OP_RSHIFT,
       OP_AND, OP_OR, OP_XOR, OP_MATMUL, OP_NBINARY };
enum { CMP_LT, CMP_LE, CMP_EQ, CMP_NE, CMP_GT, CMP_GE };
Obj *binary_op(int op, Obj *a, Obj *b, bool inplace);
Obj *unary_op(int op, Obj *a);   /* '-', '+', '~' */

void ops_init(void);
int obj_setattr_direct(Obj *o, Str *name, Obj *v);
Obj *file_readline(Obj *f);

/* ---- methods.c ---- */
void methods_init(void);
Obj *str_percent(Str *fmt, Obj *args);
Obj *obj_format(Obj *o, Str *spec);
void type_add_method(Type *t, const char *name, CFunc fn, int minargs, int maxargs, int flags);
void type_add_static(Type *t, const char *name, CFunc fn, int minargs, int maxargs, int flags);
int kw_find(Obj *kwnames, const char *name);
int kw_check(Obj *kwnames, const char *fname, const char *allowed);

/* ---- vm.c ---- */
Obj *obj_call(Obj *f, int argc, Obj **argv, Obj *kwnames);
Obj *call0(Obj *f);
Obj *call1(Obj *f, Obj *a);
Obj *call2(Obj *f, Obj *a, Obj *b);
Obj *call_method(Obj *o, Str *name, int argc, Obj **argv, bool *found);
Obj *run_code(Code *code, Dict *globals, Dict *locals);
Obj *gen_send(Generator *g, Obj *val, Obj **retval);
Obj *gen_throw(Generator *g, Obj *exc, Obj **retval);
void print_exception(Obj *exc);
void register_source(const char *filename, const char *src);

/* ---- compile.c ---- */
enum { MODE_FILE, MODE_SINGLE, MODE_EVAL };
extern bool compile_incomplete;
Code *compile_source(const char *src, const char *filename, int mode);

/* ---- builtins.c / modules.c ---- */
void builtins_init(void);
Obj *import_module(Str *name);
Obj *match_keys(Obj *subject, Tuple *keys, bool rest);
Obj *match_class(Obj *subject, Obj *cls, int npos, Tuple *kwnames);
Obj *file_new(FILE *fp, const char *name, bool readable, bool writable, bool std);
int file_write_str(Obj *file, Str *s);
extern Dict *sys_dict;
extern Obj *sys_stdout_orig;

#endif
