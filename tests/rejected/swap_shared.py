from __future__ import annotations
from dependent import *


def f(xs: list[int]) -> list[int]:
    xs[0], xs[1] = xs[1], xs[0]
    return xs
