from __future__ import annotations
from dependent import *


@trusted
def read_size(path: str) -> Nat:
    import os
    return os.path.getsize(path)


def f(n: Nat) -> Nat:
    return read_size(n)
