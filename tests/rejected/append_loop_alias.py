from __future__ import annotations
from dependent import *


def f(xs: list[int]) -> list[list[int]]:
    acc: list[int] = []
    out: list[list[int]] = []
    for x in xs:
        acc.append(x)
        out = out + [acc]
    return out
