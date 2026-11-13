from __future__ import annotations
from dependent import *


def f(xs: list[int]) -> list[int]:
    xs.append(1)
    return xs
