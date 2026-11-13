"""Proofs: equalities shown by recursion, a bounded index, and impossible cases."""
from __future__ import annotations
from dependent import *


def add(a: Nat, b: Nat) -> Nat:
    if a == 0:
        return b
    return suc(add(a - 1, b))


def add_zero(n: Nat) -> add(n, 0) == n:
    if n == 0:
        return refl()
    return cong(suc, add_zero(n - 1))


def add_suc(a: Nat, b: Nat) -> add(a, suc(b)) == suc(add(a, b)):
    if a == 0:
        return refl()
    return cong(suc, add_suc(a - 1, b))


def add_comm(a: Nat, b: Nat) -> add(a, b) == add(b, a):
    if a == 0:
        return sym(add_zero(b))
    step: suc(add(a - 1, b)) == suc(add(b, a - 1)) = cong(suc, add_comm(a - 1, b))
    return trans(step, sym(add_suc(b, a - 1)))


def add_assoc(a: Nat, b: Nat, c: Nat) -> add(add(a, b), c) == add(a, add(b, c)):
    if a == 0:
        return refl()
    return cong(suc, add_assoc(a - 1, b, c))


# the same facts about the built-in +
def plus_zero(n: Nat) -> n + 0 == n:
    if n == 0:
        return refl()
    return cong(suc, plus_zero(n - 1))


# proofs taken apart with match
def my_sym[A, x: A, y: A](p: Eq[x, y]) -> Eq[y, x]:
    match p:
        case Refl():
            return Refl()


def my_trans[A, x: A, y: A, z: A](p: x == y, q: y == z) -> x == z:
    match p:
        case Refl():
            return q


class Vec[A, n: Nat](Data): ...


@constructor
def Nil[A]() -> Vec[A, 0]: ...


@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...


# an equality between lengths lets a vector be used at the other length
def recast[A, n: Nat, m: Nat](p: n == m, xs: Vec[A, n]) -> Vec[A, m]:
    match p:
        case Refl():
            return xs


def append[A, n: Nat, m: Nat](xs: Vec[A, n], ys: Vec[A, m]) -> Vec[A, n + m]:
    match xs:
        case Nil():
            return ys
        case Cons(x, rest):
            return Cons(x, append(rest, ys))


def append_nil[A](n: Nat, xs: Vec[A, n]) -> Vec[A, n + 0]:
    return recast(sym(plus_zero(n)), xs)


# Fin[n]: a number below n. Indexing with it cannot go out of range.
class Fin[n: Nat](Data): ...


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


def fin_to_nat[n: Nat](i: Fin[n]) -> Nat:
    match i:
        case FZ():
            return 0
        case FS(j):
            return fin_to_nat(j) + 1


# a type with no values: nothing is below zero
# Less-or-equal as a family of proofs
class Le[a: Nat, b: Nat](Data): ...


@constructor
def LeZero[b: Nat]() -> Le[0, b]: ...


@constructor
def LeSuc[a: Nat, b: Nat](below: Le[a, b]) -> Le[a + 1, b + 1]: ...


def le_refl(n: Nat) -> Le[n, n]:
    if n == 0:
        return LeZero()
    return LeSuc(le_refl(n - 1))


def le_trans[a: Nat, b: Nat, c: Nat](p: Le[a, b], q: Le[b, c]) -> Le[a, c]:
    match p:
        case LeZero():
            return LeZero()
        case LeSuc(p1):
            match q:
                case LeSuc(q1):
                    return LeSuc(le_trans(p1, q1))


# nothing proves 1 <= 0, and there is no number below zero
def one_le_zero(p: Le[1, 0]) -> Empty:
    match p:
        case _:
            return unreachable()


def below_zero[A](i: Fin[0]) -> A:
    match i:
        case _:
            return unreachable()


def not_le[a: Nat](p: Le[a + 2, 1]) -> Empty:
    match p:
        case LeSuc(q):
            match q:
                case _:
                    return unreachable()


def use_empty[A](e: Empty) -> A:
    return absurd(e)


# a checked subtraction: the proof says b fits inside a
def minus[b: Nat, a: Nat](x: Nat, y: Nat, p: Le[b, a], px: x == a, py: y == b) -> Nat:
    return monus(x, y)


def family(n: Nat) -> Type:
    return Vec[Nat, n]


v: Vec[str, 3] = Cons("a", Cons("b", Cons("c", Nil())))
two: Fin[3] = FS(FS(FZ()))
print(add(2, 3), add_zero(5), add_comm(2, 3), add_assoc(1, 2, 3))
print(my_sym(add_zero(1)), my_trans(add_zero(2), refl()))
print(append_nil(3, v), recast(plus_zero(3), append(v, Nil())))
print(lookup(v, FZ()), lookup(v, FS(FZ())), lookup(v, two), fin_to_nat(two))
print(le_refl(2), le_trans(LeZero(), le_refl(3)))
moved: family(2) = subst(lambda k: Vec[Nat, k], add_comm(1, 1), Cons(1, Cons(2, Nil())))
print(moved)
assert add(20, 22) == 42
assert fin_to_nat(two) == 2
