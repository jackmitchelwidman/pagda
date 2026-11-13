# match statement, type parameters, function annotations
from __future__ import annotations


class Point:
    __match_args__ = ("x", "y")

    def __init__(self, x, y):
        self.x = x
        self.y = y


class Circle:
    __match_args__ = ("center", "r")

    def __init__(self, center, r):
        self.center = center
        self.r = r


def describe(v):
    match v:
        case 0:
            return "zero"
        case 1 | 2 | 3:
            return "small"
        case -1:
            return "minus one"
        case int(n) if n > 100:
            return f"big {n}"
        case int():
            return "int"
        case "hi" | "hello":
            return "greeting"
        case str(s):
            return "str " + s
        case None:
            return "none"
        case True:
            return "true"
        case []:
            return "empty list"
        case [x]:
            return f"one {x}"
        case [x, y]:
            return f"two {x} {y}"
        case [first, *rest]:
            return f"many {first} {rest}"
        case {"kind": "a", "v": v2}:
            return f"a {v2}"
        case {"kind": k, **others}:
            return f"kind {k} {others}"
        case {}:
            return "dict"
        case Point(0, 0):
            return "origin"
        case Point(x, y) if x == y:
            return f"diag {x}"
        case Point(x=px, y=py):
            return f"point {px} {py}"
        case Circle(Point(x, y) as c, r):
            return f"circle {x} {y} {r} {type(c).__name__}"
        case float() as f:
            return f"float {f}"
        case _:
            return "other"


for v in [0, 2, -1, 500, 7, "hi", "x", None, True, [], [1], [1, 2], [1, 2, 3], (4, 5), range(3),
          {"kind": "a", "v": 9}, {"kind": "b", "z": 1}, {"q": 1}, Point(0, 0), Point(3, 3), Point(1, 2),
          Circle(Point(1, 2), 5), 2.5, object, "hello", False, {1, 2}]:
    print(describe(v))


class Color:
    RED = 1
    GREEN = 2


def color(c):
    match c:
        case Color.RED:
            return "red"
        case Color.GREEN:
            return "green"
    return "none"


print(color(1), color(2), color(3))

# open sequence patterns, nested, star in the middle
match 1, 2, 3, 4:
    case a, *mid, d:
        print(a, mid, d)
match [1, [2, 3], {"k": (4, 5)}]:
    case [a, [b, c], {"k": (d, e)}]:
        print(a, b, c, d, e)
match (1, 2):
    case (x, y) | [x, y, _]:
        print("or", x, y)
match "abc":
    case [a, b, c]:
        print("str is a sequence?")
    case _:
        print("str is not a sequence")
match 5:
    case 1:
        pass
print("no case matched, fine")

# match and case stay usable as names
match = 3
case = 4
print(match + case)
match = [1, 2]
match[0] = 9
print(match)

# errors
try:
    match Point(1, 2):
        case Point(a, b, c):
            pass
except TypeError as e:
    print(e)
try:
    match 1:
        case int(a, b):
            pass
except TypeError as e:
    print(e)
try:
    match 1:
        case Color(a):
            pass
except TypeError as e:
    print(e)

# match inside loops and try blocks
out = []
for i in range(6):
    try:
        match i % 3:
            case 0:
                continue
            case 1:
                out.append(i)
            case _:
                if i > 4:
                    break
                out.append(-i)
    finally:
        out.append("f")
print(out)


# type parameters
def ident[T](x: T) -> T:
    return x


def pair[A, B: int, *Ts, **P](a: A, b: B = 2, *, k: A = 3) -> tuple[A, B]:
    return (a, b, k)


class Box[T]:
    def __init__(self, v: T):
        self.v = v

    def get[U](self, default: U = None) -> T | U:
        return self.v if self.v is not None else default


class Sub[T](Box[T]):
    def get(self, default=None):
        return super().get(default)


print(ident(3), pair(1), pair(1, 5, k=6), Box(4).get(), Box(None).get(7), Sub(8).get())
print(ident.__name__, ident.__qualname__, Box.get.__qualname__, Box.__name__)
print(ident.__type_params__, pair.__type_params__, Box.__type_params__)
print(ident.__annotations__, pair.__annotations__)
print(Box.__init__.__annotations__, Box.get.__annotations__)


def deco(f):
    f.tag = "decorated"
    return f


@deco
def g[T](x: list[T], *args: T, **kw: "str") -> None:
    pass


print(g.tag, g.__annotations__)


def h(a: Vec[A, n + 1], b: lambda x, *y, z=1, **k: (x, y), c: a if b else not c, d: x[1:2, ::3], e: -x ** 2 ** y,
      f: (a + b) * c - d / e // f % g, i: [1, "s", 2.5, None, ...], j: {"a": 1, **b}, k: f(x, *y, z=1, **w),
      m: a < b <= c != d is not e in f, n: a or b and not c, o: (a, b), p: (a,), q: x.y.z, r: {1, 2},
      s: [i for i in x if i], t: {k: v for k, v in x}, u: 1 if a else 2 if b else 3, v: (a or b) and c,
      w: a @ b << c >> d & e | f ^ g, x: ~a + +b - -c, y: (-1) ** 2, z: a[b, c]) -> int | None:
    pass


for k, v in h.__annotations__.items():
    print(k, repr(v))


class K:
    x: int
    y: list[str] = []


print(K.__annotations__)


# a generic function that calls itself goes through its decorator each time
calls = []


def count_calls(f):
    def wrapper(n):
        calls.append(n)
        return f(n)
    return wrapper


@count_calls
def fact[T](n: T) -> T:
    return 1 if n <= 1 else n * fact(n - 1)


print(fact(4), calls)


class Base:
    def name(self):
        return "base"


class Derived[T](Base):
    def name[U](self, extra: U = "!"):
        return "derived+" + super().name() + extra

    def make(self):
        return Derived()


print(Derived().name(), Derived().name("?"), type(Derived().make()).__name__, Derived.__qualname__, Derived.name.__qualname__)
