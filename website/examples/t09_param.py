from __future__ import annotations
from dependent import *


def at(xs: list[int], i: Nat, ok: (i < len(xs)) == True) -> int:
    return xs[i]                      # no test needed: the caller proved it


def third_or_zero(xs: list[int]) -> int:
    if 2 < len(xs):
        return at(xs, 2, known(2 < len(xs)))
    return 0


print(third_or_zero([7, 8, 9]), third_or_zero([7]))
