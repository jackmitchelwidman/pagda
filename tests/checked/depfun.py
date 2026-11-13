"""Function types whose later parts mention an earlier argument, and if / decide
on a condition that the types then know about."""
from __future__ import annotations
from dependent import *


class Vec[A, n: Nat](Data): ...


@constructor
def Nil[A]() -> Vec[A, 0]: ...


@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...


# Induction on Nat. The step takes a number k and a P(k), and gives a P(k + 1).
def induction(P: Fn[Nat, Type], base: P(0), step: Fn[k: Nat, P(k), P(k + 1)], n: Nat) -> P(n):
    if n == 0:
        return base
    return step(n - 1, induction(P, base, step, n - 1))


def plus_zero(n: Nat) -> n + 0 == n:
    return induction(lambda k: Eq[k + 0, k], refl(), lambda k, h: cong(suc, h), n)


# A function that builds a vector of whatever length it is asked for.
def zeros(n: Nat) -> Vec[Nat, n]:
    if n == 0:
        return Nil()
    return Cons(0, zeros(n - 1))


def build(make: Fn[k: Nat, Vec[Nat, k]], n: Nat) -> Vec[Nat, n + n]:
    return make(n + n)


def length[A, n: Nat](xs: Vec[A, n]) -> Nat:
    match xs:
        case Nil():
            return 0
        case Cons(_, rest):
            return length(rest) + 1


# Fn without names is Callable.
def twice[A](f: Fn[A, A], x: A) -> A:
    return f(f(x))


def compose[A, B, C](f: Callable[[B], C], g: Fn[A, B]) -> Fn[A, C]:
    return lambda x: f(g(x))


def constant(n: Nat) -> Fn[Nat]:
    return lambda: n


# Three arguments, each type using the ones before it.
def pick(choose: Fn[A: Type, n: Nat, Vec[A, n], Nat]) -> Nat:
    return choose(Nat, 2, Cons(7, Cons(8, Nil())))


def count(A: Type, n: Nat, xs: Vec[A, n]) -> Nat:
    return n + length(xs)


# A dependent pair: the type of the second part is worked out from the first.
class Sigma[A, B: Fn[A, Type]](Data): ...


@constructor
def Pair[A, B: Fn[A, Type]](fst: A, snd: B(fst)) -> Sigma[A, B]: ...


def some_vec(n: Nat) -> Sigma[Nat, lambda k: Vec[Nat, k]]:
    return Pair(n, zeros(n))


# if on a condition: in each branch the types know which way it went.
class Or[A, B](Data): ...


@constructor
def Left[A, B](a: A) -> Or[A, B]: ...


@constructor
def Right[A, B](b: B) -> Or[A, B]: ...


def larger(a: Nat, b: Nat) -> Nat:
    if a < b:
        return b
    return a


def larger_is_one(a: Nat, b: Nat) -> Or[Eq[larger(a, b), a], Eq[larger(a, b), b]]:
    if a < b:
        return Right(refl())
    return Left(refl())


def and_comm(c: bool, d: bool) -> (c and d) == (d and c):
    if c:
        if d:
            return refl()
        return refl()
    if d:
        return refl()
    return refl()


# decide gives the proof of which way the condition went.
def larger_when_less(a: Nat, b: Nat, p: (a < b) == True) -> larger(a, b) == b:
    match decide(a < b):
        case Yes(_):
            return refl()
        case No(_):
            # here the checker has replaced a < b by False, so p claims False == True
            return absurd(true_is_not_false(sym(p)))


def true_is_not_false(p: True == False) -> Empty:
    match p:
        case _:
            return unreachable()


# A function that asks its caller for a proof, and a caller that gets one from decide.
def gap(a: Nat, b: Nat, p: (a < b) == True) -> Nat:
    return monus(b, a)


def gap_or_zero(a: Nat, b: Nat) -> Nat:
    match decide(a < b):
        case Yes(p):
            return gap(a, b, p)
        case No(_):
            return 0


def as_value(c: bool) -> Dec[c]:
    return decide(c)


print(plus_zero(4), induction(lambda k: Nat, 1, lambda k, acc: acc * 2, 10))
print(build(zeros, 2), length(build(zeros, 3)))
print(twice(lambda k: k + 3, 1), compose(suc, suc)(5), constant(9)())
print(pick(count), some_vec(2))
print(larger(2, 5), larger_is_one(2, 5), larger_is_one(5, 2), and_comm(True, False))
print(larger_when_less(1, 2, refl()), gap_or_zero(3, 10), gap_or_zero(10, 3), as_value(True), as_value(False))
assert larger(3, 7) == 7
assert length(build(zeros, 4)) == 8
