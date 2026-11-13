from __future__ import annotations
from dependent import *


def at_or(xs: list[int], i: Nat, default: int) -> int:
    if i < len(xs):
        return xs[i]
    return default


def average(xs: list[int]) -> int:
    if xs:
        return sum(xs) // len(xs)
    return 0


def find(xs: list[int], target: int) -> Nat:
    i = 0
    while i < len(xs) and xs[i] != target:
        i += 1
    return i


print(at_or([1, 2, 3], 1, 0), at_or([1, 2, 3], 9, 0), average([4, 8]), average([]), find([5, 6, 7], 7))
