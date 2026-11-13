def count(n):
    i = 0
    while i < n:
        yield i
        i += 1
print(list(count(5)), sum(count(100)), list(zip(count(3), count(10))), max(count(7)))
g = count(2); print(next(g), next(g), next(g, "done"), next(g, "done"))
try: next(g)
except StopIteration: print("StopIteration")
def fibs():
    a, b = 0, 1
    while True:
        yield a
        a, b = b, a + b
f = fibs(); print([next(f) for _ in range(15)])
def take(n, it):
    for _ in range(n): yield next(it)
print(list(take(5, fibs())), list(take(3, iter("abcdef"))))
def echo():
    received = []
    while True:
        v = yield len(received)
        if v is None: break
        received.append(v)
    return received
e = echo(); print(next(e), e.send("a"), e.send("b"))
try: e.send(None)
except StopIteration as s: print("returned", s.value)
def inner():
    x = yield 1
    y = yield 2
    return x + y
def outer():
    r = yield from inner()
    yield r * 10
    yield from [7, 8]
    yield from range(2)
    return "outer done"
o = outer(); print(next(o), o.send(3), o.send(4), list(o))
def flatten(x):
    for item in x:
        if isinstance(item, list): yield from flatten(item)
        else: yield item
print(list(flatten([1, [2, [3, [4, 5]], 6], [[7]]])))
print(list(i * 2 for i in range(4)), sum(x * x for x in range(10)), tuple(c.upper() for c in "abc"), ",".join(str(i) for i in range(3)), dict((i, i * i) for i in range(3)), set(i % 2 for i in range(9)) == {0, 1})
ge = (i for i in range(3)); print(list(ge), list(ge), 1 in (i for i in range(3)))
def tree(depth):
    if depth == 0:
        yield "leaf"; return
    for sub in tree(depth - 1):
        yield "L" + sub
        yield "R" + sub
print(list(tree(2)), len(list(tree(8))))
def with_finally():
    try:
        yield 1
        yield 2
    except GeneratorExit:
        print("closed early"); raise
    finally:
        print("finally")
w = with_finally(); next(w); w.close(); w.close()
def catcher():
    while True:
        try: yield "ready"
        except ValueError as e: yield f"caught {e}"
c = catcher(); print(next(c), c.throw(ValueError("boom")), next(c))
try: c.throw(KeyError("out"))
except KeyError as e: print("propagated", e)
print(list(c))
def bad():
    raise StopIteration
    yield
try: list(bad())
except RuntimeError as e: print("RuntimeError", e)
class Countdown:
    def __init__(self, n): self.n = n
    def __iter__(self): return self
    def __next__(self):
        if self.n <= 0: raise StopIteration
        self.n -= 1
        return self.n + 1
print(list(Countdown(4)), [x for x in Countdown(2)], sum(Countdown(10)), list(zip(Countdown(3), "ab")))
class Bag:
    def __init__(self, *items): self.items = items
    def __iter__(self): return iter(self.items)
b = Bag(3, 1, 2); print(sorted(b), list(b), list(b), max(b), list(enumerate(b)), [*b], tuple(b), set(b) == {1, 2, 3}, dict(zip(b, b)))
it = iter([1, 2, 3]); print(next(it), list(it), list(it), iter(it) is it)
def primes():
    seen = []
    n = 2
    while True:
        if all(n % p for p in seen):
            seen.append(n); yield n
        n += 1
p = primes(); print([next(p) for _ in range(15)])
def pipeline(nums): return (n * n for n in nums if n % 2)
print(sum(pipeline(range(1000))), type(pipeline([])).__name__, type(count).__name__)
lazy = map(lambda v: print("evaluated", v) or v, [1, 2]); print("before"); print(list(lazy))
def gen_with_return():
    yield 1
    return 99
def delegator():
    v = yield from gen_with_return()
    yield v
print(list(delegator()), list(gen_with_return()))
def recursive_gen(n):
    if n > 0:
        yield n
        yield from recursive_gen(n - 1)
print(list(recursive_gen(5)), sum(recursive_gen(500)))
gs = [count(3) for _ in range(2)]; print([next(g) for g in gs], [next(g) for g in gs])
def running():
    yield g2.gi_running if hasattr(g2, "gi_running") else True
try:
    def selfcall():
        yield next(sc)
    sc = selfcall(); next(sc)
except ValueError as e: print("ValueError", e)
x = [1, 2, 3]
print(list(v * 2 for v in x if v != 2), [a + b for a in "ab" for b in "xy"], {k: [i for i in range(k)] for k in range(3)}, [[r * c for c in range(3)] for r in range(3)], [(a, b) for a in range(3) if a for b in range(a) if b != a])
