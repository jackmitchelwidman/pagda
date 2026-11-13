"""Exceptions inside loops, else on a try, the order of except clauses, and a value set on both paths."""
from __future__ import annotations
from dependent import *


class Bad(Exception): ...


def risky(n: int) -> int:
    if n < 0:
        raise Bad("negative")
    return n


def f0(xs: list[int]) -> int:
    count = 0
    for x in xs:
        try:
            if x < 0:
                raise Bad("neg")
            count = count + 1
        except Bad:
            count = count + 100
    return count


assert f0([1, -1, 2]) == 102
print(f0([1, -1, 2]))

def f1(n: int) -> int:
    try:
        v = 1
    except Bad:
        return 0
    else:
        raise Bad("from else")
    return v


def g1(n: int) -> int:
    try:
        r = f1(n)
    except Bad:
        return 7
    except Exception:
        return 8
    return r


assert g1(1) == 7
print(g1(1))

def f2(n: int) -> str:
    try:
        v = risky(n)
    except Exception:
        return "any"
    except Bad:
        return "bad"
    return "ok"


assert f2(-1) == "any"
print(f2(-1), f2(1))

def f3(n: int) -> int:
    try:
        v = risky(n)
    except Bad:
        v = 5
    except Exception:
        v = 6
    return v


assert f3(-3) == 5 and f3(3) == 3
print(f3(-3), f3(3))

