from __future__ import annotations
from dependent import *


def f(x: Optional[int]) -> int:
    if x is None:
        return x + 1
    return 0
