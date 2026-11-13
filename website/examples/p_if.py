from __future__ import annotations
from dependent import *


class Or[A, B](Data): ...


@constructor
def Left[A, B](a: A) -> Or[A, B]: ...


@constructor
def Right[A, B](b: B) -> Or[A, B]: ...


def larger(a: Nat, b: Nat) -> Nat:
    if a < b:
        return b
    return a


# larger(a, b) is always one of its two arguments
def larger_is_one(a: Nat, b: Nat) -> Or[Eq[larger(a, b), a], Eq[larger(a, b), b]]:
    if a < b:
        return Right(refl())
    return Left(refl())


print(larger(2, 5), larger_is_one(2, 5), larger_is_one(5, 2))
