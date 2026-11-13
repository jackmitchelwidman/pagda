"""Lists, for loops, early return, append, index loops, and int arithmetic."""
from __future__ import annotations
from dependent import *


def total(xs: list[int]) -> int:
    s = 0
    for x in xs:
        s += x
    return s


def find(xs: list[int], want: int) -> int:
    for i, x in enumerate(xs):
        if x == want:
            return i
    return -1


def squares(n: Nat) -> list[int]:
    out: list[int] = []
    for i in range(n):
        out.append(i * i)
    return out


def firsts(xs: list[int]) -> int:
    s = 0
    for i in range(len(xs)):
        s = s + xs[i]
    return s


def evens(xs: list[int]) -> list[int]:
    return [x for x in xs if x % 2 == 0]


def count_pos(xs: list[int]) -> Nat:
    c = 0
    for x in xs:
        if x <= 0:
            continue
        c += 1
    return c


def first_neg(xs: list[int]) -> int:
    r = 0
    for x in xs:
        if x < 0:
            r = x
            break
    return r


print(total([1, 2, 3, -10]), find([5, 6, 7], 7), find([5], 1), squares(5), firsts([4, 5, 6]))
print(evens([1, 2, 3, 4, -6]), count_pos([1, -2, 3]), first_neg([1, -2, -3]))
print(7 // 2, -7 // 2, 7 % -2, -7 % 2, 2 ** 10, abs(-3), min(2, -5), max(2, 5))
assert total([1, 2, 3, -10]) == -4
assert find([5, 6, 7], 7) == 2
assert find([5], 1) == -1
assert squares(4) == [0, 1, 4, 9]
assert firsts([4, 5, 6]) == 15
assert evens([1, 2, 3, 4, -6]) == [2, 4, -6]
assert count_pos([1, -2, 3]) == 2
assert first_neg([1, -2, -3]) == -2
assert -7 // 2 == -4
assert 7 // -2 == -4
assert -7 // -2 == 3
assert 7 % -2 == -1
assert -7 % 2 == 1
assert -7 % -2 == -1
assert 6 % -3 == 0
assert -6 // 3 == -2
