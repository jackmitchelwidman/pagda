from __future__ import annotations
from dependent import *


class ParseError(Exception): ...


def digit(ch: str) -> int:
    table = {"0": 0, "1": 1, "2": 2, "3": 3, "4": 4, "5": 5, "6": 6, "7": 7, "8": 8, "9": 9}
    if ch in table:
        return table[ch]
    raise ParseError(f"not a digit: {ch}")


def parse(text: str) -> int:
    value = 0
    for ch in text:
        d = digit(ch)                 # can raise: the call stands on its own line
        value = value * 10 + d
    return value


def parse_or(text: str, default: int) -> int:
    try:
        n = parse(text)
    except ParseError as e:
        print("bad input:", e)
        return default
    except Exception:
        return default
    return n


assert parse_or("120", 0) == 120      # worked out by the checker, exceptions and all
print(parse_or("120", 0), parse_or("12x", -1))
