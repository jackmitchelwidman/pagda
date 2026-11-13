"""collections: container types built on dict, list and tuple."""


class defaultdict(dict):
    def __init__(self, default_factory=None, *args, **kwargs):
        if default_factory is not None and not callable(default_factory):
            raise TypeError("first argument must be callable or None")
        super().__init__(*args, **kwargs)
        self.default_factory = default_factory

    def __missing__(self, key):
        if self.default_factory is None:
            raise KeyError(key)
        value = self.default_factory()
        self[key] = value
        return value

    def __repr__(self):
        return f"defaultdict({self.default_factory!r}, {dict.__repr__(self)})"

    def copy(self):
        return defaultdict(self.default_factory, self)


class OrderedDict(dict):
    def move_to_end(self, key, last=True):
        value = self.pop(key)
        if last:
            self[key] = value
        else:
            items = list(self.items())
            self.clear()
            self[key] = value
            for k, v in items:
                self[k] = v

    def popitem(self, last=True):
        if not self:
            raise KeyError('dictionary is empty')
        key = next(reversed(self)) if last else next(iter(self))
        return key, self.pop(key)

    def __repr__(self):
        return f"OrderedDict({dict.__repr__(self)})" if self else "OrderedDict()"

    def copy(self):
        return OrderedDict(self)


class Counter(dict):
    def __init__(self, iterable=None, **kwargs):
        super().__init__()
        self.update(iterable, **kwargs)

    def __missing__(self, key):
        return 0

    def update(self, iterable=None, **kwargs):
        if iterable is not None:
            if isinstance(iterable, dict):
                for key, count in iterable.items():
                    self[key] = self.get(key, 0) + count
            else:
                for key in iterable:
                    self[key] = self.get(key, 0) + 1
        for key, count in kwargs.items():
            self[key] = self.get(key, 0) + count

    def subtract(self, iterable=None, **kwargs):
        if iterable is not None:
            if isinstance(iterable, dict):
                for key, count in iterable.items():
                    self[key] = self.get(key, 0) - count
            else:
                for key in iterable:
                    self[key] = self.get(key, 0) - 1
        for key, count in kwargs.items():
            self[key] = self.get(key, 0) - count

    def most_common(self, n=None):
        items = sorted(self.items(), key=lambda kv: kv[1], reverse=True)
        return items if n is None else items[:n]

    def elements(self):
        for key, count in self.items():
            for _ in range(count):
                yield key

    def total(self):
        return sum(self.values())

    def __add__(self, other):
        result = Counter()
        for key in list(self) + [k for k in other if k not in self]:
            count = self[key] + other[key]
            if count > 0:
                result[key] = count
        return result

    def __sub__(self, other):
        result = Counter()
        for key, count in self.items():
            count -= other[key]
            if count > 0:
                result[key] = count
        return result

    def __and__(self, other):
        result = Counter()
        for key, count in self.items():
            count = min(count, other[key])
            if count > 0:
                result[key] = count
        return result

    def __or__(self, other):
        result = Counter()
        for key in list(self) + [k for k in other if k not in self]:
            count = max(self[key], other[key])
            if count > 0:
                result[key] = count
        return result

    def __repr__(self):
        if not self:
            return "Counter()"
        return "Counter({" + ", ".join(f"{k!r}: {v!r}" for k, v in self.most_common()) + "})"

    def copy(self):
        return Counter(self)


class deque:
    """Double-ended queue stored in a ring buffer."""

    def __init__(self, iterable=(), maxlen=None):
        if maxlen is not None and maxlen < 0:
            raise ValueError("maxlen must be non-negative")
        self._buf = [None] * 8
        self._head = 0
        self._n = 0
        self.maxlen = maxlen
        for x in iterable:
            self.append(x)

    def _grow(self):
        cap = len(self._buf)
        self._buf = [self._buf[(self._head + i) % cap] for i in range(self._n)] + [None] * cap
        self._head = 0

    def append(self, x):
        if self.maxlen is not None and self._n == self.maxlen:
            if self.maxlen == 0:
                return
            self.popleft()
        if self._n == len(self._buf):
            self._grow()
        self._buf[(self._head + self._n) % len(self._buf)] = x
        self._n += 1

    def appendleft(self, x):
        if self.maxlen is not None and self._n == self.maxlen:
            if self.maxlen == 0:
                return
            self.pop()
        if self._n == len(self._buf):
            self._grow()
        self._head = (self._head - 1) % len(self._buf)
        self._buf[self._head] = x
        self._n += 1

    def pop(self):
        if not self._n:
            raise IndexError("pop from an empty deque")
        self._n -= 1
        i = (self._head + self._n) % len(self._buf)
        x = self._buf[i]
        self._buf[i] = None
        return x

    def popleft(self):
        if not self._n:
            raise IndexError("pop from an empty deque")
        x = self._buf[self._head]
        self._buf[self._head] = None
        self._head = (self._head + 1) % len(self._buf)
        self._n -= 1
        return x

    def extend(self, iterable):
        for x in list(iterable) if iterable is self else iterable:
            self.append(x)

    def extendleft(self, iterable):
        for x in list(iterable) if iterable is self else iterable:
            self.appendleft(x)

    def rotate(self, n=1):
        if not self._n:
            return
        n %= self._n
        for _ in range(n):
            self.appendleft(self.pop())

    def clear(self):
        self._buf = [None] * 8
        self._head = 0
        self._n = 0

    def copy(self):
        return deque(self, self.maxlen)

    def count(self, x):
        return sum(1 for y in self if y == x)

    def index(self, x):
        for i, y in enumerate(self):
            if y == x:
                return i
        raise ValueError(f"{x!r} is not in deque")

    def remove(self, x):
        items = list(self)
        items.remove(x)
        self.clear()
        self.extend(items)

    def reverse(self):
        items = list(self)
        items.reverse()
        self.clear()
        self.extend(items)

    def _index(self, i):
        if not isinstance(i, int):
            raise TypeError("sequence index must be integer, not '" + type(i).__name__ + "'")
        if i < 0:
            i += self._n
        if not 0 <= i < self._n:
            raise IndexError("deque index out of range")
        return (self._head + i) % len(self._buf)

    def __getitem__(self, i):
        return self._buf[self._index(i)]

    def __setitem__(self, i, x):
        self._buf[self._index(i)] = x

    def __len__(self):
        return self._n

    def __bool__(self):
        return self._n > 0

    def __iter__(self):
        cap = len(self._buf)
        for i in range(self._n):
            yield self._buf[(self._head + i) % cap]

    def __reversed__(self):
        cap = len(self._buf)
        for i in range(self._n - 1, -1, -1):
            yield self._buf[(self._head + i) % cap]

    def __contains__(self, x):
        return any(y == x for y in self)

    def __eq__(self, other):
        return isinstance(other, deque) and list(self) == list(other)

    def __repr__(self):
        if self.maxlen is None:
            return f"deque({list(self)!r})"
        return f"deque({list(self)!r}, maxlen={self.maxlen})"


def namedtuple(typename, field_names, defaults=None, rename=False):
    if isinstance(field_names, str):
        field_names = field_names.replace(',', ' ').split()
    fields = tuple(field_names)
    for name in (typename,) + fields:
        if not isinstance(name, str) or not name.isidentifier():
            raise ValueError(f"Type names and field names must be valid identifiers: {name!r}")
    if len(set(fields)) != len(fields):
        raise ValueError("Encountered duplicate field name")
    field_defaults = {}
    if defaults is not None:
        defaults = tuple(defaults)
        if len(defaults) > len(fields):
            raise TypeError('Got more default values than field names')
        field_defaults = dict(zip(fields[len(fields) - len(defaults):], defaults))

    def __new__(cls, *args, **kwargs):
        if len(args) > len(fields):
            raise TypeError(f"{typename}.__new__() takes {len(fields) + 1} positional arguments but {len(args) + 1} were given")
        values = list(args)
        for name in fields[len(args):]:
            if name in kwargs:
                values.append(kwargs.pop(name))
            elif name in field_defaults:
                values.append(field_defaults[name])
            else:
                raise TypeError(f"{typename}.__new__() missing 1 required positional argument: '{name}'")
        if kwargs:
            raise TypeError(f"{typename}.__new__() got an unexpected keyword argument '{next(iter(kwargs))}'")
        return tuple.__new__(cls, values)

    def __repr__(self):
        return typename + "(" + ", ".join(f"{n}={v!r}" for n, v in zip(fields, self)) + ")"

    def _asdict(self):
        return dict(zip(fields, self))

    def _replace(self, **kwargs):
        values = [kwargs.pop(n, v) for n, v in zip(fields, self)]
        if kwargs:
            raise ValueError(f"Got unexpected field names: {list(kwargs)!r}")
        return type(self)(*values)

    def _make(cls, iterable):
        return cls(*iterable)

    def getter(i):
        return property(lambda self: self[i])

    namespace = {
        '__new__': __new__, '__repr__': __repr__, '_asdict': _asdict, '_replace': _replace,
        '_make': classmethod(_make), '_fields': fields, '_field_defaults': field_defaults,
        '__match_args__': fields, '__slots__': (),
    }
    for i, name in enumerate(fields):
        namespace[name] = getter(i)
    return type(typename, (tuple,), namespace)


class ChainMap:
    def __init__(self, *maps):
        self.maps = list(maps) or [{}]

    def __getitem__(self, key):
        for m in self.maps:
            if key in m:
                return m[key]
        raise KeyError(key)

    def get(self, key, default=None):
        return self[key] if key in self else default

    def __setitem__(self, key, value):
        self.maps[0][key] = value

    def __contains__(self, key):
        return any(key in m for m in self.maps)

    def __len__(self):
        return len(set().union(*self.maps))

    def __iter__(self):
        seen = {}
        for m in reversed(self.maps):
            for k in m:
                seen[k] = None
        return iter(seen)

    def keys(self):
        return list(self)

    def items(self):
        return [(k, self[k]) for k in self]

    def values(self):
        return [self[k] for k in self]

    def new_child(self, m=None):
        return ChainMap({} if m is None else m, *self.maps)

    def __repr__(self):
        return "ChainMap(" + ", ".join(repr(m) for m in self.maps) + ")"
