"""Definitions used above the line that defines them, and families that refer to each other."""
from __future__ import annotations
from dependent import *


# mutual recursion: is_odd is defined below its first use
def is_even(n: Nat) -> bool:
    if n == 0:
        return True
    return is_odd(n - 1)


def is_odd(n: Nat) -> bool:
    if n == 0:
        return False
    return is_even(n - 1)


# a constructor used above its class
def singleton(x: Nat) -> Tree:
    return Node(Leaf(), x, Leaf())


def size(t: Tree) -> Nat:
    match t:
        case Leaf():
            return 0
        case Node(l, _, r):
            return size(l) + size(r) + 1


class Tree(Data): ...


@constructor
def Leaf() -> Tree: ...


@constructor
def Node(left: Tree, value: Nat, right: Tree) -> Tree: ...


def insert(t: Tree, x: Nat) -> Tree:
    match t:
        case Leaf():
            return singleton(x)
        case Node(l, v, r):
            if x < v:
                return Node(insert(l, x), v, r)
            return Node(l, v, insert(r, x))


def to_list(t: Tree, acc: NatList) -> NatList:
    match t:
        case Leaf():
            return acc
        case Node(l, v, r):
            return to_list(l, More(v, to_list(r, acc)))


class NatList(Data): ...


@constructor
def Done() -> NatList: ...


@constructor
def More(first: Nat, rest: NatList) -> NatList: ...


# two families of proofs that refer to each other: n is even, n is odd
class Even[n: Nat](Data): ...


class Odd[n: Nat](Data): ...


@constructor
def EvenZero() -> Even[0]: ...


@constructor
def EvenNext[n: Nat](before: Odd[n]) -> Even[n + 1]: ...


@constructor
def OddNext[n: Nat](before: Even[n]) -> Odd[n + 1]: ...


def four_is_even() -> Even[4]:
    return EvenNext(OddNext(EvenNext(OddNext(EvenZero()))))


def even_plus_two[n: Nat](p: Even[n]) -> Even[n + 2]:
    return EvenNext(OddNext(p))


def steps[n: Nat](p: Even[n]) -> Nat:
    match p:
        case EvenZero():
            return 0
        case EvenNext(OddNext(q)):
            return steps(q) + 1


# a type worked out by a function that is defined below the function using it
def default_of(b: bool) -> pick_type(b):
    match b:
        case True:
            return 0
        case False:
            return "none"


def pick_type(b: bool) -> Type:
    if b:
        return Nat
    return str


t: Tree = insert(insert(insert(insert(Leaf(), 5), 2), 8), 6)
print(is_even(10), is_odd(10), is_even(7), is_odd(7))
print(t)
print(size(t), to_list(t, Done()))
print(four_is_even(), steps(even_plus_two(four_is_even())))
print(default_of(True), default_of(False))
assert size(t) == 4
assert steps(four_is_even()) == 2
