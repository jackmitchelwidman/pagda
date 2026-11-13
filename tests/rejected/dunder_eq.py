from __future__ import annotations
from dependent import *

@dataclass
class P:
    x: int

    def __eq__(self, other: P) -> bool:
        return True


assert not (P(1) == P(2))
