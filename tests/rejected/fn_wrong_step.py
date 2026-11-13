from __future__ import annotations
from dependent import *


def induction(P: Fn[Nat, Type], base: P(0), step: Fn[k: Nat, P(k), P(k + 1)], n: Nat) -> P(n):
    if n == 0:
        return base
    return step(n - 1, induction(P, base, step, n - 1))


def bad(n: Nat) -> n + 0 == n:
    return induction(lambda k: Eq[k + 0, k], refl(), lambda k, h: h, n)
