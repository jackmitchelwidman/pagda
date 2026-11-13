"""Smaller features: changing lists and dicts, known, patterns on numbers, top-level loops."""
from __future__ import annotations
from dependent import *


def tidy(xs: list[int]) -> list[int]:
    ys = list(xs)
    ys.extend([0, -1])
    ys += [7]
    ys.sort()
    ys.reverse()
    return ys


def emptied(n: Nat) -> list[Nat]:
    xs = [n, n]
    xs.clear()
    xs.append(n)
    return xs


def scores(names: list[str]) -> dict[str, int]:
    d: dict[str, int] = {}
    for i, name in enumerate(names):
        d[name] = i
    for name in names:
        if name in d:
            d[name] = d[name] * 10
    return d


def values_sum(d: dict[str, int]) -> int:
    return sum(d.values())


def safe_div(a: int, b: int) -> int:
    if b != 0:
        return a // b
    return 0


def safe_mod(a: Nat, b: Nat) -> Nat:
    if b == 0:
        return 0
    return a % b


def both(xs: list[int], i: Nat, j: Nat) -> int:
    if i < len(xs) and j < len(xs):
        return xs[i] + xs[j]
    return 0


def gap(a: Nat, b: Nat, p: (a < b) == True) -> Nat:
    return monus(b, a)


def gap_or_zero(a: Nat, b: Nat) -> Nat:
    if a < b:
        return gap(a, b, known(a < b))
    return 0


def not_less(a: Nat, b: Nat) -> Nat:
    if a < b:
        return 0
    return at_least(a, b, known(not a < b))


def at_least(a: Nat, b: Nat, p: (not a < b) == True) -> Nat:
    return monus(a, b)


def name_of(n: int) -> str:
    match n:
        case 0:
            return "zero"
        case -1:
            return "minus one"
        case _:
            return "other " + str(n)


def sign(x: int) -> int:
    return -1 if x < 0 else (1 if x > 0 else 0)


def spread(pairs: list[tuple[str, tuple[int, int]]]) -> int:
    total = 0
    for name, (lo, hi) in pairs:
        total += hi - lo
    return total


def first_big(xs: list[int], limit: int) -> int:
    found = 0
    for x in xs:
        if x > limit:
            found = x
            break
    return found


def nested(n: Nat) -> Nat:
    count = 0
    for i in range(n):
        for j in range(n):
            if j < i:
                continue
            count += 1
    return count


def steps(a: Nat, b: Nat) -> int:
    d: int = a - b
    return d


grid: list[list[int]] = [[1, 2], [3, 4]]
for row in grid:
    for cell in row:
        print(cell, sign(cell - 2), end=" ")
print()
for i, word in enumerate(["a", "b"]):
    print(i, word)
print(tidy([3, 1]), emptied(4), scores(["x", "y"]), values_sum(scores(["x", "y", "z"])))
print(safe_div(7, 0), safe_div(-7, 2), safe_mod(7, 3), both([1, 2, 3], 0, 2), gap_or_zero(2, 9), not_less(9, 2))
print(name_of(0), name_of(-1), name_of(5), spread([("a", (1, 4)), ("b", (-2, 2))]), first_big([1, 5, 9], 3), nested(3), steps(2, 5))
assert tidy([3, 1]) == [7, 3, 1, 0, -1]
assert emptied(4) == [4]
assert values_sum(scores(["x", "y", "z"])) == 30
assert safe_div(-7, 2) == -4
assert both([1, 2, 3], 0, 2) == 4
assert not_less(9, 2) == 7
assert name_of(-1) == "minus one"
assert spread([("a", (1, 4)), ("b", (-2, 2))]) == 7
assert first_big([1, 5, 9], 3) == 5
assert nested(3) == 6
assert steps(2, 5) == -3
assert 2 ** 5 == 32 and (-2) ** 3 == -8
assert min(3, -1) == -1 and max(3, 9) == 9
