"""Edge values: each assert is worked out by Agda and again by Python."""
from __future__ import annotations
from dependent import *

xs: list[int] = [5, -2, 9]
assert str(-3) == "-3" and str(0) == "0" and str(12) == "12"
assert xs[2:1] == [] and xs[1:1] == [] and xs[0:9] == [5, -2, 9] and xs[3:] == []
assert list(range(3, 1)) == [] and list(range(2, 5)) == [2, 3, 4] and list(range(0)) == []
assert 0 ** 0 == 1 and 7 % 1 == 0 and 0 // 5 == 0 and 9 // 9 == 1
assert 0 // -3 == 0 and -1 // 3 == -1 and 6 // -3 == -2 and -1 % 3 == 2 and 1 % -3 == -2 and 0 % -3 == 0
assert abs(-5) == 5 and abs(5) == 5 and to_nat(-5) == 0 and to_nat(5) == 5
assert sorted([3, -1, 3, 0]) == [-1, 0, 3, 3] and sorted(xs) == [-2, 5, 9]
assert sum([]) == 0 and sum(xs) == 12 and not any([]) and all([])
assert list(zip([1, 2, 3], ["a", "b"])) == [(1, "a"), (2, "b")]
assert list(enumerate(["a", "b"])) == [(0, "a"), (1, "b")]
assert 1 < 2 < 3 and not 1 < 3 < 2 and 2 >= 2 and not 2 != 2
assert 3 not in xs and 9 in xs and -2 in xs
assert min(2, 2) == 2 and max(-1, -5) == -1 and min(-1, -5) == -5
assert (-2) * 3 == -6 and -2 - 3 == -5 and 2 - 5 == -3 and (-2) * (-3) == 6 and -(-4) == 4
assert len({"a": 1, "b": 2, "a": 3}) == 2 and {"a": 1}.get("z", 7) == 7 and "a" in {"a": 1}
assert [1, 2] + [3] == [1, 2, 3] and [] + [1] == [1] and len([[1], []]) == 2
assert "ab" + "c" == "abc" and len("") == 0
assert (1, "a") == (1, "a") and [(1, 2)] == [(1, 2)] and [1, 2] != [2, 1]
print("ok")
