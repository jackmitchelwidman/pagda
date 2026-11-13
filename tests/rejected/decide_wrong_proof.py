from __future__ import annotations
from dependent import *


def gap(a: Nat, b: Nat, p: (a < b) == True) -> Nat:
    return monus(b, a)


def f(a: Nat, b: Nat) -> Nat:
    match decide(a < b):
        case Yes(p):
            return a
        case No(p):
            return gap(a, b, p)
