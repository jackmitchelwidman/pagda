from __future__ import annotations
from dependent import *


@dataclass
class Item:
    name: str
    price: int
    count: Nat


def total(items: list[Item]) -> int:
    t = 0
    for item in items:
        t += item.price * item.count
    return t


def main() -> None:
    stock = [Item("pen", 3, 10), Item("ink", 12, 2)]
    for item in stock:
        print(f"{item.name}: {item.price} x {item.count}")
    print("total", total(stock))


if __name__ == "__main__":
    main()
