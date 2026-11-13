from __future__ import annotations
from dependent import *


def f(a: Nat, b: Nat) -> Nat:
    match decide(a < b):
        case Yes(p):
            return a
