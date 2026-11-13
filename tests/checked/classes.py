"""A while loop with its measure, and classes with fields and methods."""
from __future__ import annotations
from dependent import *


# the fact a counting loop needs: stepping i towards n makes n - i smaller
def closer(n: Nat, i: Nat, p: (i < n) == True) -> (monus(n, i + 1) < monus(n, i)) == True:
    if n == 0:
        return absurd(false_true(p))
    if i == 0:
        return lt_suc(n - 1)
    return closer(n - 1, i - 1, p)


def false_true(p: False == True) -> Empty:
    match p:
        case _:
            return unreachable()


def lt_suc(n: Nat) -> (n < n + 1) == True:
    if n == 0:
        return refl()
    return lt_suc(n - 1)


def count_up(n: Nat) -> Nat:
    i = 0
    total = 0
    while i < n:
        j = i
        total = total + i
        i = i + 1
        decreases(monus(n, i), closer(n, j, known(j < n)))
    return total


@dataclass
class Point:
    x: int
    y: int

    def norm1(self) -> Nat:
        return abs(self.x) + abs(self.y)

    def shift(self, dx: int) -> Point:
        return Point(self.x + dx, self.y)


@dataclass
class Box[A]:
    item: A
    count: Nat

    def more(self) -> Box[A]:
        return Box(self.item, self.count + 1)


def flip(p: Point) -> Point:
    match p:
        case Point(a, b):
            return Point(x=b, y=a)


p: Point = Point(3, -4)
b: Box[str] = Box("a", 1)
print(count_up(5), p, p.norm1(), p.shift(2), flip(p), p.x, b.more(), b.more().count, p == Point(3, -4))
assert count_up(5) == 10
assert p.norm1() == 7
assert p.shift(2) == Point(5, -4)
assert flip(p).x == -4
assert b.more().count == 2
