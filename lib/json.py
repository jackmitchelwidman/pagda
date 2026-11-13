"""json: encode and decode JSON text."""


class JSONDecodeError(ValueError):
    def __init__(self, msg, doc, pos):
        lineno = doc.count('\n', 0, pos) + 1
        colno = pos - doc.rfind('\n', 0, pos)
        super().__init__(f"{msg}: line {lineno} column {colno} (char {pos})")
        self.msg = msg
        self.doc = doc
        self.pos = pos
        self.lineno = lineno
        self.colno = colno


_ESCAPES = {'"': '\\"', '\\': '\\\\', '\n': '\\n', '\r': '\\r', '\t': '\\t', '\b': '\\b', '\f': '\\f'}


def _encode_str(s, ensure_ascii):
    out = ['"']
    for ch in s:
        if ch in _ESCAPES:
            out.append(_ESCAPES[ch])
            continue
        code = ord(ch)
        if code < 0x20 or (ensure_ascii and code > 0x7e):
            if code > 0xffff:
                code -= 0x10000
                out.append("\\u%04x\\u%04x" % (0xd800 | (code >> 10), 0xdc00 | (code & 0x3ff)))
            elif code == 0x7f or code < 0x20 or code > 0x7e:
                out.append("\\u%04x" % code)
            else:
                out.append(ch)
        else:
            out.append(ch)
    out.append('"')
    return ''.join(out)


def _encode_float(x):
    if x != x:
        return 'NaN'
    if x == float('inf'):
        return 'Infinity'
    if x == float('-inf'):
        return '-Infinity'
    return float.__repr__(x)


def dumps(obj, *, skipkeys=False, ensure_ascii=True, indent=None, separators=None, default=None, sort_keys=False):
    if isinstance(indent, int):
        indent = ' ' * indent
    if separators is None:
        item_sep, key_sep = (',', ': ') if indent is not None else (', ', ': ')
    else:
        item_sep, key_sep = separators
    active = set()

    def encode(o, level):
        if o is None:
            return 'null'
        if o is True:
            return 'true'
        if o is False:
            return 'false'
        if isinstance(o, str):
            return _encode_str(o, ensure_ascii)
        if isinstance(o, int):
            return int.__repr__(o)
        if isinstance(o, float):
            return _encode_float(o)
        if isinstance(o, (list, tuple, dict)):
            if id(o) in active:
                raise ValueError("Circular reference detected")
            active.add(id(o))
            if indent is not None:
                inner = '\n' + indent * (level + 1)
                sep = item_sep + inner
                end = '\n' + indent * level
            else:
                inner = end = ''
                sep = item_sep
            if isinstance(o, dict):
                if not o:
                    result = '{}'
                else:
                    parts = []
                    items = sorted(o.items()) if sort_keys else o.items()
                    for k, v in items:
                        if isinstance(k, str):
                            key = k
                        elif k is None:
                            key = 'null'
                        elif k is True:
                            key = 'true'
                        elif k is False:
                            key = 'false'
                        elif isinstance(k, int):
                            key = int.__repr__(k)
                        elif isinstance(k, float):
                            key = _encode_float(k)
                        elif skipkeys:
                            continue
                        else:
                            raise TypeError(f'keys must be str, int, float, bool or None, not {type(k).__name__}')
                        parts.append(_encode_str(key, ensure_ascii) + key_sep + encode(v, level + 1))
                    result = '{' + inner + sep.join(parts) + end + '}'
            elif not o:
                result = '[]'
            else:
                result = '[' + inner + sep.join([encode(v, level + 1) for v in o]) + end + ']'
            active.discard(id(o))
            return result
        if default is not None:
            return encode(default(o), level)
        raise TypeError(f'Object of type {type(o).__name__} is not JSON serializable')

    return encode(obj, 0)


def dump(obj, fp, **kwargs):
    fp.write(dumps(obj, **kwargs))


_WS = ' \t\n\r'
_UNESCAPE = {'"': '"', '\\': '\\', '/': '/', 'b': '\b', 'f': '\f', 'n': '\n', 'r': '\r', 't': '\t'}


def loads(s):
    if not isinstance(s, str):
        raise TypeError(f'the JSON object must be str, bytes or bytearray, not {type(s).__name__}')
    n = len(s)

    def skip(i):
        while i < n and s[i] in _WS:
            i += 1
        return i

    def parse_string(i):
        start = i - 1
        out = []
        while True:
            j = i
            while j < n and s[j] != '"' and s[j] != '\\' and s[j] >= ' ':
                j += 1
            if j >= n:
                raise JSONDecodeError("Unterminated string starting at", s, start)
            out.append(s[i:j])
            ch = s[j]
            if ch == '"':
                return ''.join(out), j + 1
            if ch != '\\':
                raise JSONDecodeError("Invalid control character at", s, j)
            if j + 1 >= n:
                raise JSONDecodeError("Unterminated string starting at", s, start)
            esc = s[j + 1]
            if esc == 'u':
                code = hex4(j + 2)
                j += 6
                if 0xd800 <= code < 0xdc00 and s[j:j + 2] == '\\u':
                    low = hex4(j + 2)
                    if 0xdc00 <= low < 0xe000:
                        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00)
                        j += 6
                out.append(chr(code))
                i = j
            elif esc in _UNESCAPE:
                out.append(_UNESCAPE[esc])
                i = j + 2
            else:
                raise JSONDecodeError("Invalid \\escape", s, j)

    def hex4(i):
        digits = s[i:i + 4]
        if len(digits) != 4 or not all(c in '0123456789abcdefABCDEF' for c in digits):
            raise JSONDecodeError("Invalid \\uXXXX escape", s, i - 1)
        return int(digits, 16)

    def parse_value(i):
        if i >= n:
            raise JSONDecodeError("Expecting value", s, i)
        ch = s[i]
        if ch == '"':
            return parse_string(i + 1)
        if ch == '{':
            result = {}
            i = skip(i + 1)
            if i < n and s[i] == '}':
                return result, i + 1
            comma = -1
            while True:
                if i < n and s[i] == '}' and comma >= 0:
                    raise JSONDecodeError("Illegal trailing comma before end of object", s, comma)
                if i >= n or s[i] != '"':
                    raise JSONDecodeError("Expecting property name enclosed in double quotes", s, i)
                key, i = parse_string(i + 1)
                i = skip(i)
                if i >= n or s[i] != ':':
                    raise JSONDecodeError("Expecting ':' delimiter", s, i)
                value, i = parse_value(skip(i + 1))
                result[key] = value
                i = skip(i)
                if i < n and s[i] == '}':
                    return result, i + 1
                if i >= n or s[i] != ',':
                    raise JSONDecodeError("Expecting ',' delimiter", s, i)
                comma = i
                i = skip(i + 1)
        if ch == '[':
            result = []
            i = skip(i + 1)
            if i < n and s[i] == ']':
                return result, i + 1
            while True:
                value, i = parse_value(i)
                result.append(value)
                i = skip(i)
                if i < n and s[i] == ']':
                    return result, i + 1
                if i >= n or s[i] != ',':
                    raise JSONDecodeError("Expecting ',' delimiter", s, i)
                comma = i
                i = skip(i + 1)
                if i < n and s[i] == ']':
                    raise JSONDecodeError("Illegal trailing comma before end of array", s, comma)
        if s.startswith('true', i):
            return True, i + 4
        if s.startswith('false', i):
            return False, i + 5
        if s.startswith('null', i):
            return None, i + 4
        if s.startswith('NaN', i):
            return float('nan'), i + 3
        if s.startswith('Infinity', i):
            return float('inf'), i + 8
        if s.startswith('-Infinity', i):
            return float('-inf'), i + 9
        j = i
        if j < n and s[j] == '-':
            j += 1
        if j < n and s[j] == '0':
            j += 1
        elif j < n and s[j] in '123456789':
            while j < n and s[j] in '0123456789':
                j += 1
        else:
            raise JSONDecodeError("Expecting value", s, i)
        is_float = False
        if j + 1 < n and s[j] == '.' and s[j + 1] in '0123456789':
            is_float = True
            j += 1
            while j < n and s[j] in '0123456789':
                j += 1
        if j < n and s[j] in 'eE':
            k = j + 1
            if k < n and s[k] in '+-':
                k += 1
            if k < n and s[k] in '0123456789':
                is_float = True
                while k < n and s[k] in '0123456789':
                    k += 1
                j = k
        return (float(s[i:j]) if is_float else int(s[i:j])), j

    value, i = parse_value(skip(0))
    i = skip(i)
    if i != n:
        raise JSONDecodeError("Extra data", s, i)
    return value


def load(fp):
    return loads(fp.read())
