from __future__ import annotations
from dependent import *


def keep(xs: list[int]) -> list[int]:
    return xs


def f(n: int) -> list[int]:
    a = [n]
    b = keep(a)
    a.append(2)
    return b
