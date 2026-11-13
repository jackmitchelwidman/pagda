from __future__ import annotations
from dependent import *


@dataclass
class Bag:
    items: list[int]


def f(n: int) -> Bag:
    xs = [n]
    b = Bag(xs)
    xs.append(2)
    return b
