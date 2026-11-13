# evaluation order and assignment
log = []
def t(v): log.append(v); return v
d = {}; d[t("k")] = t("v"); x = [0, 0]; x[t(0)], x[t(1)] = t("a"), t("b"); print(log, d, x)
a = b = [1, 2]; a += [3]; print(b); i = 0; lst = [0, 1, 2]; i, lst[i] = 1, "x"; print(i, lst)
a, b, c = 1, 2, 3; a, b, c = c, a, b; print(a, b, c); *a, = "xy"; (a1, b1), c1 = [1, 2], 3; [p, [q, r]] = 1, (2, 3); print(a, a1, b1, c1, p, q, r)
for i, (k, v) in enumerate({"x": 1, "y": 2}.items()): print(i, k, v, end=" ")
for a, *rest in [(1, 2, 3), (4,), "ab"]: print(a, rest, end=" ")
print()
# scoping
x = "global"
def outer():
    x = "outer"
    def mid():
        nonlocal x
        x = "mid"
        def inner():
            global x
            x = "inner-global"
        inner(); return x
    r = mid(); return r, x
print(outer(), x)
def counter_factory():
    counts = {}
    def bump(k): counts[k] = counts.get(k, 0) + 1; return counts
    return bump
bump = counter_factory(); bump("a"); print(bump("a"), bump.__closure__ is not None if hasattr(bump, "__closure__") else True)
class Scope:
    v = 1
    lst = [v + i for i in range(3)] if False else [1, 2, 3]
    def m(self): return v_global
    doubled = [n * 2 for n in lst]
v_global = "vg"; print(Scope().m(), Scope.doubled)
def shadow(len=len): return len("abc")
print(shadow(), [abs for abs in [1, 2]], abs(-1))
funcs = []
for i in range(3):
    def f(i=i): return i
    funcs.append(f)
print([f() for f in funcs], (lambda: i)())
def gen_closure(): return [(lambda: j) for j in range(3)], list((lambda: j)() for j in range(3))
fs, vals = gen_closure(); print([f() for f in fs], vals)
# identity, truthiness, comparison
print([] == [], [] is not [], None == False, 0 == False, "" == 0, 1 == 1.0 == True, (1, 2) == [1, 2], {1: 2} == {1.0: 2.0}, "a" < "b" < "c", [1, 2] < [1, 2, 3], (1, "a") < (1, "b"), not [] and not {} and not "" and not 0 and not None and not () and not set() and not range(0), bool([0]), bool("0"), bool(0.0), bool(-1))
for a, b in [(1, "a"), ([1], (1,)), (None, 0), ({}, [])]:
    try: a < b
    except TypeError as e: print(e)
print(sorted([3, 1.5, True, 2, False]), sorted(["b", "A", "a", "B"]), sorted([(2, "a"), (1, "b"), (2, "A")]), sorted([[2], [1, 2], [1]]), min([], default=None), max("a", "B", key=str.lower), sorted([1, 2, 3], key=lambda v: 0))
# numbers formatting
for v in [0.1, 1 / 3, 1e21, 1e-7, 123456789012345678, 1.0e16, 1.5e300 * 1e10, -1e-320, 2.5e-5, 100.0, 1e5, 123456.789e3, 0.000123, 9007199254740993.0, 3.0e-10, 5e-324, 1.7976931348623157e308, float("-inf")]:
    print(v, repr(v), str(v), f"{v:.3g}", f"{v:10.2e}", "%.2f" % v if abs(v) < 1e20 else "-", format(v, ","), sep=" | ")
print(round(0.5), round(1.5), round(-0.5), round(2.5, 0), round(1.005, 2), round(123.456, 1), round(-123.456, -1), round(1e300), int(1e22), 10 / 3, 10 // 3.0, -10 // 3, 10 % -3, -10 % 3.0, 2 ** 0.5, 4 ** 0.5, (-8) ** (1 / 3) if False else "skip", 7 // 1, 7.0 // 1, 1e3 == 1000, 0.1 + 0.2 == 0.3, 1 / 3 * 3 == 1)
print(int("0b101", 0), int("0x1F", 0), int("-12"), int("+7"), int("  9  "), int(True), int(2.9), int(-2.9), int("1_000"), float("1e-3"), float("  3.5  "), float("inf") > 10 ** 400, float(10 ** 300), str(float(2 ** 70)), 5 // 2 * 2 + 5 % 2, -5 // 2 * 2 + -5 % 2, 2 ** 3 ** 2, -2 ** 2, (-2) ** 2, not 1 == 2, 1 + 2 * 3 - 4 / 2, 1 < 2 == 2, 5 & 3 | 4 ^ 1, ~5 + 1, 1 << 2 + 1)
# strings corner cases
print("%s %r %5s|%-5s|%05.1f %+d %x %o %e %g %c%%" % ("s", "r", "ab", "cd", 3.14159, 5, 255, 8, 12345.6789, 0.0001234, "z"), "%3d|%-3d|%03d" % (5, 5, 5), "%s" % (1,), "%s %s" % (1, (2, 3)), "%.3s" % "abcdef", "%5.1f%%" % 99.55, "%(x)d-%(y)s" % {"x": 1, "y": "b"}, "%d" % 3.9, "%i" % -2, "%10s|" % None, "%r" % "q'q", "%s" % [1, "a"], "%x" % -255, "%#x %#o" % (255, 8), "% d" % 5, "%.0f" % 2.5, "%.0f" % 3.5, "%5s" % True)
print(f"{'a':*<5}{'b':->5}{'c':^5}|{1.5:08.3f}|{-1.5:+.1e}|{1234567:,d}|{255:b}|{255:#010b}|{3:03}|{0.5:.0%}|{'x'!r:>5}|{None}|{True:>6}|{[1, 2]!s:>8}|{1e10:g}|{123.456:.4g}|{42:^+7}|{'é':>3}|{1 / 3:.3}|{2 ** 64:x}|{-5:05d}|{5:<05d}|{12345.678:_.1f}|")
name, n, items = "x", 3, {"k": [1, 2]}
print(f"{name=}, {n + 1 = }, {items['k'][0]}, {items["k"]!r}, {'{}'.format(n)}, {f'{n:>{n}}'}, {n:{n}}, {(lambda v: v * 2)(n)}, {n if n else 0}, {{n}}, {n!r:5}|, {'a' 'b'}, { n }")
print("a,b,,c".split(","), "a  b".split(" "), "".split(), "".split(","), " a ".split(), "abc".split("b", 0), "a.b.c".rsplit(".", 1), "aXbXc".split("X", 1), "\ta\n b ".split(), "abc".join(["1", "2"]), "x".join([]), "abcabc".replace("bc", "-", 1), "aaa".count("aa"), "abc".find(""), "abc"[::-1].upper().lower().title(), "hello world".title(), "o'neil mc-donald".title(), "ß".upper(), "ǅ".lower() if False else "", "İ".lower() == "i̇" or True)
print("abc" > "ab", "Z" < "a", "apple" < "banana" < "cherry", "a" * -1 == "", "ab" * 0 == "", "abc"[10:] == "", "abc"[-100:100], "abcdef"[1:-1], "abcdef"[-3:-1], "abcdef"[::3], "abcdef"[5:0:-2], "abcdef"[:-7:-1], "x" in "", "" in "x", "ab" in "cabd", len("\n\t\\"), "tab\tnew\nline".split(), "a\\b", r"a\b" == "a\\b", "\x41B\103", "日本語"[1], len("日本語"), "日本語".encode if False else "", "naïve café".upper(), "ÀÉÎ".lower(), "straße".casefold() if False else "")
print(repr("\\"), repr("'"), repr('"'), repr("'\""), repr("\n\r\t\0\x7f\x1b"), repr("é€😀"), str([None, True, 1.0, "s", (1,), {1}, {"k": "v"}, [], (), {}]), repr((1,)), repr(((),)), repr([[]]), repr({"a": {"b": {"c": []}}}), repr(1.0), repr(-0.0), repr(1e100), repr(float("nan")), [float("inf"), -float("inf")])
# misc statements
x = [1, 2, 3, 4, 5]; del x[1], x[-1]; print(x); y = {"a": 1, "b": 2}; del y["a"]; print(y)
def multi(): return 1, [2, 3], {"k": 4}
(a, [b, c], d), e = multi(), 5; print(a, b, c, d, e)
val = 10
def uses_global(): return val * 2
def makes_local():
    val = 1; return val
print(uses_global(), makes_local(), val)
z = 5 if val > 5 else 6 if val > 3 else 7; print(z, (yes := 1) + yes, [w for w in range(5) if w % 2 if w > 1], [(u, w) for u in range(2) for w in "ab"], {u: w for u in range(2) for w in "ab"}, {u % 2 for u in range(5)} == {0, 1})
try:
    try: raise KeyError("k")
    finally: print("inner finally")
except KeyError: print("outer except")
def fin_order():
    out = []
    for i in range(3):
        try:
            if i == 0: continue
            if i == 1: out.append("one")
            if i == 2: return out
        finally: out.append(f"f{i}")
print(fin_order())
def nested_fin():
    try:
        try: return "inner"
        finally: print("f1")
    finally: print("f2")
print(nested_fin())
class C:
    def __enter__(self): return 1
    def __exit__(self, *a): return False
with C() as one, C() as two: print(one + two)
with (C() as p1, C() as p2): print(p1 * p2)
# builtin edge cases
print(list(range(10, -10, -5)), list(range(-5, 5, 3)), len(range(10 ** 15)), range(10 ** 15)[-1], sum(range(101)), list(reversed(range(1, 10, 3))), range(5)[::-1], list(range(5)[1:4]), 3 in range(0, 10, 3), 4 in range(0, 10, 3), list(zip(range(3), "abc", [True, False])), list(map(lambda a, b: a * b, range(4), range(4, 8))), list(enumerate("ab", start=1)), dict(enumerate("ab")), sum([[1], [2]], [0]), sum((1.5, 2.5)), sum(range(5), 100), max(range(5)), min("hello"), max(["apple", "fig"], key=len), any(x > 3 for x in range(5)), all(x < 3 for x in range(5)), abs(-0.0), divmod(7.5, 2), pow(2, 0.5), pow(-2, 3), isinstance(1, (int, float)), isinstance("s", (int, float)), type(1 / 1).__name__, type(1 // 1).__name__, callable(len), callable("s"), chr(ord("a") + 1), ord("\n"), hex(-255), oct(8), bin(0), int("-0"), bool("False"), str(None), repr(None), list("abc"), tuple(range(3)), set("aab") == {"a", "b"}, dict(a=1) == {"a": 1}, list({"b": 1, "a": 2}), sorted({"b": 1, "a": 2}.items()))
print(isinstance(True, bool), True + 1, True == 1, {True: "a", 1: "b", 1.0: "c"}, [True, False].count(1), sum([True, True, False]), int(True), str(True), True & False, True | False, True ^ True, not True, ~True, -True, True / 2, type(True + True).__name__, type(True & True).__name__, bool(2) is True, isinstance(False, int))
print(getattr(1, "real"), getattr("s", "upper")(), hasattr([], "append"), hasattr([], "nope"), [].append.__name__, len.__name__, type(len).__name__, type(lambda: 0).__name__, (lambda: 0).__name__, type(type).__name__, type(None).__name__, type(NotImplemented).__name__, int.__name__, list.__name__, type(print).__name__, object.__name__, str(type(1)), str(int), repr(list), str(type(None)), str(ValueError), str(type(ValueError())))
