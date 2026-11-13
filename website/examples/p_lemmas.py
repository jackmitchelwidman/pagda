from __future__ import annotations
from dependent import *

from lemmas import plus_comm, len_append


def swap(a: Nat, b: Nat) -> a + b == b + a:
    return plus_comm(a, b)


def joined(xs: list[str], ys: list[str]) -> len(xs + ys) == len(xs) + len(ys):
    return len_append(xs, ys)


print(swap(2, 3), joined(["a"], ["b", "c"]))
