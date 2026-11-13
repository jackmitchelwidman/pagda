class Account:
    __count = 0
    def __init__(self, bal): self.__bal = bal; Account.__count += 1
    def __audit(self): return f"audit {self.__bal}"
    def report(self): return self.__audit(), Account.__count, self.__class__.__name__
    def __eq__(self, o): return self.__bal == o.__bal
    @property
    def bal(self): return self.__bal
class Child(Account):
    def __init__(self): super().__init__(5); self.__bal = "child"
    def both(self): return self.__bal, self._Account__bal
a = Account(10); c = Child()
print(a.report(), a._Account__bal, c.both(), a.bal, sorted(k for k in vars(c)), Account._Account__count, a == Account(10), hasattr(a, "__bal"), hasattr(a, "_Account__audit"))
try: a.__bal
except AttributeError as e: print("AttributeError", e)
class P:
    __slots__ = ("x", "y")
    def __init__(self, x, y): self.x, self.y = x, y
p = P(1, 2); p.x = 5; print(p.x, p.y)
try: p.z = 3
except AttributeError as e: print("AttributeError", e)
class Q(P): pass
q = Q(1, 2); q.z = 3; print(q.z)
# descriptors
class Positive:
    def __set_name__(self, owner, name): self.name = "_" + name
    def __get__(self, obj, objtype=None): return self if obj is None else getattr(obj, self.name, 0)
    def __set__(self, obj, value):
        if value < 0: raise ValueError(f"{self.name[1:]} must be >= 0")
        setattr(obj, self.name, value)
class Order:
    qty = Positive(); price = Positive()
    def __init__(self, qty, price): self.qty, self.price = qty, price
o = Order(2, 3.5); o.qty += 1; print(o.qty, o.price, o.qty * o.price, type(Order.qty).__name__, vars(o))
try: o.price = -1
except ValueError as e: print("ValueError", e)
class Verbose:
    def __get__(self, obj, cls): return f"get from {cls.__name__} ({'inst' if obj else 'class'})"
class H:
    v = Verbose()
h = H(); print(h.v, H.v); h.__dict__["v"] = "instance wins"; print(h.v)
# class decorators, class methods, static, inheritance of class attrs
def register(cls): cls.registered = True; registry.append(cls.__name__); return cls
registry = []
@register
class Plugin:
    instances = []
    def __init__(self): type(self).instances.append(self)
    @classmethod
    def create(cls, *a): return cls(*a)
    @staticmethod
    def helper(x): return x * 2
    @classmethod
    def count(cls): return len(cls.instances)
@register
class Sub(Plugin):
    instances = []
Plugin.create(); Sub.create(); Sub(); print(registry, Plugin.registered, Plugin.count(), Sub.count(), Sub.helper(4), Plugin().helper(1), type(Sub.create()).__name__, Sub.create.__self__ is Sub)
class Base:
    registry = {}
    def __init_subclass__(cls, **kw): Base.registry[cls.__name__] = cls
    def tmpl(self): return f"{self.prefix()}-{self.body()}"
    def prefix(self): return "base"
    def body(self): raise NotImplementedError("subclass must implement body")
class One(Base):
    def body(self): return "one"
class Two(One):
    def prefix(self): return "two"
print(sorted(Base.registry), One().tmpl(), Two().tmpl())
try: Base().tmpl()
except NotImplementedError as e: print("NotImplementedError", e)
# operator overloading: vectors and fractions
class Vec:
    def __init__(self, *c): self.c = tuple(c)
    def __repr__(self): return f"Vec{self.c}"
    def __add__(self, o): return Vec(*(a + b for a, b in zip(self.c, o.c)))
    def __sub__(self, o): return Vec(*(a - b for a, b in zip(self.c, o.c)))
    def __mul__(self, k): return sum(a * b for a, b in zip(self.c, k.c)) if isinstance(k, Vec) else Vec(*(a * k for a in self.c))
    __rmul__ = __mul__
    def __matmul__(self, o): return self * o
    def __truediv__(self, k): return Vec(*(a / k for a in self.c))
    def __floordiv__(self, k): return Vec(*(a // k for a in self.c))
    def __mod__(self, k): return Vec(*(a % k for a in self.c))
    def __pow__(self, k): return Vec(*(a ** k for a in self.c))
    def __neg__(self): return Vec(*(-a for a in self.c))
    def __pos__(self): return self
    def __abs__(self): return sum(a * a for a in self.c) ** 0.5
    def __eq__(self, o): return isinstance(o, Vec) and self.c == o.c
    def __hash__(self): return hash(self.c)
    def __bool__(self): return any(self.c)
    def __len__(self): return len(self.c)
    def __getitem__(self, i): return self.c[i] if isinstance(i, int) else Vec(*self.c[i])
    def __iter__(self): return iter(self.c)
    def __contains__(self, v): return v in self.c
    def __iadd__(self, o): self.c = tuple(a + b for a, b in zip(self.c, o.c)); return self
    def __lshift__(self, n): return Vec(*(self.c[n:] + self.c[:n]))
    def __rshift__(self, n): return self << -n
    def __and__(self, o): return Vec(*(a & b for a, b in zip(self.c, o.c)))
    def __or__(self, o): return Vec(*(a | b for a, b in zip(self.c, o.c)))
    def __xor__(self, o): return Vec(*(a ^ b for a, b in zip(self.c, o.c)))
    def __invert__(self): return Vec(*(~a for a in self.c))
    def __lt__(self, o): return abs(self) < abs(o)
    def __le__(self, o): return abs(self) <= abs(o)
    def __round__(self, n=0): return Vec(*(round(a, n) for a in self.c))
    def __call__(self, i): return self.c[i]
    def __format__(self, spec): return "<" + ", ".join(format(a, spec) for a in self.c) + ">"
    def __reversed__(self): return Vec(*reversed(self.c))
    def __index__(self): return len(self.c)
    def __divmod__(self, k): return (self // k, self % k)
    def __rsub__(self, o): return Vec(*(o - a for a in self.c))
    def __rtruediv__(self, o): return Vec(*(o / a for a in self.c))
    def __rpow__(self, o): return Vec(*(o ** a for a in self.c))
u, v = Vec(1, 2, 3), Vec(4, 5, 6); w = u; w += v
print(u, v, u + v, v - u, u * 2, 2 * u, u * v, u @ v, v / 2, v // 4, v % 4, v ** 2, -u, +u, abs(Vec(3, 4)), u == Vec(5, 7, 9), u != v, bool(Vec(0, 0)), len(u), u[0], u[1:], list(u), 7 in u, w is u, u << 1, u >> 1, v & u, v | u, v ^ u, ~u, u < v, u >= v, v > u, max(u, v), sorted([v, u])[0], round(Vec(1.26, 2.5), 1), u(0), f"{v:.1f}", f"{v:03d}", list(reversed(u)), [0, 1, 2, 3, 4][Vec(1, 2)], {u: 1}[Vec(5, 7, 9)], 10 - u, 1 / Vec(2, 4), 2 ** Vec(1, 2), sum([u, v], Vec(0, 0, 0)), min(u, v, key=abs))
class Frac:
    def __init__(self, n, d=1):
        from math import gcd
        g = gcd(n, d) * (-1 if d < 0 else 1); self.n, self.d = n // g, d // g
    def __repr__(self): return f"{self.n}/{self.d}" if self.d != 1 else str(self.n)
    def _c(self, o): return o if isinstance(o, Frac) else Frac(o)
    def __add__(self, o): o = self._c(o); return Frac(self.n * o.d + o.n * self.d, self.d * o.d)
    __radd__ = __add__
    def __sub__(self, o): o = self._c(o); return Frac(self.n * o.d - o.n * self.d, self.d * o.d)
    def __rsub__(self, o): return self._c(o) - self
    def __mul__(self, o): o = self._c(o); return Frac(self.n * o.n, self.d * o.d)
    __rmul__ = __mul__
    def __truediv__(self, o): o = self._c(o); return Frac(self.n * o.d, self.d * o.n)
    def __rtruediv__(self, o): return self._c(o) / self
    def __eq__(self, o): o = self._c(o); return (self.n, self.d) == (o.n, o.d)
    def __lt__(self, o): o = self._c(o); return self.n * o.d < o.n * self.d
    def __float__(self): return self.n / self.d
    def __hash__(self): return hash((self.n, self.d))
h = sum((Frac(1, k) for k in range(1, 30)), Frac(0)); print(h, float(h), Frac(1, 2) + 1, 1 - Frac(1, 3), 3 * Frac(2, 9), 1 / Frac(3, 4), Frac(2, 4) == Frac(1, 2), Frac(1, 2) == 0.5 if False else "", sorted([Frac(1, 2), Frac(1, 3), Frac(3, 4)]), Frac(6, -8), max(Frac(1, 3), Frac(2, 5)), Frac(10 ** 30, 10 ** 29 * 4))
# iteration protocol corners
class Rng:
    def __init__(self, n): self.n = n
    def __getitem__(self, i):
        if i >= self.n: raise IndexError
        return i * i
class Bad:
    def __iter__(self): return 5
print(list(Rng(4)), 9 in Rng(5), [x for x in Rng(3)], sum(Rng(10)), list(zip(Rng(2), Rng(3))), max(Rng(5)), sorted(Rng(4), reverse=True), tuple(Rng(3)), dict(enumerate(Rng(2))), list(map(str, Rng(3))), "-".join(map(str, Rng(3))))
try: list(Bad())
except TypeError as e: print("TypeError", e)
class Tree:
    def __init__(self, v, *kids): self.v, self.kids = v, kids
    def __iter__(self):
        yield self.v
        for k in self.kids: yield from k
    def depth(self): return 1 + max((k.depth() for k in self.kids), default=0)
    def map(self, f): return Tree(f(self.v), *(k.map(f) for k in self.kids))
t = Tree(1, Tree(2, Tree(4), Tree(5)), Tree(3, Tree(6, Tree(7))))
print(list(t), sum(t), t.depth(), list(t.map(lambda v: v * v)), max(t), len(list(t)), sorted(t, reverse=True)[:3])
# object model details
class K:
    """doc"""
    attr = 1
    def meth(self): return 1
k = K()
print(K.__name__, K.__doc__, K.__dict__["attr"], k.__class__ is K, type(k) is K, K.meth(k), k.meth.__func__ is K.meth, k.meth.__self__ is k, isinstance(K, type), issubclass(K, object), K.__bases__, K.__mro__ == (K, object), k.__dict__, callable(K), callable(k), K.meth.__name__, K.meth.__qualname__, k.meth.__name__, getattr(K, "attr"), hasattr(K, "nope"))
K.attr = 2; K.new_method = lambda self, x: x + self.attr; k.attr = 10; print(K().attr, k.attr, k.new_method(1), K().new_method(1)); del k.attr; print(k.attr); del K.new_method; print(hasattr(k, "new_method"))
m = k.meth; n = K.meth; print(m(), n(k), m == k.meth, m is k.meth, {m: 1}[k.meth] if False else 1)
print(str(k).startswith("<__main__.K object at 0x"), repr(K), str(K.meth).startswith("<function K.meth at 0x"), repr(k.meth).startswith("<bound method K.meth of <__main__.K object at 0x"), repr(len), repr([].append).startswith("<built-in method append of list object at 0x"))
for bad in ["K().nope", "K.nope", "k.meth(1)", "K.meth()", "K(1)", "del k.zzz", "k.meth.x = 1", "(1).x = 1", "[].y = 1", "K.__name__()", "type('X', (object,), {})().q"]:
    try: exec(bad)
    except Exception as e: print(type(e).__name__, "->", e)
