"""Tuples, list patterns, dicts, slices, comprehensions and the built-in functions."""
from __future__ import annotations
from dependent import *


def divmod2(a: int, b: int) -> tuple[int, int]:
    if b == 0:
        return (0, 0)
    return (a // b, a % b)


def swap(p: tuple[int, str]) -> tuple[str, int]:
    a, b = p
    return (b, a)


def third(t: tuple[Nat, Nat, Nat]) -> Nat:
    return t[2] + t[0]


def describe(xs: list[int]) -> str:
    match xs:
        case []:
            return "empty"
        case [x]:
            return "one"
        case [x, y, *rest]:
            return "many"


def pairs(xs: list[int]) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for i, x in enumerate(xs):
        for y in xs[i + 1:]:
            out.append((x, y))
    return out


def count_words(words: list[str]) -> dict[str, Nat]:
    counts: dict[str, Nat] = {}
    for w in words:
        counts[w] = counts.get(w, 0) + 1
    return counts


def lookup(d: dict[str, Nat], k: str) -> Nat:
    if k in d:
        return d[k]
    return 0


def keys_of(d: dict[str, Nat]) -> list[str]:
    return [k for k in d]


def total_of(d: dict[str, Nat]) -> Nat:
    s = 0
    for k, v in d.items():
        s += v
    return s


def safe_get(xs: list[int], i: Nat) -> int:
    if i < len(xs):
        return xs[i]
    return 0


def bump(xs: list[int]) -> list[int]:
    ys = list(xs)
    for i in range(len(ys)):
        if i < len(ys):
            ys[i] += 1
    return ys


def matrix(n: Nat) -> list[list[Nat]]:
    rows: list[list[Nat]] = []
    for i in range(n):
        row: list[Nat] = []
        for j in range(n):
            row.append(i * n + j)
        rows.append(row)
    return rows


print(divmod2(7, 2), divmod2(-7, 2), divmod2(1, 0), swap((1, "a")), third((1, 2, 3)))
print(describe([]), describe([1]), describe([1, 2, 3]), pairs([1, 2, 3]))
print(count_words(["a", "b", "a"]), lookup(count_words(["a", "b", "a"]), "a"), lookup({"x": 1}, "y"))
print(keys_of({"p": 1, "q": 2}), total_of({"p": 1, "q": 2}), safe_get([1, 2], 1), safe_get([1, 2], 5), bump([1, 2]), matrix(2))
print([1, 2, 3][1], [1, 2, 3][1:], [1, 2, 3][:2], [1, 2, 3, 4][1:3], [1, 2][5:], len([1, 2] + [3]), 3 in [1, 2, 3], sorted([3, 1, 2]))
assert divmod2(-7, 2) == (-4, 1)
assert third((1, 2, 3)) == 4
assert describe([1, 2, 3]) == "many"
assert pairs([1, 2, 3]) == [(1, 2), (1, 3), (2, 3)]
assert lookup(count_words(["a", "b", "a"]), "a") == 2
assert keys_of({"p": 1, "q": 2, "p": 3}) == ["p", "q"]
assert total_of({"p": 1, "q": 2, "p": 3}) == 5
assert safe_get([1, 2], 5) == 0
assert bump([1, 2]) == [2, 3]
assert matrix(2) == [[0, 1], [2, 3]]
assert [1, 2, 3, 4][1:3] == [2, 3]
assert [1, 2][5:] == []
assert [1, 2][:9] == [1, 2]
assert sorted([3, 1, 2]) == [1, 2, 3]
assert sum([x * x for x in range(4)]) == 14
assert [x + y for x in [1, 2] for y in [10, 20]] == [11, 21, 12, 22]
assert any(x == 2 for x in [1, 2]) and not all(x == 2 for x in [1, 2])
assert list(reversed([1, 2, 3])) == [3, 2, 1]
assert [a + b for a, b in zip([1, 2, 3], [10, 20])] == [11, 22]
assert str(12) + "!" == "12!"
assert len("héllo") == 5
