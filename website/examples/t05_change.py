from __future__ import annotations
from dependent import *


def squares(n: Nat) -> list[Nat]:
    out: list[Nat] = []
    for i in range(n):
        out.append(i * i)
    return out


def bumped(xs: list[int]) -> list[int]:
    ys = list(xs)                     # a copy that this function owns
    for i in range(len(ys)):
        ys[i] += 1
    return ys


print(squares(5), bumped([1, 2, 3]))
