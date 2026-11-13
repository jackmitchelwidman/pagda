"""Everyday program shape: print and f-strings in functions, None, Optional, defaults."""
from __future__ import annotations
from dependent import *


@dataclass
class Item:
    name: str
    price: int
    count: Nat


def find(items: list[Item], name: str) -> Optional[Item]:
    for item in items:
        if item.name == name:
            return item
    return None


def price_of(items: list[Item], name: str, missing: int = -1) -> int:
    item = find(items, name)
    if item is None:
        return missing
    return item.price


def first_cheap(items: list[Item], limit: int) -> str | None:
    for item in items:
        if item.price < limit:
            return item.name
    return None


def label(n: int, unit: str = "item", plural: bool = True) -> str:
    if n == 1 or not plural:
        return f"{n} {unit}"
    return f"{n} {unit}s"


def describe(items: list[Item]) -> None:
    if not items:
        print("nothing here")
        return
    for item in items:
        print(f"{item.name}: {item.price} x {item.count}", item)
    print("total", total(items), sep=": ")


def total(items: list[Item]) -> int:
    t = 0
    for item in items:
        t += item.price * item.count
    return t


def average(xs: list[int]) -> int:
    if xs:
        return sum(xs) // len(xs)
    return 0


def halve(n: int, by: int = 2) -> int:
    if by:
        return n // by
    return 0


def checked_index(xs: list[int], i: Nat) -> int:
    if i < len(xs):
        assert i < len(xs)
        return xs[i]
    return 0


def lookup(d: dict[str, int], key: str) -> int:
    value = d.get(key)
    if value is not None:
        return value + 1
    return 0


def chatty(n: Nat) -> Nat:
    print("got", n)
    describe([])
    if n == 0:
        print("zero")
        return 0
    m = n * 2
    print(f"double is {m}")
    return m


def main() -> None:
    stock = [Item("pen", 3, 10), Item("ink", 12, 2)]
    describe(stock)
    describe([])
    print(price_of(stock, "ink"), price_of(stock, "cup"), price_of(stock, "cup", missing=0))
    print(first_cheap(stock, 5), first_cheap(stock, 1), find(stock, "cup"))
    print(label(1), label(3), label(3, "box", False), label(2, unit="cup"))
    print(average([1, 2, 6]), average([]), halve(9), halve(9, 3), checked_index([4, 5], 1), lookup({"a": 1}, "a"), lookup({}, "a"))
    print(chatty(2), chatty(0))


class Pair[A, B](Data): ...


@constructor
def MkPair[A, B](left: A, right: B) -> Pair[A, B]: ...


swapped: Pair[str, int] = MkPair(right=1, left="a")
print(swapped, MkPair(1, right=2))
assert price_of([Item("pen", 3, 10)], "pen") == 3
assert price_of([], "pen") == -1
assert first_cheap([Item("pen", 3, 10)], 5) == "pen"
assert first_cheap([Item("pen", 3, 10)], 1) is None
assert label(3) == "3 items" and label(1) == "1 item" and label(-2, "cup") == "-2 cups"
assert f"{True} {0} {-7}" == "True 0 -7"
assert average([1, 2, 6]) == 3 and average([]) == 0
assert lookup({"a": 1}, "a") == 2 and lookup({"a": 1}, "b") == 0
assert total([Item("pen", 3, 10), Item("ink", 12, 2)]) == 54
assert chatty(3) == 6

if __name__ == "__main__":
    main()
