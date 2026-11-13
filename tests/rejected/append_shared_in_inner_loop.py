from __future__ import annotations
from dependent import *


def f(n: Nat) -> list[list[int]]:
    xs: list[int] = [1]
    keep: list[list[int]] = []
    for i in range(n):
        for j in range(n):
            if j < i:
                keep.append(xs)
            else:
                continue
    xs.append(2)
    return keep
