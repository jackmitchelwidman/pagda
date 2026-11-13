"""Strings, the last item of a list, pop and insert, ranges over ints, and while loops that need no proof."""
from __future__ import annotations
from dependent import *


def initials(names: list[str]) -> str:
    out = ""
    for name in names:
        if name:
            out += name[0]
    return out


def shout(s: str, times: Nat = 2) -> str:
    return s * times + "!"


def is_palindrome(s: str) -> bool:
    letters = [ch for ch in s if ch != " "]
    return letters == list(reversed(letters))


def csv_total(line: str) -> Nat:
    return len(line.split(","))


def tidy_path(p: str) -> str:
    if p.startswith("./"):
        return p[2:]
    if p.endswith("/"):
        return p[:-1]
    return p.replace("//", "/")


def last_or(xs: list[int], default: int) -> int:
    if xs:
        return xs[-1]
    return default


def drain(xs: list[int]) -> list[int]:
    stack = list(xs)
    out: list[int] = []
    for _ in range(len(xs)):
        if stack:
            top = stack.pop()
            out.append(top)
    return out


def put_first(xs: list[int], x: int) -> list[int]:
    ys = list(xs)
    ys.insert(0, x)
    ys.insert(99, x)
    return ys


def window(lo: int, hi: int) -> list[int]:
    return [k * k for k in range(lo, hi)]


def neighbours(xs: list[int]) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for i in range(len(xs) - 1):
        if i + 1 < len(xs):
            out.append((xs[i], xs[i + 1]))
    return out


def triangle(n: Nat) -> Nat:
    i = 0
    total = 0
    while i < n:
        total += i
        i += 1
    return total


def find_from(xs: list[int], want: int, start: Nat = 0) -> int:
    i = start
    while i < len(xs):
        if xs[i] == want:
            return i
        i = i + 1
    return -1


def countdown(n: int) -> list[int]:
    out: list[int] = []
    k = n
    while k > 0:
        out.append(k)
        k -= 3
    return out


print(initials(["ada", "", "bob"]), shout("hey"), shout("x", 3), is_palindrome("never odd or even"), is_palindrome("no"))
print(csv_total("a,b,,c"), tidy_path("./x"), tidy_path("x/"), tidy_path("a//b//c"), "ell" in "hello", "abc" < "abd", "b" >= "a")
print(last_or([1, 2, 3], 0), last_or([], 9), drain([1, 2, 3]), put_first([1, 2], 7), window(-2, 3), neighbours([1, 2, 3]), neighbours([]))
print(triangle(5), find_from([4, 5, 6], 6), find_from([4, 5, 6], 4, 1), countdown(10), ", ".join(["a", "b"]), "a-b-c".split("-"))
assert initials(["ada", "", "bob"]) == "ab"
assert shout("x", 3) == "xxx!" and shout("", 0) == "!"
assert is_palindrome("never odd or even") and not is_palindrome("no")
assert "a,b,,c".split(",") == ["a", "b", "", "c"] and "".split(",") == [""] and "abc".split("bc") == ["a", ""]
assert "a--b".split("--") == ["a", "b"] and ",".split(",") == ["", ""]
assert "a//b//c".replace("//", "/") == "a/b/c" and "aaa".replace("aa", "b") == "ba"
assert "-".join([]) == "" and "-".join(["a"]) == "a" and "-".join(["a", "b", "c"]) == "a-b-c"
assert "hello"[1] == "e" and "hello"[1:3] == "el" and "hello"[3:] == "lo" and "hello"[:9] == "hello" and "hello"[4:2] == ""
assert "ell" in "hello" and "" in "hello" and "lo" in "hello" and not "le" in "hello" and "" in ""
assert "abc" < "abd" and "ab" < "abc" and not "abc" < "abc" and "b" > "abc" and "" < "a" and "a" <= "a"
assert "hello".startswith("he") and "hello".endswith("lo") and not "hello".startswith("lo") and "hi".startswith("")
assert [1, 2, 3][:-1] == [1, 2] and [1, 2, 3][-2:] == [2, 3] and [1][-3:] == [1] and [1][:-3] == [] and "abc"[1:-1] == "b"
assert last_or([1, 2, 3], 0) == 3 and last_or([], 9) == 9
assert drain([1, 2, 3]) == [3, 2, 1]
assert put_first([1, 2], 7) == [7, 1, 2, 7]
assert window(-2, 3) == [4, 1, 0, 1, 4] and window(3, -2) == []
assert neighbours([1, 2, 3]) == [(1, 2), (2, 3)] and neighbours([]) == []
assert triangle(5) == 10 and find_from([4, 5, 6], 4, 1) == -1 and countdown(10) == [10, 7, 4, 1]
assert [ch for ch in "héy"] == ["h", "é", "y"]
