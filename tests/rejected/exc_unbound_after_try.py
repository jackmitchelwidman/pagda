from __future__ import annotations
from dependent import *


class Bad(Exception): ...


def risky(n: int) -> int:
    if n < 0:
        raise Bad("negative")
    return n


def f(n: int) -> int:
    try:
        v = risky(n)
    except Bad:
        pass
    except Exception:
        pass
    return v


assert f(-1) == 0
