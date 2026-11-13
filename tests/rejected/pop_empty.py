from __future__ import annotations
from dependent import *


def f(xs: list[int]) -> int:
    ys = list(xs)
    x = ys.pop()
    return x
