from __future__ import annotations
from dependent import *


def add(a: Nat, b: Nat) -> Nat:
    if a == 0:
        return b
    return add(a - 1, b) + 1


# a fact about add, for every n. The return type is the fact.
def add_zero(n: Nat) -> add(n, 0) == n:
    if n == 0:
        return refl()                      # add(0, 0) is 0 by working it out
    return cong(suc, add_zero(n - 1))      # the fact for n - 1 gives the fact for n


assert add(20, 22) == 42                   # worked out by the checker
print(add(20, 22), add_zero(7))
