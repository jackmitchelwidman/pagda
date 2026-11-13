import os, sys
base = os.path.join(os.environ.get("TMPDIR", "/tmp"), "pyc_t16_%d" % os.getpid())
# print follows sys.stdout
class Collector:
    def __init__(self): self.parts = []
    def write(self, s): self.parts.append(s); return len(s)
    def flush(self): pass
col = Collector(); old = sys.stdout; sys.stdout = col
print("captured", 1, sep="|"); print("second", end="")
sys.stdout = old
print(col.parts, sys.stdout is sys.__stdout__)
with open(base + ".log", "w") as f:
    sys.stdout = f
    print("to file"); print("again", 2)
    sys.stdout = sys.__stdout__
print(open(base + ".log").read().splitlines())
# encodings, error handlers, newline handling
with open(base, "w", encoding="utf-8") as f: f.write("héllo €\n")
print(repr(open(base, encoding="UTF8").read()), repr(open(base, encoding="latin-1").read()), repr(open(base, encoding="ascii", errors="replace").read()), repr(open(base, encoding="ascii", errors="ignore").read()))
try: open(base, encoding="ascii").read()
except UnicodeDecodeError as e: print("UnicodeDecodeError", isinstance(e, UnicodeError), isinstance(e, ValueError))
with open(base, "w", encoding="latin-1") as f: f.write("café")
print(os.path.getsize(base), repr(open(base, encoding="latin-1").read()), repr(open(base, errors="replace").read()), repr(open(base, errors="ignore").read()))
try: open(base).read()
except UnicodeDecodeError as e: print("UnicodeDecodeError again")
for enc, text in [("ascii", "é"), ("latin-1", "€")]:
    try:
        with open(base, "w", encoding=enc) as f: f.write(text)
    except UnicodeEncodeError as e: print("UnicodeEncodeError", enc)
for bad in [dict(encoding="klingon"), dict(errors="nope"), dict(buffering=0), dict(newline="x")]:
    try: open(base, **bad)
    except (LookupError, ValueError) as e: print(type(e).__name__)
with open(base, "w", newline="") as f: f.write("a\r\nb\rc\nd")
print(repr(open(base).read()), repr(open(base, newline="").read()), open(base).readlines(), [l for l in open(base, newline="\n")], repr(open(base).read(3)), len(open(base).readline()))
with open(base, "w", newline="\r\n") as f: f.write("x\ny\n")
print(repr(open(base, newline="").read()), open(base, "r", -1, "utf-8", "strict", None).read() == "x\ny\n", open(base, buffering=1).readline(), sep="|")
os.remove(base); os.remove(base + ".log")
# zip(strict=True)
print(list(zip("ab", [1, 2], strict=True)), list(zip(strict=True)), list(zip("abc", "xy")))
for args in [("ab", "x"), ("a", "xy"), ("ab", "xy", "z"), ("a", "x", "yz")]:
    try: list(zip(*args, strict=True))
    except ValueError as e: print("ValueError", e)
# exception notes, cause checks
e = ValueError("base"); e.add_note("note one"); e.add_note("note two"); print(e.__notes__, str(e))
for bad in ["e.add_note(5)", "setattr(e, '__cause__', 'x')", "setattr(e, '__context__', 1)"]:
    try: eval(bad)
    except TypeError as err: print("TypeError", err)
e.__cause__ = KeyError("k"); e.__context__ = None; print(repr(e.__cause__), e.__context__)
# methods called through the type check their self argument
for bad in ["str.upper(1)", "list.append({}, 1)", "dict.get([], 0)", "sorted(['b', 3], key=str.lower)", "set.add([], 1)", "list.__len__('abc')", "int.bit_length('x')", "str.join(1, [])", "tuple.count([1], 1)"]:
    try: eval(bad)
    except TypeError as err: print("TypeError", err)
print(str.upper("ok"), list.pop([1, 2]), dict.get({1: 2}, 1), sorted(["b", "A"], key=str.lower), list(map(str.strip, [" a ", "b "])), int.bit_length(255), set.union({1}, [2]) == {1, 2}, str.join("-", ["a", "b"]), dict.fromkeys("ab", 0), list.__len__([1, 2]), dict.items({1: 2}))
# functools / dataclasses options
from functools import lru_cache
@lru_cache(maxsize=2, typed=True)
def typed(x): return type(x).__name__
print(typed(1), typed(1.0), typed(1), typed.cache_info(), typed.cache_info().hits, typed.cache_info().currsize, tuple(typed.cache_info()))
typed("a"); typed("b"); print(typed.cache_info().currsize, typed.cache_info().maxsize); typed.cache_clear(); print(typed.cache_info())
from dataclasses import dataclass, field
@dataclass(kw_only=True)
class Opts:
    name: str
    level: int = 1
print(Opts(name="n"), Opts(level=3, name="m").level)
for bad in ["Opts('n')", "Opts()", "dataclass(order=True, eq=False)(Opts)"]:
    try: eval(bad)
    except (TypeError, ValueError) as err: print(type(err).__name__, err)
@dataclass(unsafe_hash=True)
class Key:
    a: int
    b: str = field(default="x", compare=False)
    c: list = field(default_factory=list, hash=False, repr=False)
    d: int = field(default=0, kw_only=True)
print(hash(Key(1)) == hash(Key(1, "other")), Key(1) == Key(1, "zzz"), {Key(2): "v"}[Key(2, "q")], Key(1, "y", [1], d=5), Key.__match_args__, Key(3).c)
@dataclass
class NoHash:
    v: int
try: hash(NoHash(1))
except TypeError as err: print("TypeError", err)
@dataclass(eq=False)
class ById:
    v: int
print(ById(1) == ById(1), isinstance(hash(ById(1)), int))
d = base + "_d"; os.makedirs(d + "/x/y", exist_ok=True); os.makedirs(d + "/x/y", exist_ok=True); os.mkdir(d + "/z", 0o755)
try: os.makedirs(d + "/x/y")
except FileExistsError: print("FileExistsError")
print(sorted(os.listdir(d))); os.rmdir(d + "/x/y"); os.rmdir(d + "/x"); os.rmdir(d + "/z"); os.rmdir(d)
print(eval("a + 1", {"a": 1}), eval("b", None), type(exec("pass", {}, None)).__name__) if (b := 5) else None
# union types, lazy library iterators
import heapq, itertools
print(isinstance(1, int | str), isinstance(None, int | None), isinstance(2.5, int | str | None), issubclass(bool, int | str), int | str, int | None, (int | str) | float, None | int, int | int)
@dataclass
class Cfg:
    name: str | None = None
    retries: int | float = 3
    tags: list[str] | None = None
print(Cfg(), Cfg("n", 1.5, ["a"]))
print(list(heapq.merge([1, 4, 7], [2, 5], [3, 6, 8])), list(heapq.merge([7, 4, 1], [5, 2], reverse=True)), list(heapq.merge(["bb", "dddd"], ["a", "ccc"], key=len)), list(itertools.islice(heapq.merge(itertools.count(0, 2), itertools.count(1, 2)), 6)))
a, b = itertools.tee(itertools.count()); print(next(a), next(a), next(b), next(a), next(b), next(b), [next(x) for x in itertools.tee("xy", 3)])
print("Straße".casefold(), "ÀB".casefold(), "\x82\xad​".isprintable(), repr("\x82\xad"), repr("​é"))
