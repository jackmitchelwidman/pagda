"""Exceptions: raise, try / except, classes of exception, re-raising, and one that nothing catches."""
from __future__ import annotations
from dependent import *


class ParseError(Exception): ...


class EmptyInput(ParseError): ...


def digit(ch: str) -> int:
    table = {"0": 0, "1": 1, "2": 2, "3": 3, "4": 4, "5": 5, "6": 6, "7": 7, "8": 8, "9": 9}
    if ch in table:
        return table[ch]
    raise ParseError(f"not a digit: {ch}")


def parse(text: str) -> int:
    if not text:
        raise EmptyInput("nothing to parse")
    value = 0
    for ch in text:
        d = digit(ch)
        value = value * 10 + d
    return value


def parse_or(text: str, default: int) -> int:
    try:
        n = parse(text)
        return n
    except EmptyInput:
        return default
    except ParseError as e:
        print("bad input:", e)
        return default
    except Exception:
        return default


def describe(text: str) -> str:
    try:
        n = parse(text)
    except ParseError as e:
        return "error: " + str(e)
    except Exception:
        return "other"
    return f"ok {n}"


def checked_ratio(a: int, b: int) -> int:
    if b == 0:
        raise ZeroDivisionError("b is zero")
    return a // b


def safe_ratio(a: int, b: int) -> int:
    try:
        return checked_ratio(a, b)
    except ZeroDivisionError:
        return 0
    except Exception:
        return -1


def classify(n: int) -> str:
    try:
        if n < 0:
            raise ValueError("negative")
        if n == 0:
            raise RuntimeError("zero")
        kind = "positive"
    except ValueError as e:
        kind = str(e)
    except RuntimeError:
        kind = "nothing"
    return kind


def rethrow(text: str) -> int:
    try:
        n = parse(text)
    except EmptyInput:
        raise
    except ParseError as e:
        raise ValueError("wrapped: " + str(e))
    return n


def outer(text: str) -> str:
    try:
        v = rethrow(text)
    except ValueError as e:
        return str(e)
    except Exception as e:
        return "caught " + str(e)
    return str(to_nat(v))


def main() -> None:
    print(parse_or("123", -1), parse_or("", -1), parse_or("12x", -1))
    print(describe("42"), "|", describe("4a"), "|", describe(""))
    print(safe_ratio(7, 2), safe_ratio(7, 0), classify(-1), classify(0), classify(5))
    print(outer("77"), "|", outer("7z"), "|", outer(""))
    last = parse("9x")        # nothing catches this one: the program ends here, as in Python
    print(last)


assert parse_or("123", -1) == 123 and parse_or("", -1) == -1 and parse_or("12x", 7) == 7
assert describe("42") == "ok 42" and describe("4a") == "error: not a digit: a" and describe("") == "error: nothing to parse"
assert safe_ratio(7, 2) == 3 and safe_ratio(7, 0) == 0
assert classify(-1) == "negative" and classify(0) == "nothing" and classify(5) == "positive"
assert outer("77") == "77" and outer("7z") == "wrapped: not a digit: z" and outer("") == "caught nothing to parse"

if __name__ == "__main__":
    main()
