# Pagda

Pagda is Python with dependent types. A file that asks for it is type-checked by Agda before it runs, and the types can state facts about values: "this list has 10 items", "this index is inside the list", "`a + b` equals `b + a`". Files that do not ask for it run as ordinary Python.

The `pagda` program is a Python 3 interpreter written in C. It reads Python source, compiles it to bytecode, and runs the bytecode on a stack machine. For a checked file it first translates the file to Agda and has the `agda` program check it.

## Build and run

```
make                 # builds ./pagda
./pagda script.py      # run a file
./pagda -c 'print(1)'  # run a string
./pagda                # interactive prompt
echo 'print(2)' | ./pagda
```

Needs a C compiler (gcc or clang) and make. No other dependencies.

## Tests

```
make test         # runs every test; see below
make test-debug   # same, on a build with address and undefined-behavior checks,
                  # collecting garbage every 13 allocations
```

There are three groups:

- `tests/NN_*.py` run under `./pagda` and under `python3`. Output and exit code must match.
- `tests/checked/*.py` are checked files (see below). They must pass the type checker and then match `python3`.
- `tests/rejected/*.py` must be refused before they run, with the exact message stored in the `.expected` file beside each one.

`python3` (3.12 or newer) must be installed. It is the reference the output is compared against. The last two groups also need `agda`. Without it they are skipped and the run prints a notice.

Two environment variables help when hunting memory bugs:

- `PAGDA_GC_STRESS=n` collects every n allocations and allocates each object with plain `malloc`, so AddressSanitizer sees any use of a freed object.
- `PAGDA_GC_POISON=n` collects every n allocations with the normal allocator and overwrites freed objects.

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
| `lib/lemmas.py` | ready-made proofs, itself a checked file |
| `lib/PagdaPrelude.agda` | the Agda meaning of each name in `dependent`, embedded in the binary |

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

A file that imports `dependent` is type-checked before it runs. `pagda` translates the file to Agda, runs the `agda` program on the translation, and runs the Python only if Agda accepts it. Agda is a language whose types can mention values. "A list of length `n`" is a type there, and so is "a proof that `a + b` equals `b + a`".

This needs `agda` on the `PATH` (developed with Agda 2.8.0), or `PAGDA_AGDA` set to its path. If `agda` cannot be started, a checked file is refused and does not run.

```python
from __future__ import annotations
from dependent import *

def total(xs: list[int]) -> int:               # ordinary Python
    s = 0
    for x in xs:
        s += x
    return s

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

assert total([1, 2, -5]) == -2                 # checked by Agda before the program runs
print(head(Cons(1, Nil())))
print(head(Nil()))                             # refused: 0 is not n + 1
```

```
./pagda file.py           # check, then run
./pagda --check file.py   # check only
./pagda --agda file.py    # print the Agda text the file turns into
```

A file that passed is remembered in `~/.cache/pagda` (or `$XDG_CACHE_HOME/pagda`), keyed by its translated text, so Agda runs again only when the file changes.

The same file runs under `python3` 3.12 or newer with `lib/dependent.py` (and `lib/lemmas.py`, if used) beside it, and prints the same output. There the types are not checked.

`tests/checked/` has a working example of everything below.

### The first lines

`from __future__ import annotations`, then `from dependent import ...`. After that a checked file may import names from other checked files: `from shapes import Point, width`. It has no other imports.

### A program

```python
def main() -> None:
    stock = [Item("pen", 3, 10), Item("ink", 12, 2)]
    for item in stock:
        print(f"{item.name}: {item.price}")

if __name__ == "__main__":
    main()
```

A def can return `None`, call other defs for what they do, and `print`. What `print` writes is not part of what the checker reasons about. Its arguments are checked like any other expression.

### Numbers, strings and booleans

- **`Nat`** is 0, 1, 2 and so on. It has `+ * // % **` and the comparisons. `if n == 0:` splits into the zero case and the other case, and in the other case `n - 1` is allowed. Elsewhere `a - b` needs the checker to see that `b` is not larger than `a` (see Operations that can fail). `monus(a, b)` is subtraction that stops at 0.
- **`int`** is any whole number. It has `+ - * // % **`, unary minus, `abs`, `min`, `max` and the comparisons. `//` and `%` round the way Python does.
- **A literal** like `3` is a `Nat` unless the place it stands in needs an `int`. A `Nat` used where an `int` is wanted is converted. Going the other way is written `to_nat(x)`, which gives 0 for a negative `x`, or `abs(x)`.
- **Locals follow their use.** A local that is given an int anywhere in a function is an int from its first assignment, so `s = 0` followed by `s += x` works for an int `x`. `xs = [1, 2]` is a list of ints when `xs` is later passed where a `list[int]` is wanted. `best = None` followed by `best = x` makes `best` an Optional.
- **`str`** has `+`, `*` by a number, `==`, `<`, `in`, `len`, `s[i]`, slices, looping over the characters, `str(n)`, f-strings, and the methods `startswith`, `endswith`, `join`, `split(sep)`, `replace(old, new)`, `strip`, `lstrip` and `rstrip`. `split` and `replace` take the text to look for written out, and not empty.
- **f-strings** hold `str`, `Nat`, `int` and `bool` values. Inside `print(...)` they can hold anything, and can use format specs.
- **`bool`** has `and`, `or`, `not`. In a condition, a list, dict or string counts as true when it is not empty, and a number when it is not 0.
- There are no floats, so `/` is refused.

### None and Optional

`Optional[T]` (or `T | None`) is a value that is a `T` or `None`. A plain value is accepted where an Optional is wanted. Going the other way needs a test:

```python
def price_of(items: list[Item], name: str) -> int:
    item = find(items, name)          # Optional[Item]
    if item is None:
        return -1
    return item.price                 # here item is an Item
```

`d.get(k)` gives an Optional. `T` has to be a type the checker knows, and not a name in square brackets: Python cannot tell "None" from "a None inside", so an Optional of an Optional would not mean the same in both languages.

### Lists, tuples and dicts

- **`list[A]`:** `[1, 2]`, `xs + ys`, `len(xs)`, `x in xs`, `xs[i]`, `xs[-1]`, `xs[a:b]` (ends may be negative), comprehensions, `sum`, `any`, `all`, `sorted` (of `Nat` or `int`), `list(...)`, and `match xs:` with `case []:` and `case [first, *rest]:`.
- **`tuple[A, B]`:** `(a, b)`, `a, b = t`, `a, b = b, a`, `t[0]`, and `case (a, b):`. Two or more parts.
- **`dict[K, V]`:** `{k: v}`, `d[k]`, `k in d`, `d.get(k)`, `d.get(k, default)`, `len(d)`, and looping over `d`, `d.items()`, `d.keys()` or `d.values()`. Keys keep the order they were first added. A key is a `Nat`, `int`, `bool`, `str`, or a tuple of those: the kinds Python can use as keys. `d.get(k)` needs values that are never `None`, since it answers `None` for a missing key.
- **`range`, `enumerate`, `zip`, `reversed` and the dict views** can stand after `in` of a loop or a comprehension, and inside `list(...)`. On their own they are not lists, and Python would not call them equal to one, so they are refused as plain values. For the same reason two dicts cannot be compared with `==`.

### Operations that can fail

`xs[i]` fails when `i` is past the end, `xs[-1]` and `xs.pop()` when the list is empty, `d[k]` when the key is missing, `//` and `%` when the divisor is 0, and `a - b` on Nats has no answer when `b` is larger. Agda has no failing, so each of these needs a reason it is fine. The checker finds the reason by itself when:

- the code is under a test of the condition: `if i < len(xs):`, `if xs:`, `if k in d:`, `if b != 0:`, `if b <= a:`. The test can be an `if`, a `while`, or the test of `x if c else y`;
- an earlier part of the same `and` tests it, as in `while i < len(xs) and xs[i] != t:`, or an earlier part of an `or` rules the bad case out, as in `not xs or xs[0] < 10`;
- the loop is `for i in range(len(xs)):` and the body reads or writes `xs[i]`;
- a parameter states it: `def at(xs: list[int], i: Nat, ok: (i < len(xs)) == True) -> int:`. The caller passes `known(i < len(xs))` from inside its own test;
- an `assert` states it, where Agda can work the assert out;
- Agda can work the condition out, as in `[1, 2, 3][1]` or `n // 2`.

An `int` used as an index needs both ends: `if 0 <= i < len(xs):`. Otherwise the line is refused with a message that lists what such lines need.

### Changing a list or dict

`xs.append(x)`, `xs.extend(ys)`, `xs.insert(i, x)`, `x = xs.pop()`, `xs += ys`, `xs[i] = v`, `xs[i], xs[j] = xs[j], xs[i]`, `xs.sort()`, `xs.reverse()`, `xs.clear()` and `d[k] = v` are allowed on a local name. In the Agda text the name simply gets a new value.

The Python still runs as Python, where these change the object itself, and every other holder of that object sees the change. Agda's version and Python's agree only when there is no other holder. So the checker allows a change only on a list or dict that the function made itself (a literal, a comprehension, `list(xs)`, `xs.copy()`, a slice, `a + b`, `sorted(...)`) and has not since given another name, passed to a function, put inside another value, or used in a lambda or inner def. A parameter, or the result of a call, has to be copied first: `xs = list(xs)`. A loop cannot change the list it is running over.

### Loops

- **`for`** runs over a list, a dict, a string, `range(...)` and the like. `break`, `continue` and `return` work inside it. The names the body assigns must have been assigned before the loop, and each keeps one type through the loop.
- **The loop variable** is gone after the loop. To keep its last value, give it to another name inside the loop.
- **`while`** needs a reason to end. The checker sees it by itself for the two commonest shapes, also when the test has more parts joined by `and`:

```python
i = 0
while i < len(xs):        # a Nat counted up to a limit the loop does not change
    ...
    i += 1                # the last statement

while k > 0:              # an int counted down to 0
    ...
    k -= 1
```

- **Any other `while`** ends with `decreases(measure, proof)`. The measure is a `Nat` worked out from the loop's variables. The proof shows `(measure at the end of a pass < measure at the start) == True`. `lemmas` has two ready ones, `monus_step` and `down_step`.
- **A `for` loop at the top level** may hold `print` statements and further `for` loops.
- In the Agda text a loop is one expression that folds the body over the items. So `if` inside a loop does not change what the types know (see Conditions), and `case _: return unreachable()` is not available there. A property of a loop's result is proved by writing the loop as a recursive function instead.

### Classes

- **A class with fields** is written with `@dataclass`. Fields are `name: type` lines, and a field's type can mention the fields before it, so a field can hold a proof about another field. It can have methods, whose first parameter is `self`. `Point(1, 2)`, `Point(x=1, y=2)`, `p.x`, `p.norm()`, `case Point(a, b):` and (when every field can be compared) `==` all work. Fields cannot be assigned to: `replace(p, x=3)` gives a new value with that field changed.
- **A family of types** is `class Name[params](Data): ...`. Each constructor is a `def` marked `@constructor` with `...` as its body. Its parameters are the fields, and its return annotation says which member of the family it builds.
- **Names in square brackets** (`def f[A, n: Nat]`, `class Box[A]`) exist only for the checker. They can appear in annotations and nowhere else, because they are gone when the program runs. A name without a `: bound` is a type.

### Functions

- Every parameter and the result must be annotated. Every path must return, unless the def returns `None`.
- **Default values** are numbers, strings, `True`, `False` or `None`, written out. Arguments can be passed by name.
- **A def inside a function** can use the names around it. It cannot call itself, and it cannot change a list or dict of the outer function in place.
- **A lambda or inner def made inside a loop** cannot use a name that the loop sets. In Python such a function sees the name's value at the time it is called, which is usually the last one. Pass the value in as an argument.
- **Function types:** `Fn[A, B, C]` means the same as `Callable[[A, B], C]`: the last item is the result. An argument can be given a name, and the types after it can then use it. `Fn[k: Nat, Vec[A, k]]` takes a number `k` and gives a `Vec` of that length. The named form is for annotations only.
- **Inside a lambda** whose argument types the checker cannot see, numbers are taken to be `Nat`. Use a `def` for a function on ints that is passed to a generic function.

### Conditions and proofs

- **In an annotation, `a == b`** is the type of proofs that `a` equals `b`. `refl()` proves a thing equals itself. `sym`, `trans`, `cong` and `subst` combine proofs, and `match p: case Refl():` uses one.
- **Inside `if c:`** (outside a loop) the checker has replaced `c` by `True` in the types it still has to check, and by `False` in the `else` part. So a proof about a function that tests `a < b` can test `a < b` itself and finish each branch separately. `if a and b:` and `if a < b < c:` count as several tests, one inside the other.
- **`known(c)`** inside `if c:` or `while c:` is the proof that `c` is true. In the `else` part, `known(not c)` is the proof for the other way. `match decide(c):` with `case Yes(p):` and `case No(p):` does the same job and names the proof.
- **`assert c` inside a function** has to be a condition the checker can see holds. After it, the rest of the function may rely on it.
- **Impossible cases:** `case _: return unreachable()` is accepted only when Agda can see that no value reaches it.
- **`lemmas`** is a built-in checked file of ready-made proofs: `plus_zero`, `plus_suc`, `plus_comm`, `plus_assoc`, `times_zero`, `times_one`, `lt_suc`, `lt_irrefl`, `lt_asym`, `lt_trans`, `monus_step`, `down_step`, `append_nil`, `append_assoc`, `len_append`, `false_is_not_true`, `true_is_not_false`. Import the ones needed by name.

### Holes

`hole()` stands for a part that is not written yet. The checker checks the rest of the file, then lists each hole with the type it needs and the names in scope there. A file with a hole does not run.

```
TypeCheckError: this file has 1 hole, so it was not run.
line 8: hole() needs a value of type
  n₁ + 0 ≡ n₁
with these names to build it from:
  n₁ : Nat
```

### Calling unchecked Python

A checked file cannot import an ordinary Python file. It can hold a `def` marked `@trusted`. The checker reads its annotations and nothing else, so the body can be any Python, including imports, loops over anything, and exceptions.

```python
@trusted
def file_size(path: str) -> Nat:
    import os
    return os.path.getsize(path)
```

Three things are taken on trust: that the def returns a value of the type it says, that the same arguments always give the same result, and that it does not change the lists and dicts it is given. Agda treats it as a function, so `assert f(x) == f(x)` passes the check whatever `f` does.

### The top level

`name: T = value`, calls, `print(...)`, `assert`, classes, defs, and `for` loops that print. An `assert` is checked by Agda before the program runs. A final `if __name__ == "__main__":` block may hold the same statements. `from dependent import *` brings in every name of the module, including `Fn`, `Dec`, `Yes`, `No`, `decide`, `known`, `hole`, `dataclass`, `replace`, `trusted` and `Optional`. A file that defines its own `Yes` has to import the names it uses one by one.

### What the check covers, and what it does not

- **Covered:** if a checked file is accepted and run as the main program, every call inside it gets arguments of the annotated types, every `match` finds a case, every recursion and loop ends, no index, key, pop, division or subtraction fails, no `None` is used as a value, and every `assert` holds.
- **`@trusted` defs:** the guarantee holds only as far as each one keeps its three promises.
- **Callers outside:** an ordinary Python file can import a checked file and call it with anything. Annotations are not checked while the program runs. An outside caller can also keep a list it passed in and change it later.
- **Deep recursion:** Agda proves that a recursion ends. The interpreter still stops at 1000 nested calls with `RecursionError`. A checked file cannot import `sys`, so raise the limit from an ordinary file that imports the checked one.
- **Large numbers in asserts:** Agda works an `assert` out itself, and it counts `Nat` values one step at a time in proofs. An assert over large numbers can be slow.
- **The translation:** the guarantee rests on `src/agda.c` giving each construct the same meaning the interpreter does, and on `lib/dependent.py` matching `lib/PagdaPrelude.agda`. The tests compare the two on edge values through `assert`, which Agda and Python each work out. `pagda` always loads its built-in `dependent`, so a `dependent.py` in the script's folder is ignored.

### Missing from checked files

- Floats, `bytes`, sets, and string methods beyond the ones listed (no `upper`, `lower`, `format`, `int("12")`).
- Exceptions (`raise`, `try`), `with` blocks, and anything else that touches the outside world except `print`. Use a `@trusted` def.
- Assigning to a field, inheritance between `@dataclass` classes, field defaults, and methods named `__x__` (Python calls those by itself).
- Changing a list that sits inside another list (`grid[i][j] = v`), and changing a list or dict that might be shared.
- `list.remove`, `dict.pop`, `del`, slices with a step, tuples of one part, `range` with a step.
- Loop state whose type changes from pass to pass, and `else` on a loop.
- A def inside a function that calls itself, or has more than 4 parameters.
- Explicit type arguments at a call, as in `ident[Nat](3)`. The checker always works them out.
- A constructor field that holds a type. Agda reports that `Set₁` does not fit into `Set`.
- Function types with more than 4 arguments.
- `case ... if`, mapping patterns, and keyword patterns.
- Two checked files that import each other.

Each of these is refused with a message that names the Python line.

### Reading the error messages

Type errors come from Agda, with positions mapped back to Python lines. Agda writes some things differently:

| Agda | Python |
| --- | --- |
| `Vec Nat 2` | `Vec[Nat, 2]` |
| `suc n` | `n + 1` |
| `zero` | `0` |
| `pos 3`, `negsuc 2` | the ints `3` and `-3` |
| `Int`, `String`, `Bool`, `List A` | `int`, `str`, `bool`, `list[A]` |
| `A × B`, `(a , b)` | `tuple[A, B]`, `(a, b)` |
| `x ∷ xs`, `[]` | `[x, *xs]`, `[]` |
| `xs ++ ys` | `xs + ys` |
| `a +ᵢ b`, `a <ᵢ b` | `a + b` and `a < b` on ints |
| `x ≡ y` | `x == y` in an annotation, or `Eq[x, y]` |
| `Fun₂ A (λ _ → B) (λ _ _ → C)` | `Callable[[A, B], C]` |
| `Fun₂ A (λ x → B) (λ x y → C)` | `Fn[x: A, y: B, C]` |
| `f x \| true` | `f(x)` in the branch where its `if` test was true |
| `Dec₀ c` | `Dec[c]` |
| `Maybe A`, `just x`, `nothing` | `Optional[A]`, `x`, `None` |
| `⊤`, `tt` | `None` as a return type, and the value a def that returns `None` gives |
| `natMinus a b p` | `a - b` on Nats |
| `index xs i p`, `setAt xs i v p` | `xs[i]`, `xs[i] = v` |
| `Set` | `Type` |
| `⊥` | `Empty` |
| `new-Point 1 2`, `Point.x p`, `Point-norm p` | `Point(1, 2)`, `p.x`, `p.norm()` |
| `x₁` | a second variable that was also called `x` |
| `_n_14` | a value Agda has not worked out yet |
