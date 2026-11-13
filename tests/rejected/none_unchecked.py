from __future__ import annotations
from dependent import *


def first(xs: list[int]) -> Optional[int]:
    for x in xs:
        return x
    return None


def f(xs: list[int]) -> int:
    return first(xs) + 1
