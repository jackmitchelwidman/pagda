from __future__ import annotations
from dependent import *


@trusted
def file_size(path: str) -> Nat:
    import os
    return os.path.getsize(path)


def is_big(path: str) -> bool:
    return 1000000 < file_size(path)
