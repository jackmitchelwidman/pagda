from __future__ import annotations
from dependent import *


def f(xs: list[int]) -> list[int]:
    xs += [1]
    return xs
