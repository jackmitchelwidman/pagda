a = [3, 1, 4, 1, 5, 9, 2, 6]
print(a, len(a), a[0], a[-1], a[2:5], a[::-1], a[::3], a[10:], a[-3:])
a.append(7); a.extend([8, 9]); a.insert(0, 0); a.insert(100, 99); a.insert(-1, 55)
print(a, a.pop(), a.pop(0), a.pop(-2), a)
a.remove(1); print(a, a.index(4), a.count(9), 4 in a, 100 in a)
a.sort(); print(a); a.sort(reverse=True); print(a); a.reverse(); print(a)
b = a.copy(); b.clear(); print(a, b, a == a.copy(), a is a.copy())
words = ["banana", "Apple", "cherry", "date"]
print(sorted(words), sorted(words, key=str.lower), sorted(words, key=len, reverse=True), sorted(words, key=lambda w: (len(w), w)))
pairs = [(1, "b"), (0, "z"), (1, "a"), (0, "y")]
print(sorted(pairs, key=lambda p: p[0]), sorted(pairs, key=lambda p: p[0], reverse=True), sorted(pairs))
x = [1, 2, 3, 4, 5, 6]
x[1:3] = ["a", "b", "c"]; print(x)
x[::2] = [0, 0, 0, 0]; print(x)
del x[0]; del x[1:3]; print(x); del x[::2]; print(x)
x[:] = range(5); x[2:2] = [9, 9]; x[-1:] = []; print(x)
print([1, 2] + [3], [0] * 3, [[1, 2]] * 2, [1, 2] * 0, [1, 2, 3] == [1, 2, 3], [1, 2] < [1, 3], [1, 2] < [1, 2, 0], [] < [0])
m = [[0] * 3 for _ in range(2)]; m[0][1] = 5; print(m)
n = [[0] * 2] * 2; n[0][0] = 1; print(n)
print([i * i for i in range(10) if i % 2], [(i, j) for i in range(3) for j in range(i)], [[j for j in range(i)] for i in range(4)])
t = (1, 2, 3)
print(t, t[0], t[1:], t + (4,), t * 2, len(t), (1,), (), t.count(2), t.index(3), 2 in t, tuple("abc"), tuple([1, 2]), (1, 2) < (1, 3))
a, b, *c = range(5); print(a, b, c)
*a, b = "xyz"; print(a, b)
a, (b, c), [d, *e] = 1, (2, 3), [4, 5, 6]; print(a, b, c, d, e)
a, b = b, a; print(a, b)
print(list(range(5)), list(range(2, 10, 3)), list(range(10, 0, -3)), list(range(0)), len(range(1, 100, 7)), range(10)[3], range(10)[-1], range(10)[2:8:2], 5 in range(10), 10 in range(10), range(3) == range(0, 3))
print(list(enumerate("abc")), list(enumerate([5, 6], 10)), list(zip([1, 2, 3], "ab")), list(zip()), list(map(str, [1, 2])), list(map(lambda x, y: x + y, [1, 2], [10, 20])), list(filter(None, [0, 1, "", "a"])), list(filter(lambda v: v > 1, [1, 2, 3])))
print(list(reversed([1, 2, 3])), list(reversed("abc")), list(reversed(range(5))), list(reversed((1, 2))), any([0, 0, 1]), all([1, 1, 0]), any([]), all([]))
big = list(range(1000)); big.sort(key=lambda v: -v); print(big[:5], big[-3:], sum(big), min(big), max(big))
l = [1, [2, [3, [4]]]]; l2 = l[:]; l2[1][1][1].append(5); print(l)
self_ref = [1]; self_ref.append(self_ref); print(self_ref)
print(list("hello"), [*range(3), *"ab"], (*[1, 2], 3), [x for x in [1, 2, 3]][-1])
stack = []
for i in range(5): stack.append(i)
while stack: print(stack.pop(), end=" ")
print()
print(max([1, 5, 3], key=lambda v: -v), min([(2, "a"), (1, "b")]), max([], default="empty"), sorted([3, 1, 2], reverse=True), sorted("hello"), sorted({3: 1, 1: 2}))
for bad in ["[1,2][5]", "[].pop()", "[1].remove(2)", "[1].index(9)", "(1,2)[3]", "[1,2] + (3,)", "sorted([1,'a'])", "[1,2][1:2:0]", "a, b = [1, 2, 3]", "a, b, c = [1, 2]", "a, b = 5", "max([])", "[1][1.5]", "[1]['a']"]:
    try:
        exec(bad)
    except Exception as e:
        print(type(e).__name__, "->", e)
