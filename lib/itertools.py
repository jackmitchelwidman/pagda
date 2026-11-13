"""itertools: building blocks for iterators."""


def count(start=0, step=1):
    n = start
    while True:
        yield n
        n += step


def cycle(iterable):
    saved = []
    for element in iterable:
        yield element
        saved.append(element)
    while saved:
        for element in saved:
            yield element


def repeat(obj, times=None):
    if times is None:
        while True:
            yield obj
    else:
        for _ in range(times):
            yield obj


def chain(*iterables):
    for it in iterables:
        yield from it


def _from_iterable(iterables):
    for it in iterables:
        yield from it


chain.from_iterable = _from_iterable


def islice(iterable, *args):
    s = slice(*args)
    start = s.start or 0
    stop = s.stop
    step = 1 if s.step is None else s.step
    if start < 0 or (stop is not None and stop < 0) or step <= 0:
        raise ValueError("Indices for islice() must be None or an integer: 0 <= x <= sys.maxsize.")
    it = iter(iterable)
    i = 0
    nexti = start
    while stop is None or nexti < stop:
        try:
            element = next(it)
        except StopIteration:
            return
        if i == nexti:
            yield element
            nexti += step
        i += 1


def takewhile(predicate, iterable):
    for x in iterable:
        if not predicate(x):
            break
        yield x


def dropwhile(predicate, iterable):
    it = iter(iterable)
    for x in it:
        if not predicate(x):
            yield x
            break
    for x in it:
        yield x


def filterfalse(predicate, iterable):
    if predicate is None:
        predicate = bool
    for x in iterable:
        if not predicate(x):
            yield x


def compress(data, selectors):
    return (d for d, s in zip(data, selectors) if s)


def starmap(function, iterable):
    for args in iterable:
        yield function(*args)


_none = object()


def accumulate(iterable, func=None, *, initial=None):
    it = iter(iterable)
    total = initial
    if initial is None:
        try:
            total = next(it)
        except StopIteration:
            return
    yield total
    for element in it:
        total = total + element if func is None else func(total, element)
        yield total


def zip_longest(*iterables, fillvalue=None):
    iterators = [iter(it) for it in iterables]
    active = len(iterators)
    if not active:
        return
    while True:
        values = []
        for i, it in enumerate(iterators):
            try:
                value = next(it)
            except StopIteration:
                active -= 1
                if not active:
                    return
                iterators[i] = repeat(fillvalue)
                value = fillvalue
            values.append(value)
        yield tuple(values)


def product(*iterables, repeat=1):
    pools = [tuple(pool) for pool in iterables] * repeat
    result = [[]]
    for pool in pools:
        result = [x + [y] for x in result for y in pool]
    for prod in result:
        yield tuple(prod)


def permutations(iterable, r=None):
    pool = tuple(iterable)
    n = len(pool)
    r = n if r is None else r
    if r > n:
        return
    indices = list(range(n))
    cycles = list(range(n, n - r, -1))
    yield tuple(pool[i] for i in indices[:r])
    while n:
        for i in reversed(range(r)):
            cycles[i] -= 1
            if cycles[i] == 0:
                indices[i:] = indices[i + 1:] + indices[i:i + 1]
                cycles[i] = n - i
            else:
                j = cycles[i]
                indices[i], indices[-j] = indices[-j], indices[i]
                yield tuple(pool[i] for i in indices[:r])
                break
        else:
            return


def combinations(iterable, r):
    pool = tuple(iterable)
    n = len(pool)
    if r > n:
        return
    indices = list(range(r))
    yield tuple(pool[i] for i in indices)
    while True:
        for i in reversed(range(r)):
            if indices[i] != i + n - r:
                break
        else:
            return
        indices[i] += 1
        for j in range(i + 1, r):
            indices[j] = indices[j - 1] + 1
        yield tuple(pool[i] for i in indices)


def combinations_with_replacement(iterable, r):
    pool = tuple(iterable)
    n = len(pool)
    if not n and r:
        return
    indices = [0] * r
    yield tuple(pool[i] for i in indices)
    while True:
        for i in reversed(range(r)):
            if indices[i] != n - 1:
                break
        else:
            return
        indices[i:] = [indices[i] + 1] * (r - i)
        yield tuple(pool[i] for i in indices)


def groupby(iterable, key=None):
    it = iter(iterable)
    try:
        current = next(it)
    except StopIteration:
        return
    while True:
        k = current if key is None else key(current)
        group = [current]
        done = True
        for current in it:
            if (current if key is None else key(current)) == k:
                group.append(current)
            else:
                done = False
                break
        yield k, iter(group)
        if done:
            return


def tee(iterable, n=2):
    if n < 0:
        raise ValueError("n must be >= 0")
    it = iter(iterable)
    queues = [[] for _ in range(n)]

    def gen(mine):
        while True:
            if not mine:
                try:
                    value = next(it)
                except StopIteration:
                    return
                for q in queues:
                    q.append(value)
            yield mine.pop(0)
    return tuple(gen(q) for q in queues)


def pairwise(iterable):
    it = iter(iterable)
    try:
        a = next(it)
    except StopIteration:
        return
    for b in it:
        yield a, b
        a = b


def batched(iterable, n):
    if n < 1:
        raise ValueError('n must be at least one')
    batch = []
    for x in iterable:
        batch.append(x)
        if len(batch) == n:
            yield tuple(batch)
            batch = []
    if batch:
        yield tuple(batch)
