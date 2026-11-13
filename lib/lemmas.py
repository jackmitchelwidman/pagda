"""Ready-made proofs for checked files.

    from lemmas import plus_comm, monus_step

Each def here is a fact, proved once. Its return annotation is the fact, and its
parameters are what the fact is about. Calling it gives the proof.
"""
from __future__ import annotations
from dependent import *


# ----- True and False are different -----

def false_is_not_true(p: False == True) -> Empty:
    match p:
        case _:
            return unreachable()


def true_is_not_false(p: True == False) -> Empty:
    match p:
        case _:
            return unreachable()


# ----- addition and multiplication on Nat -----

def plus_zero(n: Nat) -> n + 0 == n:
    if n == 0:
        return refl()
    return cong(suc, plus_zero(n - 1))


def plus_suc(a: Nat, b: Nat) -> a + (b + 1) == (a + b) + 1:
    if a == 0:
        return refl()
    return cong(suc, plus_suc(a - 1, b))


def plus_comm(a: Nat, b: Nat) -> a + b == b + a:
    if a == 0:
        return sym(plus_zero(b))
    step: (a - 1 + b) + 1 == (b + (a - 1)) + 1 = cong(suc, plus_comm(a - 1, b))
    return trans(step, sym(plus_suc(b, a - 1)))


def plus_assoc(a: Nat, b: Nat, c: Nat) -> (a + b) + c == a + (b + c):
    if a == 0:
        return refl()
    return cong(suc, plus_assoc(a - 1, b, c))


def times_zero(n: Nat) -> n * 0 == 0:
    if n == 0:
        return refl()
    return times_zero(n - 1)


def times_one(n: Nat) -> n * 1 == n:
    if n == 0:
        return refl()
    return cong(suc, times_one(n - 1))


# ----- < on Nat -----

def lt_suc(n: Nat) -> (n < n + 1) == True:
    if n == 0:
        return refl()
    return lt_suc(n - 1)


def lt_irrefl(n: Nat) -> (n < n) == False:
    if n == 0:
        return refl()
    return lt_irrefl(n - 1)


def lt_asym(a: Nat, b: Nat, p: (a < b) == True) -> (b < a) == False:
    if a == 0:
        if b == 0:
            return refl()
        return refl()
    if b == 0:
        return absurd(false_is_not_true(p))
    return lt_asym(a - 1, b - 1, p)


def lt_trans(a: Nat, b: Nat, c: Nat, p: (a < b) == True, q: (b < c) == True) -> (a < c) == True:
    if c == 0:
        return absurd(false_is_not_true(q))
    if b == 0:
        return absurd(false_is_not_true(p))
    if a == 0:
        return refl()
    return lt_trans(a - 1, b - 1, c - 1, p, q)


# ----- measures for while loops -----

# A loop that counts i up to n: the distance still to go, monus(n, i), gets smaller.
def monus_step(n: Nat, i: Nat, p: (i < n) == True) -> (monus(n, i + 1) < monus(n, i)) == True:
    if n == 0:
        return absurd(false_is_not_true(p))
    if i == 0:
        return lt_suc(n - 1)
    return monus_step(n - 1, i - 1, p)


# A loop that counts k down to 0: k itself gets smaller.
def down_step(k: Nat, p: (0 < k) == True) -> (monus(k, 1) < k) == True:
    if k == 0:
        return absurd(false_is_not_true(p))
    return lt_suc(k - 1)


# ----- lists -----

def append_nil[A](xs: list[A]) -> xs + [] == xs:
    match xs:
        case []:
            return refl()
        case [x, *rest]:
            return cong(lambda r: [x] + r, append_nil(rest))


def append_assoc[A](xs: list[A], ys: list[A], zs: list[A]) -> (xs + ys) + zs == xs + (ys + zs):
    match xs:
        case []:
            return refl()
        case [x, *rest]:
            return cong(lambda r: [x] + r, append_assoc(rest, ys, zs))


def len_append[A](xs: list[A], ys: list[A]) -> len(xs + ys) == len(xs) + len(ys):
    match xs:
        case []:
            return refl()
        case [x, *rest]:
            return cong(suc, len_append(rest, ys))
