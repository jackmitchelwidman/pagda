from __future__ import annotations
from dependent import *


def find(names: list[str], prefix: str) -> Optional[str]:
    for name in names:
        if name.startswith(prefix):
            return name
    return None


def shout(names: list[str], prefix: str) -> str:
    hit = find(names, prefix)
    if hit is None:
        return "nobody"
    return hit + "!"


print(shout(["ada", "bob"], "b"), shout(["ada"], "z"))
