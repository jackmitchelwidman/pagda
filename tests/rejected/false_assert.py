from __future__ import annotations
from dependent import *


class Vec[A, n: Nat](Data): ...


@constructor
def Nil[A]() -> Vec[A, 0]: ...


@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...


assert 1 + 1 == 3
