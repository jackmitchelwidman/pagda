from __future__ import annotations
from dependent import *


def f(n: Nat) -> Nat:
    i = 0
    while i < n:
        i = i * 2 + 1
    return i
