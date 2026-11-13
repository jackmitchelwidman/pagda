from __future__ import annotations
from dependent import *


def f(n: int) -> Fn[list[int]]:
    a = [n]
    g: Fn[list[int]] = lambda: a
    a.append(2)
    return g
