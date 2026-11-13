from __future__ import annotations
from dependent import *

def f() -> list[int]:
    xs: list[int] = []
    def add(x: int) -> None:
        xs.append(x)
    add(1)
    return xs


assert f() == []
