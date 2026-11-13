for i in range(3):
    if i == 0: print("zero")
    elif i == 1: print("one")
    else: print("other")
i = 0
while True:
    i += 1
    if i % 2: continue
    if i > 6: break
    print(i, end=" ")
else:
    print("never")
print()
for i in range(3): pass
else: print("for-else ran", i)
for i in range(5):
    if i == 2: break
else: print("not printed")
while i < 4: i += 1
else: print("while-else ran", i)
for i in range(3):
    for j in range(3):
        if j == 1: continue
        if i == 2: break
        print(i, j, end="; ")
print()
x = 5
print("pos" if x > 0 else "neg", x and "t", 0 or "dflt", None or 0 or [], 1 and 2 and 3, not x, not 0, x > 3 and x < 10, 3 < x <= 5 < 6)
def side(v, log=[]):
    log.append(v); return v
print(side(0) and side(1), side(1) or side(2), side.__defaults__)
print(1 if True else 2 if False else 3, (lambda: 7)(), [y := 10, y ** 2], y)
if (n := len("hello")) > 3: print("long", n)
data = [1, 2, 3, 4]
while (item := data.pop()) != 2: print("popped", item)
print(type(1).__name__, type("s").__name__, type(None).__name__, type([]) is list, isinstance(True, int), isinstance(1, (str, int)), issubclass(bool, int), type(type))
print(None is None, [] is [], () == (), 1 is not None, "a" is not None, Ellipsis, ..., NotImplemented)
a = b = c = []; a.append(1); print(b, c is a)
x = 10; x += 5; x -= 3; x *= 2; x //= 5; x **= 2; x %= 7; x <<= 3; x >>= 1; x |= 1; x &= 13; x ^= 2; print(x)
x = 7; x /= 2; print(x)
lst = [1, 2]; alias = lst; lst += [3]; print(alias); lst = lst + [4]; print(alias, lst)
tup = (1,); t2 = tup; tup += (2,); print(t2, tup)
dd = {"k": [1]}; dd["k"] += [2]; dd["n"] = dd.get("n", 0) + 1; print(dd)
class O: pass
o = O(); o.v = 1; o.v += 10; o.lst = [1]; o.lst[0] += 5; print(o.v, o.lst)
assert x == 3.5, "no"
try:
    assert 1 == 2, "math broke"
except AssertionError as e:
    print("AssertionError:", e)
try:
    assert False
except AssertionError as e:
    print("AssertionError:", repr(str(e)))
del x
try:
    print(x)
except NameError as e:
    print(e)
def f():
    print(zz)
    zz = 1
try: f()
except UnboundLocalError as e: print(e)
g = 1
def setg():
    global g; g = 2
setg(); print(g)
total = 0
for i in range(100000): total += i
print(total)
print(sum(i * i for i in range(1000) if i % 3 == 0), 10 ** 3 if total else 0)
i = 10
print([i for i in range(3)], i)
pass; print("semi"); print("colons")
if True: print("inline if")
long_expr = (1 +
             2 +
             3)
print(long_expr, [
    1,
    2,
], "back" \
   "slash")
