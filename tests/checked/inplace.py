"""Loops that write into the positions of a list, with no extra bounds test."""
from __future__ import annotations
from dependent import *


def bump(xs: list[int]) -> list[int]:
    ys = list(xs)
    for i in range(len(ys)):
        ys[i] += 1
    return ys


def double_evens(xs: list[int]) -> list[int]:
    ys = list(xs)
    for i in range(len(ys)):
        if ys[i] % 2 == 0:
            ys[i] = ys[i] * 2
        else:
            ys[i] = 0
    return ys


def bubble_sort(xs: list[int]) -> list[int]:
    ys = list(xs)
    for _ in range(len(ys)):
        for j in range(len(ys)):
            if j + 1 < len(ys):
                if ys[j + 1] < ys[j]:
                    tmp = ys[j]
                    ys[j] = ys[j + 1]
                    ys[j + 1] = tmp
    return ys


def prefix_sums(xs: list[int]) -> list[int]:
    out = list(xs)
    running = 0
    for i in range(len(out)):
        running += out[i]
        out[i] = running
    return out


print(bump([1, 2]), double_evens([1, 2, 3, 4]), bubble_sort([3, 1, 2, -5, 3]), prefix_sums([1, 2, 3]))
assert bump([1, 2]) == [2, 3]
assert double_evens([1, 2, 3, 4]) == [0, 4, 0, 8]
assert bubble_sort([3, 1, 2, -5, 3]) == [-5, 1, 2, 3, 3]
assert prefix_sums([1, 2, 3]) == [1, 3, 6]
