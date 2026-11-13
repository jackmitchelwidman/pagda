from __future__ import annotations
from dependent import *


class Vec[A, n: Nat](Data): ...


@constructor
def Nil[A]() -> Vec[A, 0]: ...


def zero_only(xs: Vec[Nat, 0]) -> Nat:
    return 0


def use(f: Fn[k: Nat, Vec[Nat, k], Nat]) -> Nat:
    return f(0, Nil())


print(use(lambda k, xs: zero_only(xs)))
