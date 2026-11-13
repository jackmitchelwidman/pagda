d = {"one": 1, "two": 2, "three": 3}
print(d, len(d), d["one"], d.get("two"), d.get("zzz"), d.get("zzz", 0), "one" in d, "four" in d, 1 in d)
d["four"] = 4; d["one"] = 11; del d["two"]
print(d, list(d), list(d.keys()), list(d.values()), list(d.items()))
print(d.pop("three"), d.pop("nope", "dflt"), d.setdefault("five", 5), d.setdefault("five", 55), d.popitem(), d)
d.update({"a": 1}); d.update(b=2, c=3); d.update([("d", 4), ("e", 5)]); print(d)
print(d.keys(), d.values(), d.items(), len(d.keys()), "a" in d.keys())
e = d.copy(); e.clear(); print(e, d == d.copy(), {1: 2} == {1: 2}, {1: 2} == {1: 3}, {"a": 1, "b": 2} == {"b": 2, "a": 1})
print({i: i * i for i in range(5)}, {k: v for k, v in zip("abc", range(3))}, dict(a=1, b=2), dict([(1, 2), (3, 4)]), dict({"x": 1}, y=2), dict.fromkeys("abc", 0))
print({1: "int", 1.0: "float", True: "bool"}, {(1, 2): "t", "s": "s", 3.5: "f", None: "n"}, {**{"a": 1}, "b": 2, **{"a": 3}})
print({"a": 1} | {"b": 2}, {} == dict(), bool({}), bool({0: 0}))
nested = {"k": {"inner": [1, 2, {"deep": True}]}}; nested["k"]["inner"][2]["deep"] = False; print(nested)
for k, v in {"x": 1, "y": 2}.items(): print(k, v, end="; ")
print()
counts = {}
for ch in "mississippi": counts[ch] = counts.get(ch, 0) + 1
print(counts, sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])), max(counts, key=counts.get))
big = {i: str(i) for i in range(1000)}
for i in range(0, 1000, 2): del big[i]
print(len(big), big[999], 500 in big, 501 in big, list(big)[:5], sum(big))
for i in range(2000, 2500): big[i] = i
print(len(big), list(big)[-3:])
s = {3, 1, 2, 3, 1}
print(sorted(s), len(s), 2 in s, 5 in s, set(), {1} == {1}, sorted(set("hello")), frozenset([1, 2]) == {1, 2})
s.add(10); s.discard(1); s.discard(99); s.remove(2); print(sorted(s))
a, b = {1, 2, 3, 4}, {3, 4, 5}
print(sorted(a | b), sorted(a & b), sorted(a - b), sorted(a ^ b), a <= b, {3} <= b, a >= {1}, a < a, a.isdisjoint({9}), a.issubset(range(10)), b.issuperset([3]))
print(sorted(a.union([7], (8,))), sorted(a.intersection(b, [4])), sorted(a.difference([1], [2])), sorted(a.symmetric_difference(b)))
a |= {9}; a &= {1, 2, 9}; a -= {1}; a ^= {2, 5}; print(sorted(a))
c = a.copy(); c.update([1], "xy"); c.intersection_update(["x", 1, 9]); c.difference_update([9]); print(sorted(c, key=str), len({i % 7 for i in range(100)}))
print({1, 2} == {2, 1}, {1, 2} != {1}, len(frozenset("aab")), sorted({(1, 2), (1, 2), (2, 1)}), {frozenset([1]): "fs"}[frozenset([1])])
print(set([1, 2]).pop() in (1, 2), sorted(set(range(5)) - set(range(3))), {1.0, 1, True} == {1})
for bad in ["{}['k']", "{[1]: 2}", "{1}.remove(5)", "set().pop()", "{}.popitem()", "{{1}}", "dict([(1,)])", "{1: 2}.pop(3)", "hash([])", "{1: 2} < {1: 3}"]:
    try:
        eval(bad)
    except Exception as e:
        print(type(e).__name__, "->", e)
d = {1: 1}
try:
    for k in d: d[k + 1] = 1
except RuntimeError as e:
    print("RuntimeError", e)
