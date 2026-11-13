# pyc

A Python 3 interpreter written in C. It reads Python source, compiles it to bytecode, and runs the bytecode on a stack machine.

## Build and run

```
make                 # builds ./pyc
./pyc script.py      # run a file
./pyc -c 'print(1)'  # run a string
./pyc                # interactive prompt
echo 'print(2)' | ./pyc
```

Needs a C compiler (gcc or clang) and make. No other dependencies.

## Tests

```
make test         # runs every test; see below
make test-debug   # same, on a build with address and undefined-behavior checks,
                  # collecting garbage every 13 allocations
```

There are three groups:

- `tests/NN_*.py` run under `./pyc` and under `python3`. Output and exit code must match.
- `tests/checked/*.py` are checked files (see below). They must pass the type checker and then match `python3`.
- `tests/rejected/*.py` must be refused before they run, with the exact message stored in the `.expected` file beside each one.

`python3` (3.12 or newer) must be installed. It is the reference the output is compared against. The last two groups also need `agda`. Without it they are skipped and the run prints a notice.

Two environment variables help when hunting memory bugs:

- `PYC_GC_STRESS=n` collects every n allocations and allocates each object with plain `malloc`, so AddressSanitizer sees any use of a freed object.
- `PYC_GC_POISON=n` collects every n allocations with the normal allocator and overwrites freed objects.

## Layout

| File | What it holds |
| --- | --- |
| `src/parse.c` | lexer and parser, producing a syntax tree |
| `src/compile.c` | scope analysis and bytecode generation |
| `src/vm.c` | the bytecode loop, calls, exceptions, generators |
| `src/object.c`, `src/ops.c` | object types, attribute lookup, arithmetic, classes, sorting |
| `src/int.c` | integers of any size |
| `src/gc.c` | allocator and mark-and-sweep garbage collector |
| `src/builtins.c`, `src/methods.c` | built-in functions, methods, string formatting |
| `src/modules.c` | `math`, `sys`, `time`, `random`, `gc`, the import system |
| `src/agda.c` | checked files: translation to Agda, running Agda, error reports |
| `lib/*.py` | standard-library modules written in Python, embedded in the binary |
| `lib/dependent.py` | the module a checked file imports; the part that runs |
| `lib/PycPrelude.agda` | the Agda meaning of each name in `dependent`, embedded in the binary |

## What works

- All statements except `async` and `type X = ...`: functions with every kind of parameter, closures, decorators, classes with multiple inheritance and `super()`, properties, descriptors, `__slots__`, private name mangling, generators with `send`/`throw`/`close` and `yield from`, comprehensions, `try`/`except`/`else`/`finally`, `with`, `match` with every kind of pattern, f-strings, the walrus operator, star unpacking, `global`/`nonlocal`, `import` of `.py` files and packages.
- Type parameters in square brackets (`def f[T](x: T)`, `class Box[T]`), function `__annotations__`, and `from __future__ import annotations`.
- Types: `int` (any size), `float`, `bool`, `str` (Unicode), `list`, `tuple`, `dict` (keeps insertion order), `set`, `frozenset`, `range`, `slice`, files. Classes can extend `int`, `float`, `str`, `tuple`, `list`, `dict`, `set` and exceptions.
- The usual built-in functions and methods, `%` formatting, `str.format`, format specs.
- Modules: `math`, `sys`, `time`, `random` (same number stream as CPython for integer seeds), `gc`, `os` (a small part), `json`, `collections`, `functools`, `itertools`, `dataclasses`, `typing`, `heapq`, `bisect`, `operator`, `string`, `copy`, `abc`.
- Error messages and tracebacks follow CPython's wording for the common cases.

## What is missing

- `bytes` and `bytearray`, binary files, `str.encode`.
- Complex numbers.
- `async`/`await`, metaclasses, the `type X = ...` statement, `\N{...}` string escapes.
- Modules not listed above, including `re`, `enum`, `datetime`, `pathlib`, `subprocess`.
- `exec` and `eval` with a separate locals mapping.

## Differences from CPython

- Sets iterate and print in insertion order. CPython orders them by hash, so `print({3, 1, 2})` shows `{3, 1, 2}` here and `{1, 2, 3}` there.
- `__del__` and the cleanup of an abandoned generator run at a later garbage collection or at exit. CPython usually runs them the moment the last reference goes away.
- Module-level annotations (`x: int = 1` outside a class or function) are not stored in `__annotations__`.
- A sequence pattern in `match` accepts lists, tuples and ranges. CPython also accepts any class registered as a `Sequence`; mapping patterns accept `dict` only.
- The bound in `def f[T: bound]` is parsed and never evaluated, and `T.__bound__` does not exist.
- `list[int]` returns `list` itself.
- Tracebacks have no `^^^` markers under the failing expression.
- Ctrl-C takes effect at the next loop iteration.
- `range` bounds must fit in 64 bits. `random.seed` accepts integers only.
- File encodings: `utf-8`, `ascii`, `latin-1`.
- Building a long string with `s += piece` in a loop copies the string each time, so it slows down as the string grows. `"".join(parts)` is fast.
- Converting a very large `int` to `str` has no digit limit (CPython stops at 4300 digits unless told otherwise).

## Checked files (dependent types)

A file that imports `dependent` is type-checked before it runs. `pyc` translates the file to Agda, runs the `agda` program on the translation, and runs the Python only if Agda accepts it. Agda is a language whose types can mention values. "A list of length `n`" is a type there, and so is "a proof that `a + b` equals `b + a`".

This needs `agda` on the `PATH` (developed with Agda 2.8.0), or `PYC_AGDA` set to its path. If `agda` cannot be started, a checked file is refused and does not run.

```python
from __future__ import annotations
from dependent import *

class Vec[A, n: Nat](Data): ...              # a list whose length is part of its type

@constructor
def Nil[A]() -> Vec[A, 0]: ...

@constructor
def Cons[A, n: Nat](head: A, tail: Vec[A, n]) -> Vec[A, n + 1]: ...

def head[A, n: Nat](xs: Vec[A, n + 1]) -> A:   # only accepts a non-empty Vec
    match xs:
        case Cons(x, _):
            return x

def plus_zero(n: Nat) -> n + 0 == n:           # a proof, by recursion on n
    if n == 0:
        return refl()
    return cong(suc, plus_zero(n - 1))

v: Vec[Nat, 2] = Cons(1, Cons(2, Nil()))
print(head(v))
print(head(Nil()))                             # refused: 0 is not n + 1
```

```
./pyc file.py           # check, then run
./pyc --check file.py   # check only
./pyc --agda file.py    # print the Agda text the file turns into
```

A file that passed is remembered in `~/.cache/pyc` (or `$XDG_CACHE_HOME/pyc`), keyed by its translated text, so Agda runs again only when the file changes.

The same file runs under `python3` 3.12 or newer with `lib/dependent.py` beside it, and prints the same output. There the types are not checked.

### What a checked file contains

- **The first lines** are `from __future__ import annotations` and `from dependent import ...`. No other imports.
- **Types:** `Nat` (0, 1, 2, ...), `bool`, `str`, `Type` (the type of types), `Callable[[A, B], C]`, `Fn[...]`, `Eq[x, y]`, `Dec[c]`, `Empty`, and the families the file declares.
- **Function types:** `Fn[A, B, C]` means the same as `Callable[[A, B], C]`: the last item is the result. An argument can be given a name, and the types after it can then use it. `Fn[k: Nat, Vec[A, k]]` takes a number `k` and gives a `Vec` of that length. The named form is for annotations only.
- **A family of types** is `class Name[params](Data): ...`. Each constructor is a `def` marked `@constructor` with `...` as its body. Its parameters are the fields, and its return annotation says which member of the family it builds.
- **Names in square brackets** (`def f[A, n: Nat]`) exist only for the checker. They can appear in annotations and nowhere else, because they are gone when the program runs. A name without a `: bound` is a type.
- **A function body** is built from assignments, `if`, `match` and `return`. Every path must return. Every parameter and the result must be annotated.
- **Numbers:** `+` and `*`. `if n == 0:` splits into the zero case and the other case, and in the other case `n - 1` is allowed. `monus(a, b)` is subtraction that stops at 0.
- **Conditions:** inside `if c:` the checker has replaced `c` by `True` in the types it still has to check, and by `False` in the `else` part. So a proof about a function that tests `a < b` can test `a < b` itself and finish each branch separately.
- **A proof of the condition:** `match decide(c):` with `case Yes(p):` and `case No(p):` does the same, and `p` is a proof of `c == True` or `c == False` to pass on to a function that asks for one.
- **Proofs:** in an annotation, `a == b` is the type of proofs that `a` equals `b`. `refl()` proves a thing equals itself. `sym`, `trans`, `cong` and `subst` combine proofs, and `match p: case Refl():` uses one.
- **Impossible cases:** `case _: return unreachable()` is accepted only when Agda can see that no value reaches it.
- **Top level:** `name: T = value`, `print(...)`, and `assert`. An `assert` is checked by Agda before the program runs. A final `if __name__ == "__main__":` block may hold the same three.

### What the check covers, and what it does not

- **Covered:** if a checked file is accepted and run as the main program, every call inside it gets arguments of the annotated types, every `match` finds a case, every recursion ends, and every `assert` holds.
- **Callers outside:** an ordinary Python file can import a checked file and call it with anything. Annotations are not checked while the program runs.
- **Deep recursion:** Agda proves that a recursion ends. The interpreter still stops at 1000 nested calls with `RecursionError`. A checked file cannot import `sys`, so raise the limit from an ordinary file that imports the checked one.
- **The translation:** the guarantee rests on `src/agda.c` giving each construct the same meaning the interpreter does, and on `lib/dependent.py` matching `lib/PycPrelude.agda`. `pyc` always loads its built-in `dependent`, so a `dependent.py` in the script's folder is ignored.

### Missing from checked files

- `int`, `float` and negative numbers. `Nat` is the only number type.
- Lists, tuples, dicts and sets. Declare a `Data` family.
- Loops, exceptions, attribute access, assignment to anything but a plain name, nested functions, default and keyword arguments.
- Imports of other files. A checked program is one file.
- Function types with more than 4 arguments.
- A constructor field that holds a type. Agda reports that `Set₁` does not fit into `Set`.
- String operations beyond literals, `==`, `concat(a, b)`, and `+` beside a literal.
- `case ... if`, sequence and mapping patterns, and keyword patterns.

Each of these is refused with a message that names the Python line.

### Reading the error messages

Type errors come from Agda, with positions mapped back to Python lines. Agda writes some things differently:

| Agda | Python |
| --- | --- |
| `Vec Nat 2` | `Vec[Nat, 2]` |
| `suc n` | `n + 1` |
| `zero` | `0` |
| `x ≡ y` | `x == y` in an annotation, or `Eq[x, y]` |
| `Fun₂ A (λ _ → B) (λ _ _ → C)` | `Callable[[A, B], C]` |
| `Fun₂ A (λ x → B) (λ x y → C)` | `Fn[x: A, y: B, C]` |
| `f x \| true` | `f(x)` in the branch where its `if` test was true |
| `Dec₀ c` | `Dec[c]` |
| `Set` | `Type` |
| `⊥` | `Empty` |
| `x₁` | a second variable that was also called `x` |
| `_n_14` | a value Agda has not worked out yet |
