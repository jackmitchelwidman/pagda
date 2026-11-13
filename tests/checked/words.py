"""Text processing, searching and while loops, with one proved property."""
from __future__ import annotations
from dependent import *
from lemmas import len_append


def tokenize(text: str) -> list[str]:
    words = []
    current = ""
    for ch in text:
        if ch == " " or ch == "\n":
            if current:
                words.append(current)
                current = ""
        else:
            current += ch
    if current:
        words.append(current)
    return words


def word_counts(words: list[str]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for w in words:
        if w in counts:
            counts[w] += 1
        else:
            counts[w] = 1
    return counts


def longest(words: list[str]) -> str:
    best = ""
    for w in words:
        if len(w) > len(best):
            best = w
    return best


def is_sorted(xs: list[int]) -> bool:
    i = 1
    while i < len(xs):
        if i > 0 and xs[i - 1] > xs[i]:
            return False
        i += 1
    return True


def binary_search(xs: list[int], target: int) -> int:
    lo = 0
    hi = len(xs)
    steps = 0
    while steps < len(xs) and lo < hi:
        mid = (lo + hi) // 2
        if mid < len(xs):
            if xs[mid] == target:
                return mid
            if xs[mid] < target:
                lo = mid + 1
            else:
                hi = mid
        steps += 1
    return -1


def collatz_steps(n: int, limit: int = 1000) -> int:
    steps = 0
    left = limit
    while left > 0:
        if n == 1:
            break
        if n % 2 == 0:
            n = n // 2
        else:
            n = 3 * n + 1
        steps += 1
        left -= 1
    return steps


def fib(n: Nat) -> Nat:
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a


def caesar(text: str, table: dict[str, str]) -> str:
    out = ""
    for ch in text:
        out += table.get(ch, ch)
    return out


# a property, proved once for every pair of lists
def joined_length(a: list[str], b: list[str]) -> len(a + b) == len(a) + len(b):
    return len_append(a, b)


def main() -> None:
    text = "the quick brown fox jumps over the lazy dog the end"
    words = tokenize(text)
    counts = word_counts(words)
    print(len(words), longest(words), counts["the"] if "the" in counts else 0)
    for w, c in counts.items():
        if c > 1:
            print(f"{w} appears {c} times")
    print(is_sorted([1, 2, 2, 5]), is_sorted([3, 1]), binary_search([1, 3, 5, 7, 9], 7), binary_search([1, 3, 5], 4))
    print(collatz_steps(27), fib(10), caesar("abc", {"a": "b", "b": "c"}))


assert tokenize("a  b\nc") == ["a", "b", "c"]
assert fib(10) == 55
assert binary_search([1, 3, 5, 7, 9], 7) == 3
assert is_sorted([1, 2, 2, 5]) and not is_sorted([3, 1])
assert collatz_steps(6) == 8

if __name__ == "__main__":
    main()
