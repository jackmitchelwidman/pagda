from __future__ import annotations
from dependent import *


@dataclass
class P:
    x: int


def f(p: P) -> int:
    return p.y
