from __future__ import annotations
from dependent import *


def first[A](xs: list[A]) -> Optional[A]:
    for x in xs:
        return x
    return None
