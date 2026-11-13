from __future__ import annotations
from dependent import *


def at(xs: list[int], i: Nat, ok: (i < len(xs)) == True) -> int:
    return xs[i]


def third(xs: list[int]) -> int:
    return at(xs, 2, refl())
