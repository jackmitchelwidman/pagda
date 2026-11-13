from __future__ import annotations
from dependent import *


class Bad(Exception): ...


def risky(n: int) -> int:
    if n < 0:
        raise Bad("negative")
    return n


def apply(g: Fn[int, int], n: int) -> int:
    return g(n)


assert apply(risky, 1) == 1
