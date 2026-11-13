"""A checked file that other checked files import. It also has a @trusted def."""
from __future__ import annotations
from dependent import *


@dataclass
class Point:
    x: int
    y: int

    def dist1(self, other: Point) -> Nat:
        return abs(self.x - other.x) + abs(self.y - other.y)


class Shape(Data): ...


@constructor
def Circle(centre: Point, radius: Nat) -> Shape: ...


@constructor
def Box(low: Point, high: Point) -> Shape: ...


def width(s: Shape) -> Nat:
    match s:
        case Circle(_, r):
            return r * 2
        case Box(a, b):
            return abs(b.x - a.x)


# The body is ordinary Python that the checker does not read. Its type is trusted.
@trusted
def digits(n: Nat) -> list[Nat]:
    out = []
    while True:
        out.insert(0, n % 10)
        n //= 10
        if n == 0:
            break
    return out


origin: Point = Point(0, 0)
