from __future__ import annotations
from dependent import *


def f(x: Optional[int]) -> int:
    if x:
        return 1
    return 0
