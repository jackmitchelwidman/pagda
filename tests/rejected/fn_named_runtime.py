from __future__ import annotations
from dependent import *


def family(n: Nat) -> Type:
    return Fn[k: Nat, Nat]
