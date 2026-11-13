class Point:
    """A point."""
    count = 0
    def __init__(self, x, y=0):
        self.x, self.y = x, y
        Point.count += 1
    def __repr__(self): return f"Point({self.x}, {self.y})"
    def __eq__(self, o): return isinstance(o, Point) and (self.x, self.y) == (o.x, o.y)
    def __hash__(self): return hash((self.x, self.y))
    def __add__(self, o): return Point(self.x + o.x, self.y + o.y)
    def __mul__(self, k): return Point(self.x * k, self.y * k)
    def __rmul__(self, k): return self * k
    def __neg__(self): return Point(-self.x, -self.y)
    def __lt__(self, o): return (self.x, self.y) < (o.x, o.y)
    def __bool__(self): return bool(self.x or self.y)
    def __iter__(self): yield self.x; yield self.y
    def __getitem__(self, i): return (self.x, self.y)[i]
    def __len__(self): return 2
    def __contains__(self, v): return v in (self.x, self.y)
    def __call__(self, d): return Point(self.x + d, self.y + d)
    def dist2(self): return self.x ** 2 + self.y ** 2
    @property
    def mag(self): return self.dist2() ** 0.5
    @staticmethod
    def origin(): return Point(0, 0)
    @classmethod
    def diag(cls, v): return cls(v, v)
p, q = Point(1, 2), Point(3, 4)
print(p, q, p + q, p * 3, 2 * p, -p, p == Point(1, 2), p != q, p < q, sorted([q, p]), bool(Point(0)), list(p), p[1], len(p), 2 in p, p(10))
print(p.mag, Point.origin(), Point.diag(5), Point.count, p.count, Point.__name__, Point.__doc__, type(p).__name__, {p: "found"}[Point(1, 2)], max(p, q))
x, y = p; print(x, y, p.dist2(), Point.dist2(q), isinstance(p, Point), isinstance(p, object), hasattr(p, "x"), hasattr(p, "nope"), getattr(p, "y"), getattr(p, "z", "dflt"))
setattr(p, "z", 9); print(p.z, vars(p), p.__dict__ is vars(p)); del p.z; print(hasattr(p, "z"), sorted(k for k in dir(p) if not k.startswith("_")))
class Animal:
    sound = "..."
    def __init__(self, name): self.name = name
    def speak(self): return f"{self.name} says {self.sound}"
    def __str__(self): return f"<{type(self).__name__} {self.name}>"
class Dog(Animal):
    sound = "woof"
    def __init__(self, name, tricks=()):
        super().__init__(name)
        self.tricks = list(tricks)
    def speak(self): return super().speak() + "!"
class Puppy(Dog):
    def speak(self): return Animal.speak(self).upper()
for a in (Animal("cat"), Dog("rex", ["sit"]), Puppy("bit")): print(a, a.speak(), str(a), repr(a).startswith("<__main__."))
print(Dog.__bases__ == (Animal,), [c.__name__ for c in Puppy.__mro__], issubclass(Puppy, Animal), isinstance(Puppy("x"), Dog), Dog.__mro__[-1] is object)
class A:
    def who(self): return "A"
    def chain(self): return ["A"]
class B(A):
    def who(self): return "B"
    def chain(self): return ["B"] + super().chain()
class C(A):
    def who(self): return "C"
    def chain(self): return ["C"] + super().chain()
class D(B, C):
    def chain(self): return ["D"] + super().chain()
print(D().who(), D().chain(), [k.__name__ for k in D.mro()])
class Temp:
    def __init__(self): self._c = 0
    @property
    def c(self): return self._c
    @c.setter
    def c(self, v):
        if v < -273: raise ValueError("too cold")
        self._c = v
    @c.deleter
    def c(self): self._c = None
    @property
    def f(self): return self._c * 9 / 5 + 32
t = Temp(); t.c = 100; print(t.c, t.f); del t.c; print(t.c)
try: t.c = -300
except ValueError as e: print("ValueError", e)
try: t.f = 1
except AttributeError as e: print("AttributeError", e)
class Dyn:
    def __getattr__(self, name):
        if name.startswith("x_"): return name[2:]
        raise AttributeError(name)
    def __setattr__(self, name, v): object.__setattr__(self, name, v * 2)
d = Dyn(); d.a = 5; print(d.a, d.x_hello, hasattr(d, "nope"))
class Stack:
    def __init__(self): self.items = []
    def push(self, v): self.items.append(v); return self
    def __len__(self): return len(self.items)
    def __getitem__(self, i): return self.items[i]
    def __setitem__(self, i, v): self.items[i] = v
    def __delitem__(self, i): del self.items[i]
    def __eq__(self, o): return self.items == o.items
s = Stack().push(1).push(2).push(3); s[0] = 10; del s[1]
print(len(s), s[0], s[-1], list(s), [v for v in s], 3 in s, bool(Stack()), s == s, s[0:2], list(reversed(s)))
try: hash(s)
except TypeError as e: print("TypeError", e)
class Num:
    def __init__(self, v): self.v = v
    def __repr__(self): return f"Num({self.v})"
    def __add__(self, o): return Num(self.v + (o.v if isinstance(o, Num) else o))
    __radd__ = __add__
    def __iadd__(self, o): self.v += 100; return self
    def __sub__(self, o): return NotImplemented
    def __eq__(self, o): return NotImplemented
    def __int__(self): return self.v
    def __float__(self): return float(self.v)
    def __index__(self): return self.v
    def __format__(self, spec): return f"N[{self.v:{spec}}]"
    def __abs__(self): return Num(abs(self.v))
    def __pow__(self, o): return Num(self.v ** o)
    def __truediv__(self, o): return "div"
    def __floordiv__(self, o): return "floordiv"
    def __mod__(self, o): return "mod"
    def __and__(self, o): return "and"
    def __invert__(self): return "inv"
    def __le__(self, o): return True
n = Num(5); m = n; m += 1
print(n + 1, 1 + n, sum([Num(1), Num(2)]), n, n == Num(5), n != n, int(n), float(n), [0, 1, 2, 3, 4, 5, 6][Num(2)], f"{Num(7):>4}", abs(Num(-3)), n ** 2, n / 1, n // 1, n % 1, n & 1, ~n, n <= 1, 5 >= n)
try: n - 1
except TypeError as e: print("TypeError", e)
try: n < n
except TypeError as e: print("TypeError", e)
class Ctx:
    def __init__(self, swallow): self.swallow = swallow
    def __enter__(self): print("enter"); return self
    def __exit__(self, et, ev, tb): print("exit", et.__name__ if et else None, ev); return self.swallow
with Ctx(False) as c: print("body", c.swallow)
with Ctx(True): raise KeyError("swallowed")
try:
    with Ctx(False): raise KeyError("propagated")
except KeyError as e: print("caught", e)
with Ctx(False) as a, Ctx(True) as b: print("two")
def early():
    with Ctx(False): return "returned"
print(early())
for i in range(2):
    with Ctx(False):
        if i == 0: continue
        break
class Single:
    _inst = None
    def __new__(cls, *a):
        if cls._inst is None: cls._inst = super().__new__(cls)
        return cls._inst
    def __init__(self, v=0): self.v = v
print(Single(1) is Single(2), Single().v)
class Outer:
    class Inner:
        val = 5
    def get(self): return self.Inner.val
    lst = [i * 2 for i in range(3)]
    total = sum(lst)
print(Outer().get(), Outer.Inner.__qualname__, Outer.lst, Outer.total, Outer.get.__qualname__)
def make_class(n):
    class Local:
        value = n
        def get(self): return n * 2
    return Local
print(make_class(4).value, make_class(5)().get(), type("Dynamic", (), {"v": 3}).v)
class Meta: pass
m1 = Meta(); m1.fn = lambda: "instance fn"; print(m1.fn(), Meta.__module__, m1.__class__ is Meta, type(m1) is Meta)
bound = [].append; print(bound.__name__, Point(1, 2).dist2.__self__, Point.dist2.__name__, str.upper("abc"), list.append is list.append)
for bad in ["Point()", "Point(1, 2, 3)", "Point(1).nope", "Point.nope", "Animal('a').speak(1)", "object().x", "Meta(1)", "p + 1", "Stack() < Stack()", "len(Meta())", "Meta()[0]", "for i in Meta(): pass", "Meta()()", "class Z(bool): pass", "class Y(type(None)): pass"]:
    try:
        exec(bad)
    except Exception as e:
        print(type(e).__name__, "->", e)
