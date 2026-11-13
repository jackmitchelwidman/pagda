"""os: files, directories and the environment (a small part of CPython's os)."""
import _os

name = 'posix'
sep = '/'
linesep = '\n'
pathsep = ':'
curdir = '.'
pardir = '..'
environ = _os.environ
getcwd = _os.getcwd
listdir = _os.listdir
remove = _os.remove
unlink = _os.remove
rmdir = _os.rmdir
rename = _os.rename
chdir = _os.chdir
getpid = _os.getpid
system = _os.system


def getenv(key, default=None):
    return environ.get(key, default)


def mkdir(path, mode=0o777):
    _os.mkdir(path, mode)


def makedirs(name, mode=0o777, exist_ok=False):
    parts = name.split('/')
    current = '/' if name.startswith('/') else ''
    for i, part in enumerate(parts):
        if not part:
            continue
        current = current + part if current in ('', '/') else current + '/' + part
        if _os.kind(current) == 2:
            if i == len(parts) - 1 and not exist_ok:
                raise FileExistsError(f"[Errno 17] File exists: '{name}'")
            continue
        _os.mkdir(current, mode)


def walk(top):
    dirs, files = [], []
    for entry in sorted(_os.listdir(top)):
        (dirs if _os.kind(top + '/' + entry) == 2 else files).append(entry)
    yield top, dirs, files
    for d in dirs:
        yield from walk(top + '/' + d)


class _Path:
    sep = '/'

    def join(self, a, *parts):
        path = a
        for b in parts:
            if b.startswith('/'):
                path = b
            elif not path or path.endswith('/'):
                path += b
            else:
                path += '/' + b
        return path

    def split(self, p):
        i = p.rfind('/') + 1
        head, tail = p[:i], p[i:]
        if head and head != '/' * len(head):
            head = head.rstrip('/')
        return head, tail

    def basename(self, p):
        return p[p.rfind('/') + 1:]

    def dirname(self, p):
        return self.split(p)[0]

    def splitext(self, p):
        base = p.rfind('/') + 1
        dot = p.rfind('.')
        if dot <= base or p[base:dot].strip('.') == '':
            return p, ''
        return p[:dot], p[dot:]

    def exists(self, p):
        return _os.kind(p) != 0

    def isfile(self, p):
        return _os.kind(p) == 1

    def isdir(self, p):
        return _os.kind(p) == 2

    def isabs(self, p):
        return p.startswith('/')

    def getsize(self, p):
        return _os.getsize(p)

    def normpath(self, p):
        if not p:
            return '.'
        lead = '/' if p.startswith('/') else ''
        out = []
        for part in p.split('/'):
            if part in ('', '.'):
                continue
            if part == '..' and out and out[-1] != '..':
                out.pop()
            elif part == '..' and lead:
                continue
            else:
                out.append(part)
        return lead + '/'.join(out) or lead or '.'

    def abspath(self, p):
        return self.normpath(p if p.startswith('/') else self.join(_os.getcwd(), p))

    def expanduser(self, p):
        if p == '~' or p.startswith('~/'):
            return environ.get('HOME', '~') + p[1:]
        return p


path = _Path()
