from __future__ import annotations
from dependent import *

fs: list[Fn[Nat]] = [lambda: x for x in [1, 2]]
assert fs[0]() == 1
