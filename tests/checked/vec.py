from __future__ import annotations
from dependent import *

class Vec[A: Type, n: Nat](Data): ...

@constructor
def Nil[A]() -> Vec[A, 0]: ...

@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...

def add(n: Nat, m: Nat) -> Nat:
    if n == 0:
        return m
    return suc(add(n - 1, m))

def app[A, n: Nat, m: Nat](xs: Vec[A, n], ys: Vec[A, m]) -> Vec[A, n + m]:
    match xs:
        case Nil():
            return ys
        case Cons(x, rest):
            return Cons(x, app(rest, ys))

def head[A, n: Nat](xs: Vec[A, n + 1]) -> A:
    match xs:
        case Cons(x, _):
            return x

def vmap[A, B, n: Nat](f: Callable[[A], B], xs: Vec[A, n]) -> Vec[B, n]:
    match xs:
        case Nil():
            return Nil()
        case Cons(x, rest):
            return Cons(f(x), vmap(f, rest))

def length[A, n: Nat](xs: Vec[A, n]) -> Nat:
    match xs:
        case Nil():
            return 0
        case Cons(_, rest):
            return length(rest) + 1

def plus_zero(n: Nat) -> n + 0 == n:
    if n == 0:
        return refl()
    return cong(suc, plus_zero(n - 1))

v: Vec[Nat, 2] = Cons(1, Cons(2, Nil()))
w: Vec[Nat, 3] = app(v, Cons(3, Nil()))
print(w, head(w), length(w), add(2, 3))
print(vmap(lambda x: x * 2, w))
print(plus_zero(4))
assert length(w) == 3
