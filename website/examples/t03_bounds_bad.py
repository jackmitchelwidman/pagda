from __future__ import annotations
from dependent import *


def average(xs: list[int]) -> int:
    return sum(xs) // len(xs)
