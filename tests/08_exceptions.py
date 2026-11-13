def t(fn):
    try: fn()
    except Exception as e: print(type(e).__name__, "|", e, "|", repr(e), "|", e.args)
t(lambda: 1 / 0); t(lambda: [][0]); t(lambda: {}["k"]); t(lambda: int("x")); t(lambda: None.x); t(lambda: undefined)
t(lambda: "a" + 1); t(lambda: next(iter([]))); t(lambda: (_ for _ in ()).throw(ValueError("thrown")))
class MyError(Exception): pass
class Coded(MyError):
    def __init__(self, msg, code):
        super().__init__(msg)
        self.code = code
class Custom(Exception):
    def __str__(self): return "custom str"
t(lambda: (_ for _ in ()).throw(MyError("mine", 2)))
try: raise Coded("bad", 42)
except MyError as e: print(type(e).__name__, e, e.code, e.args, isinstance(e, Exception), repr(e))
try: raise Custom(1, 2)
except Custom as e: print(e, repr(e), e.args)
try: raise MyError
except MyError as e: print(repr(e), str(e) == "", e.args)
try: raise KeyError("key")
except (ValueError, KeyError) as e: print("tuple match", e, str(e), repr(e))
try: raise ValueError("a", "b")
except ValueError as e: print(e, e.args)
def order():
    try:
        print("try"); return "from try"
    finally:
        print("finally")
print(order())
def override():
    try: return 1
    finally: return 2
print(override())
def nested():
    for i in range(3):
        try:
            if i == 1: continue
            if i == 2: break
            print("body", i)
        finally:
            print("cleanup", i)
    return "done"
print(nested())
try:
    try: raise ValueError("inner")
    except ValueError as e: raise TypeError("outer") from e
except TypeError as e: print(e, repr(e.__cause__), type(e.__context__).__name__)
try:
    try: raise ValueError("first")
    except ValueError: raise KeyError("second")
except KeyError as e: print(repr(e), repr(e.__context__), e.__cause__)
try:
    try: 1 / 0
    except ZeroDivisionError: raise
except ZeroDivisionError as e: print("reraised", e)
try:
    try: raise ValueError("x")
    except TypeError: print("wrong")
    else: print("no else")
    finally: print("fin1")
except ValueError: print("outer caught")
try: pass
except Exception: print("no")
else: print("else ran")
finally: print("fin2")
def gen():
    try:
        yield 1
        yield 2
    finally:
        print("gen cleanup")
g = gen(); print(next(g)); g.close()
e = None
try: raise ValueError("scope")
except ValueError as e: pass
try: print(e)
except NameError as err: print("NameError", err)
print(issubclass(KeyError, LookupError), issubclass(ZeroDivisionError, ArithmeticError), issubclass(FileNotFoundError, OSError), issubclass(RecursionError, RuntimeError), issubclass(Exception, BaseException), issubclass(KeyboardInterrupt, Exception), issubclass(UnboundLocalError, NameError), issubclass(ModuleNotFoundError, ImportError), IOError is OSError)
try: raise 5
except TypeError as e: print("TypeError", e)
try:
    try: raise ValueError
    except 5: pass
except TypeError as e: print("TypeError", e)
try: raise
except RuntimeError as e: print("RuntimeError", e)
def lvl3(): raise RuntimeError("deep")
def lvl2(): lvl3()
def lvl1():
    try: lvl2()
    except RuntimeError as e: return "handled " + str(e)
print(lvl1())
try: raise StopIteration(5)
except StopIteration as e: print(e.args, e.value if hasattr(e, "value") else e.args[0])
try: open("/nonexistent/file.txt")
except FileNotFoundError as e: print("FileNotFoundError", e, e.errno, isinstance(e, OSError))
except OSError as e: print("wrong OSError")
x = 0
while True:
    try:
        x += 1
        if x < 3: raise ValueError
        break
    except ValueError: continue
    finally: x += 10
print(x)
class Res:
    def __enter__(self): return self
    def __exit__(self, *a): print("released", a[0] is ZeroDivisionError); return False
try:
    with Res():
        1 / 0
except ZeroDivisionError: print("after with")
try:
    raise Exception("a") from None
except Exception as e: print(e.__cause__, e.__suppress_context__ if hasattr(e, "__suppress_context__") else True)
try: import nonexistent_module_xyz
except ModuleNotFoundError as e: print("ModuleNotFoundError", e)
try: from math import nonexistent_name
except ImportError as e: print("ImportError")
err = ValueError("stored"); err.extra = 1; print(err.extra, err.args, str(err), type(err), BaseException("b").args)
print(Exception("a", 1), repr(Exception()), str(KeyError(1)), str(KeyError("a", "b")))
