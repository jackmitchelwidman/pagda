import helper_mod
import helper_mod as hm
from helper_mod import VALUE, bump as b, Thing
print(helper_mod.VALUE, hm is helper_mod, VALUE, b(), b(), helper_mod.counter, Thing(), helper_mod.__name__, helper_mod.__doc__, hasattr(helper_mod, "_private"), Thing.__module__)
import pkg, pkg.tools, pkg.sub.leaf
from pkg.tools import double
from pkg.sub import leaf
from pkg.tools import *
import pkg.tools as pt
print(pkg.NAME, pkg.tools.double(4), double(5), leaf.where(), pkg.sub.leaf.where(), pt.bump_twice(), helper_mod.counter, "bump_twice" in dir(), pt.__name__)
import sys
print("helper_mod" in sys.modules, "pkg.tools" in sys.modules, sys.modules["pkg"] is pkg, __name__, type(sys.path).__name__)
def lazy():
    import math as m
    from math import pi
    return m.floor(pi)
print(lazy())
try: import pkg.nope
except ImportError as e: print(type(e).__name__, e)
try: from helper_mod import nope
except ImportError as e: print(type(e).__name__)
import os
path = os.path.join(os.environ.get("TMPDIR", "/tmp"), "pyc_test_%d.txt" % os.getpid())
with open(path, "w") as f:
    print(f.write("line one\n"), f.write("second line\nthird\n"), f.closed)
    print("via print", 1, 2, sep="-", file=f)
    f.writelines(["a\n", "b"])
print(f.closed, os.path.exists(path), os.path.isfile(path), os.path.getsize(path))
with open(path) as f: print(repr(f.read()))
with open(path) as f: print(f.readline(), f.readline().strip(), f.readlines())
with open(path) as f: print([line.rstrip("\n") for line in f])
with open(path) as f: print(repr(f.read(4)), repr(f.read(3)), len(f.read()), repr(f.read()))
with open(path, "a") as f: f.write("\nappended é€")
f = open(path, "r"); lines = f.read().splitlines(); f.close(); print(lines[-1], len(lines), sum(1 for _ in open(path)))
with open(path, "w") as f: f.write("x" * 10000 + "\n" + "y")
with open(path) as f: print(len(f.readline()), f.read(), f.tell())
for bad in ["f.read()", "open(path, 'q')", "open('/nonexistent_dir/x', 'w')", "open(path).write('x')"]:
    try: eval(bad)
    except Exception as e: print(type(e).__name__, e if "nonexistent" not in str(e) else "ok")
os.remove(path); print(os.path.exists(path))
try: os.remove(path)
except FileNotFoundError as e: print("FileNotFoundError", e.errno)
d = path + "_dir"; os.makedirs(d + "/a/b"); open(d + "/a/f.txt", "w").close(); print(sorted(os.listdir(d + "/a")), [(r[len(d):], ds, fs) for r, ds, fs in os.walk(d)])
os.remove(d + "/a/f.txt"); os.rmdir(d + "/a/b"); os.rmdir(d + "/a"); os.rmdir(d); print(os.path.isdir(d))
sys.stdout.write("direct write\n"); print("to stderr", file=sys.stderr); print("a", "b", sep="", end="!\n"); print(); print(end=""); print("x", "y", sep="\n")
print(eval("1 + 2 * 3"), eval("[i for i in range(3)]"), eval("x + 1", {"x": 10}))
ns = {}; exec("def f(n): return n * 2\nresult = f(21)", ns); print(ns["result"], ns["f"](4))
exec("z_global = 99"); print(z_global)
for bad in ["eval('1 +')", "exec('if x')", "eval('a b')", "exec('def f(:\\n pass')", "exec(' x = 1')", "exec('x = (1, 2')", "exec('return 5')", "exec('break')", "exec('f(a=1, a=2)')", "exec('def g(a, a): pass')", "exec('x = 1 +* 2')", "exec('1 = x')", "exec('for i in range(3):\\nprint(i)')", "exec('\"abc')", "exec('nonlocal q')", "exec('class A:\\n  yield 1')"]:
    try: eval(bad)
    except SyntaxError as e: print(type(e).__name__)
print(globals()["VALUE"], "lazy" in globals(), sorted(k for k in locals() if k.startswith("hel")), callable(print), type(print).__name__, len.__name__, print.__name__)
def show_locals(a, b=2):
    c = a + b
    return sorted(locals().items())
print(show_locals(1), vars(helper_mod)["VALUE"], id(helper_mod) == id(hm), isinstance(id(1), int), hash("a") == hash("a"), hash((1, "a")) == hash((1, "a")))
print(repr(input.__name__), ascii("é€"), format(255, "x"), chr(0x1F600) == "\U0001F600", divmod(-7, 2), isinstance(iter([]), object), next(iter("x")), list(zip(*[(1, 2), (3, 4)])), dict(zip("ab", range(2))), sorted(dir(Thing()))[-1], type(...).__name__, slice(1, 5, 2), [0, 1, 2, 3, 4, 5][slice(1, 5, 2)], slice(3).stop)
if __name__ == "__main__": print("main guard ok", __file__.endswith("12_imports_io.py"))
raise SystemExit(3)
