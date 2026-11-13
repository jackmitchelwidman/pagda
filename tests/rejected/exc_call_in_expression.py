from __future__ import annotations
from dependent import *


def risky(n: int) -> int:
    if n < 0:
        raise ValueError("negative")
    return n


def f(n: int) -> int:
    return risky(n) + 1
