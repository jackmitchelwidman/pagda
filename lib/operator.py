"""operator: the built-in operators as functions."""
import builtins as _builtins


def add(a, b): return a + b
def sub(a, b): return a - b
def mul(a, b): return a * b
def truediv(a, b): return a / b
def floordiv(a, b): return a // b
def mod(a, b): return a % b
def pow(a, b): return a ** b
def neg(a): return -a
def pos(a): return +a
def abs(a): return _builtins.abs(a)
def inv(a): return ~a
def invert(a): return ~a
def lshift(a, b): return a << b
def rshift(a, b): return a >> b
def and_(a, b): return a & b
def or_(a, b): return a | b
def xor(a, b): return a ^ b
def not_(a): return not a
def truth(a): return bool(a)
def is_(a, b): return a is b
def is_not(a, b): return a is not b
def lt(a, b): return a < b
def le(a, b): return a <= b
def eq(a, b): return a == b
def ne(a, b): return a != b
def gt(a, b): return a > b
def ge(a, b): return a >= b
def contains(a, b): return b in a
def getitem(a, b): return a[b]
def concat(a, b): return a + b
def index(a): return a.__index__()


def setitem(a, b, c):
    a[b] = c


def delitem(a, b):
    del a[b]


def countOf(a, b):
    return sum(1 for x in a if x is b or x == b)


def indexOf(a, b):
    for i, x in enumerate(a):
        if x is b or x == b:
            return i
    raise ValueError('sequence.index(x): x not in sequence')


def itemgetter(*items):
    if len(items) == 1:
        item = items[0]
        return lambda obj: obj[item]
    return lambda obj: tuple(obj[i] for i in items)


def attrgetter(*attrs):
    def resolve(obj, attr):
        for name in attr.split("."):
            obj = getattr(obj, name)
        return obj
    if len(attrs) == 1:
        attr = attrs[0]
        return lambda obj: resolve(obj, attr)
    return lambda obj: tuple(resolve(obj, a) for a in attrs)


def methodcaller(name, *args, **kwargs):
    return lambda obj: getattr(obj, name)(*args, **kwargs)
