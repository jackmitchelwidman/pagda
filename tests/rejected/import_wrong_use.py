from __future__ import annotations
from dependent import *
from helper_checked import twice


def f(s: str) -> Nat:
    return twice(s)
