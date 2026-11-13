/* Built-in modules (math, sys, time, random) and the import system. */
#include "py.h"
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#define ARGS int argc, Obj **argv, Obj *kwnames
#define UNUSED (void)argc; (void)argv; (void)kwnames
#define KW(name) (kw_find(kwnames, name) >= 0 ? argv[argc + kw_find(kwnames, name)] : NULL)

extern const char *const pylib_names[];
extern const char *const pylib_sources[];

Dict *sys_dict;
Obj *sys_stdout_orig;

static void mdef(Obj *m, const char *name, CFunc fn, int lo, int hi, int flags) {
    dict_setc(((Module *)m)->dict, name, builtin_new(name, fn, lo, hi, flags));
}

/* ================= math ================= */
#define MATH1(name, expr, domain)                                         \
    static Obj *m_##name(ARGS) {                                          \
        UNUSED;                                                           \
        double x;                                                         \
        if (obj_as_double(argv[0], &x) < 0) return NULL;                  \
        if (!(domain)) return err(E_ValueError, "math domain error");     \
        double r = (expr);                                                \
        if (isinf(r) && isfinite(x)) return err(E_OverflowError, "math range error"); \
        return float_new(r);                                              \
    }
MATH1(sqrt, sqrt(x), x >= 0 || isnan(x))
MATH1(sin, sin(x), !isinf(x)) MATH1(cos, cos(x), !isinf(x)) MATH1(tan, tan(x), !isinf(x))
MATH1(asin, asin(x), (x >= -1 && x <= 1) || isnan(x)) MATH1(acos, acos(x), (x >= -1 && x <= 1) || isnan(x))
MATH1(atan, atan(x), true) MATH1(sinh, sinh(x), true) MATH1(cosh, cosh(x), true) MATH1(tanh, tanh(x), true)
MATH1(asinh, asinh(x), true) MATH1(acosh, acosh(x), x >= 1 || isnan(x)) MATH1(atanh, atanh(x), (x > -1 && x < 1) || isnan(x))
MATH1(exp, exp(x), true) MATH1(expm1, expm1(x), true) MATH1(log2, log2(x), x > 0 || isnan(x))
MATH1(log10, log10(x), x > 0 || isnan(x)) MATH1(log1p, log1p(x), x > -1 || isnan(x))
MATH1(fabs, fabs(x), true) MATH1(degrees, x * (180.0 / M_PI), true) MATH1(radians, x * (M_PI / 180.0), true)
MATH1(erf, erf(x), true) MATH1(erfc, erfc(x), true)
MATH1(gamma, tgamma(x), !(x <= 0 && x == floor(x)) && x != -INFINITY) MATH1(lgamma, lgamma(x), !(x <= 0 && x == floor(x)))
MATH1(cbrt, cbrt(x), true) MATH1(exp2, exp2(x), true)

static Obj *m_log(ARGS) {
    UNUSED;
    double x, b;
    if (IS_INT(argv[0]) && !int_fits(argv[0], &(int64_t){0}) && int_sign(argv[0]) > 0) {
        /* Big integer: log(m * 2^e) = log(m) + e*log(2). */
        int64_t bits = int_bit_length(argv[0]), shift = bits - 60;
        x = log(int_to_double(int_rshift(argv[0], int_new(shift)), NULL)) + (double)shift * M_LN2;
    } else {
        if (obj_as_double(argv[0], &x) < 0) return NULL;
        if (!(x > 0) && !isnan(x)) return err(E_ValueError, "math domain error");
        x = log(x);
    }
    if (argc > 1) {
        if (obj_as_double(argv[1], &b) < 0) return NULL;
        if (!(b > 0)) return err(E_ValueError, "math domain error");
        if (b == 1.0) return err(E_ZeroDivisionError, "float division by zero");
        x /= log(b);
    }
    return float_new(x);
}

static Obj *round_dir(Obj *o, double (*fn)(double), Str *dunder) {
    if (IS_INT(o)) return KIND(o) == K_BOOL ? int_new(o == Py_True) : o;
    if (o->type->user) { Obj *f = type_lookup(o->type, dunder); if (f) return call1(f, o); }
    double x;
    if (obj_as_double(o, &x) < 0) return NULL;
    if (isnan(x)) return err(E_ValueError, "cannot convert float NaN to integer");
    if (isinf(x)) return err(E_OverflowError, "cannot convert float infinity to integer");
    return int_from_double(fn(x));
}
static Obj *m_floor(ARGS) { UNUSED; return round_dir(argv[0], floor, AS_STR(str_c("__floor__"))); }
static Obj *m_ceil(ARGS) { UNUSED; return round_dir(argv[0], ceil, AS_STR(str_c("__ceil__"))); }
static Obj *m_trunc(ARGS) { UNUSED; return round_dir(argv[0], trunc, AS_STR(str_c("__trunc__"))); }

static int two_doubles(Obj **argv, double *x, double *y) {
    return (obj_as_double(argv[0], x) < 0 || obj_as_double(argv[1], y) < 0) ? -1 : 0;
}
static Obj *m_atan2(ARGS) { UNUSED; double x, y; return two_doubles(argv, &x, &y) < 0 ? NULL : float_new(atan2(x, y)); }
static Obj *m_copysign(ARGS) { UNUSED; double x, y; return two_doubles(argv, &x, &y) < 0 ? NULL : float_new(copysign(x, y)); }
static Obj *m_fmod(ARGS) {
    UNUSED;
    double x, y;
    if (two_doubles(argv, &x, &y) < 0) return NULL;
    if (y == 0 || isinf(x)) return err(E_ValueError, "math domain error");
    return float_new(fmod(x, y));
}
static Obj *m_pow(ARGS) {
    UNUSED;
    double x, y;
    if (two_doubles(argv, &x, &y) < 0) return NULL;
    if (x == 0 && y < 0) return err(E_ValueError, "math domain error");
    if (x < 0 && isfinite(y) && y != floor(y)) return err(E_ValueError, "math domain error");
    double r = pow(x, y);
    if (isinf(r) && isfinite(x) && isfinite(y)) return err(E_OverflowError, "math range error");
    return float_new(r);
}
static Obj *m_hypot(ARGS) {
    UNUSED;
    double s = 0, x;
    for (int i = 0; i < argc; i++) {
        if (obj_as_double(argv[i], &x) < 0) return NULL;
        s = hypot(s, x);
    }
    return float_new(s);
}
static Obj *m_dist(ARGS) {
    UNUSED;
    Tuple *a = (Tuple *)seq_to_tuple(argv[0]), *b = a ? (Tuple *)seq_to_tuple(argv[1]) : NULL;
    if (!b) return NULL;
    if (a->n != b->n) return err(E_ValueError, "both points must have the same number of dimensions");
    double s = 0, x, y;
    for (int64_t i = 0; i < a->n; i++) {
        if (obj_as_double(a->items[i], &x) < 0 || obj_as_double(b->items[i], &y) < 0) return NULL;
        s = hypot(s, x - y);
    }
    return float_new(s);
}
static Obj *m_isnan(ARGS) { UNUSED; double x; return obj_as_double(argv[0], &x) < 0 ? NULL : BOOL(isnan(x)); }
static Obj *m_isinf(ARGS) { UNUSED; double x; return obj_as_double(argv[0], &x) < 0 ? NULL : BOOL(isinf(x)); }
static Obj *m_isfinite(ARGS) { UNUSED; double x; return obj_as_double(argv[0], &x) < 0 ? NULL : BOOL(isfinite(x)); }
static Obj *m_isclose(ARGS) {
    if (kw_check(kwnames, "isclose", "rel_tol,abs_tol") < 0) return NULL;
    double a, b, rt = 1e-9, at = 0.0;
    if (two_doubles(argv, &a, &b) < 0) return NULL;
    if (KW("rel_tol") && obj_as_double(KW("rel_tol"), &rt) < 0) return NULL;
    if (KW("abs_tol") && obj_as_double(KW("abs_tol"), &at) < 0) return NULL;
    if (rt < 0 || at < 0) return err(E_ValueError, "tolerances must be non-negative");
    if (a == b) return Py_True;
    if (isinf(a) || isinf(b)) return Py_False;
    double d = fabs(b - a);
    return BOOL(d <= fabs(rt * b) || d <= fabs(rt * a) || d <= at);
}
static Obj *m_modf(ARGS) {
    UNUSED;
    double x, ip;
    if (obj_as_double(argv[0], &x) < 0) return NULL;
    double fp = modf(x, &ip);
    return tuple_pack(2, float_new(fp), float_new(ip));
}
static Obj *m_frexp(ARGS) {
    UNUSED;
    double x;
    int e;
    if (obj_as_double(argv[0], &x) < 0) return NULL;
    double m = frexp(x, &e);
    return tuple_pack(2, float_new(m), int_new(e));
}
static Obj *m_ldexp(ARGS) {
    UNUSED;
    double x;
    int64_t e;
    if (obj_as_double(argv[0], &x) < 0 || obj_index(argv[1], &e) < 0) return NULL;
    double r = ldexp(x, e > 100000 ? 100000 : e < -100000 ? -100000 : (int)e);
    if (isinf(r) && isfinite(x)) return err(E_OverflowError, "math range error");
    return float_new(r);
}
static Obj *m_fsum(ARGS) {
    UNUSED;
    Obj *it = obj_iter(argv[0]), *v;
    if (!it) return NULL;
    /* Neumaier compensated summation. */
    double sum = 0, comp = 0, x;
    while ((v = iter_next(it))) {
        if (obj_as_double(v, &x) < 0) return NULL;
        double t = sum + x;
        comp += fabs(sum) >= fabs(x) ? (sum - t) + x : (x - t) + sum;
        sum = t;
    }
    return vm.exc ? NULL : float_new(sum + comp);
}
static Obj *m_prod(ARGS) {
    if (kw_check(kwnames, "prod", "start") < 0) return NULL;
    Obj *acc = KW("start") ? KW("start") : int_new(1), *it = obj_iter(argv[0]), *v;
    if (!it) return NULL;
    while ((v = iter_next(it))) { acc = binary_op(OP_MUL, acc, v, false); if (!acc) return NULL; }
    return vm.exc ? NULL : acc;
}

static Obj *need_int(Obj *o) {
    if (KIND(o) == K_BOOL) return int_new(o == Py_True);
    if (KIND(o) == K_INT) return o;
    return err(E_TypeError, "'%s' object cannot be interpreted as an integer", tname(o));
}
static Obj *int_gcd(Obj *a, Obj *b) {
    if (int_sign(a) < 0) a = int_neg(a);
    if (int_sign(b) < 0) b = int_neg(b);
    while (int_sign(b) != 0) {
        Obj *r;
        int_divmod(a, b, NULL, &r);
        a = b; b = r;
    }
    return a;
}
static Obj *m_gcd(ARGS) {
    UNUSED;
    Obj *g = int_new(0);
    for (int i = 0; i < argc; i++) {
        Obj *x = need_int(argv[i]);
        if (!x) return NULL;
        g = int_gcd(g, x);
    }
    return g;
}
static Obj *m_lcm(ARGS) {
    UNUSED;
    Obj *l = int_new(1);
    for (int i = 0; i < argc; i++) {
        Obj *x = need_int(argv[i]), *q;
        if (!x) return NULL;
        if (int_sign(x) == 0) return int_new(0);
        if (int_sign(x) < 0) x = int_neg(x);
        int_divmod(l, int_gcd(l, x), &q, NULL);
        l = int_mul(q, x);
    }
    return l;
}
static Obj *m_factorial(ARGS) {
    UNUSED;
    Obj *x = need_int(argv[0]);
    int64_t n;
    if (!x) return NULL;
    if (int_sign(x) < 0) return err(E_ValueError, "factorial() not defined for negative values");
    if (!int_fits(x, &n) || n > 1000000) return err(E_OverflowError, "factorial() argument should not exceed 1000000");
    Obj *r = int_new(1);
    for (int64_t i = 2; i <= n; i++) r = int_mul(r, int_new(i));
    return r;
}
static Obj *comb_perm(Obj **argv, int argc, bool comb) {
    Obj *no = need_int(argv[0]), *ko = argc > 1 && argv[1] != Py_None ? need_int(argv[1]) : no;
    int64_t k;
    if (!no || !ko) return NULL;
    if (int_sign(no) < 0) return err(E_ValueError, "n must be a non-negative integer");
    if (int_sign(ko) < 0) return err(E_ValueError, "k must be a non-negative integer");
    if (int_cmp(ko, no) > 0) return int_new(0);
    if (comb) {
        Obj *alt = int_sub(no, ko);
        if (int_cmp(alt, ko) < 0) ko = alt;
    }
    if (!int_fits(ko, &k) || k > 10000000) return err(E_OverflowError, "k is too large");
    Obj *r = int_new(1);
    for (int64_t i = 0; i < k; i++) {
        r = int_mul(r, int_sub(no, int_new(i)));
        if (comb) int_divmod(r, int_new(i + 1), &r, NULL);
    }
    return r;
}
static Obj *m_comb(ARGS) { (void)kwnames; return comb_perm(argv, argc, true); }
static Obj *m_perm(ARGS) { (void)kwnames; return comb_perm(argv, argc, false); }
static Obj *m_isqrt(ARGS) {
    UNUSED;
    Obj *n = need_int(argv[0]);
    if (!n) return NULL;
    if (int_sign(n) < 0) return err(E_ValueError, "isqrt() argument must be nonnegative");
    if (int_sign(n) == 0) return n;
    /* Newton iteration from an upper bound. */
    Obj *x = int_lshift(int_new(1), int_new((int_bit_length(n) + 1) / 2)), *q;
    for (;;) {
        int_divmod(n, x, &q, NULL);
        Obj *y = int_rshift(int_add(x, q), int_new(1));
        if (int_cmp(y, x) >= 0) return x;
        x = y;
    }
}

static Obj *make_math(void) {
    Obj *m = module_new("math");
    Dict *d = ((Module *)m)->dict;
    dict_setc(d, "pi", float_new(M_PI));
    dict_setc(d, "e", float_new(M_E));
    dict_setc(d, "tau", float_new(2 * M_PI));
    dict_setc(d, "inf", float_new(INFINITY));
    dict_setc(d, "nan", float_new(NAN));
#define D1(n) mdef(m, #n, m_##n, 1, 1, 0);
    D1(sqrt) D1(sin) D1(cos) D1(tan) D1(asin) D1(acos) D1(atan) D1(sinh) D1(cosh) D1(tanh) D1(asinh) D1(acosh)
    D1(atanh) D1(exp) D1(expm1) D1(log2) D1(log10) D1(log1p) D1(fabs) D1(degrees) D1(radians) D1(erf) D1(erfc)
    D1(gamma) D1(lgamma) D1(cbrt) D1(exp2) D1(floor) D1(ceil) D1(trunc) D1(isnan) D1(isinf) D1(isfinite) D1(modf)
    D1(frexp) D1(fsum) D1(factorial) D1(isqrt)
#undef D1
    mdef(m, "log", m_log, 1, 2, 0); mdef(m, "atan2", m_atan2, 2, 2, 0); mdef(m, "copysign", m_copysign, 2, 2, 0);
    mdef(m, "fmod", m_fmod, 2, 2, 0); mdef(m, "pow", m_pow, 2, 2, 0); mdef(m, "hypot", m_hypot, 0, -1, 0);
    mdef(m, "dist", m_dist, 2, 2, 0); mdef(m, "isclose", m_isclose, 2, 2, BF_KW); mdef(m, "ldexp", m_ldexp, 2, 2, 0);
    mdef(m, "prod", m_prod, 1, 1, BF_KW); mdef(m, "gcd", m_gcd, 0, -1, 0); mdef(m, "lcm", m_lcm, 0, -1, 0);
    mdef(m, "comb", m_comb, 2, 2, 0); mdef(m, "perm", m_perm, 1, 2, 0);
    return m;
}

/* ================= time ================= */
static double clock_seconds(clockid_t id) {
    struct timespec ts;
    clock_gettime(id, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
static Obj *tm_time(ARGS) { UNUSED; return float_new(clock_seconds(CLOCK_REALTIME)); }
static Obj *tm_monotonic(ARGS) { UNUSED; return float_new(clock_seconds(CLOCK_MONOTONIC)); }
static Obj *tm_process_time(ARGS) { UNUSED; return float_new(clock_seconds(CLOCK_PROCESS_CPUTIME_ID)); }
static Obj *tm_time_ns(ARGS) {
    UNUSED;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return int_new((int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec);
}
static Obj *tm_sleep(ARGS) {
    UNUSED;
    double s;
    if (obj_as_double(argv[0], &s) < 0) return NULL;
    if (s < 0) return err(E_ValueError, "sleep length must be non-negative");
    fflush(stdout);
    struct timespec ts = {(time_t)s, (long)((s - floor(s)) * 1e9)};
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
        if (vm.interrupted) { vm.interrupted = 0; err_set(exc_new(E_KeyboardInterrupt, (Tuple *)tuple_new(0))); return NULL; }
    }
    return Py_None;
}
static Obj *make_time(void) {
    Obj *m = module_new("time");
    mdef(m, "time", tm_time, 0, 0, 0); mdef(m, "monotonic", tm_monotonic, 0, 0, 0);
    mdef(m, "perf_counter", tm_monotonic, 0, 0, 0); mdef(m, "process_time", tm_process_time, 0, 0, 0);
    mdef(m, "time_ns", tm_time_ns, 0, 0, 0); mdef(m, "sleep", tm_sleep, 1, 1, 0);
    return m;
}

/* ================= random (Mersenne Twister, same stream as CPython) ================= */
static uint32_t mt[624];
static int mti = 625;
static bool gauss_has;
static double gauss_next;

static void mt_init(uint32_t s) {
    mt[0] = s;
    for (mti = 1; mti < 624; mti++) mt[mti] = 1812433253u * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + (uint32_t)mti;
}
static void mt_init_by_array(const uint32_t *key, size_t len) {
    mt_init(19650218u);
    size_t i = 1, j = 0, k = 624 > len ? 624 : len;
    for (; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525u)) + key[j] + (uint32_t)j;
        i++; j++;
        if (i >= 624) { mt[0] = mt[623]; i = 1; }
        if (j >= len) j = 0;
    }
    for (k = 623; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941u)) - (uint32_t)i;
        i++;
        if (i >= 624) { mt[0] = mt[623]; i = 1; }
    }
    mt[0] = 0x80000000u;
}
static uint32_t mt_next(void) {
    static const uint32_t mag[2] = {0, 0x9908b0dfu};
    if (mti >= 624) {
        if (mti == 625) {
            uint32_t key[2] = {(uint32_t)time(NULL), (uint32_t)getpid()};
            FILE *f = fopen("/dev/urandom", "rb");
            if (f) { if (fread(key, sizeof key, 1, f) != 1) key[0] ^= 0x5bd1e995u; fclose(f); }
            mt_init_by_array(key, 2);
        }
        int kk;
        uint32_t y;
        for (kk = 0; kk < 624 - 397; kk++) { y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu); mt[kk] = mt[kk + 397] ^ (y >> 1) ^ mag[y & 1]; }
        for (; kk < 623; kk++) { y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu); mt[kk] = mt[kk + (397 - 624)] ^ (y >> 1) ^ mag[y & 1]; }
        y = (mt[623] & 0x80000000u) | (mt[0] & 0x7fffffffu);
        mt[623] = mt[396] ^ (y >> 1) ^ mag[y & 1];
        mti = 0;
    }
    uint32_t y = mt[mti++];
    y ^= y >> 11; y ^= (y << 7) & 0x9d2c5680u; y ^= (y << 15) & 0xefc60000u; y ^= y >> 18;
    return y;
}
static double rnd_double(void) {
    uint32_t a = mt_next() >> 5, b = mt_next() >> 6;
    return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}
static Obj *rnd_bits(int64_t k) {
    if (k <= 32) return int_new(k ? (int64_t)(mt_next() >> (32 - k)) : 0);
    Obj *r = int_new(0);
    for (int64_t shift = 0; k > 0; shift += 32, k -= 32) {
        uint32_t w = mt_next();
        if (k < 32) w >>= (32 - k);
        r = int_bitop('|', r, int_lshift(int_new(w), int_new(shift)));
    }
    return r;
}
static Obj *rnd_below(Obj *n) {
    int64_t k = int_bit_length(n);
    Obj *r = rnd_bits(k);
    while (int_cmp(r, n) >= 0) r = rnd_bits(k);
    return r;
}
static int64_t rnd_below64(int64_t n) {
    int64_t v = 0;
    int_fits(rnd_below(int_new(n)), &v);
    return v;
}

static Obj *r_seed(ARGS) {
    UNUSED;
    gauss_has = false;
    if (argc == 0 || argv[0] == Py_None) { mti = 625; return Py_None; }
    Obj *a = argv[0];
    if (KIND(a) == K_FLOAT && ((Float *)a)->v == floor(((Float *)a)->v) && isfinite(((Float *)a)->v)) a = int_from_double(((Float *)a)->v);
    if (!IS_INT(a)) return err(E_TypeError, "random.seed() supports int seeds only (got %s)", tname(a));
    if (int_sign(a) < 0) a = int_neg(a);
    int64_t bits = int_bit_length(a), words = bits ? (bits - 1) / 32 + 1 : 1;
    uint32_t *key = calloc((size_t)words, sizeof *key);
    Obj *mask = int_new(0xFFFFFFFFll);
    for (int64_t i = 0; i < words; i++) {
        int64_t w = 0;
        int_fits(int_bitop('&', int_rshift(a, int_new(i * 32)), mask), &w);
        key[i] = (uint32_t)w;
    }
    mt_init_by_array(key, (size_t)words);
    free(key);
    return Py_None;
}
static Obj *r_random(ARGS) { UNUSED; return float_new(rnd_double()); }
static Obj *r_getrandbits(ARGS) {
    UNUSED;
    int64_t k;
    if (obj_index(argv[0], &k) < 0) return NULL;
    if (k < 0) return err(E_ValueError, "number of bits must be non-negative");
    return rnd_bits(k);
}
static Obj *r_randrange(ARGS) {
    UNUSED;
    Obj *start = need_int(argv[0]), *stop = argc > 1 ? need_int(argv[1]) : NULL, *step = argc > 2 ? need_int(argv[2]) : int_new(1);
    if (!start || (argc > 1 && !stop) || !step) return NULL;
    if (!stop) {
        if (int_sign(start) <= 0) return err(E_ValueError, "empty range for randrange()");
        return rnd_below(start);
    }
    Obj *width = int_sub(stop, start);
    if (int_cmp(step, int_new(1)) == 0) {
        if (int_sign(width) <= 0) return err(E_ValueError, "empty range in randrange(%s, %s)", AS_STR(obj_repr(start))->s, AS_STR(obj_repr(stop))->s);
        return int_add(start, rnd_below(width));
    }
    Obj *n;
    if (int_sign(step) > 0) int_divmod(int_sub(int_add(width, step), int_new(1)), step, &n, NULL);
    else if (int_sign(step) < 0) int_divmod(int_add(int_add(width, step), int_new(1)), step, &n, NULL);
    else return err(E_ValueError, "zero step for randrange()");
    if (int_sign(n) <= 0) return err(E_ValueError, "empty range for randrange()");
    return int_add(start, int_mul(step, rnd_below(n)));
}
static Obj *r_randint(ARGS) {
    UNUSED;
    Obj *a = need_int(argv[0]), *b = a ? need_int(argv[1]) : NULL;
    if (!b) return NULL;
    Obj *width = int_add(int_sub(b, a), int_new(1));
    if (int_sign(width) <= 0) return err(E_ValueError, "empty range in randrange(%s, %s)", AS_STR(obj_repr(a))->s, AS_STR(obj_repr(int_add(b, int_new(1))))->s);
    return int_add(a, rnd_below(width));
}
static Obj *r_choice(ARGS) {
    UNUSED;
    int64_t n = obj_len(argv[0]);
    if (n < 0) return NULL;
    if (n == 0) return err(E_IndexError, "Cannot choose from an empty sequence");
    return obj_getitem(argv[0], int_new(rnd_below64(n)));
}
static Obj *r_shuffle(ARGS) {
    UNUSED;
    if (KIND(argv[0]) != K_LIST) return err(E_TypeError, "shuffle() needs a list");
    List *l = (List *)argv[0];
    for (int64_t i = l->n - 1; i >= 1; i--) {
        int64_t j = rnd_below64(i + 1);
        Obj *t = l->items[i]; l->items[i] = l->items[j]; l->items[j] = t;
    }
    return Py_None;
}
static Obj *r_uniform(ARGS) {
    UNUSED;
    double a, b;
    if (two_doubles(argv, &a, &b) < 0) return NULL;
    return float_new(a + (b - a) * rnd_double());
}
static Obj *r_sample(ARGS) {
    if (kw_check(kwnames, "sample", "k") < 0) return NULL;
    Obj *ko = argc > 1 ? argv[1] : KW("k");
    int64_t k;
    if (!ko) return err(E_TypeError, "sample() missing required argument 'k'");
    if (KIND(argv[0]) == K_SET || KIND(argv[0]) == K_DICT) return err(E_TypeError, "Population must be a sequence.  For dicts or sets, use sorted(d).");
    List *pop = (List *)seq_to_list(argv[0]);
    if (!pop || obj_index(ko, &k) < 0) return NULL;
    int64_t n = pop->n;
    if (k < 0 || k > n) return err(E_ValueError, "Sample larger than population or is negative");
    List *result = (List *)list_new(k);
    int64_t setsize = 21;
    if (k > 5) setsize += (int64_t)pow(4, ceil(log((double)k * 3) / log(4)));
    if (n <= setsize) {
        for (int64_t i = 0; i < k; i++) {
            int64_t j = rnd_below64(n - i);
            result->items[i] = pop->items[j];
            pop->items[j] = pop->items[n - i - 1];
        }
    } else {
        Set *sel = (Set *)set_new(T_set);
        Obj *tmp;
        for (int64_t i = 0; i < k; i++) {
            Obj *j = int_new(rnd_below64(n));
            while (dict_get(sel->d, j, &tmp) == 1) j = int_new(rnd_below64(n));
            set_add(sel, j);
            int64_t ji = 0;
            int_fits(j, &ji);
            result->items[i] = pop->items[ji];
        }
    }
    return (Obj *)result;
}
static Obj *r_choices(ARGS) {
    if (kw_check(kwnames, "choices", "weights,cum_weights,k") < 0) return NULL;
    List *pop = (List *)seq_to_list(argv[0]);
    Obj *wo = argc > 1 ? argv[1] : KW("weights"), *co = KW("cum_weights"), *ko = KW("k");
    int64_t k = 1;
    if (!pop || (ko && obj_index(ko, &k) < 0)) return NULL;
    if (wo == Py_None) wo = NULL;
    if (pop->n == 0) return err(E_IndexError, "Cannot choose from an empty sequence");
    List *result = (List *)list_new(0);
    if (!wo && !co) {
        for (int64_t i = 0; i < k; i++) list_append(result, pop->items[(int64_t)floor(rnd_double() * (double)pop->n)]);
        return (Obj *)result;
    }
    List *w = (List *)seq_to_list(wo ? wo : co);
    if (!w) return NULL;
    if (w->n != pop->n) return err(E_ValueError, "The number of weights does not match the population");
    double *cum = malloc((size_t)w->n * sizeof *cum), acc = 0, x;
    for (int64_t i = 0; i < w->n; i++) {
        if (obj_as_double(w->items[i], &x) < 0) { free(cum); return NULL; }
        cum[i] = wo ? (acc += x) : x;
    }
    double total = cum[w->n - 1];
    if (total <= 0) { free(cum); return err(E_ValueError, "Total of weights must be greater than zero"); }
    for (int64_t i = 0; i < k; i++) {
        double r = rnd_double() * total;
        int64_t lo = 0, hi = w->n - 1;
        while (lo < hi) { int64_t mid = (lo + hi) / 2; if (r < cum[mid]) hi = mid; else lo = mid + 1; }
        list_append(result, pop->items[lo]);
    }
    free(cum);
    return (Obj *)result;
}
static int mu_sigma(int argc, Obj **argv, double *mu, double *sigma) {
    *mu = 0; *sigma = 1;
    if (argc > 0 && obj_as_double(argv[0], mu) < 0) return -1;
    if (argc > 1 && obj_as_double(argv[1], sigma) < 0) return -1;
    return 0;
}
static Obj *r_gauss(ARGS) {
    UNUSED;
    double mu, sigma, z;
    if (mu_sigma(argc, argv, &mu, &sigma) < 0) return NULL;
    if (gauss_has) { z = gauss_next; gauss_has = false; }
    else {
        double x2pi = rnd_double() * 2 * M_PI, g2rad = sqrt(-2.0 * log(1.0 - rnd_double()));
        z = cos(x2pi) * g2rad;
        gauss_next = sin(x2pi) * g2rad;
        gauss_has = true;
    }
    return float_new(mu + z * sigma);
}
static Obj *r_normalvariate(ARGS) {
    UNUSED;
    double mu, sigma, z;
    if (mu_sigma(argc, argv, &mu, &sigma) < 0) return NULL;
    const double magic = 4 * exp(-0.5) / sqrt(2.0);
    for (;;) {
        double u1 = rnd_double(), u2 = 1.0 - rnd_double();
        z = magic * (u1 - 0.5) / u2;
        if (z * z / 4.0 <= -log(u2)) break;
    }
    return float_new(mu + z * sigma);
}
static Obj *r_expovariate(ARGS) {
    UNUSED;
    double l = 1.0;
    if (argc > 0 && obj_as_double(argv[0], &l) < 0) return NULL;
    if (l == 0) return err(E_ZeroDivisionError, "float division by zero");
    return float_new(-log(1.0 - rnd_double()) / l);
}
static Obj *make_random(void) {
    Obj *m = module_new("random");
    mdef(m, "seed", r_seed, 0, 1, 0); mdef(m, "random", r_random, 0, 0, 0); mdef(m, "getrandbits", r_getrandbits, 1, 1, 0);
    mdef(m, "randrange", r_randrange, 1, 3, 0); mdef(m, "randint", r_randint, 2, 2, 0); mdef(m, "choice", r_choice, 1, 1, 0);
    mdef(m, "shuffle", r_shuffle, 1, 1, 0); mdef(m, "uniform", r_uniform, 2, 2, 0); mdef(m, "sample", r_sample, 1, 2, BF_KW);
    mdef(m, "choices", r_choices, 1, 2, BF_KW); mdef(m, "gauss", r_gauss, 0, 2, 0);
    mdef(m, "normalvariate", r_normalvariate, 0, 2, 0); mdef(m, "expovariate", r_expovariate, 0, 1, 0);
    return m;
}

/* ================= gc ================= */
static bool gc_user_disabled;
static Obj *gcm_collect(ARGS) { UNUSED; gc_collect(); run_finalizers(); return vm.exc ? NULL : int_new(0); }
static Obj *gcm_disable(ARGS) { UNUSED; if (!gc_user_disabled) { gc_user_disabled = true; gc_paused++; } return Py_None; }
static Obj *gcm_enable(ARGS) { UNUSED; if (gc_user_disabled) { gc_user_disabled = false; gc_paused--; } return Py_None; }
static Obj *gcm_isenabled(ARGS) { UNUSED; return BOOL(!gc_user_disabled); }
static Obj *make_gc(void) {
    Obj *m = module_new("gc");
    mdef(m, "collect", gcm_collect, 0, 1, 0); mdef(m, "disable", gcm_disable, 0, 0, 0);
    mdef(m, "enable", gcm_enable, 0, 0, 0); mdef(m, "isenabled", gcm_isenabled, 0, 0, 0);
    return m;
}

/* ================= sys ================= */
static Obj *sys_exit(ARGS) {
    UNUSED;
    err_set(exc_new(E_SystemExit, (Tuple *)tuple_from(argv, argc)));
    return NULL;
}
static Obj *sys_getrecursionlimit(ARGS) { UNUSED; return int_new(vm.recursion_limit); }
static Obj *sys_setrecursionlimit(ARGS) {
    UNUSED;
    int64_t n;
    if (obj_index(argv[0], &n) < 0) return NULL;
    if (n < 1) return err(E_ValueError, "recursion limit must be greater or equal than 1");
    if (n > vm.max_recursion) return err(E_RecursionError, "recursion limit %lld is above the supported maximum of %d", (long long)n, vm.max_recursion);
    if (n <= vm.depth) return err(E_RecursionError, "cannot set the recursion limit to %lld at the recursion depth %d: the limit is too low", (long long)n, vm.depth);
    vm.recursion_limit = (int)n;
    return Py_None;
}
static Obj *sys_getsizeof(ARGS) { UNUSED; return int_new(argv[0]->size); }
static Obj *sys_intern(ARGS) { UNUSED; return argv[0]; }

static Obj *make_sys(void) {
    Obj *m = module_new("sys");
    Dict *d = ((Module *)m)->dict;
    dict_setc(d, "argv", vm.argv ? vm.argv : list_new(0));
    sys_stdout_orig = file_new(stdout, "<stdout>", false, true, true);
    gc_root(&sys_stdout_orig);
    sys_dict = d;
    gc_root((Obj **)&sys_dict);
    dict_setc(d, "stdout", sys_stdout_orig);
    dict_setc(d, "__stdout__", sys_stdout_orig);
    dict_setc(d, "stderr", file_new(stderr, "<stderr>", false, true, true));
    dict_setc(d, "stdin", file_new(stdin, "<stdin>", true, false, true));
    dict_setc(d, "maxsize", int_new(INT64_MAX));
    dict_setc(d, "version", str_c("3.13 (pyc, a Python interpreter in C)"));
    dict_setc(d, "version_info", tuple_pack(5, int_new(3), int_new(13), int_new(0), str_c("final"), int_new(0)));
    dict_setc(d, "platform", str_c("linux"));
    dict_setc(d, "byteorder", str_c("little"));
    dict_setc(d, "modules", (Obj *)vm.modules);
    List *path = (List *)list_new(0);
    list_append(path, str_c(vm.script_dir ? vm.script_dir : ""));
    dict_setc(d, "path", (Obj *)path);
    mdef(m, "exit", sys_exit, 0, 1, 0);
    mdef(m, "getrecursionlimit", sys_getrecursionlimit, 0, 0, 0);
    mdef(m, "setrecursionlimit", sys_setrecursionlimit, 1, 1, 0);
    mdef(m, "getsizeof", sys_getsizeof, 1, 2, 0);
    mdef(m, "intern", sys_intern, 1, 1, 0);
    return m;
}

/* ================= _os (wrapped by lib/os.py) ================= */
#include <dirent.h>
extern char **environ;
static Obj *os_fail(const char *path) {
    int e = errno;
    Type *t = e == ENOENT ? E_FileNotFoundError : e == EACCES ? E_PermissionError : e == EEXIST ? E_FileExistsError :
              e == EISDIR ? E_IsADirectoryError : E_OSError;
    err(t, "[Errno %d] %s: '%s'", e, strerror(e), path);
    Exc *x = (Exc *)vm.exc;
    x->dict = (Dict *)dict_new();
    dict_setc(x->dict, "errno", int_new(e));
    dict_setc(x->dict, "filename", str_c(path));
    return NULL;
}
#define PATH_ARG(i) (AS_STR(argv[i])->s)
#define NEED_PATH(i) if (!IS_STR(argv[i])) return err(E_TypeError, "expected str path, not %s", tname(argv[i]))
static Obj *os_getcwd(ARGS) {
    UNUSED;
    char buf[4096];
    if (!getcwd(buf, sizeof buf)) return os_fail(".");
    return str_c(buf);
}
static Obj *os_listdir(ARGS) {
    UNUSED;
    const char *path = ".";
    if (argc > 0) { NEED_PATH(0); path = PATH_ARG(0); }
    DIR *d = opendir(path);
    if (!d) return os_fail(path);
    List *l = (List *)list_new(0);
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) list_append(l, str_c(e->d_name));
    closedir(d);
    return (Obj *)l;
}
static Obj *os_remove(ARGS) { UNUSED; NEED_PATH(0); return unlink(PATH_ARG(0)) < 0 ? os_fail(PATH_ARG(0)) : Py_None; }
static Obj *os_rmdir(ARGS) { UNUSED; NEED_PATH(0); return rmdir(PATH_ARG(0)) < 0 ? os_fail(PATH_ARG(0)) : Py_None; }
static Obj *os_mkdir(ARGS) {
    UNUSED;
    int64_t mode = 0777;
    NEED_PATH(0);
    if (argc > 1 && obj_index(argv[1], &mode) < 0) return NULL;
    return mkdir(PATH_ARG(0), (mode_t)mode) < 0 ? os_fail(PATH_ARG(0)) : Py_None;
}
static Obj *os_rename(ARGS) { UNUSED; NEED_PATH(0); NEED_PATH(1); return rename(PATH_ARG(0), PATH_ARG(1)) < 0 ? os_fail(PATH_ARG(0)) : Py_None; }
static Obj *os_chdir(ARGS) { UNUSED; NEED_PATH(0); return chdir(PATH_ARG(0)) < 0 ? os_fail(PATH_ARG(0)) : Py_None; }
static Obj *os_kind(ARGS) { /* 0 missing, 1 file, 2 directory, 3 other */
    UNUSED;
    struct stat st;
    if (!IS_STR(argv[0])) return int_new(0);
    if (stat(PATH_ARG(0), &st) < 0) return int_new(0);
    return int_new(S_ISREG(st.st_mode) ? 1 : S_ISDIR(st.st_mode) ? 2 : 3);
}
static Obj *os_getsize(ARGS) {
    UNUSED;
    struct stat st;
    NEED_PATH(0);
    if (stat(PATH_ARG(0), &st) < 0) return os_fail(PATH_ARG(0));
    return int_new(st.st_size);
}
static Obj *os_getpid(ARGS) { UNUSED; return int_new(getpid()); }
static Obj *os_system(ARGS) {
    UNUSED;
    NEED_PATH(0);
    fflush(stdout);
    return int_new(system(PATH_ARG(0)));
}
static Obj *make_os(void) {
    Obj *m = module_new("_os");
    Dict *env = (Dict *)dict_new();
    for (char **e = environ; e && *e; e++) {
        const char *eq = strchr(*e, '=');
        if (eq) dict_set(env, str_new(*e, (size_t)(eq - *e)), str_c(eq + 1));
    }
    dict_setc(((Module *)m)->dict, "environ", (Obj *)env);
    mdef(m, "getcwd", os_getcwd, 0, 0, 0); mdef(m, "listdir", os_listdir, 0, 1, 0); mdef(m, "remove", os_remove, 1, 1, 0);
    mdef(m, "rmdir", os_rmdir, 1, 1, 0); mdef(m, "mkdir", os_mkdir, 1, 2, 0); mdef(m, "rename", os_rename, 2, 2, 0);
    mdef(m, "chdir", os_chdir, 1, 1, 0); mdef(m, "kind", os_kind, 1, 1, 0); mdef(m, "getsize", os_getsize, 1, 1, 0);
    mdef(m, "getpid", os_getpid, 0, 0, 0); mdef(m, "system", os_system, 1, 1, 0);
    return m;
}

/* ================= import ================= */
static char *read_file(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = malloc((size_t)st.st_size + 1);
    size_t n = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

static Obj *exec_module(Str *name, const char *src, const char *filename) {
    Obj *m = module_new(name->s);
    Dict *d = ((Module *)m)->dict;
    dict_sets(d, N_file, str_c(filename));
    dict_sets(vm.modules, name, m);
    register_source(filename, src);
    Code *co = compile_source(src, filename, MODE_FILE);
    if (!co || !run_code(co, d, NULL)) { dict_dels(vm.modules, name); return NULL; }
    return m;
}

static Obj *load_from_path(Str *name) {
    /* a.b.c -> a/b/c.py or a/b/c/__init__.py, searched in each sys.path entry */
    Obj *sys = dict_getc(vm.modules, "sys");
    Obj *path = sys ? dict_getc(((Module *)sys)->dict, "path") : NULL;
    if (!path || KIND(path) != K_LIST) return NULL;
    SB rel;
    sb_init(&rel);
    for (uint32_t i = 0; i < name->len; i++) sb_putc(&rel, name->s[i] == '.' ? '/' : name->s[i]);
    sb_putc(&rel, 0);
    Obj *result = NULL;
    for (int64_t i = 0; i < ((List *)path)->n && !result && !vm.exc; i++) {
        Obj *dir = ((List *)path)->items[i];
        if (!IS_STR(dir)) continue;
        const char *ds = AS_STR(dir)->len ? AS_STR(dir)->s : ".";
        char file[4096];
        snprintf(file, sizeof file, "%s/%s.py", ds, rel.s);
        char *src = read_file(file);
        if (!src) { snprintf(file, sizeof file, "%s/%s/__init__.py", ds, rel.s); src = read_file(file); }
        if (src) { result = exec_module(name, src, file); free(src); break; }
        struct stat st;
        snprintf(file, sizeof file, "%s/%s", ds, rel.s);
        if (stat(file, &st) == 0 && S_ISDIR(st.st_mode)) {
            result = module_new(name->s); /* a directory with no __init__.py */
            dict_sets(vm.modules, name, result);
        }
    }
    sb_free(&rel);
    return result;
}

Obj *import_module(Str *name) {
    Obj *m = dict_gets(vm.modules, name);
    if (m) return m;
    if (name->len == 0) return err(E_ImportError, "relative imports need a package, which is not supported");
    /* Import parent packages first. */
    const char *dot = NULL;
    for (uint32_t i = 0; i < name->len; i++) if (name->s[i] == '.') dot = name->s + i;
    Obj *parent = NULL;
    if (dot) {
        parent = import_module(AS_STR(str_new(name->s, (size_t)(dot - name->s))));
        if (!parent) return NULL;
        /* The parent may already provide the name as an attribute (os.path). */
        Obj *attr = KIND(parent) == K_MODULE ? dict_getc(((Module *)parent)->dict, dot + 1) : NULL;
        if (attr) { dict_sets(vm.modules, name, attr); return attr; }
    }
    if (str_eqc(name, "math")) m = make_math();
    else if (str_eqc(name, "time")) m = make_time();
    else if (str_eqc(name, "random")) m = make_random();
    else if (str_eqc(name, "sys")) m = make_sys();
    else if (str_eqc(name, "_os")) m = make_os();
    else if (str_eqc(name, "gc")) m = make_gc();
    else if (str_eqc(name, "builtins")) { m = module_new("builtins"); ((Module *)m)->dict = vm.builtins; }
    if (m) { dict_sets(vm.modules, name, m); return m; }
    /* Checked files were proved against the built-in dependent module, so a file of
       that name on the path is never used in its place. */
    m = str_eqc(name, "dependent") ? NULL : load_from_path(name);
    if (!m && !vm.exc)
        for (int i = 0; pylib_names[i]; i++)
            if (str_eqc(name, pylib_names[i])) {
                char fn[128];
                snprintf(fn, sizeof fn, "<builtin %s>", pylib_names[i]);
                m = exec_module(name, pylib_sources[i], fn);
                break;
            }
    if (!m) {
        if (!vm.exc) err(E_ModuleNotFoundError, "No module named '%s'", name->s);
        return NULL;
    }
    if (parent && KIND(parent) == K_MODULE) dict_setc(((Module *)parent)->dict, dot + 1, m);
    return m;
}
