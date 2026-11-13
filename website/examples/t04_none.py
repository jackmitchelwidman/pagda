from __future__ import annotations
from dependent import *


def price(prices: dict[str, int], name: str) -> int:
    found = prices.get(name)          # an int, or None
    if found is None:
        return 0
    return found * 2                  # here it is an int


def longest(words: list[str]) -> Optional[str]:
    best = None
    for w in words:
        if best is None or len(w) > len(best):
            best = w
    return best


print(price({"pen": 3}, "pen"), price({"pen": 3}, "ink"), longest(["a", "ccc", "bb"]), longest([]))
