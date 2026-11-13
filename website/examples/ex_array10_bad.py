from __future__ import annotations
from dependent import *


@dataclass
class Array10:
    items: list[int]
    size: len(items) == 10


short: Array10 = Array10([1, 2, 3], refl())
