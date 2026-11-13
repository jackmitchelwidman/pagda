from __future__ import annotations
from dependent import *


class Vec[A, n: Nat](Data): ...


@constructor
def Nil[A]() -> Vec[A, 0]: ...


@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...


def head[A, n: Nat](xs: Vec[A, n + 1]) -> A:
    match xs:
        case Cons(x, _):
            return x


empty: Vec[Nat, 0] = Nil()
print(head(empty))
