class Stack(list):
    def push(self, v): self.append(v); return self
    def peek(self): return self[-1]
s = Stack([1, 2]); s.push(3).push(4); s.name = "st"
print(s, len(s), s.peek(), s.pop(), s[0], s[1:], isinstance(s, list), type(s).__name__, s.name, s == [1, 2, 3], sorted(s, reverse=True), s + [9], bool(Stack()), [x * 2 for x in s], 2 in s, type(s + [1]).__name__)
class Named(list):
    def __init__(self, name, items=()):
        super().__init__(items)
        self.name = name
    def __repr__(self): return f"Named({self.name!r}, {list(self)!r})"
    def __getitem__(self, i): return super().__getitem__(i) * 10
    def __len__(self): return 99
    def __contains__(self, v): return v == "magic"
    def __eq__(self, o): return isinstance(o, Named) and self.name == o.name
n = Named("n", [1, 2, 3]); print(n, n[0], len(n), "magic" in n, 1 in n, n == Named("n"), n != Named("m"), list(n), str(n), [n], list.__len__(n), list.__getitem__(n, 1))
class Config(dict):
    def __getattr__(self, k):
        try: return self[k]
        except KeyError: raise AttributeError(k)
    def __setitem__(self, k, v): super().__setitem__(k.lower(), v)
    def __missing__(self, k): return f"<no {k}>"
c = Config(a=1); c["B"] = 2; c.update({"C": 3})
print(c, c.a, c.b, c["zzz"], c.get("zzz"), sorted(c), len(c), "b" in c, isinstance(c, dict), dict(c) == {"a": 1, "b": 2, "C": 3}, hasattr(c, "q"), list(c.items()))
class DefaultDict(dict):
    def __init__(self, factory, *a, **k):
        super().__init__(*a, **k)
        self.factory = factory
    def __missing__(self, key):
        v = self[key] = self.factory()
        return v
dd = DefaultDict(list, x=[0]); dd["a"].append(1); dd["a"].append(2); dd["b"]; print(dd, dd.factory is list, len(dd))
cnt = DefaultDict(int)
for ch in "abracadabra": cnt[ch] += 1
print(sorted(cnt.items()))
class Celsius(float):
    def __new__(cls, v): return super().__new__(cls, round(v, 1))
    def __repr__(self): return f"{float(self)}C"
    @property
    def f(self): return self * 9 / 5 + 32
t = Celsius(36.66); print(t, t.f, t + 1, t > 30, isinstance(t, float), f"{t:.2f}", float(t), [t], type(t + 1).__name__, hash(t) == hash(36.7), max(t, 50.0))
class MyInt(int):
    def __add__(self, o): return MyInt(int(self) + int(o) + 1000)
    def __str__(self): return f"<{int(self)}>"
    def double(self): return self * 2
m = MyInt(5); print(m, repr(m), m + 1, 1 + m, m * 2, m.double(), m == 5, {5: "five"}[m], str(m), range(m)[-1], [0] * m, "%d" % m, f"{m}", int("7") + m, m > 3, -m, abs(MyInt(-3)), isinstance(m, int), m.bit_length())
class Upper(str):
    def __new__(cls, s): return super().__new__(cls, s.upper())
    def shout(self): return self + "!"
    def __eq__(self, o): return str(self).lower() == str(o).lower()
    def __hash__(self): return hash(str(self).lower())
u = Upper("hello"); print(u, u.shout(), len(u), u[1:3], u == "HeLLo", u.lower(), isinstance(u, str), u + "x", "x" + u, u * 2, u.startswith("HE"), {u: 1}[Upper("HELLO")], repr(u), f"{u:>8}", "-".join([u, u]), u in "say HELLO", type(u.lower()).__name__)
class Pt(tuple):
    def __new__(cls, x, y): return super().__new__(cls, (x, y))
    @property
    def x(self): return self[0]
    @property
    def y(self): return self[1]
    def __add__(self, o): return Pt(self.x + o.x, self.y + o.y)
    def __repr__(self): return f"Pt({self.x}, {self.y})"
p = Pt(1, 2); a, b = p; print(p, p.x, p.y, p + Pt(10, 20), a, b, len(p), p == (1, 2), hash(p) == hash((1, 2)), isinstance(p, tuple), tuple(p), p[::-1], {p: "pt"}[(1, 2)], sorted([Pt(2, 1), Pt(1, 5)]))
class OrderedSet(set):
    def __repr__(self): return "OS" + repr(sorted(self))
o = OrderedSet([3, 1, 2]); o.add(0); o.tag = "t"; print(o, len(o), 3 in o, o | {9} == {0, 1, 2, 3, 9}, o.tag, isinstance(o, set), sorted(o & {1, 2, 7}))
class Base(dict): pass
class Derived(Base):
    def keys(self): return sorted(super().keys())
d = Derived(z=1, a=2); print(d.keys(), type(d).__mro__[1].__name__, isinstance(d, Base), d == {"z": 1, "a": 2}, Derived.__bases__[0] is Base)
print((5).__add__(3), (5).__radd__(3), "abc".__len__(), [1, 2].__contains__(2), (1.5).__mul__(2), [3, 1].__getitem__(0), {"a": 1}.__getitem__("a"), (1).__eq__(1), (1).__lt__("a"), "a".__add__("b"), int.__add__(1, 2), sorted(["bb", "a"], key=str.__len__))
class Counter2(dict):
    def __init__(self, it=()):
        super().__init__()
        for x in it: self[x] = self.get(x, 0) + 1
    def most_common(self, n): return sorted(self.items(), key=lambda kv: (-kv[1], kv[0]))[:n]
print(Counter2("mississippi").most_common(2), Counter2())
for bad in ["class A(list, dict): pass", "class B(bool): pass", "Named()", "Pt(1)", "MyInt('x')"]:
    try: exec(bad)
    except Exception as e: print(type(e).__name__, "->", e)
