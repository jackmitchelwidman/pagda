import math, sys, time
from math import sqrt, pi, floor as fl
print(math.sqrt(16), sqrt(2), round(pi, 5), fl(3.7), math.ceil(3.2), math.floor(-3.5), math.trunc(-3.7), math.gcd(12, 18), math.gcd(2 ** 100, 6 ** 50) == 2 ** 50, math.lcm(4, 6), math.factorial(20), math.factorial(25), math.comb(10, 3), math.perm(5, 2), math.isqrt(10 ** 30), math.isqrt(99))
print(math.sin(0), math.cos(0), round(math.atan2(1, 1), 6), math.exp(0), math.log(math.e), math.log(8, 2), math.log2(1024), math.log10(1000), math.hypot(3, 4), math.degrees(math.pi), math.fabs(-2), math.pow(2, 10), math.inf > 1e308, math.isnan(math.nan), math.isinf(-math.inf), math.isclose(0.1 + 0.2, 0.3), math.copysign(3, -1), math.fmod(7, 3), math.fsum([0.1] * 10), math.prod([1, 2, 3, 4]), math.modf(3.5), math.frexp(8.0), math.dist((0, 0), (3, 4)), round(math.log(2 ** 200), 6), math.tau == 2 * math.pi)
for bad in ["math.sqrt(-1)", "math.log(0)", "math.factorial(-1)", "math.sqrt('x')", "math.exp(1000)"]:
    try: eval(bad)
    except Exception as e: print(type(e).__name__, e)
print(type(sys.argv), sys.maxsize, sys.version_info[0], isinstance(time.time(), float), time.perf_counter() <= time.perf_counter(), sys.getrecursionlimit())
import random
random.seed(42); print(random.random(), random.randint(1, 100), random.randrange(10), random.choice("abcdef"), random.uniform(1, 2), random.randrange(0, 100, 5), random.getrandbits(40), random.getrandbits(100))
l = list(range(10)); random.shuffle(l); print(l, random.sample(range(100), 5), random.sample(l, 3), random.choices("abc", k=5), random.choices([1, 2, 3], weights=[1, 1, 10], k=6), random.gauss(0, 1), random.normalvariate(5, 2), random.randint(10 ** 20, 10 ** 21))
random.seed(2 ** 70 + 5); print(random.random(), random.randrange(10 ** 30))
from collections import defaultdict, Counter, deque, OrderedDict, namedtuple
dd = defaultdict(list)
for k, v in [("a", 1), ("b", 2), ("a", 3)]: dd[k].append(v)
print(dd, dict(dd), dd["zz"], len(dd), defaultdict(int)["q"], isinstance(dd, dict), sorted(dd.items()))
c = Counter("abracadabra"); print(c, c.most_common(2), c["a"], c["zz"], sum(c.values()), sorted(c.elements())[:4], c + Counter("aab"), c - Counter("aaaaab"), Counter([1, 1, 2]) == {1: 2, 2: 1}, Counter(a=3), c.total())
c.update("aaa"); c.subtract("b"); print(c["a"], c["b"], len(c), Counter())
d = deque([1, 2, 3], maxlen=5); d.append(4); d.appendleft(0); d.append(5); print(d, d.pop(), d.popleft(), list(d), len(d), d[0], d[-1], 3 in d)
d = deque(range(5)); d.rotate(2); print(d, list(reversed(d))); d.rotate(-1); d.extend([9, 8]); d.extendleft([7]); print(d, bool(deque()), d.count(9), d.index(8))
q = deque()
for i in range(1000): q.append(i)
print(sum(q.popleft() for _ in range(500)), len(q), q[0])
od = OrderedDict(a=1, b=2, c=3); od.move_to_end("a"); print(od, list(od), od.popitem(), od.popitem(last=False), od)
P = namedtuple("P", "x y"); p = P(1, y=2); x, y = p
print(p, p.x, p.y, p[0], x, y, p._asdict(), p._replace(x=9), P._fields, p == (1, 2), isinstance(p, tuple), len(p), P._make([5, 6]), namedtuple("Q", ["a", "b"], defaults=[0])(1), hash(p) == hash((1, 2)))
from functools import reduce, partial, lru_cache, wraps, cache, cmp_to_key, total_ordering, cached_property
print(reduce(lambda a, b: a * b, range(1, 6)), reduce(lambda a, b: a + b, [], 10), partial(pow, 2)(10), partial(int, base=2)("101"), sorted([3, 1, 2], key=cmp_to_key(lambda a, b: b - a)))
@lru_cache(maxsize=None)
def fib(n): return n if n < 2 else fib(n - 1) + fib(n - 2)
print(fib(80), fib.__name__, fib.cache_info())
@cache
def sq(x): print("computing", x); return x * x
print(sq(4), sq(4))
def logged(fn):
    @wraps(fn)
    def w(*a, **k): return fn(*a, **k)
    return w
@logged
def documented():
    "the doc"
print(documented.__name__, documented.__doc__, documented.__wrapped__.__name__)
@total_ordering
class V:
    def __init__(self, v): self.v = v
    def __eq__(self, o): return self.v == o.v
    def __lt__(self, o): return self.v < o.v
print(V(1) < V(2), V(1) <= V(1), V(3) > V(2), V(3) >= V(4), max(V(1), V(5)).v)
class Lazy:
    @cached_property
    def val(self): print("once"); return 42
z = Lazy(); print(z.val, z.val)
import itertools as it
print(list(it.islice(it.count(5, 2), 4)), list(it.islice("abcdefg", 1, 6, 2)), list(it.chain([1], (2, 3), "ab")), list(it.chain.from_iterable([[1], [2, 3]])), list(it.islice(it.cycle("ab"), 5)), list(it.repeat("x", 3)), list(it.accumulate([1, 2, 3, 4])), list(it.accumulate([1, 2, 3], lambda a, b: a * b, initial=10)))
print(list(it.product("ab", [1, 2])), list(it.product([0, 1], repeat=2)), list(it.permutations([1, 2, 3], 2)), len(list(it.permutations(range(5)))), list(it.combinations("abcd", 2)), list(it.combinations_with_replacement("ab", 2)), list(it.zip_longest("ab", [1, 2, 3], fillvalue="-")))
print([(k, list(g)) for k, g in it.groupby("aaabbcaa")], [(k, len(list(g))) for k, g in it.groupby(sorted(["bob", "al", "amy", "bo"]), key=lambda s: s[0])], list(it.takewhile(lambda v: v < 3, [1, 2, 5, 1])), list(it.dropwhile(lambda v: v < 3, [1, 2, 5, 1])), list(it.starmap(pow, [(2, 3), (3, 2)])), list(it.pairwise("abcd")), list(it.batched(range(7), 3)), list(it.compress("abcd", [1, 0, 1, 0])), list(it.filterfalse(lambda v: v % 2, range(6))))
import heapq, bisect, operator, string, copy
h = [5, 3, 8, 1, 9, 2]; heapq.heapify(h); heapq.heappush(h, 0); print([heapq.heappop(h) for _ in range(4)], h[0], heapq.nlargest(2, [4, 1, 7, 3]), heapq.nsmallest(2, [4, 1, 7, 3]), heapq.heappushpop(h, 6), heapq.heapreplace(h, 1))
a = [1, 3, 3, 5]; bisect.insort(a, 4); print(bisect.bisect_left(a, 3), bisect.bisect_right(a, 3), bisect.bisect(a, 10), a)
print(operator.add(1, 2), operator.itemgetter(1)("abc"), operator.itemgetter(0, 2)("abc"), sorted([(1, "b"), (0, "a")], key=operator.itemgetter(1)), operator.attrgetter("real")(5), operator.methodcaller("upper")("a"), reduce(operator.mul, [1, 2, 3, 4]), string.ascii_lowercase[:5], string.digits, string.capwords("hello world"))
orig = {"a": [1, [2, 3]], "b": (4, {5})}; sh = copy.copy(orig); dp = copy.deepcopy(orig); orig["a"][1].append(99)
print(sh["a"][1], dp["a"][1], dp == {"a": [1, [2, 3]], "b": (4, {5})}, sh is not orig)
class Node:
    def __init__(self, v): self.v = v; self.kids = []
n = Node(1); n.kids.append(Node(2)); n.self = n; m = copy.deepcopy(n); print(m.v, m.kids[0].v, m.self is m, m.kids[0] is not n.kids[0], copy.copy(n).kids is n.kids)
import json
data = {"name": "Zoë", "n": [1, 2.5, -3e10, True, None], "nested": {"k": "v\n\"q\"", "empty": [], "e": {}}, "t": (1, 2), 5: "five"}
s = json.dumps(data); print(s); print(json.loads(s) == {"name": "Zoë", "n": [1, 2.5, -3e10, True, None], "nested": {"k": "v\n\"q\"", "empty": [], "e": {}}, "t": [1, 2], "5": "five"})
print(json.dumps(data, indent=2, sort_keys=False, ensure_ascii=False)); print(json.dumps({"b": 1, "a": [1, {"z": 0, "y": 1}]}, sort_keys=True, separators=(",", ":")), json.dumps("é\U0001F600\x01"), json.dumps([float("inf"), 1e100, 0.1]))
print(json.loads(' {"a": [1, 2, {"b": null}], "c": "\\u00e9\\ud83d\\ude00\\n", "d": -1.5e3, "e": true} '), json.loads("[]"), json.loads('"x"'), json.loads("3"), json.loads("1E2"))
for bad in ['', '{"a": }', '[1, 2', '{"a" 1}', "{'a': 1}", '[1,]', 'nul', '"abc', '[1] x', '01']:
    try: json.loads(bad)
    except json.JSONDecodeError as e: print("JSONDecodeError:", e)
try: json.dumps({"s": {1, 2}})
except TypeError as e: print("TypeError:", e)
from dataclasses import dataclass, field, asdict, astuple, replace, fields
@dataclass
class Item:
    name: str
    price: float = 0.0
    tags: list = field(default_factory=list)
    def total(self, qty): return self.price * qty
i1 = Item("pen", 1.5); i2 = Item("pen", 1.5); i1.tags.append("x")
print(i1, i2, i1 == i2, i1 == Item("pen", 1.5, ["x"]), i1.total(3), asdict(i1), astuple(i2), replace(i2, price=2), [f.name for f in fields(Item)], Item(name="k", tags=[1]))
@dataclass(frozen=True, order=True)
class Ver:
    major: int
    minor: int = 0
print(Ver(1, 2) < Ver(1, 3), sorted([Ver(2), Ver(1, 5), Ver(1)]), {Ver(1): "a"}[Ver(1, 0)], Ver(3) >= Ver(3))
try: Ver(1).major = 5
except AttributeError as e: print(type(e).__name__, e)
@dataclass
class Sub(Item):
    extra: int = 7
    def __post_init__(self): self.name = self.name.upper()
print(Sub("a"), Sub("b", 2.0, [], 9).extra)
for bad in ["Item()", "Item(1, 2, 3, 4)", "Item('a', nope=1)"]:
    try: eval(bad)
    except TypeError as e: print("TypeError:", e)
from abc import ABC, abstractmethod
class Shape(ABC):
    @abstractmethod
    def area(self): ...
    def describe(self): return f"{type(self).__name__} with area {self.area()}"
class Sq(Shape):
    def __init__(self, s): self.s = s
    def area(self): return self.s ** 2
print(Sq(3).describe(), isinstance(Sq(1), Shape))
try: Shape()
except TypeError as e: print("TypeError:", e)
from typing import List, Optional, Dict, Any, TypeVar, Generic, Callable, Union
T = TypeVar("T")
class Box(Generic[T]):
    def __init__(self, v: T) -> None: self.v = v
    def get(self) -> Optional[T]: return self.v
def typed(a: List[int], b: Dict[str, Any] = None, c: Union[int, str] = 0, d: Callable[[int], int] = abs) -> Optional[int]: return d(sum(a))
print(Box[int](5).get(), typed([1, -5]), Box.__name__)
import os, os.path
print(os.path.join("a", "b", "c.txt"), os.path.basename("/x/y/z.py"), os.path.dirname("/x/y/z.py"), os.path.splitext("a/b.tar.gz"), os.path.split("/a/b"), os.path.exists("/nonexistent"), os.path.isdir("/tmp"), os.sep, os.path.normpath("a/./b/../c"), os.path.isabs("/a"), type(os.getcwd()).__name__, os.getenv("NOPE_XYZ", "dflt"), "PATH" in os.environ)
