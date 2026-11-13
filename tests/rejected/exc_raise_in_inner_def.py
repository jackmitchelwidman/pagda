from __future__ import annotations
from dependent import *


class Bad(Exception): ...


def risky(n: int) -> int:
    if n < 0:
        raise Bad("negative")
    return n


def f(n: int) -> int:
    def g(k: int) -> int:
        raise Bad("no")
    return g(n)
