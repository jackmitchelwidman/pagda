"""Defs inside functions, swaps through list positions, replace on a class, strip."""
from __future__ import annotations
from dependent import *


@dataclass
class Account:
    owner: str
    balance: int
    history: list[int]


def deposit(a: Account, amount: int) -> Account:
    return replace(a, balance=a.balance + amount, history=a.history + [amount])


def rename(a: Account, owner: str) -> Account:
    return replace(a, owner=owner.strip())


def scale_all(xs: list[int], factor: int) -> list[int]:
    def scale(x: int) -> int:
        return x * factor

    return [scale(x) for x in xs]


def clamp_all(xs: list[int], lo: int, hi: int) -> list[int]:
    def clamp(x: int) -> int:
        if x < lo:
            return lo
        if x > hi:
            return hi
        return x

    out: list[int] = []
    for x in xs:
        out.append(clamp(x))
    return out


def count_matching(words: list[str], prefix: str) -> Nat:
    def matches(w: str) -> bool:
        return w.strip().startswith(prefix)

    n = 0
    for w in words:
        if matches(w):
            n += 1
    return n


def apply_twice(f: Fn[int, int], x: int) -> int:
    return f(f(x))


def add_four(x: int) -> int:
    def add_two(y: int) -> int:
        return y + 2

    return apply_twice(add_two, x)


def reverse_in_place(xs: list[int]) -> list[int]:
    ys = list(xs)
    n = len(ys)
    for i in range(n // 2):
        j = monus(monus(n, 1), i)
        if i < len(ys) and j < len(ys):
            ys[i], ys[j] = ys[j], ys[i]
    return ys


def selection_sort(xs: list[int]) -> list[int]:
    ys = list(xs)
    for i in range(len(ys)):
        best = i
        for j in range(len(ys)):
            if i < j and best < len(ys) and ys[j] < ys[best]:
                best = j
        if best < len(ys):
            ys[i], ys[best] = ys[best], ys[i]
    return ys


def rotate(a: int, b: int, c: int) -> tuple[int, int, int]:
    a, b, c = b, c, a
    return (a, b, c)


acct: Account = Account("  ada ", 10, [])
print(deposit(acct, 5), rename(acct, " bob\n"), scale_all([1, 2], 3), clamp_all([-5, 3, 50], 0, 10))
print(count_matching([" apple", "apricot ", "banana"], "ap"), add_four(1), reverse_in_place([1, 2, 3, 4, 5]))
print(selection_sort([3, 1, 2, -7, 3]), rotate(1, 2, 3), "  x y\t\n".strip() + "|", "  x ".lstrip() + "|", "  x ".rstrip() + "|")
assert deposit(acct, 5).balance == 15 and deposit(acct, 5).history == [5] and deposit(acct, 5).owner == "  ada "
assert rename(acct, " bob\n").owner == "bob"
assert scale_all([1, 2], 3) == [3, 6]
assert clamp_all([-5, 3, 50], 0, 10) == [0, 3, 10]
assert count_matching([" apple", "apricot ", "banana"], "ap") == 2
assert add_four(1) == 5
assert reverse_in_place([1, 2, 3, 4, 5]) == [5, 4, 3, 2, 1] and reverse_in_place([]) == []
assert selection_sort([3, 1, 2, -7, 3]) == [-7, 1, 2, 3, 3]
assert rotate(1, 2, 3) == (2, 3, 1)
assert "  x y\t\n".strip() == "x y" and "".strip() == "" and "  x ".strip() == "x" and "  x ".lstrip() == "x " and "  x ".rstrip() == "  x"
