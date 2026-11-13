"""Int indexes under a range test, defaults of None, Nat subtraction, format specs in print."""
from __future__ import annotations
from dependent import *


def pick(xs: list[int], i: int) -> int:
    if 0 <= i < len(xs):
        return xs[i]
    return -1


def put(xs: list[int], i: int, v: int) -> list[int]:
    ys = list(xs)
    if i >= 0 and i < len(ys):
        ys[i] = v
    return ys


def greet(name: Optional[str] = None, times: Nat = 1) -> str:
    if name is None:
        return "hello" * times
    return ("hello " + name) * times


def diff(a: Nat, b: Nat) -> Nat:
    if b <= a:
        return a - b
    return b - a


def last_gap(xs: list[Nat]) -> Nat:
    if len(xs) > 1:
        assert len(xs) > 1
        return diff(xs[-1], xs[0])
    return 0


def show(xs: list[int]) -> None:
    for i, x in enumerate(xs):
        print(f"{i:>3}: {x:5d} {xs!r}")


show([7, -8])
print(pick([1, 2, 3], 1), pick([1, 2, 3], -1), pick([1, 2, 3], 3), put([1, 2], 1, 9), put([1, 2], 5, 9))
print(greet(), greet("ada"), greet(times=2), greet("bo", 2), diff(3, 10), diff(10, 3), last_gap([4, 9, 20]))
assert pick([1, 2, 3], 1) == 2 and pick([1, 2, 3], -1) == -1 and pick([1, 2, 3], 3) == -1
assert put([1, 2], 1, 9) == [1, 9] and put([1, 2], -1, 9) == [1, 2]
assert greet(times=2) == "hellohello" and greet("bo", 2) == "hello bohello bo"
assert diff(3, 10) == 7 and diff(10, 3) == 7 and last_gap([4, 9, 20]) == 16
