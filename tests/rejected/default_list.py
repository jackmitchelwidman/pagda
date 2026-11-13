from __future__ import annotations
from dependent import *


def f(x: int, acc: list[int] = []) -> list[int]:
    return acc + [x]
