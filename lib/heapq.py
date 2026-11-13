"""heapq: a min-heap kept in a list."""


def _siftdown(heap, startpos, pos):
    newitem = heap[pos]
    while pos > startpos:
        parentpos = (pos - 1) >> 1
        parent = heap[parentpos]
        if newitem < parent:
            heap[pos] = parent
            pos = parentpos
            continue
        break
    heap[pos] = newitem


def _siftup(heap, pos):
    endpos = len(heap)
    startpos = pos
    newitem = heap[pos]
    childpos = 2 * pos + 1
    while childpos < endpos:
        rightpos = childpos + 1
        if rightpos < endpos and not heap[childpos] < heap[rightpos]:
            childpos = rightpos
        heap[pos] = heap[childpos]
        pos = childpos
        childpos = 2 * pos + 1
    heap[pos] = newitem
    _siftdown(heap, startpos, pos)


def heappush(heap, item):
    heap.append(item)
    _siftdown(heap, 0, len(heap) - 1)


def heappop(heap):
    lastelt = heap.pop()
    if heap:
        returnitem = heap[0]
        heap[0] = lastelt
        _siftup(heap, 0)
        return returnitem
    return lastelt


def heapreplace(heap, item):
    returnitem = heap[0]
    heap[0] = item
    _siftup(heap, 0)
    return returnitem


def heappushpop(heap, item):
    if heap and heap[0] < item:
        item, heap[0] = heap[0], item
        _siftup(heap, 0)
    return item


def heapify(x):
    for i in reversed(range(len(x) // 2)):
        _siftup(x, i)


def nlargest(n, iterable, key=None):
    return sorted(iterable, key=key, reverse=True)[:n]


def nsmallest(n, iterable, key=None):
    return sorted(iterable, key=key)[:n]


class _Reversed:
    """Wraps a key so the min-heap pops the largest first."""

    def __init__(self, k):
        self.k = k

    def __lt__(self, other):
        return other.k < self.k


def merge(*iterables, key=None, reverse=False):
    heap = []
    for order, it in enumerate(map(iter, iterables)):
        for value in it:
            k = value if key is None else key(value)
            heap.append((_Reversed(k) if reverse else k, order, value, it))
            break
    heapify(heap)
    while heap:
        k, order, value, it = heap[0]
        yield value
        for value in it:
            k = value if key is None else key(value)
            heapreplace(heap, (_Reversed(k) if reverse else k, order, value, it))
            break
        else:
            heappop(heap)
