"""functools: tools for working with functions."""
from collections import namedtuple

_CacheInfo = namedtuple("CacheInfo", ["hits", "misses", "maxsize", "currsize"])

WRAPPER_ASSIGNMENTS = ('__module__', '__name__', '__qualname__', '__doc__')


def update_wrapper(wrapper, wrapped):
    for attr in WRAPPER_ASSIGNMENTS:
        try:
            value = getattr(wrapped, attr)
        except AttributeError:
            continue
        try:
            setattr(wrapper, attr, value)
        except (AttributeError, TypeError):
            pass
    try:
        wrapper.__dict__.update(wrapped.__dict__)
    except AttributeError:
        pass
    wrapper.__wrapped__ = wrapped
    return wrapper


def wraps(wrapped):
    def decorator(wrapper):
        return update_wrapper(wrapper, wrapped)
    return decorator


_missing = object()


def reduce(function, iterable, initial=_missing):
    it = iter(iterable)
    if initial is _missing:
        try:
            value = next(it)
        except StopIteration:
            raise TypeError("reduce() of empty iterable with no initial value") from None
    else:
        value = initial
    for element in it:
        value = function(value, element)
    return value


class partial:
    def __init__(self, func, *args, **keywords):
        if not callable(func):
            raise TypeError("the first argument must be callable")
        self.func = func
        self.args = args
        self.keywords = keywords

    def __call__(self, *args, **keywords):
        return self.func(*self.args, *args, **{**self.keywords, **keywords})

    def __repr__(self):
        parts = [repr(self.func)] + [repr(a) for a in self.args]
        parts += [f"{k}={v!r}" for k, v in self.keywords.items()]
        return "functools.partial(" + ", ".join(parts) + ")"


def lru_cache(maxsize=128, typed=False):
    def decorator(func):
        cache = {}
        stats = [0, 0]

        def wrapper(*args, **kwargs):
            key = (args, tuple(sorted(kwargs.items()))) if kwargs else args
            if typed:
                key = (key, tuple(type(a) for a in args), tuple(type(v) for _, v in sorted(kwargs.items())))
            if key in cache:
                stats[0] += 1
                value = cache[key]
                if limit is not None:
                    del cache[key]
                    cache[key] = value
                return value
            stats[1] += 1
            value = func(*args, **kwargs)
            cache[key] = value
            if limit is not None and len(cache) > limit:
                del cache[next(iter(cache))]
            return value

        def cache_info():
            return _CacheInfo(stats[0], stats[1], limit, len(cache))

        def cache_clear():
            cache.clear()
            stats[0] = stats[1] = 0

        update_wrapper(wrapper, func)
        wrapper.cache_info = cache_info
        wrapper.cache_clear = cache_clear
        return wrapper

    if callable(maxsize):
        func, limit = maxsize, 128
        return decorator(func)
    limit = maxsize
    return decorator


def cache(func):
    return lru_cache(maxsize=None)(func)


def cmp_to_key(mycmp):
    class K:
        def __init__(self, obj):
            self.obj = obj

        def __lt__(self, other):
            return mycmp(self.obj, other.obj) < 0

        def __gt__(self, other):
            return mycmp(self.obj, other.obj) > 0

        def __eq__(self, other):
            return mycmp(self.obj, other.obj) == 0

        def __le__(self, other):
            return mycmp(self.obj, other.obj) <= 0

        def __ge__(self, other):
            return mycmp(self.obj, other.obj) >= 0

        __hash__ = None
    return K


def total_ordering(cls):
    defined = cls.__dict__
    if '__lt__' in defined:
        if '__gt__' not in defined:
            cls.__gt__ = lambda self, other: not (self < other or self == other)
        if '__le__' not in defined:
            cls.__le__ = lambda self, other: self < other or self == other
        if '__ge__' not in defined:
            cls.__ge__ = lambda self, other: not self < other
    elif '__gt__' in defined:
        cls.__lt__ = lambda self, other: not (self > other or self == other)
        if '__ge__' not in defined:
            cls.__ge__ = lambda self, other: self > other or self == other
        if '__le__' not in defined:
            cls.__le__ = lambda self, other: not self > other
    elif '__le__' in defined:
        cls.__gt__ = lambda self, other: not self <= other
        cls.__lt__ = lambda self, other: self <= other and not self == other
        if '__ge__' not in defined:
            cls.__ge__ = lambda self, other: not self <= other or self == other
    elif '__ge__' in defined:
        cls.__lt__ = lambda self, other: not self >= other
        cls.__gt__ = lambda self, other: self >= other and not self == other
        cls.__le__ = lambda self, other: not self >= other or self == other
    else:
        raise ValueError('must define at least one ordering operation: < > <= >=')
    return cls


class cached_property:
    def __init__(self, func):
        self.func = func
        self.attrname = None
        self.__doc__ = func.__doc__

    def __set_name__(self, owner, name):
        self.attrname = name

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        cache = instance.__dict__
        if self.attrname in cache:
            return cache[self.attrname]
        value = self.func(instance)
        cache[self.attrname] = value
        return value
