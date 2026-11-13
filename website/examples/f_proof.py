from __future__ import annotations
from dependent import *


def plus_zero(n: Nat) -> n + 0 == n:
    if n == 0:
        return refl()
    return cong(suc, plus_zero(n - 1))


print(plus_zero(5))
