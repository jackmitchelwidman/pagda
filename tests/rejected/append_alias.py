from __future__ import annotations
from dependent import *


def f(n: int) -> list[int]:
    a = [n]
    b = a
    a.append(2)
    return b
