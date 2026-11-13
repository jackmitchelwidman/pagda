from __future__ import annotations
from dependent import *


class Vec[A, n: Nat](Data): ...


@constructor
def Nil[A]() -> Vec[A, 0]: ...


@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...


def dot[n: Nat](a: Vec[int, n], b: Vec[int, n]) -> int:
    match a:
        case Nil():
            return 0
        case Cons(x, xs):
            match b:
                case Cons(y, ys):
                    return x * y + dot(xs, ys)


v: Vec[int, 2] = Cons(1, Cons(2, Nil()))
w: Vec[int, 3] = Cons(3, Cons(4, Cons(5, Nil())))
print(dot(v, w))
