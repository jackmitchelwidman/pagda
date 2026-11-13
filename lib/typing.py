"""typing: names used in type hints. Hints are not checked at run time."""

TYPE_CHECKING = False


class _Alias:
    def __init__(self, name):
        self._name = name

    def __getitem__(self, params):
        return self

    def __call__(self, *args, **kwargs):
        raise TypeError(f"Type {self._name} cannot be instantiated")

    def __or__(self, other):
        return self

    def __ror__(self, other):
        return self

    def __repr__(self):
        return "typing." + self._name


Any = _Alias("Any")
Union = _Alias("Union")
Optional = _Alias("Optional")
List = _Alias("List")
Dict = _Alias("Dict")
Set = _Alias("Set")
FrozenSet = _Alias("FrozenSet")
Tuple = _Alias("Tuple")
Type = _Alias("Type")
Callable = _Alias("Callable")
Iterable = _Alias("Iterable")
Iterator = _Alias("Iterator")
Generator = _Alias("Generator")
Sequence = _Alias("Sequence")
MutableSequence = _Alias("MutableSequence")
Mapping = _Alias("Mapping")
MutableMapping = _Alias("MutableMapping")
Collection = _Alias("Collection")
Hashable = _Alias("Hashable")
Sized = _Alias("Sized")
Literal = _Alias("Literal")
Final = _Alias("Final")
ClassVar = _Alias("ClassVar")
Annotated = _Alias("Annotated")
NoReturn = _Alias("NoReturn")
Never = _Alias("Never")
Self = _Alias("Self")
TypeAlias = _Alias("TypeAlias")
DefaultDict = _Alias("DefaultDict")
Deque = _Alias("Deque")
Counter = _Alias("Counter")
OrderedDict = _Alias("OrderedDict")
IO = _Alias("IO")
TextIO = _Alias("TextIO")


class TypeVar:
    def __init__(self, name, *constraints, bound=None, covariant=False, contravariant=False):
        self.__name__ = name
        self._bracket = False

    def __repr__(self):
        return self.__name__ if self._bracket else "~" + self.__name__


class TypeVarTuple(TypeVar):
    def __iter__(self):
        yield self


class ParamSpec(TypeVar):
    pass


def _type_param(name, kind):
    """Makes the object for T in "def f[T]". kind: 0 plain, 1 *Ts, 2 **P."""
    t = (TypeVar, TypeVarTuple, ParamSpec)[kind](name)
    t._bracket = True
    return t


def _generic_base(*params):
    return Generic


class Generic:
    def __class_getitem__(cls, params):
        return cls


class Protocol:
    def __class_getitem__(cls, params):
        return cls


def cast(typ, val):
    return val


def overload(func):
    return func


def final(func):
    return func


def no_type_check(func):
    return func


def runtime_checkable(cls):
    return cls


def NewType(name, tp):
    def new_type(x):
        return x
    new_type.__name__ = name
    return new_type


def get_type_hints(obj):
    return dict(getattr(obj, "__annotations__", {}))
