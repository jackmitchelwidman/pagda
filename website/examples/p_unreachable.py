from __future__ import annotations
from dependent import *


class Vec[A, n: Nat](Data): ...


@constructor
def Nil[A]() -> Vec[A, 0]: ...


@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...


class Fin[n: Nat](Data): ...          # a number below n


@constructor
def FZ[n: Nat]() -> Fin[n + 1]: ...


@constructor
def FS[n: Nat](before: Fin[n]) -> Fin[n + 1]: ...


def lookup[A, n: Nat](xs: Vec[A, n], i: Fin[n]) -> A:
    match xs:
        case Cons(x, rest):
            match i:
                case FZ():
                    return x
                case FS(j):
                    return lookup(rest, j)


def nothing_below_zero[A](i: Fin[0]) -> A:
    match i:
        case _:
            return unreachable()


v: Vec[str, 3] = Cons("a", Cons("b", Cons("c", Nil())))
print(lookup(v, FZ()), lookup(v, FS(FS(FZ()))))
