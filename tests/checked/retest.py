"""Testing a value a second time, after an earlier test has already settled it."""
from __future__ import annotations
from dependent import *


class Shape(Data): ...


@constructor
def Dot() -> Shape: ...


@constructor
def Circle(r: Nat) -> Shape: ...


def f(s: Shape) -> Nat:
    match s:
        case Dot():
            match s:
                case Dot():
                    return 0
                case Circle(r):
                    return 1
        case Circle(r):
            return 2


def g(n: Nat) -> Nat:
    if n == 0:
        return 0
    m = n - 1
    if m == 0:
        if n - 1 == 0:
            return 1
        return 2
    return 3


def h(n: Nat) -> Nat:
    match n:
        case 0:
            match n:
                case 0:
                    return 10
                case _:
                    return 11
        case _:
            if n == 0:
                return 12
            return 13


def b(x: bool) -> Nat:
    match x:
        case True:
            match x:
                case False:
                    return 20
                case True:
                    return 21
        case False:
            if x:
                return 22
            return 23


def z(n: Nat) -> Nat:
    if n == 0:
        if n == 0:
            return 30
        return 31
    if n == 0:
        return 32
    return 33


def w(s: Shape) -> Nat:
    match s:
        case Dot() as d:
            match d:
                case Circle(_):
                    return 40
                case Dot():
                    return 41
        case Circle(0) as c:
            match c:
                case Circle(k):
                    return k + 42
                case Dot():
                    return 43
        case other:
            match other:
                case Circle(k):
                    return k + 44
                case Dot():
                    return 45


print(f(Dot()), f(Circle(5)))
print(g(0), g(1), g(2), g(5))
print(h(0), h(1), h(7))
print(b(True), b(False))
print(z(0), z(1), z(4))
print(w(Dot()), w(Circle(0)), w(Circle(3)))
assert f(Circle(5)) == 2
assert g(5) == 3
