def f(a, b=2, *args, c, d=4, **kw): return (a, b, args, c, d, kw)
print(f(1, c=3), f(1, 2, 3, 4, c=5, e=6), f(b=1, a=0, c=2), f(*[1, 2, 3], **{"c": 9, "z": 0}))
def g(x, /, y, *, z): return x, y, z
print(g(1, 2, z=3), g(1, y=2, z=3))
def defaults(a, lst=[]):
    lst.append(a); return lst
print(defaults(1), defaults(2), defaults(3, []))
def outer():
    count = 0
    def inc(n=1):
        nonlocal count
        count += n
        return count
    return inc
c1, c2 = outer(), outer()
print(c1(), c1(5), c2(), c1.__name__, outer.__name__)
adders = [lambda x, i=i: x + i for i in range(3)]
late = [lambda x: x + i for i in range(3)]
print([a(10) for a in adders], [a(10) for a in late])
def deco(fn):
    def wrapper(*a, **k):
        return ("wrapped", fn(*a, **k))
    return wrapper
def repeat(n):
    def d(fn):
        def w(*a): return [fn(*a) for _ in range(n)]
        return w
    return d
@deco
@repeat(2)
def hi(name): return "hi " + name
print(hi("bob"))
def fact(n): return 1 if n <= 1 else n * fact(n - 1)
print(fact(20), fact(30))
def fib(n, memo={}):
    if n in memo: return memo[n]
    memo[n] = n if n < 2 else fib(n - 1) + fib(n - 2)
    return memo[n]
print(fib(90))
def noreturn(): pass
print(noreturn(), (lambda *a, **k: (a, k))(1, x=2), (lambda: None)())
def kwonly(*, key): return key
def manyret(): return 1, 2, 3
x, y, z = manyret(); print(x, y, z, kwonly(key="v"))
def doc():
    """Docstring here."""
print(doc.__doc__, doc.__name__, callable(doc), callable(1))
def counter():
    counter.calls = getattr(counter, "calls", 0) + 1
    return counter.calls
counter(); counter(); print(counter.calls)
def apply(fn, *args, **kwargs): return fn(*args, **kwargs)
print(apply(max, 1, 5, 3), apply(sorted, [3, 1, 2], reverse=True), apply(lambda **k: sorted(k), b=1, a=2))
def compose(*fs):
    def run(x):
        for fn in reversed(fs): x = fn(x)
        return x
    return run
print(compose(str, lambda v: v * 2, abs)(-21))
def scope_test():
    x = "local"
    def inner():
        return x
    x = "changed"
    return inner()
print(scope_test())
def recurse(n):
    return recurse(n + 1)
try: recurse(0)
except RecursionError as e: print("RecursionError:", e)
def deep(n): return 0 if n == 0 else 1 + deep(n - 1)
print(deep(900))
for bad in ["f()", "f(1)", "f(1, 2, 3, c=1, a=5)", "g(1, 2, 3)", "g(x=1, y=2, z=3)", "kwonly(1)", "kwonly()", "noreturn(1)", "noreturn(a=1)", "(lambda a, b: 0)(1)", "5()", "len()", "len(1, 2)", "len(5)"]:
    try:
        eval(bad)
    except TypeError as e:
        print("TypeError ->", e)
print(list(map(lambda p: p[0] * p[1], [(1, 2), (3, 4)])), (lambda x=5: x)(), (lambda *, k=1: k)(k=3))
def gen_args(*args): return len(args)
print(gen_args(*range(100)), gen_args(*"abc", *[1, 2]), gen_args(*(i for i in range(4))))
def with_annotations(a: int, b: "str" = "x", *c: float, d: list[int] = None, **e: dict) -> bool: return (a, b, c, d, e)
print(with_annotations(1), with_annotations(1, "y", 2.0, d=[1], k=1))
v: int = 5
w: str
print(v, globals()["v"], "w" in globals())
