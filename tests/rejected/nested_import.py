def f():
    from dependent import Nat
    return Nat


print(f())
