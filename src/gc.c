/* Memory: allocation and a mark-and-sweep collector.
   Roots are the registered globals, the VM state, and every word on the C stack
   that points into a live object (the C stack is scanned conservatively). */
#include "py.h"
#include <setjmp.h>

VM vm;
int gc_paused;
void *gc_stack_bottom;

/* Small objects come from 64 KB pages. Each page holds slots of one size, so the
   object that contains any address is found with arithmetic. A free slot has a
   NULL type word. Larger objects are allocated one by one and kept in a list. */
#define PAGE_SIZE 65536u
#define PAGE_MASK (~(uintptr_t)(PAGE_SIZE - 1))
#define PAGE_HDR 16u
#define MAX_SMALL 512u
#define NCLASSES (MAX_SMALL / 16 + 1)
#define CHUNK_PAGES 16
typedef struct { uint32_t slot_size, nslots; } Page;
typedef struct FreeSlot { void *type; struct FreeSlot *next; } FreeSlot;

static FreeSlot *freelists[NCLASSES];
static Page **pages;
static size_t npages, cappages;
static uintptr_t *pageset;          /* open-addressing hash set of page addresses */
static size_t pageset_size, pageset_bits;
static uintptr_t lo_page = UINTPTR_MAX, hi_page;
static char *spare_pages;           /* unused pages left in the current chunk */
static int nspare;
static Obj **large;
static size_t nlarge, caplarge;
static size_t since_n, since_bytes;
static size_t thresh_n = 100000, thresh_bytes = 64u << 20;
static Obj ***roots;
static size_t nroots, caproots;
static Obj **gray;
static size_t ngray, capgray;
static int stress, stress_pages;
static bool collecting;

void fatal(const char *msg) {
    fflush(stdout);
    fprintf(stderr, "fatal: %s\n", msg);
    exit(2);
}

void gc_init(void) {
    /* PYC_GC_STRESS=n: collect every n allocations and use plain malloc for every
       object, so memory checkers can see use of a freed object. */
    const char *s = getenv("PYC_GC_STRESS");
    if (s) stress = atoi(s);
    /* PYC_GC_POISON=n: collect every n allocations with the normal page allocator
       and overwrite freed objects, so a use of a freed object fails loudly. */
    s = getenv("PYC_GC_POISON");
    if (s) stress_pages = atoi(s);
}

void gc_root(Obj **p) {
    if (nroots == caproots) {
        caproots = caproots ? caproots * 2 : 256;
        roots = realloc(roots, caproots * sizeof *roots);
    }
    roots[nroots++] = p;
}

static size_t pageset_slot(uintptr_t pg) {
    return (size_t)(((pg >> 16) * 0x9E3779B97F4A7C15ull) >> (64 - pageset_bits));
}

static void pageset_add(uintptr_t pg) {
    if ((npages + 1) * 2 > pageset_size) {
        pageset_bits = pageset_bits ? pageset_bits + 1 : 10;
        pageset_size = (size_t)1 << pageset_bits;
        free(pageset);
        pageset = calloc(pageset_size, sizeof *pageset);
        if (!pageset) fatal("out of memory");
        for (size_t i = 0; i < npages; i++) {
            size_t j = pageset_slot((uintptr_t)pages[i]);
            while (pageset[j]) j = (j + 1) & (pageset_size - 1);
            pageset[j] = (uintptr_t)pages[i];
        }
    }
    size_t j = pageset_slot(pg);
    while (pageset[j]) j = (j + 1) & (pageset_size - 1);
    pageset[j] = pg;
}

static void refill(unsigned cls) {
    if (!nspare) {
        spare_pages = aligned_alloc(PAGE_SIZE, (size_t)PAGE_SIZE * CHUNK_PAGES);
        if (!spare_pages) fatal("out of memory");
        nspare = CHUNK_PAGES;
    }
    Page *p = (Page *)spare_pages;
    spare_pages += PAGE_SIZE;
    nspare--;
    p->slot_size = cls * 16;
    p->nslots = (PAGE_SIZE - PAGE_HDR) / p->slot_size;
    pageset_add((uintptr_t)p);
    if (npages == cappages) {
        cappages = cappages ? cappages * 2 : 64;
        pages = realloc(pages, cappages * sizeof *pages);
        if (!pages) fatal("out of memory");
    }
    pages[npages++] = p;
    if ((uintptr_t)p < lo_page) lo_page = (uintptr_t)p;
    if ((uintptr_t)p > hi_page) hi_page = (uintptr_t)p;
    char *base = (char *)p + PAGE_HDR;
    for (uint32_t i = p->nslots; i-- > 0;) {
        FreeSlot *s = (FreeSlot *)(base + (size_t)i * p->slot_size);
        s->type = NULL;
        s->next = freelists[cls];
        freelists[cls] = s;
    }
}

void *gc_alloc(Type *t, size_t size) {
    if (!gc_paused && !collecting) {
        if (since_n >= thresh_n || since_bytes >= thresh_bytes) gc_collect();
        else if (stress && since_n >= (size_t)stress) gc_collect();
        else if (stress_pages && since_n >= (size_t)stress_pages) gc_collect();
    }
    if (size > UINT32_MAX) fatal("object too large");
    Obj *o;
    if (size <= MAX_SMALL && !stress) {
        unsigned cls = (unsigned)((size + 15) >> 4);
        if (!freelists[cls]) refill(cls);
        FreeSlot *s = freelists[cls];
        freelists[cls] = s->next;
        memset(s, 0, (size_t)cls << 4);
        o = (Obj *)s;
    } else {
        o = calloc(1, size);
        if (!o) fatal("out of memory");
        if (nlarge == caplarge) {
            caplarge = caplarge ? caplarge * 2 : 1024;
            large = realloc(large, caplarge * sizeof *large);
            if (!large) fatal("out of memory");
        }
        large[nlarge++] = o;
    }
    o->type = t;
    o->size = (uint32_t)size;
    since_n++;
    since_bytes += size;
    return o;
}

void gc_account(size_t bytes) { since_bytes += bytes; }

/* Bytes an object holds in separately allocated buffers. */
static size_t side_bytes(Obj *o) {
    switch (o->type->kind) {
    case K_LIST: return (size_t)((List *)o)->cap * sizeof(Obj *);
    case K_DICT: return (size_t)((Dict *)o)->cap * sizeof(DEntry) + (size_t)((Dict *)o)->isize * sizeof(int32_t);
    case K_INT: return (size_t)((Int *)o)->nd * sizeof(uint32_t);
    default: return 0;
    }
}

static void queue_finalizable(bool all);

static inline void mark(Obj *o) {
    if (o && !(o->mark & 1)) {
        o->mark |= 1;
        if (ngray == capgray) {
            capgray = capgray ? capgray * 2 : 1024;
            gray = realloc(gray, capgray * sizeof *gray);
            if (!gray) fatal("out of memory");
        }
        gray[ngray++] = o;
    }
}
#define M(x) mark((Obj *)(x))

static int cmp_ptr(const void *a, const void *b) {
    uintptr_t x = (uintptr_t) * (Obj *const *)a, y = (uintptr_t) * (Obj *const *)b;
    return x < y ? -1 : x > y;
}

/* Mark the object that contains address w, if there is one. */
static void mark_maybe(uintptr_t w) {
    uintptr_t pg = w & PAGE_MASK;
    if (pg >= lo_page && pg <= hi_page) {
        size_t j = pageset_slot(pg);
        while (pageset[j]) {
            if (pageset[j] == pg) {
                Page *p = (Page *)pg;
                uintptr_t off = w - pg;
                if (off < PAGE_HDR) return;
                size_t idx = (off - PAGE_HDR) / p->slot_size;
                if (idx >= p->nslots) return;
                Obj *o = (Obj *)(pg + PAGE_HDR + idx * p->slot_size);
                if (o->type) mark(o);
                return;
            }
            j = (j + 1) & (pageset_size - 1);
        }
    }
    if (!nlarge || w < (uintptr_t)large[0]) return;
    size_t lo = 0, hi = nlarge;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if ((uintptr_t)large[mid] <= w) lo = mid; else hi = mid;
    }
    Obj *o = large[lo];
    if (w < (uintptr_t)o + o->size) mark(o);
}

static void trace(Obj *o) {
    M(o->type);
    switch (o->type->kind) {
    case K_INSTANCE: M(((Instance *)o)->dict); break;
    case K_EXC: { Exc *e = (Exc *)o; M(e->dict); M(e->args); M(e->cause); M(e->context); M(e->tb); break; }
    case K_LIST: { List *l = (List *)o; for (int64_t i = 0; i < l->n; i++) M(l->items[i]); break; }
    case K_TUPLE: { Tuple *t = (Tuple *)o; for (int64_t i = 0; i < t->n; i++) M(t->items[i]); break; }
    case K_DICT: {
        Dict *d = (Dict *)o;
        for (int64_t i = 0; i < d->n; i++) { M(d->e[i].key); M(d->e[i].val); }
        break;
    }
    case K_SET: M(((Set *)o)->d); break;
    case K_SLICE: { Slice *s = (Slice *)o; M(s->start); M(s->stop); M(s->step); break; }
    case K_FUNC: {
        Func *f = (Func *)o;
        M(f->code); M(f->globals); M(f->defaults); M(f->kwdefaults); M(f->closure);
        M(f->name); M(f->qualname); M(f->doc); M(f->dict); M(f->annotations);
        break;
    }
    case K_METHOD: M(((Method *)o)->func); M(((Method *)o)->self); break;
    case K_BUILTIN: M(((Builtin *)o)->owner); break;
    case K_TYPE: { Type *t = (Type *)o; M(t->name); M(t->bases); M(t->mro); M(t->dict); M(t->slots); break; }
    case K_MODULE: M(((Module *)o)->name); M(((Module *)o)->dict); break;
    case K_CODE: {
        Code *c = (Code *)o;
        M(c->name); M(c->qualname); M(c->filename); M(c->doc); M(c->consts); M(c->names);
        M(c->varnames); M(c->cellnames);
        break;
    }
    case K_FRAME: {
        Frame *f = (Frame *)o;
        M(f->code); M(f->globals); M(f->locals); M(f->func); M(f->back);
        if (f->code) {
            for (int i = 0; i < f->code->nlocals; i++) M(f->fast[i]);
            for (int i = 0; i < f->code->ncells + f->code->nfree; i++) M(f->cells[i]);
        }
        for (int i = 0; i < f->nblocks; i++) M(f->blocks[i].prev);
        /* Slots above sp may hold stale pointers, so treat the stack like C memory. */
        for (int i = 0; i < f->nstack; i++) mark_maybe((uintptr_t)f->stack[i]);
        break;
    }
    case K_CELL: M(((Cell *)o)->value); break;
    case K_GEN: M(((Generator *)o)->frame); M(((Generator *)o)->name); break;
    case K_ITER: M(((Iter *)o)->a); M(((Iter *)o)->b); break;
    case K_PROPERTY: { Property *p = (Property *)o; M(p->fget); M(p->fset); M(p->fdel); M(p->doc); break; }
    case K_WRAPPER: M(((Wrapper *)o)->fn); break;
    case K_SUPER: M(((Super *)o)->cls); M(((Super *)o)->self); break;
    case K_DICTVIEW: M(((DictView *)o)->d); break;
    case K_FILE: M(((File *)o)->name); break;
    default: break;
    }
    int k = o->type->kind;
    if (o->type->user && k != K_INSTANCE && k != K_EXC) M(*(Dict **)((char *)o + o->size - sizeof(Dict *)));
}

static void finalize(Obj *o) {
    switch (o->type->kind) {
    case K_INT: free(((Int *)o)->d); break;
    case K_LIST: free(((List *)o)->items); break;
    case K_DICT: free(((Dict *)o)->e); free(((Dict *)o)->idx); break;
    case K_CODE: free(((Code *)o)->code); free(((Code *)o)->lines); free(((Code *)o)->cell2arg); break;
    case K_FILE: { File *f = (File *)o; if (f->fp && !f->closed && !f->std) fclose(f->fp); break; }
    default: break;
    }
}

/* True if o must run Python code before it can be freed. */
static bool needs_finalizer(Obj *o) {
    int k = o->type->kind;
    if (k == K_GEN) {
        Generator *g = (Generator *)o;
        return g->frame && g->frame->state == FS_SUSPENDED && g->frame->nblocks > 0 && !g->running;
    }
    return o->type->has_del && !(o->mark & 2);
}

static void consider(Obj *o, bool all) {
    if (!o->type || (!all && (o->mark & 1)) || !needs_finalizer(o)) return;
    list_append(vm.pending, o);
    mark(o);
}

static void queue_finalizable(bool all) {
    if (!vm.pending) return;
    for (size_t i = 0; i < npages; i++) {
        Page *p = pages[i];
        for (uint32_t k = 0; k < p->nslots; k++) consider((Obj *)((char *)p + PAGE_HDR + (size_t)k * p->slot_size), all);
    }
    for (size_t i = 0; i < nlarge; i++) consider(large[i], all);
}

/* At exit: queue every object that still has a finalizer to run. Frees nothing. */
void gc_find_all_finalizable(void) {
    if (!vm.fin_possible || collecting) return;
    collecting = true;
    queue_finalizable(true);
    ngray = 0;
    for (size_t i = 0; i < npages; i++) {
        Page *p = pages[i];
        for (uint32_t k = 0; k < p->nslots; k++) {
            Obj *o = (Obj *)((char *)p + PAGE_HDR + (size_t)k * p->slot_size);
            if (o->type) o->mark &= (uint8_t)~1;
        }
    }
    for (size_t i = 0; i < nlarge; i++) large[i]->mark &= (uint8_t)~1;
    collecting = false;
}

__attribute__((noinline, no_sanitize("address")))
static void scan_stack(void) {
    uintptr_t *p = (uintptr_t *)((uintptr_t)__builtin_frame_address(0) & ~(uintptr_t)7);
    uintptr_t *end = (uintptr_t *)gc_stack_bottom;
    for (; p < end; p++) mark_maybe(*p);
}

__attribute__((noinline))
void gc_collect(void) {
    if (collecting) return;
    collecting = true;
    jmp_buf jb;
    setjmp(jb);
    __builtin_unwind_init(); /* spill callee-saved registers so the scan sees them */
    qsort(large, nlarge, sizeof *large, cmp_ptr);
    for (size_t i = 0; i < nroots; i++) M(*roots[i]);
    M(vm.exc); M(vm.handled); M(vm.frame); M(vm.builtins); M(vm.modules); M(vm.argv); M(vm.pending);
    for (uintptr_t *p = (uintptr_t *)&jb; (char *)p < (char *)&jb + sizeof jb; p++) mark_maybe(*p);
    scan_stack();
    while (ngray) trace(gray[--ngray]);
    if (vm.fin_possible) {
        /* Unreachable objects that still need __del__ or generator cleanup are
           kept alive for one more cycle and queued; the VM runs them later. */
        queue_finalizable(false);
        while (ngray) trace(gray[--ngray]);
    }
    /* Sweep. finalize() reads the object's type. A type freed earlier in this
       same pass is still readable: freeing a slot only rewrites its first two
       words, and types are always small objects. */
    size_t live = 0, live_bytes = 0;
    if (stress_pages)   /* poisoning destroys freed types, so finalize everything first */
        for (size_t i = 0; i < npages; i++) {
            Page *p = pages[i];
            for (uint32_t k = 0; k < p->nslots; k++) {
                Obj *o = (Obj *)((char *)p + PAGE_HDR + (size_t)k * p->slot_size);
                if (o->type && !(o->mark & 1)) finalize(o);
            }
        }
    for (size_t i = 0; i < npages; i++) {
        Page *p = pages[i];
        unsigned cls = p->slot_size / 16;
        char *base = (char *)p + PAGE_HDR;
        for (uint32_t k = 0; k < p->nslots; k++) {
            Obj *o = (Obj *)(base + (size_t)k * p->slot_size);
            if (!o->type) continue;
            if (o->mark & 1) { o->mark &= (uint8_t)~1; live++; live_bytes += o->size + side_bytes(o); continue; }
            if (stress_pages) memset((char *)o + 16, 0xDB, p->slot_size - 16);
            else finalize(o);
            FreeSlot *s = (FreeSlot *)o;
            s->type = NULL;
            s->next = freelists[cls];
            freelists[cls] = s;
        }
    }
    size_t keep = 0;
    for (size_t i = 0; i < nlarge; i++)
        if (!(large[i]->mark & 1)) finalize(large[i]);
    for (size_t i = 0; i < nlarge; i++) {
        Obj *o = large[i];
        if (o->mark & 1) { o->mark &= (uint8_t)~1; live++; live_bytes += o->size + side_bytes(o); large[keep++] = o; }
        else free(o);
    }
    nlarge = keep;
    since_n = 0;
    since_bytes = 0;
    thresh_n = live > 100000 ? live : 100000;
    thresh_bytes = live_bytes > (64u << 20) ? live_bytes : (64u << 20);
    collecting = false;
}

/* ---- string builder ---- */
void sb_init(SB *b) { b->s = NULL; b->n = b->cap = 0; }
static void sb_grow(SB *b, size_t extra) {
    if (b->n + extra + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 64;
        while (cap < b->n + extra + 1) cap *= 2;
        b->s = realloc(b->s, cap);
        if (!b->s) fatal("out of memory");
        b->cap = cap;
    }
}
void sb_putc(SB *b, char c) { sb_grow(b, 1); b->s[b->n++] = c; }
void sb_put(SB *b, const char *s, size_t n) { sb_grow(b, n); if (n) memcpy(b->s + b->n, s, n); b->n += n; }
void sb_puts(SB *b, const char *s) { sb_put(b, s, strlen(s)); }
void sb_printf(SB *b, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    char probe[1];
    int n = vsnprintf(probe, 0, fmt, ap);
    va_end(ap);
    sb_grow(b, (size_t)n);
    vsnprintf(b->s + b->n, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->n += (size_t)n;
}
void sb_putcp(SB *b, uint32_t cp) {
    if (cp < 0x80) sb_putc(b, (char)cp);
    else if (cp < 0x800) { sb_putc(b, (char)(0xC0 | (cp >> 6))); sb_putc(b, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        sb_putc(b, (char)(0xE0 | (cp >> 12))); sb_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_putc(b, (char)(0x80 | (cp & 0x3F)));
    } else {
        sb_putc(b, (char)(0xF0 | (cp >> 18))); sb_putc(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        sb_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F))); sb_putc(b, (char)(0x80 | (cp & 0x3F)));
    }
}
void sb_putstr(SB *b, Obj *s) { sb_put(b, AS_STR(s)->s, AS_STR(s)->len); }
Obj *sb_finish(SB *b) {
    Obj *r = str_new(b->s ? b->s : "", b->n);
    free(b->s);
    b->s = NULL; b->n = b->cap = 0;
    return r;
}
void sb_free(SB *b) { free(b->s); b->s = NULL; b->n = b->cap = 0; }
