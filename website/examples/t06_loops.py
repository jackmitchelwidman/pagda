from __future__ import annotations
from dependent import *


def triangle(n: Nat) -> Nat:
    i = 0
    total = 0
    while i < n:
        total += i
        i += 1
    return total


def countdown(k: int) -> list[int]:
    out: list[int] = []
    while k > 0:
        out.append(k)
        k -= 1
    return out


print(triangle(5), countdown(3))
