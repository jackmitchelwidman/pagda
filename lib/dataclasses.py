"""dataclasses: classes that are mostly data, with generated methods."""


class _MissingType:
    def __repr__(self):
        return "MISSING"


MISSING = _MissingType()


class FrozenInstanceError(AttributeError):
    pass


class Field:
    def __init__(self, default, default_factory, init, repr, compare, hash=None, kw_only=MISSING):
        self.name = None
        self.type = None
        self.default = default
        self.default_factory = default_factory
        self.init = init
        self.repr = repr
        self.compare = compare
        self.hash = hash
        self.kw_only = kw_only

    def __repr__(self):
        return f"Field(name={self.name!r},type={self.type!r},default={self.default!r})"


def field(*, default=MISSING, default_factory=MISSING, init=True, repr=True, compare=True, hash=None, kw_only=MISSING):
    if default is not MISSING and default_factory is not MISSING:
        raise ValueError('cannot specify both default and default_factory')
    return Field(default, default_factory, init, repr, compare, hash, kw_only)


def _collect_fields(cls, kw_only_default):
    fields = {}
    for base in reversed(cls.__mro__[1:]):
        for f in getattr(base, "__dataclass_fields__", {}).values():
            fields[f.name] = f
    for name, type_ in cls.__dict__.get("__annotations__", {}).items():
        default = cls.__dict__.get(name, MISSING)
        if isinstance(default, Field):
            f = default
            if f.default is MISSING:
                if name in cls.__dict__:
                    delattr(cls, name)
            else:
                setattr(cls, name, f.default)
        else:
            if isinstance(default, (list, dict, set)):
                raise ValueError(f"mutable default {type(default)} for field {name} is not allowed: use default_factory")
            f = Field(default, MISSING, True, True, True)
        f.name = name
        f.type = type_
        if f.kw_only is MISSING:
            f.kw_only = kw_only_default
        fields[name] = f
    return fields


def _process(cls, init, repr_, eq, order, frozen, unsafe_hash, match_args, kw_only):
    fields = _collect_fields(cls, kw_only)
    cls.__dataclass_fields__ = fields
    flist = list(fields.values())

    if init:
        seen_default = False
        for f in flist:
            if not f.init or f.kw_only:
                continue
            has_default = f.default is not MISSING or f.default_factory is not MISSING
            if seen_default and not has_default:
                raise TypeError(f"non-default argument {f.name!r} follows default argument")
            seen_default = seen_default or has_default
        init_fields = [f for f in flist if f.init and not f.kw_only]
        cls_name = cls.__name__

        def __init__(self, *args, **kwargs):
            if len(args) > len(init_fields):
                most = len(init_fields) + 1
                least = 1 + sum(1 for f in init_fields if f.default is MISSING and f.default_factory is MISSING)
                takes = f"{most}" if least == most else f"from {least} to {most}"
                noun = "argument" if takes == "1" else "arguments"
                raise TypeError(f"{cls_name}.__init__() takes {takes} positional {noun} but {len(args) + 1} were given")
            values = {}
            for f, value in zip(init_fields, args):
                values[f.name] = value
            for name, value in kwargs.items():
                if name not in fields or not fields[name].init:
                    raise TypeError(f"{cls_name}.__init__() got an unexpected keyword argument {name!r}")
                if name in values:
                    raise TypeError(f"{cls_name}.__init__() got multiple values for argument {name!r}")
                values[name] = value
            missing = []
            missing_kw = []
            for f in flist:
                if f.name in values:
                    value = values[f.name]
                elif f.default_factory is not MISSING:
                    value = f.default_factory()
                elif f.default is not MISSING:
                    value = f.default
                elif f.init:
                    (missing_kw if f.kw_only else missing).append(f.name)
                    continue
                else:
                    continue
                object.__setattr__(self, f.name, value)
            if missing:
                plural = "s" if len(missing) > 1 else ""
                listed = " and ".join(repr(m) for m in missing) if len(missing) < 3 else ", ".join(repr(m) for m in missing[:-1]) + ", and " + repr(missing[-1])
                raise TypeError(f"{cls_name}.__init__() missing {len(missing)} required positional argument{plural}: {listed}")
            if missing_kw:
                plural = "s" if len(missing_kw) > 1 else ""
                listed = " and ".join(repr(m) for m in missing_kw)
                raise TypeError(f"{cls_name}.__init__() missing {len(missing_kw)} required keyword-only argument{plural}: {listed}")
            post = getattr(self, "__post_init__", None)
            if post is not None:
                post()
        cls.__init__ = __init__

    if repr_ and "__repr__" not in cls.__dict__:
        def __repr__(self):
            parts = ", ".join(f"{f.name}={getattr(self, f.name)!r}" for f in flist if f.repr)
            return f"{type(self).__qualname__}({parts})"
        cls.__repr__ = __repr__

    def key(obj):
        return tuple(getattr(obj, f.name) for f in flist if f.compare)

    def hash_key(obj):
        return tuple(getattr(obj, f.name) for f in flist if (f.compare if f.hash is None else f.hash))

    if eq and "__eq__" not in cls.__dict__:
        def __eq__(self, other):
            if other.__class__ is self.__class__:
                return key(self) == key(other)
            return NotImplemented
        cls.__eq__ = __eq__
        if "__hash__" not in cls.__dict__ or cls.__dict__["__hash__"] is None:
            cls.__hash__ = (lambda self: hash(hash_key(self))) if frozen else None
    if unsafe_hash:
        cls.__hash__ = lambda self: hash(hash_key(self))
    if match_args and "__match_args__" not in cls.__dict__:
        cls.__match_args__ = tuple(f.name for f in flist if f.init and not f.kw_only)

    if order:
        def make(op):
            def compare(self, other):
                if other.__class__ is self.__class__:
                    return op(key(self), key(other))
                return NotImplemented
            return compare
        cls.__lt__ = make(lambda a, b: a < b)
        cls.__le__ = make(lambda a, b: a <= b)
        cls.__gt__ = make(lambda a, b: a > b)
        cls.__ge__ = make(lambda a, b: a >= b)

    if frozen:
        def __setattr__(self, name, value):
            raise FrozenInstanceError(f"cannot assign to field {name!r}")

        def __delattr__(self, name):
            raise FrozenInstanceError(f"cannot delete field {name!r}")
        cls.__setattr__ = __setattr__
        cls.__delattr__ = __delattr__
    return cls


def dataclass(cls=None, /, *, init=True, repr=True, eq=True, order=False, unsafe_hash=False, frozen=False,
              match_args=True, kw_only=False, slots=False, weakref_slot=False):
    if slots or weakref_slot:
        raise NotImplementedError("dataclass(slots=True) is not supported by this interpreter")
    if order and not eq:
        raise ValueError('eq must be true if order is true')

    def wrap(c):
        return _process(c, init, repr, eq, order, frozen, unsafe_hash, match_args, kw_only)
    if cls is None:
        return wrap
    return wrap(cls)


def fields(obj):
    try:
        return tuple(obj.__dataclass_fields__.values())
    except AttributeError:
        raise TypeError('must be called with a dataclass type or instance') from None


def is_dataclass(obj):
    return hasattr(obj, "__dataclass_fields__")


def asdict(obj):
    def convert(v):
        if is_dataclass(v) and not isinstance(v, type):
            return {f.name: convert(getattr(v, f.name)) for f in fields(v)}
        if isinstance(v, (list, tuple)):
            return type(v)(convert(i) for i in v)
        if isinstance(v, dict):
            return {convert(k): convert(x) for k, x in v.items()}
        return v
    if not is_dataclass(obj) or isinstance(obj, type):
        raise TypeError("asdict() should be called on dataclass instances")
    return convert(obj)


def astuple(obj):
    return tuple(getattr(obj, f.name) for f in fields(obj))


def replace(obj, **changes):
    values = {f.name: getattr(obj, f.name) for f in fields(obj) if f.init}
    values.update(changes)
    return type(obj)(**values)
