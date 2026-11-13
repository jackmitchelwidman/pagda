from __future__ import annotations
from dependent import *


def get(xs: list[int], i: Nat) -> int:
    if i < len(xs):
        return xs[i]
    return 0


print(get([4, 8, 15], 1), get([4, 8, 15], 7))
