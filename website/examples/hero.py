from __future__ import annotations
from dependent import *


def mean(xs: list[int]) -> int:
    if xs:                            # the test is the proof
        return sum(xs) // len(xs)     # so this cannot divide by zero
    return 0


class Vec[A, n: Nat](Data): ...       # a list whose length is in its type


@constructor
def Nil[A]() -> Vec[A, 0]: ...


@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...


# n + 1 is never 0, so this takes only a non-empty Vec
def first[A, n: Nat](xs: Vec[A, n + 1]) -> A:
    match xs:
        case Cons(x, _):
            return x


assert mean([3, 4, 8]) == 5           # checked before the program runs
print(mean([3, 4, 8]), first(Cons("a", Nil())))
