"""and / or where a later part relies on an earlier one, as in a linear search."""
from __future__ import annotations
from dependent import *


def find(xs: list[int], t: int) -> Nat:
    i = 0
    while i < len(xs) and xs[i] != t:
        i += 1
    return i


def skip_spaces(s: str, start: Nat) -> Nat:
    i = start
    while i < len(s) and s[i] == " ":
        i += 1
    return i


def first_positive(xs: list[int]) -> bool:
    return len(xs) > 0 and xs[0] > 0


def empty_or_small(xs: list[int]) -> bool:
    return not xs or xs[0] < 10


def both_ok(xs: list[int], i: Nat, j: Nat) -> bool:
    ok = i < len(xs) and j < len(xs) and xs[i] == xs[j]
    return ok


def safe_ratio(a: int, b: int) -> bool:
    return b != 0 and a // b > 1


def count_run(xs: list[int]) -> Nat:
    n = 0
    while n < len(xs) and n < 100 and xs[n] > 0:
        n += 1
    return n


def pick(xs: list[int], i: Nat) -> int:
    return xs[i] if i < len(xs) and xs[i] > 0 else 0


print(find([1, 2, 3], 3), find([1, 2], 9), skip_spaces("  x", 0), first_positive([]), first_positive([3]), empty_or_small([]), empty_or_small([50]))
print(both_ok([1, 2, 1], 0, 2), both_ok([1], 0, 5), safe_ratio(9, 0), safe_ratio(9, 2), count_run([1, 2, 0, 3]), pick([5], 0), pick([5], 3))
assert find([1, 2, 3], 3) == 2 and find([1, 2], 9) == 2 and find([], 1) == 0
assert skip_spaces("  x", 0) == 2 and skip_spaces("", 0) == 0
assert not first_positive([]) and first_positive([3]) and empty_or_small([]) and not empty_or_small([50])
assert both_ok([1, 2, 1], 0, 2) and not both_ok([1], 0, 5)
assert not safe_ratio(9, 0) and safe_ratio(9, 2) and count_run([1, 2, 0, 3]) == 2
