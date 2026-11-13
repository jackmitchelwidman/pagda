"""A program written the way a Python user would write it: grades and a report."""
from __future__ import annotations
from dependent import *


@dataclass
class Student:
    name: str
    scores: list[int]


def mean(xs: list[int]) -> int:
    if not xs:
        return 0
    return sum(xs) // len(xs)


def best(xs: list[int]) -> Optional[int]:
    result = None
    for x in xs:
        if result is None or x > result:
            result = x
    return result


def grade(score: int) -> str:
    if score >= 90:
        return "A"
    elif score >= 80:
        return "B"
    elif score >= 70:
        return "C"
    return "F"


def histogram(students: list[Student]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for s in students:
        g = grade(mean(s.scores))
        counts[g] = counts.get(g, 0) + 1
    return counts


def top_students(students: list[Student], n: int = 2) -> list[str]:
    ranked = sorted([mean(s.scores) for s in students])
    ranked.reverse()
    names = []
    for target in ranked[:n]:
        for s in students:
            if mean(s.scores) == target and s.name not in names:
                names.append(s.name)
    return names


def report(students: list[Student]) -> None:
    for s in students:
        b = best(s.scores)
        line = f"{s.name}: mean {mean(s.scores)} grade {grade(mean(s.scores))}"
        if b is not None:
            line += f" best {b}"
        print(line)
    hist = histogram(students)
    for g in ["A", "B", "C", "F"]:
        if g in hist:
            print(g, "#" * hist[g])


def main() -> None:
    students = [
        Student("ada", [95, 88, 92]),
        Student("bob", [72, 65, 80]),
        Student("cy", []),
        Student("dee", [85, 91]),
    ]
    report(students)
    print(top_students(students))
    print(top_students(students, n=1))


if __name__ == "__main__":
    main()
