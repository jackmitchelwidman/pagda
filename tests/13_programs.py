# N-queens
def queens(n):
    sols = []
    def place(row, cols, d1, d2, cur):
        if row == n: sols.append(tuple(cur)); return
        for c in range(n):
            if c in cols or row - c in d1 or row + c in d2: continue
            place(row + 1, cols | {c}, d1 | {row - c}, d2 | {row + c}, cur + [c])
    place(0, frozenset(), frozenset(), frozenset(), [])
    return sols
print(len(queens(6)), queens(6)[0], len(queens(8)))
# Sieve and primes
def sieve(n):
    is_p = [True] * (n + 1); is_p[0] = is_p[1] = False
    for i in range(2, int(n ** 0.5) + 1):
        if is_p[i]:
            for j in range(i * i, n + 1, i): is_p[j] = False
    return [i for i, p in enumerate(is_p) if p]
ps = sieve(10000); print(len(ps), ps[:10], ps[-1], sum(ps))
# Matrix
def matmul(a, b): return [[sum(x * y for x, y in zip(row, col)) for col in zip(*b)] for row in a]
def matpow(m, n):
    r = [[int(i == j) for j in range(len(m))] for i in range(len(m))]
    while n:
        if n & 1: r = matmul(r, m)
        m = matmul(m, m); n >>= 1
    return r
print(matpow([[1, 1], [1, 0]], 300)[0][1], matmul([[1, 2], [3, 4]], [[5, 6], [7, 8]]), list(zip(*[[1, 2, 3], [4, 5, 6]])))
# Text processing
text = """the quick brown fox jumps over the lazy dog the fox
barks at the dog and the dog runs away quick quick"""
freq = {}
for w in text.split(): freq[w] = freq.get(w, 0) + 1
top = sorted(freq.items(), key=lambda kv: (-kv[1], kv[0]))[:5]
print(top, max(freq, key=lambda w: (len(w), w)), " ".join(w.capitalize() for w in text.split()[:4]), len(set(text.split())), sorted({len(w) for w in text.split()}))
for word, n in top: print(f"{word:<8}{'#' * n:<10}{n:>3}")
# Linked list, tree, graph
class Node:
    __slots__ = ("val", "next")
    def __init__(self, val, next=None): self.val, self.next = val, next
class LinkedList:
    def __init__(self, items=()):
        self.head = None; self.size = 0
        for x in reversed(list(items)): self.push(x)
    def push(self, v): self.head = Node(v, self.head); self.size += 1
    def __iter__(self):
        n = self.head
        while n: yield n.val; n = n.next
    def __len__(self): return self.size
    def reverse(self):
        prev, cur = None, self.head
        while cur: cur.next, prev, cur = prev, cur, cur.next
        self.head = prev; return self
    def __repr__(self): return " -> ".join(map(str, self)) or "empty"
ll = LinkedList(range(1, 6)); print(ll, len(ll), ll.reverse(), sum(ll), LinkedList())
class BST:
    def __init__(self): self.root = None; self.n = 0
    def insert(self, k):
        self.n += 1
        if self.root is None: self.root = [k, None, None]; return
        node = self.root
        while True:
            i = 1 if k < node[0] else 2
            if node[i] is None: node[i] = [k, None, None]; return
            node = node[i]
    def inorder(self, node="root"):
        if node == "root": node = self.root
        if node is None: return
        yield from self.inorder(node[1]); yield node[0]; yield from self.inorder(node[2])
    def height(self, node="root"):
        if node == "root": node = self.root
        return 0 if node is None else 1 + max(self.height(node[1]), self.height(node[2]))
t = BST()
for k in [50, 30, 70, 20, 40, 60, 80, 35, 45, 65]: t.insert(k)
print(list(t.inorder()), t.height(), t.n)
graph = {"A": ["B", "C"], "B": ["D"], "C": ["D", "E"], "D": ["F"], "E": ["F"], "F": []}
def bfs(g, s):
    seen, order, queue = {s}, [], [s]
    while queue:
        n = queue.pop(0); order.append(n)
        for m in g[n]:
            if m not in seen: seen.add(m); queue.append(m)
    return order
def dfs(g, n, seen=None):
    seen = seen if seen is not None else []
    if n not in seen:
        seen.append(n)
        for m in g[n]: dfs(g, m, seen)
    return seen
def paths(g, a, b, path=()):
    path = path + (a,)
    if a == b: return [path]
    return [p for n in g[a] if n not in path for p in paths(g, n, b, path)]
def topo(g):
    indeg = {n: 0 for n in g}
    for n in g:
        for m in g[n]: indeg[m] += 1
    ready = sorted(n for n in g if indeg[n] == 0); out = []
    while ready:
        n = ready.pop(0); out.append(n)
        for m in g[n]:
            indeg[m] -= 1
            if indeg[m] == 0: ready.append(m)
    return out
print(bfs(graph, "A"), dfs(graph, "A"), ["".join(p) for p in paths(graph, "A", "F")], topo(graph))
import heapq
def dijkstra(g, src):
    dist = {src: 0}; pq = [(0, src)]
    while pq:
        d, u = heapq.heappop(pq)
        if d > dist.get(u, float("inf")): continue
        for v, w in g.get(u, []):
            nd = d + w
            if nd < dist.get(v, float("inf")): dist[v] = nd; heapq.heappush(pq, (nd, v))
    return dist
print(sorted(dijkstra({"a": [("b", 7), ("c", 9), ("f", 14)], "b": [("c", 10), ("d", 15)], "c": [("d", 11), ("f", 2)], "d": [("e", 6)], "f": [("e", 9)]}, "a").items()))
# Dynamic programming
def lcs(a, b):
    dp = [[0] * (len(b) + 1) for _ in range(len(a) + 1)]
    for i, x in enumerate(a):
        for j, y in enumerate(b):
            dp[i + 1][j + 1] = dp[i][j] + 1 if x == y else max(dp[i][j + 1], dp[i + 1][j])
    out, i, j = [], len(a), len(b)
    while i and j:
        if a[i - 1] == b[j - 1]: out.append(a[i - 1]); i -= 1; j -= 1
        elif dp[i - 1][j] >= dp[i][j - 1]: i -= 1
        else: j -= 1
    return "".join(reversed(out))
def edit(a, b):
    prev = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        cur = [i]
        for j, y in enumerate(b, 1): cur.append(min(prev[j] + 1, cur[-1] + 1, prev[j - 1] + (x != y)))
        prev = cur
    return prev[-1]
def knap(items, cap):
    best = [0] * (cap + 1)
    for w, v in items:
        for c in range(cap, w - 1, -1): best[c] = max(best[c], best[c - w] + v)
    return best[cap]
def change(coins, total):
    ways = [1] + [0] * total
    for c in coins:
        for t in range(c, total + 1): ways[t] += ways[t - c]
    return ways[total]
print(lcs("AGGTAB", "GXTXAYB"), edit("kitten", "sitting"), knap([(1, 1), (3, 4), (4, 5), (5, 7)], 7), change([1, 2, 5, 10, 25, 50], 500), change([1, 2, 5], 10000))
# Sorting algorithms
def quicksort(a):
    if len(a) <= 1: return a
    p = a[len(a) // 2]
    return quicksort([x for x in a if x < p]) + [x for x in a if x == p] + quicksort([x for x in a if x > p])
def mergesort(a):
    if len(a) <= 1: return a
    m = len(a) // 2; l, r = mergesort(a[:m]), mergesort(a[m:]); out = []; i = j = 0
    while i < len(l) and j < len(r):
        if l[i] <= r[j]: out.append(l[i]); i += 1
        else: out.append(r[j]); j += 1
    return out + l[i:] + r[j:]
import random
random.seed(7); data = [random.randint(-1000, 1000) for _ in range(2000)]
print(quicksort(data) == sorted(data), mergesort(data) == sorted(data), sorted(data)[:5], sorted(data, key=abs)[:5], sorted(data, reverse=True)[:3])
# Big number work
def fact(n):
    r = 1
    for i in range(2, n + 1): r *= i
    return r
f100 = fact(100); print(f100, len(str(f100)), sum(int(d) for d in str(f100)), f100 % (10 ** 9 + 7), fact(200) // fact(198), str(2 ** 1000)[:20], len(str(3 ** 5000)), int(str(7 ** 300)) == 7 ** 300)
def fib(n):
    a, b = 0, 1
    for _ in range(n): a, b = b, a + b
    return a
print(fib(500), fib(1000) % 10 ** 10, pow(7, 10 ** 18, 10 ** 9 + 7), (10 ** 50 + 1) * (10 ** 50 - 1) == 10 ** 100 - 1, -(-7 ** 77 // 13) , hex(255 ** 20), int("z" * 30, 36), 123456789 ** 5 % 987654321, divmod(10 ** 40, 7 ** 20), (2 ** 127 - 1) % 1000003)
def collatz(n):
    steps = 0
    while n != 1: n = n // 2 if n % 2 == 0 else 3 * n + 1; steps += 1
    return steps
print(max(range(1, 3000), key=collatz), collatz(27), collatz(2 ** 100 + 1))
# State machine / tokenizer / evaluator
def tokenize(s):
    i = 0
    while i < len(s):
        c = s[i]
        if c.isspace(): i += 1
        elif c.isdigit() or c == ".":
            j = i
            while j < len(s) and (s[j].isdigit() or s[j] == "."): j += 1
            yield ("num", float(s[i:j]) if "." in s[i:j] else int(s[i:j])); i = j
        elif c in "+-*/()^": yield ("op", c); i += 1
        else: raise SyntaxError(f"bad char {c!r} at {i}")
class Parser:
    def __init__(self, toks): self.toks = list(toks); self.i = 0
    def peek(self): return self.toks[self.i] if self.i < len(self.toks) else (None, None)
    def eat(self): t = self.peek(); self.i += 1; return t
    def expr(self):
        v = self.term()
        while self.peek() in (("op", "+"), ("op", "-")):
            v = v + self.term() if self.eat()[1] == "+" else v - self.term()
        return v
    def term(self):
        v = self.factor()
        while self.peek() in (("op", "*"), ("op", "/")):
            v = v * self.factor() if self.eat()[1] == "*" else v / self.factor()
        return v
    def factor(self):
        b = self.atom()
        return b ** self.factor() if self.peek() == ("op", "^") and self.eat() else b
    def atom(self):
        k, v = self.eat()
        if k == "num": return v
        if v == "-": return -self.atom()
        if v == "(":
            r = self.expr(); self.eat(); return r
        raise SyntaxError(f"unexpected {v!r}")
for src in ["1 + 2 * 3", "(1 + 2) * 3", "2 ^ 3 ^ 2", "-4 + 10 / 4", "2 * (3 + 4) * (5 - 1)", "1.5 * 4", "3 $ 4"]:
    try: print(src, "=", Parser(tokenize(src)).expr())
    except SyntaxError as e: print(src, "-> error:", e)
# Bank simulation with exceptions and properties
class InsufficientFunds(Exception):
    def __init__(self, need, have): super().__init__(f"need {need}, have {have}"); self.short = need - have
class Account:
    _next_id = 1
    def __init__(self, owner, balance=0):
        self.owner, self._balance, self.history = owner, balance, []
        self.id = Account._next_id; Account._next_id += 1
    @property
    def balance(self): return self._balance
    def deposit(self, amt):
        if amt <= 0: raise ValueError("deposit must be positive")
        self._balance += amt; self.history.append(("dep", amt)); return self
    def withdraw(self, amt):
        if amt > self._balance: raise InsufficientFunds(amt, self._balance)
        self._balance -= amt; self.history.append(("wd", amt)); return self
    def __repr__(self): return f"Account#{self.id}({self.owner}: {self._balance:,.2f})"
class Savings(Account):
    rate = 0.05
    def add_interest(self): return self.deposit(round(self._balance * self.rate, 2))
a, s = Account("ann", 100), Savings("sam", 1000)
a.deposit(50).withdraw(30); s.add_interest()
for action in (lambda: a.withdraw(500), lambda: a.deposit(-1), lambda: setattr(a, "balance", 5)):
    try: action()
    except InsufficientFunds as e: print("short by", e.short, "|", e)
    except (ValueError, AttributeError) as e: print(type(e).__name__, e)
print(a, s, a.history, s.history, sorted([a, s], key=lambda x: -x.balance)[0].owner, Account._next_id)
# Generators pipeline over records
records = [{"name": n, "dept": d, "pay": p} for n, d, p in [("al", "eng", 120), ("bo", "ops", 80), ("cy", "eng", 150), ("di", "ops", 95), ("ed", "hr", 70)]]
by_dept = {}
for r in records: by_dept.setdefault(r["dept"], []).append(r)
report = {d: {"n": len(rs), "avg": sum(r["pay"] for r in rs) / len(rs), "top": max(rs, key=lambda r: r["pay"])["name"]} for d, rs in sorted(by_dept.items())}
print(report)
print("\n".join(f"{d:>4} | {v['n']} | {v['avg']:7.2f} | {v['top']}" for d, v in report.items()))
print(sum(r["pay"] for r in records if r["dept"] == "eng"), [r["name"] for r in sorted(records, key=lambda r: (r["dept"], -r["pay"]))], any(r["pay"] > 140 for r in records), all(r["pay"] > 75 for r in records))
# Game of life, string building
def life(cells, w, h):
    def n(x, y): return sum((x + dx, y + dy) in cells for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy)
    return {(x, y) for x in range(w) for y in range(h) if n(x, y) == 3 or ((x, y) in cells and n(x, y) == 2)}
g = {(1, 0), (2, 1), (0, 2), (1, 2), (2, 2)}
for _ in range(4): g = life(g, 8, 8)
print("\n".join("".join("#" if (x, y) in g else "." for x in range(6)) for y in range(5)))
def hanoi(n, a="A", b="B", c="C"): return hanoi(n - 1, a, c, b) + [(a, c)] + hanoi(n - 1, b, a, c) if n else []
print(len(hanoi(10)), hanoi(2), "".join(chr((ord(ch) - 97 + 13) % 26 + 97) if ch.islower() else ch for ch in "hello, world"), bin(sum(1 << i for i in range(0, 20, 3))), "{:b}".format(10 ** 9).count("1"))
def perms(s): return [s] if len(s) <= 1 else sorted({c + p for i, c in enumerate(s) for p in perms(s[:i] + s[i + 1:])})
print(perms("abc"), len(perms("abcdef")), [list(map(int, f"{i:03b}")) for i in range(4)], sum(int(c) for c in str(2 ** 15)), "".join(reversed("stressed")), "A man a plan".lower().replace(" ", "")[::-1])
