from __future__ import annotations
from dependent import *


def f(n: int) -> list[int]:
    xs = [n]
    for x in xs:
        xs.append(x)
    return xs
