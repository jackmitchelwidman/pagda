from __future__ import annotations
from dependent import *


class Bad(Exception): ...


def risky(n: int) -> int:
    if n < 0:
        raise Bad("negative")
    return n


def f(xs: list[int]) -> int:
    total = 0
    try:
        for x in xs:
            v = risky(x)
            total += v
    except Exception:
        return -1
    return total
