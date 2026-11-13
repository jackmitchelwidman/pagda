"""Imports from another checked file and from the built-in lemmas."""
from __future__ import annotations
from dependent import *
from shapes import Point, Shape, Circle, Box, width, digits, origin
from lemmas import monus_step, down_step, plus_comm, len_append


def widest(shapes: list[Shape]) -> Nat:
    best = 0
    for s in shapes:
        best = max(best, width(s))
    return best


def far(points: list[Point], limit: Nat) -> list[Point]:
    return [p for p in points if limit < origin.dist1(p)]


def digit_sum(n: Nat) -> Nat:
    return sum(digits(n))


def triangle(n: Nat) -> Nat:
    i = 0
    total = 0
    while i < n:
        before = i
        i = i + 1
        total = total + i
        decreases(monus(n, i), monus_step(n, before, known(before < n)))
    return total


def countdown(n: Nat) -> list[Nat]:
    out: list[Nat] = []
    k = n
    while 0 < k:
        out.append(k)
        was = k
        k = monus(k, 1)
        decreases(k, down_step(was, known(0 < was)))
    return out


def swap_sum(a: Nat, b: Nat) -> a + b == b + a:
    return plus_comm(a, b)


def joined(xs: list[int], ys: list[int]) -> len(xs + ys) == len(xs) + len(ys):
    return len_append(xs, ys)


shapes: list[Shape] = [Circle(Point(1, 1), 3), Box(Point(0, 0), Point(10, 2))]
print(widest(shapes), far([Point(1, 1), Point(5, -5)], 4), digit_sum(1234), triangle(4), countdown(3))
print(swap_sum(1, 2), joined([1], [2, 3]), origin, Point(1, 2) == Point(1, 2))
assert widest(shapes) == 10
assert far([Point(1, 1), Point(5, -5)], 4) == [Point(5, -5)]
assert triangle(4) == 10
assert countdown(3) == [3, 2, 1]
