"""Each way a function body can turn into Agda clauses."""
from __future__ import annotations
from dependent import *


class Vec[A, n: Nat](Data): ...


@constructor
def Nil[A]() -> Vec[A, 0]: ...


@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...


class Shape(Data): ...


@constructor
def Circle(r: Nat) -> Shape: ...


@constructor
def Rect(w: Nat, h: Nat) -> Shape: ...


@constructor
def Dot() -> Shape: ...


def double(n: Nat) -> Nat:
    if n == 0:
        return 0
    return double(n - 1) + 2


def is_even(n: Nat) -> bool:
    if n != 0:
        m = n - 1
        if m == 0:
            return False
        return is_even(m - 1)
    else:
        return True


# assignment before a split: plain and annotated
def classify(n: Nat) -> str:
    d = double(n)
    if d == 0:
        return "nothing"
    e: Nat = d - 1
    if e > 0:
        return "several"
    return "one"


# a Bool test with a match inside one branch
def area(big: bool, s: Shape) -> Nat:
    if big:
        match s:
            case Circle(r):
                return r * r * 3
            case Rect(w, h):
                return w * h
            case Dot():
                return 0
    return 1


# match on the result of a call
def first_or[A, n: Nat](xs: Vec[A, n], default: A) -> A:
    match xs:
        case Nil():
            return default
        case Cons(x, _):
            return x


def pick(n: Nat) -> Shape:
    if n == 0:
        return Dot()
    if n == 1:
        return Circle(5)
    return Rect(n, n)


def describe(n: Nat) -> str:
    match pick(n):
        case Dot():
            return "dot"
        case Circle(_):
            return "circle"
        case Rect(_, _):
            return "rect"


# straight-line code: lets and nested ifs, with no split
def clamp(lo: Nat, hi: Nat, x: Nat) -> Nat:
    a = x
    if a < lo:
        return lo
    b: Nat = a
    if hi < b:
        c = hi
        return c
    elif b == hi:
        return hi
    else:
        return b


# a body annotation that mentions names in square brackets
def rev_onto[A, n: Nat, m: Nat](xs: Vec[A, n], acc: Vec[A, m]) -> Vec[A, m]:
    keep: Vec[A, m] = acc
    return keep


def replicate[A](n: Nat, x: A) -> Vec[A, n]:
    if n == 0:
        return Nil()
    rest: Vec[A, n - 1] = replicate(n - 1, x)
    return Cons(x, rest)


# statements after a match: reached when no case returns
def rank(s: Shape) -> Nat:
    match s:
        case Dot():
            return 0
        case Circle(r):
            size = r
        case Rect(w, h):
            size = w + h
    return size + 1


def after(s: Shape) -> str:
    match s:
        case Dot():
            return "dot"
    return "not a dot"


# | and as, literal patterns, a bare name as a capture
def kind(s: Shape) -> str:
    match s:
        case Dot() | Circle(0):
            return "point"
        case Circle(1) as c:
            return "unit " + name(c)
        case Rect(0, _) | Rect(_, 0):
            return "flat"
        case Nil:
            return "other " + name(Nil)


def name(s: Shape) -> str:
    match s:
        case Circle(_):
            return "circle"
        case Rect(_, _):
            return "rect"
        case _:
            return "dot"


def word(n: Nat) -> str:
    match n:
        case 0:
            return "zero"
        case 1:
            return "one"
        case _:
            return "many"


def flag(b: bool, s: str) -> Nat:
    match b:
        case True:
            match s:
                case "yes":
                    return 2
                case _:
                    return 1
        case False:
            return 0


# functions as values
def twice(f: Callable[[Nat], Nat], x: Nat) -> Nat:
    return f(f(x))


def fold[A, B, n: Nat](f: Callable[[A, B], B], z: B, xs: Vec[A, n]) -> B:
    match xs:
        case Nil():
            return z
        case Cons(x, rest):
            return f(x, fold(f, z, rest))


def force[A](thunk: Callable[[], A]) -> A:
    return thunk()


def add(a: Nat, b: Nat) -> Nat:
    return a + b


def cons_nat(x: Nat, xs: Vec[Nat, 2]) -> Vec[Nat, 3]:
    return Cons(x, xs)


def apply2(f: Callable[[Nat, Vec[Nat, 2]], Vec[Nat, 3]], xs: Vec[Nat, 2]) -> Vec[Nat, 3]:
    return f(7, xs)


def adder(k: Nat) -> Callable[[Nat], Nat]:
    return lambda x: x + k


# names that mean something to Agda
def data(zero: Nat, where: Nat) -> Nat:
    let = zero + where
    return let


def zero(Cons: Nat, Nil: Nat) -> Nat:
    return Cons + Nil


def snake_case_name(some_value: Nat, _private: Nat) -> Nat:
    __x = some_value * _private
    return __x


three: Vec[Nat, 3] = Cons(1, Cons(2, Cons(3, Nil())))
inc: Callable[[Nat], Nat] = lambda x: x + 1
text: str = "quote \" backslash \\ newline \n tab \t é ∀ 日本"

print(double(4), is_even(10), is_even(7))
print(classify(0), classify(1), classify(5))
print(area(True, Circle(2)), area(True, Rect(3, 4)), area(True, Dot()), area(False, Dot()))
print(first_or(three, 9), first_or(Nil(), 9))
print(describe(0), describe(1), describe(2))
print(clamp(2, 5, 1), clamp(2, 5, 9), clamp(2, 5, 5), clamp(2, 5, 3))
print(rev_onto(three, Cons(0, Nil())), replicate(3, "x"))
print(rank(Dot()), rank(Circle(4)), rank(Rect(2, 3)), after(Dot()), after(Circle(1)))
print(kind(Dot()), kind(Circle(0)), kind(Circle(1)), kind(Circle(2)), kind(Rect(0, 3)), kind(Rect(3, 0)), kind(Rect(1, 1)))
print(word(0), word(1), word(2), flag(True, "yes"), flag(True, "no"), flag(False, "yes"))
print(twice(inc, 5), twice(adder(10), 1), twice(double, 3), twice(suc, 0))
print(fold(add, 0, three), fold(lambda x, acc: x * acc, 1, three), force(lambda: 42))
print(apply2(cons_nat, Cons(8, Cons(9, Nil()))))
print(data(1, 2), zero(3, 4), snake_case_name(5, 6))
print(text, text == "x", "a" == "a", True == False)
print(1 < 2 <= 2, 3 >= 4, not (1 == 1) or 2 != 3, 1 if True and False else 0, monus(3, 5), monus(5, 3), pred(0), pred(4))
assert double(21) == 42
assert is_even(100)
assert fold(add, 0, three) == 6, "sum"

if __name__ == "__main__":
    total: Nat = fold(add, 0, replicate(4, 5))
    print(total)
    assert total == 20
