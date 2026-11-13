from __future__ import annotations
from dependent import *


@dataclass
class Array10:
    items: list[int]
    size: len(items) == 10          # a proof: the list has exactly 10 items

    def get(self, i: Nat, ok: (i < 10) == True) -> int:
        inside: (i < len(self.items)) == True = subst(lambda n: Eq[i < n, True], sym(self.size), ok)
        return self.items[i]

    def total(self) -> int:
        return sum(self.items)


a: Array10 = Array10([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], refl())
print(a.get(0, refl()), a.get(9, refl()), a.total())
