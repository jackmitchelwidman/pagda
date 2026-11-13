from __future__ import annotations
from dependent import *


class Bad(Exception): ...


def risky(n: int) -> int:
    if n < 0:
        raise Bad("negative")
    return n


def f(n: int) -> str:
    try:
        v = risky(n)
    except Bad as e:
        v = 0
    except Exception:
        v = 1
    return str(e)


assert f(-1) == "negative"
