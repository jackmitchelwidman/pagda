from __future__ import annotations
from dependent import *


class Vec[A, n: Nat](Data): ...


@constructor
def Nil[A]() -> Vec[A, 0]: ...


@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...


def f[A, n: Nat](xs: Vec[A, n]) -> Callable[[], Nat]:
    return lambda: 0


def g[n: Nat](xs: Vec[Nat, n]) -> Nat:
    return h(xs)


def h[n: Nat](xs: Vec[Nat, n]) -> Nat:
    match xs:
        case Nil():
            return 0
        case Cons(x, rest):
            return x + h(rest) + "s"
