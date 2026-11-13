from __future__ import annotations
from dependent import *

def f() -> Nat:
    g: Fn[Nat] = lambda: 7
    for i in range(3):
        if i == 0:
            g = lambda: i
    return g()


assert f() == 0
