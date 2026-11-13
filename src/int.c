/* Integers. Values that fit in 64 bits are stored directly. Larger values use
   sign + magnitude with base 2^32 digits, least significant digit first. */
#include "py.h"

typedef struct { const uint32_t *d; uint32_t n; bool neg; uint32_t buf[2]; } Mag;

#define NSMALL 262
static Obj *small_ints[NSMALL]; /* -5 .. 256 */

static Obj *int_alloc(int64_t v) {
    Int *o = gc_alloc(T_int, sizeof(Int));
    o->v = v;
    return (Obj *)o;
}

Obj *int_new(int64_t v) {
    if (v >= -5 && v <= 256) {
        Obj **slot = &small_ints[v + 5];
        if (!*slot) { *slot = int_alloc(v); gc_root(slot); }
        return *slot;
    }
    return int_alloc(v);
}

static void mag_of(Obj *o, Mag *m) {
    Int *i = (Int *)o;
    if (i->d) { m->d = i->d; m->n = i->nd; m->neg = i->neg; return; }
    uint64_t a = i->v < 0 ? (uint64_t)0 - (uint64_t)i->v : (uint64_t)i->v;
    m->buf[0] = (uint32_t)a;
    m->buf[1] = (uint32_t)(a >> 32);
    m->n = m->buf[1] ? 2 : (m->buf[0] ? 1 : 0);
    m->d = m->buf;
    m->neg = i->v < 0;
}

/* Takes ownership of d. */
static Obj *from_mag(uint32_t *d, uint32_t n, bool neg) {
    while (n > 0 && d[n - 1] == 0) n--;
    if (n <= 2) {
        uint64_t a = n == 0 ? 0 : n == 1 ? d[0] : ((uint64_t)d[1] << 32 | d[0]);
        if (!neg && a <= (uint64_t)INT64_MAX) { free(d); return int_new((int64_t)a); }
        if (neg && a <= (uint64_t)1 << 63) { free(d); return int_new((int64_t)((uint64_t)0 - a)); }
    }
    Int *o = (Int *)int_alloc(0);
    o->d = d; o->nd = n; o->neg = neg;
    gc_account((size_t)n * sizeof *d);
    return (Obj *)o;
}

Obj *int_from_u64(uint64_t v) {
    if (v <= (uint64_t)INT64_MAX) return int_new((int64_t)v);
    uint32_t *d = malloc(2 * sizeof *d);
    d[0] = (uint32_t)v; d[1] = (uint32_t)(v >> 32);
    return from_mag(d, 2, false);
}

bool int_fits(Obj *o, int64_t *out) {
    Int *i = (Int *)o;
    if (i->d) return false;
    *out = i->v;
    return true;
}

int int_sign(Obj *o) {
    Int *i = (Int *)o;
    if (i->d) return i->neg ? -1 : 1;
    return i->v < 0 ? -1 : i->v > 0;
}

static int mag_cmp(const uint32_t *a, uint32_t na, const uint32_t *b, uint32_t nb) {
    if (na != nb) return na < nb ? -1 : 1;
    for (uint32_t i = na; i-- > 0;)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

int int_cmp(Obj *a, Obj *b) {
    Int *x = (Int *)a, *y = (Int *)b;
    if (!x->d && !y->d) return x->v < y->v ? -1 : x->v > y->v;
    Mag m, n;
    mag_of(a, &m); mag_of(b, &n);
    if (m.neg != n.neg) return m.neg ? -1 : 1;
    int c = mag_cmp(m.d, m.n, n.d, n.n);
    return m.neg ? -c : c;
}

/* r must have room for max(na, nb) + 1 digits. Returns the digit count. */
static uint32_t mag_add(uint32_t *r, const uint32_t *a, uint32_t na, const uint32_t *b, uint32_t nb) {
    if (na < nb) { const uint32_t *t = a; a = b; b = t; uint32_t tn = na; na = nb; nb = tn; }
    uint64_t c = 0;
    uint32_t i;
    for (i = 0; i < nb; i++) { c += (uint64_t)a[i] + b[i]; r[i] = (uint32_t)c; c >>= 32; }
    for (; i < na; i++) { c += a[i]; r[i] = (uint32_t)c; c >>= 32; }
    r[i++] = (uint32_t)c;
    return i;
}

/* Requires |a| >= |b|. r needs na digits. */
static uint32_t mag_sub(uint32_t *r, const uint32_t *a, uint32_t na, const uint32_t *b, uint32_t nb) {
    int64_t c = 0;
    uint32_t i;
    for (i = 0; i < nb; i++) { c += (int64_t)a[i] - b[i]; r[i] = (uint32_t)c; c >>= 32; }
    for (; i < na; i++) { c += a[i]; r[i] = (uint32_t)c; c >>= 32; }
    return na;
}

static Obj *add_signed(Mag *m, Mag *n, bool nneg) {
    uint32_t *r = malloc((size_t)((m->n > n->n ? m->n : n->n) + 1) * sizeof *r);
    if (m->neg == nneg) return from_mag(r, mag_add(r, m->d, m->n, n->d, n->n), m->neg);
    int c = mag_cmp(m->d, m->n, n->d, n->n);
    if (c == 0) { free(r); return int_new(0); }
    if (c > 0) return from_mag(r, mag_sub(r, m->d, m->n, n->d, n->n), m->neg);
    return from_mag(r, mag_sub(r, n->d, n->n, m->d, m->n), nneg);
}

Obj *int_add(Obj *a, Obj *b) {
    Int *x = (Int *)a, *y = (Int *)b;
    int64_t r;
    if (!x->d && !y->d && !__builtin_add_overflow(x->v, y->v, &r)) return int_new(r);
    Mag m, n;
    mag_of(a, &m); mag_of(b, &n);
    return add_signed(&m, &n, n.neg);
}

Obj *int_sub(Obj *a, Obj *b) {
    Int *x = (Int *)a, *y = (Int *)b;
    int64_t r;
    if (!x->d && !y->d && !__builtin_sub_overflow(x->v, y->v, &r)) return int_new(r);
    Mag m, n;
    mag_of(a, &m); mag_of(b, &n);
    return add_signed(&m, &n, !n.neg);
}

Obj *int_neg(Obj *a) {
    Int *x = (Int *)a;
    if (!x->d && x->v != INT64_MIN) return int_new(-x->v);
    Mag m;
    mag_of(a, &m);
    uint32_t *r = malloc((size_t)(m.n + 1) * sizeof *r);
    memcpy(r, m.d, m.n * sizeof *r);
    return from_mag(r, m.n, !m.neg);
}

Obj *int_mul(Obj *a, Obj *b) {
    Int *x = (Int *)a, *y = (Int *)b;
    int64_t v;
    if (!x->d && !y->d && !__builtin_mul_overflow(x->v, y->v, &v)) return int_new(v);
    Mag m, n;
    mag_of(a, &m); mag_of(b, &n);
    if (m.n == 0 || n.n == 0) return int_new(0);
    uint32_t *r = calloc((size_t)m.n + n.n, sizeof *r);
    if (!r) fatal("out of memory");
    for (uint32_t i = 0; i < m.n; i++) {
        uint64_t c = 0, ai = m.d[i];
        if (!ai) continue;
        for (uint32_t j = 0; j < n.n; j++) {
            c += ai * n.d[j] + r[i + j];
            r[i + j] = (uint32_t)c;
            c >>= 32;
        }
        r[i + n.n] = (uint32_t)c;
    }
    return from_mag(r, m.n + n.n, m.neg != n.neg);
}

/* Knuth algorithm D. u has m digits, v has n digits, m >= n >= 1, v[n-1] != 0.
   q gets m-n+1 digits, r gets n digits. */
static void mag_divmod(const uint32_t *u, uint32_t m, const uint32_t *v, uint32_t n, uint32_t *q, uint32_t *r) {
    const uint64_t B = (uint64_t)1 << 32;
    if (n == 1) {
        uint64_t k = 0;
        for (uint32_t j = m; j-- > 0;) {
            uint64_t cur = (k << 32) | u[j];
            q[j] = (uint32_t)(cur / v[0]);
            k = cur % v[0];
        }
        r[0] = (uint32_t)k;
        return;
    }
    int s = __builtin_clz(v[n - 1]);
    uint32_t *vn = malloc(n * sizeof *vn), *un = malloc(((size_t)m + 1) * sizeof *un);
    for (uint32_t i = n - 1; i > 0; i--) vn[i] = s ? (v[i] << s) | (v[i - 1] >> (32 - s)) : v[i];
    vn[0] = v[0] << s;
    un[m] = s ? u[m - 1] >> (32 - s) : 0;
    for (uint32_t i = m - 1; i > 0; i--) un[i] = s ? (u[i] << s) | (u[i - 1] >> (32 - s)) : u[i];
    un[0] = u[0] << s;
    for (int64_t j = (int64_t)m - n; j >= 0; j--) {
        uint64_t num = ((uint64_t)un[j + n] << 32) | un[j + n - 1];
        uint64_t qhat = num / vn[n - 1], rhat = num % vn[n - 1];
        while (qhat >= B || qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {
            qhat--;
            rhat += vn[n - 1];
            if (rhat >= B) break;
        }
        int64_t k = 0, t;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t p = qhat * vn[i];
            t = (int64_t)un[i + j] - k - (int64_t)(p & 0xFFFFFFFFu);
            un[i + j] = (uint32_t)t;
            k = (int64_t)(p >> 32) - (t >> 32);
        }
        t = (int64_t)un[j + n] - k;
        un[j + n] = (uint32_t)t;
        q[j] = (uint32_t)qhat;
        if (t < 0) {
            q[j]--;
            uint64_t c = 0;
            for (uint32_t i = 0; i < n; i++) {
                c += (uint64_t)un[i + j] + vn[i];
                un[i + j] = (uint32_t)c;
                c >>= 32;
            }
            un[j + n] += (uint32_t)c;
        }
    }
    for (uint32_t i = 0; i < n; i++)
        r[i] = s ? (un[i] >> s) | (uint32_t)((uint64_t)un[i + 1] << (32 - s)) : un[i];
    free(vn); free(un);
}

/* Floor division and modulo. Returns -1 when b is zero (no exception is set). */
int int_divmod(Obj *a, Obj *b, Obj **qo, Obj **ro) {
    Int *x = (Int *)a, *y = (Int *)b;
    if (!y->d && y->v == 0) return -1;
    if (!x->d && !y->d && !(x->v == INT64_MIN && y->v == -1)) {
        int64_t q = x->v / y->v, r = x->v % y->v;
        if (r != 0 && ((r < 0) != (y->v < 0))) { q--; r += y->v; }
        if (qo) *qo = int_new(q);
        if (ro) *ro = int_new(r);
        return 0;
    }
    Mag m, n;
    mag_of(a, &m); mag_of(b, &n);
    Obj *q, *r;
    if (mag_cmp(m.d, m.n, n.d, n.n) < 0) {
        q = int_new(0);
        uint32_t *rd = malloc((size_t)(m.n + 1) * sizeof *rd);
        memcpy(rd, m.d, m.n * sizeof *rd);
        r = from_mag(rd, m.n, false);
    } else {
        uint32_t *qd = malloc((size_t)(m.n - n.n + 1) * sizeof *qd), *rd = malloc((size_t)n.n * sizeof *rd);
        mag_divmod(m.d, m.n, n.d, n.n, qd, rd);
        q = from_mag(qd, m.n - n.n + 1, false);
        r = from_mag(rd, n.n, false);
    }
    /* q and r are for |a| / |b|. Fix signs for floor semantics. */
    bool rzero = int_sign(r) == 0;
    Obj *babs = n.neg ? int_neg(b) : b;
    if (m.neg != n.neg) {
        q = int_neg(q);
        if (!rzero) { q = int_sub(q, int_new(1)); r = int_sub(babs, r); }
    }
    if (n.neg) r = int_neg(r);
    if (qo) *qo = q;
    if (ro) *ro = r;
    return 0;
}

Obj *int_pow(Obj *a, Obj *b) {
    int64_t e;
    if (!int_fits(b, &e)) return err(E_OverflowError, "exponent too large");
    Obj *result = int_new(1), *base = a;
    while (e > 0) {
        if (e & 1) result = int_mul(result, base);
        e >>= 1;
        if (e) base = int_mul(base, base);
    }
    return result;
}

Obj *int_lshift(Obj *a, Obj *b) {
    int64_t n;
    if (int_sign(b) < 0) return err(E_ValueError, "negative shift count");
    if (int_sign(a) == 0) return int_new(0);
    if (!int_fits(b, &n) || n > ((int64_t)1 << 34)) return err(E_OverflowError, "too many digits in integer");
    Int *x = (Int *)a;
    if (!x->d && n < 62) {
        int64_t v = x->v, lim = INT64_MAX >> n;
        if (v <= lim && v >= -lim) return int_new(v * ((int64_t)1 << n));
    }
    Mag m;
    mag_of(a, &m);
    uint32_t words = (uint32_t)(n / 32), bits = (uint32_t)(n % 32);
    uint32_t *r = calloc((size_t)m.n + words + 1, sizeof *r);
    if (!r) fatal("out of memory");
    uint64_t c = 0;
    for (uint32_t i = 0; i < m.n; i++) {
        c |= (uint64_t)m.d[i] << bits;
        r[i + words] = (uint32_t)c;
        c >>= 32;
    }
    r[m.n + words] = (uint32_t)c;
    return from_mag(r, m.n + words + 1, m.neg);
}

Obj *int_rshift(Obj *a, Obj *b) {
    int64_t n;
    if (int_sign(b) < 0) return err(E_ValueError, "negative shift count");
    Int *x = (Int *)a;
    if (!int_fits(b, &n)) return int_new(int_sign(a) < 0 ? -1 : 0);
    if (!x->d) return int_new(n >= 63 ? (x->v < 0 ? -1 : 0) : x->v >> n);
    Mag m;
    mag_of(a, &m);
    uint64_t words = (uint64_t)n / 32;
    uint32_t bits = (uint32_t)(n % 32);
    if (words >= m.n) return int_new(m.neg ? -1 : 0);
    bool lost = false;
    for (uint32_t i = 0; i < words; i++) if (m.d[i]) lost = true;
    if (bits && (m.d[words] & (((uint32_t)1 << bits) - 1))) lost = true;
    uint32_t rn = m.n - (uint32_t)words;
    uint32_t *r = calloc((size_t)rn + 1, sizeof *r);
    for (uint32_t i = 0; i < rn; i++) {
        uint64_t cur = m.d[i + words];
        if (i + words + 1 < m.n) cur |= (uint64_t)m.d[i + words + 1] << 32;
        r[i] = (uint32_t)(cur >> bits);
    }
    Obj *res = from_mag(r, rn, m.neg);
    if (m.neg && lost) res = int_sub(res, int_new(1));
    return res;
}

static void twos(uint32_t *out, uint32_t len, Mag *m) {
    for (uint32_t i = 0; i < len; i++) out[i] = i < m->n ? m->d[i] : 0;
    if (m->neg) {
        uint64_t c = 1;
        for (uint32_t i = 0; i < len; i++) { c += (uint32_t)~out[i]; out[i] = (uint32_t)c; c >>= 32; }
    }
}

Obj *int_bitop(int op, Obj *a, Obj *b) {
    Int *x = (Int *)a, *y = (Int *)b;
    if (!x->d && !y->d)
        return int_new(op == '&' ? x->v & y->v : op == '|' ? x->v | y->v : x->v ^ y->v);
    Mag m, n;
    mag_of(a, &m); mag_of(b, &n);
    uint32_t len = (m.n > n.n ? m.n : n.n) + 1;
    uint32_t *p = malloc(len * sizeof *p), *q = malloc(len * sizeof *q);
    twos(p, len, &m); twos(q, len, &n);
    for (uint32_t i = 0; i < len; i++)
        p[i] = op == '&' ? p[i] & q[i] : op == '|' ? p[i] | q[i] : p[i] ^ q[i];
    free(q);
    bool neg = (p[len - 1] >> 31) != 0;
    if (neg) {
        uint64_t c = 1;
        for (uint32_t i = 0; i < len; i++) { c += (uint32_t)~p[i]; p[i] = (uint32_t)c; c >>= 32; }
    }
    return from_mag(p, len, neg);
}

double int_to_double(Obj *o, bool *overflow) {
    Int *i = (Int *)o;
    if (overflow) *overflow = false;
    if (!i->d) return (double)i->v;
    double r = 0;
    /* The top three digits carry more bits than a double holds. */
    uint32_t lo = i->nd > 3 ? i->nd - 3 : 0;
    for (uint32_t k = i->nd; k-- > lo;) r = r * 4294967296.0 + i->d[k];
    r = ldexp(r, (int)(lo * 32 > 100000 ? 100000 : lo * 32));
    if (isinf(r) && overflow) *overflow = true;
    return i->neg ? -r : r;
}

Obj *int_from_double(double v) {
    v = trunc(v);
    if (v > -9.2e18 && v < 9.2e18) return int_new((int64_t)v);
    int e;
    double m = frexp(fabs(v), &e);
    Obj *r = int_lshift(int_new((int64_t)ldexp(m, 53)), int_new(e - 53));
    return v < 0 ? int_neg(r) : r;
}

void int_format(SB *b, Obj *o, int base, bool upper) {
    Int *i = (Int *)o;
    const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    Mag m;
    mag_of(o, &m);
    if (m.neg) sb_putc(b, '-');
    if (m.n == 0) { sb_putc(b, '0'); return; }
    if (!i->d && base == 10) {
        uint64_t a = (uint64_t)m.buf[1] << 32 | m.buf[0];
        sb_printf(b, "%llu", (unsigned long long)a);
        return;
    }
    if (base == 10) {
        uint32_t *t = malloc(m.n * sizeof *t), n = m.n;
        memcpy(t, m.d, n * sizeof *t);
        size_t cap = (size_t)n * 2 + 2, nch = 0;
        uint32_t *chunks = malloc(cap * sizeof *chunks);
        while (n > 0) {
            uint64_t k = 0;
            for (uint32_t j = n; j-- > 0;) {
                uint64_t cur = (k << 32) | t[j];
                t[j] = (uint32_t)(cur / 1000000000u);
                k = cur % 1000000000u;
            }
            chunks[nch++] = (uint32_t)k;
            while (n > 0 && t[n - 1] == 0) n--;
        }
        sb_printf(b, "%u", chunks[nch - 1]);
        for (size_t j = nch - 1; j-- > 0;) sb_printf(b, "%09u", chunks[j]);
        free(t); free(chunks);
        return;
    }
    int shift = base == 2 ? 1 : base == 8 ? 3 : 4;
    uint64_t nbits = (uint64_t)m.n * 32;
    while (nbits > 0 && !((m.d[(nbits - 1) / 32] >> ((nbits - 1) % 32)) & 1)) nbits--;
    uint64_t ndig = (nbits + (uint64_t)shift - 1) / (uint64_t)shift;
    for (uint64_t k = ndig; k-- > 0;) {
        uint64_t bit = k * (uint64_t)shift;
        uint32_t w = (uint32_t)(bit / 32), off = (uint32_t)(bit % 32);
        uint64_t cur = m.d[w];
        if (w + 1 < m.n) cur |= (uint64_t)m.d[w + 1] << 32;
        sb_putc(b, digs[(cur >> off) & (((uint32_t)1 << shift) - 1)]);
    }
}

static int digit_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/* s holds only digits valid for the base. */
Obj *int_from_digits(const char *s, size_t n, int base) {
    uint64_t acc = 0;
    size_t i = 0;
    for (; i < n; i++) {
        uint64_t next;
        if (__builtin_mul_overflow(acc, (uint64_t)base, &next) ||
            __builtin_add_overflow(next, (uint64_t)digit_val(s[i]), &next) || next > (uint64_t)INT64_MAX)
            break;
        acc = next;
    }
    if (i == n) return int_new((int64_t)acc);
    size_t cap = n / 6 + 4;
    uint32_t *d = calloc(cap, sizeof *d), len = 2;
    d[0] = (uint32_t)acc; d[1] = (uint32_t)(acc >> 32);
    for (; i < n; i++) {
        uint64_t c = (uint64_t)digit_val(s[i]);
        for (uint32_t j = 0; j < len; j++) {
            c += (uint64_t)d[j] * (uint64_t)base;
            d[j] = (uint32_t)c;
            c >>= 32;
        }
        if (c) {
            if (len + 1 >= cap) { cap *= 2; d = realloc(d, cap * sizeof *d); }
            d[len++] = (uint32_t)c;
        }
    }
    return from_mag(d, len, false);
}

uint64_t int_hash(Obj *o) {
    const uint64_t P = ((uint64_t)1 << 61) - 1;
    Mag m;
    mag_of(o, &m);
    uint64_t h = 0;
    for (uint32_t i = m.n; i-- > 0;)
        h = (uint64_t)((((unsigned __int128)h << 32) + m.d[i]) % P);
    int64_t r = m.neg ? -(int64_t)h : (int64_t)h;
    if (r == -1) r = -2;
    return (uint64_t)r;
}

int64_t int_bit_length(Obj *o) {
    Mag m;
    mag_of(o, &m);
    if (m.n == 0) return 0;
    return (int64_t)(m.n - 1) * 32 + (32 - __builtin_clz(m.d[m.n - 1]));
}
