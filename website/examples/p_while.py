from __future__ import annotations
from dependent import *

from lemmas import down_step


def halvings(n: Nat) -> Nat:
    steps = 0
    left = n
    while 0 < left:
        was = left
        left = monus(left, 1)
        steps += 1
        decreases(left, down_step(was, known(0 < was)))
    return steps


print(halvings(4))
