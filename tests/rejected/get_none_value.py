from __future__ import annotations
from dependent import *

def f(d: dict[str, Optional[int]]) -> int:
    v = d.get("a")
    if v is None:
        return 0
    return 1


assert f({"a": None}) == 1
