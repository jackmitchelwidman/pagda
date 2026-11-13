from __future__ import annotations
from dependent import *


def f(n: Nat) -> Nat:
    def loop(k: Nat) -> Nat:
        return loop(k)

    return loop(n)
