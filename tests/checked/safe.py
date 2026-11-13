"""Ordinary code beside dependent types: proofs as arguments, Vec, a class that carries a proof."""
from __future__ import annotations
from dependent import *


# ---- functions that ask their caller for a reason ----

def at(xs: list[int], i: Nat, ok: (i < len(xs)) == True) -> int:
    return xs[i]


def ratio(a: int, b: int, ok: (b != 0) == True) -> int:
    return a // b


def first(xs: list[int], ok: (len(xs) > 0) == True) -> int:
    return xs[0]


def middle(xs: list[int]) -> int:
    if len(xs) > 0:
        return at(xs, len(xs) // 2, known(len(xs) // 2 < len(xs))) if len(xs) // 2 < len(xs) else first(xs, known(len(xs) > 0))
    return 0


def average(xs: list[int]) -> int:
    n = len(xs)
    if n != 0:
        return ratio(sum(xs), n, known(n != 0))
    return 0


# ---- a list whose length is part of its type ----

class Vec[A, n: Nat](Data): ...


@constructor
def Nil[A]() -> Vec[A, 0]: ...


@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...


def to_list[A, n: Nat](v: Vec[A, n]) -> list[A]:
    match v:
        case Nil():
            return []
        case Cons(x, rest):
            return [x] + to_list(rest)


def dot[n: Nat](a: Vec[int, n], b: Vec[int, n]) -> int:
    match a:
        case Nil():
            return 0
        case Cons(x, xs):
            match b:
                case Cons(y, ys):
                    return x * y + dot(xs, ys)


def length_kept[A, n: Nat](v: Vec[A, n]) -> len(to_list(v)) == n:
    match v:
        case Nil():
            return refl()
        case Cons(x, rest):
            return cong(suc, length_kept(rest))


# ---- a class that carries a proof ----

@dataclass
class Percent:
    value: int
    low: (0 <= value) == True
    high: (value <= 100) == True


def percent(x: int) -> Optional[Percent]:
    if 0 <= x and x <= 100:
        return Percent(x, known(0 <= x), known(x <= 100))
    return None


def remaining(p: Percent) -> Nat:
    return to_nat(100 - p.value)


def main() -> None:
    xs = [4, 8, 15, 16, 23, 42]
    print(middle(xs), middle([]), average(xs), average([]))
    v: Vec[int, 3] = Cons(1, Cons(2, Cons(3, Nil())))
    w: Vec[int, 3] = Cons(4, Cons(5, Cons(6, Nil())))
    print(dot(v, w), to_list(v), length_kept(v))
    p = percent(30)
    q = percent(130)
    if p is not None:
        print(p.value, remaining(p))
    print(q is None)


assert dot(Cons(1, Cons(2, Nil())), Cons(3, Cons(4, Nil()))) == 11
assert average([4, 8]) == 6
assert middle([1, 2, 3]) == 2

if __name__ == "__main__":
    main()
