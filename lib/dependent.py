"""What a checked file imports.

A file that imports from this module is type-checked before it runs. This module
is the part that runs afterwards: by then the types have done their work, so
most things here are small. It runs the same under python3 and pyc.

    from __future__ import annotations
    from dependent import *

    class Vec[A, n: Nat](Data): ...

    @constructor
    def Nil[A]() -> Vec[A, 0]: ...

    @constructor
    def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...
"""

__all__ = ["Type", "Nat", "Data", "constructor", "Callable", "Eq", "Refl", "refl", "suc", "pred", "monus",
           "concat", "sym", "trans", "cong", "subst", "Empty", "absurd", "unreachable", "Fn", "Dec", "Yes", "No",
           "decide"]

Type = type
Nat = int


class Callable:
    """Callable[[A, B], C] is the type of functions from A and B to C."""

    def __class_getitem__(cls, item):
        return cls


class Fn:
    """Fn[A, B, C] is the type of functions from A and B to C, the same as
    Callable[[A, B], C]. An argument can be given a name, and the types after it
    can then mention it: Fn[k: Nat, Vec[A, k]] takes a number k and gives a Vec of
    that length. The named form is for annotations."""

    def __class_getitem__(cls, item):
        return cls


class Data:
    """Base of every declared family of types. A constructor is a subclass whose
    fields are listed in __match_args__."""
    __match_args__ = ()

    def __init__(self, *args):
        names = type(self).__match_args__
        if len(args) != len(names):
            raise TypeError(f"{type(self).__name__}() takes {len(names)} arguments but {len(args)} were given")
        for name, value in zip(names, args):
            setattr(self, name, value)

    def __repr__(self):
        fields = ", ".join(repr(getattr(self, name)) for name in type(self).__match_args__)
        return f"{type(self).__name__}({fields})"

    def __eq__(self, other):
        if type(self) is not type(other):
            return False
        return all(getattr(self, name) == getattr(other, name) for name in type(self).__match_args__)

    def __hash__(self):
        return hash((type(self).__name__,) + tuple(getattr(self, name) for name in type(self).__match_args__))

    def __class_getitem__(cls, item):
        return cls


def constructor(fn):
    """Turns "def Cons(head, tail) -> Vec[...]: ..." into the class Cons, a subclass of Vec."""
    code = fn.__code__
    fields = tuple(code.co_varnames[:code.co_argcount])
    family = fn.__globals__[fn.__annotations__["return"].split("[")[0].strip()]
    return type(fn.__name__, (family,), {"__match_args__": fields, "__module__": fn.__module__})


class Eq(Data):
    """Eq[x, y] is the type of proofs that x equals y. In an annotation, x == y means the same."""


class Refl(Eq):
    """The one proof of equality: a thing equals itself."""


def refl():
    return Refl()


def sym(p):
    return p


def trans(p, q):
    return q


def cong(f, p):
    return p


def subst(family, p, value):
    return value


def suc(n):
    return n + 1


def pred(n):
    return n - 1 if n > 0 else 0


def monus(a, b):
    return a - b if a > b else 0


def concat(a, b):
    """Joins two strings. a + b also works when one side is a string literal."""
    return a + b


class Dec(Data):
    """Dec[c] is what decide(c) returns: Yes(p) with p a proof of c == True, or
    No(p) with p a proof of c == False."""


class Yes(Dec):
    __match_args__ = ("proof",)


class No(Dec):
    __match_args__ = ("proof",)


def decide(c):
    """Written as "match decide(c):" with "case Yes(p):" and "case No(p):". Inside
    each case the type checker knows which value c has, and p is the proof."""
    return Yes(Refl()) if c else No(Refl())


class Empty(Data):
    """The type with no values."""


def absurd(value):
    raise AssertionError("absurd() was reached; a value of the Empty type cannot exist")


def unreachable():
    """Written as "case _: return unreachable()" where no value can arrive. The type
    checker accepts it only when that is so."""
    raise AssertionError("unreachable() was reached")
