"""abc: abstract base classes."""


def abstractmethod(funcobj):
    funcobj.__isabstractmethod__ = True
    return funcobj


def _abstract_names(cls):
    names = []
    for name in dir(cls):
        if getattr(getattr(cls, name, None), "__isabstractmethod__", False):
            names.append(name)
    return names


class ABC:
    def __new__(cls, *args, **kwargs):
        names = _abstract_names(cls)
        if names:
            plural = "s" if len(names) > 1 else ""
            listed = ", ".join(f"'{n}'" for n in names)
            raise TypeError(f"Can't instantiate abstract class {cls.__name__} without an implementation for abstract method{plural} {listed}")
        return super().__new__(cls)

