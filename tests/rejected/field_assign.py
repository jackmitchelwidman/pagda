from __future__ import annotations
from dependent import *


@dataclass
class P:
    x: int


def f(p: P) -> int:
    p.x = 3
    return p.x
