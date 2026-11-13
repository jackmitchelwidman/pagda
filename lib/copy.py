"""copy: shallow and deep copies."""


class Error(Exception):
    pass


_atomic = (int, float, bool, str, type(None), type, range, type(len), type(lambda: 0))


def copy(x):
    cls = type(x)
    if isinstance(x, _atomic) and cls in _atomic:
        return x
    copier = getattr(x, "__copy__", None)
    if copier is not None:
        return copier()
    if cls in (list, dict, set):
        return x.copy()
    if cls in (tuple, frozenset):
        return x
    return _reconstruct(x, None)


def deepcopy(x, memo=None):
    if memo is None:
        memo = {}
    cls = type(x)
    if isinstance(x, _atomic) and cls in _atomic:
        return x
    key = id(x)
    if key in memo:
        return memo[key]
    copier = getattr(x, "__deepcopy__", None)
    if copier is not None:
        y = copier(memo)
    elif cls is list:
        y = []
        memo[key] = y
        for item in x:
            y.append(deepcopy(item, memo))
    elif cls is dict:
        y = {}
        memo[key] = y
        for k, v in x.items():
            y[deepcopy(k, memo)] = deepcopy(v, memo)
    elif cls is set:
        y = {deepcopy(item, memo) for item in x}
    elif cls is tuple:
        y = tuple(deepcopy(item, memo) for item in x)
    elif cls is frozenset:
        y = frozenset(deepcopy(item, memo) for item in x)
    else:
        y = _reconstruct(x, memo)
    memo[key] = y
    return y


def _reconstruct(x, memo):
    cls = type(x)
    deep = memo is not None
    if isinstance(x, BaseException):
        y = cls(*x.args)
    elif isinstance(x, (int, float, str, tuple, frozenset)):
        y = cls.__new__(cls, x) if not isinstance(x, tuple) else tuple.__new__(cls, x)
    else:
        y = cls.__new__(cls)
    if deep:
        memo[id(x)] = y
    if isinstance(x, list):
        list.extend(y, (deepcopy(i, memo) for i in x) if deep else x)
    elif isinstance(x, dict):
        for k, v in dict.items(x):
            dict.__setitem__(y, deepcopy(k, memo) if deep else k, deepcopy(v, memo) if deep else v)
    elif isinstance(x, set):
        set.update(y, (deepcopy(i, memo) for i in x) if deep else x)
    state = getattr(x, "__dict__", None)
    if state:
        for k, v in state.items():
            y.__dict__[k] = deepcopy(v, memo) if deep else v
    return y
