#!/usr/bin/env python3
"""Builds the Pagda website into website/site/.

    python3 website/build.py

Every code sample on the site is a file in website/examples/. The build runs each
one through ./pagda and puts what it really printed on the page, so a sample that
stops working stops the build. The language reference is made from README.md.
"""
import html
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PAGDA = os.path.join(ROOT, "pagda")
EX = os.path.join(HERE, "examples")
OUT = os.path.join(HERE, "site")

NAV = [("index", "Home"), ("start", "Get started"), ("tour", "Tour"), ("proofs", "Proofs"),
       ("reference", "Reference"), ("examples", "Examples"), ("faq", "FAQ")]

HEADER = "from __future__ import annotations\nfrom dependent import *\n"

KEYWORDS = set("""False None True and as assert break case class continue def del elif else except finally for from
if import in is lambda match not or pass raise return try while with yield""".split())
TYPES = set("""Nat int str bool list dict tuple Optional Type Fn Callable Eq Data Vec Fin Dec Empty""".split())
TOKEN = re.compile(r"""
    (?P<comment>\#[^\n]*)
  | (?P<string>[fFrR]?(?:"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'))
  | (?P<deco>@[A-Za-z_][A-Za-z_0-9]*)
  | (?P<number>\b\d+\b)
  | (?P<name>[A-Za-z_][A-Za-z_0-9]*)
""", re.X)


def highlight(src):
    out, at, prev = [], 0, ""
    for m in TOKEN.finditer(src):
        out.append(html.escape(src[at:m.start()]))
        text, kind = m.group(0), m.lastgroup
        cls = None
        if kind == "comment":
            cls = "c"
        elif kind == "string":
            cls = "s"
        elif kind == "deco":
            cls = "d"
        elif kind == "number":
            cls = "n"
        elif text in KEYWORDS:
            cls = "k"
        elif text in TYPES:
            cls = "t"
        elif prev in ("def", "class"):
            cls = "f"
        out.append(f'<span class="{cls}">{html.escape(text)}</span>' if cls else html.escape(text))
        if kind == "name":
            prev = text
        at = m.end()
    out.append(html.escape(src[at:]))
    return "".join(out)


_runs = {}


def run(name):
    """Runs an example with a fresh cache, so Agda really checks it."""
    if name not in _runs:
        env = dict(os.environ, XDG_CACHE_HOME=os.path.join(HERE, ".cache"))
        p = subprocess.run([PAGDA, name + ".py"], cwd=EX, env=env, capture_output=True, text=True, stdin=subprocess.DEVNULL)
        _runs[name] = (p.returncode, p.stdout.rstrip("\n"), p.stderr.rstrip("\n"))
    return _runs[name]


def source(name):
    return open(os.path.join(EX, name + ".py")).read().rstrip("\n") + "\n"


def shown(src):
    """The sample without the two lines every checked file starts with."""
    return src[len(HEADER):].lstrip("\n") if src.startswith(HEADER) else src


def panel(name, mode):
    src = source(name)
    code, out, err = run(name)
    want = 1 if mode == "refuse" else 0
    if code != want:
        sys.exit(f"website/examples/{name}.py: expected exit {want}, got {code}\n{out}\n{err}")
    full = html.escape(src, quote=True)
    status = '<span class="chip bad">refused</span>' if mode == "refuse" else '<span class="chip ok">checked</span>'
    parts = [f'<figure class="sample {"is-bad" if mode == "refuse" else "is-ok"}">',
             f'<figcaption><span class="file">{name}.py</span>{status}'
             f'<button class="copy" type="button" data-src="{full}">Copy</button></figcaption>',
             f'<pre class="code"><code>{highlight(shown(src))}</code></pre>']
    if mode == "run" and out:
        parts.append(f'<div class="result"><div class="cmd">$ pagda {name}.py</div><pre>{html.escape(out)}</pre></div>')
    if mode == "check":
        parts.append(f'<div class="result"><div class="cmd">$ pagda --check {name}.py</div><pre>{name}.py: no type errors</pre></div>')
    if mode == "refuse":
        # the first line of the report names the file and line; the Python line follows
        parts.append(f'<div class="result"><div class="cmd">$ pagda {name}.py</div><pre>{html.escape(err)}</pre></div>')
    parts.append("</figure>")
    return "\n".join(parts)


def inline(text):
    text = html.escape(text, quote=False)
    text = re.sub(r"`([^`]+)`", lambda m: "<code>" + m.group(1).replace("\\|", "|") + "</code>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    return text


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def markdown(md):
    """The small part of Markdown that README.md uses."""
    lines, out, i = md.split("\n"), [], 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("```"):
            lang, body = line[3:].strip(), []
            i += 1
            while not lines[i].startswith("```"):
                body.append(lines[i])
                i += 1
            code = "\n".join(body)
            out.append(f'<pre class="code plain"><code>{highlight(code) if lang == "python" else html.escape(code)}</code></pre>')
        elif line.startswith("#"):
            level = len(line) - len(line.lstrip("#"))
            text = line[level:].strip()
            tag = "h2" if level <= 3 else "h3"
            out.append(f'<{tag} id="{slug(text)}">{inline(text)}</{tag}>')
        elif line.startswith("- "):
            items = []
            while i < len(lines) and (lines[i].startswith("- ") or lines[i].startswith("  ")):
                if lines[i].startswith("- "):
                    items.append(lines[i][2:])
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            out.append("<ul>" + "".join(f"<li>{inline(x)}</li>" for x in items) + "</ul>")
            continue
        elif line.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in re.split(r"(?<!\\)\|", lines[i].strip().strip("|"))])
                i += 1
            head, body = rows[0], rows[2:]
            out.append('<div class="table"><table><thead><tr>' + "".join(f"<th>{inline(c)}</th>" for c in head) +
                       "</tr></thead><tbody>" +
                       "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>" for r in body) +
                       "</tbody></table></div>")
            continue
        elif line.strip():
            para = [line]
            while i + 1 < len(lines) and lines[i + 1].strip() and not re.match(r"(```|#|- |\|)", lines[i + 1]):
                i += 1
                para.append(lines[i])
            out.append(f"<p>{inline(' '.join(para))}</p>")
        i += 1
    return "\n".join(out)


def reference():
    md = open(os.path.join(ROOT, "README.md")).read()
    start = md.index("### The first lines")
    return markdown(md[start:])


def expand(body):
    body = re.sub(r"\{\{(run|check|refuse):([a-z0-9_]+)\}\}", lambda m: panel(m.group(2), m.group(1)), body)
    body = body.replace("{{reference}}", reference())
    return body


def toc(body):
    heads = re.findall(r'<h2 id="([^"]+)">(.*?)</h2>', body)
    if len(heads) < 3:
        return ""
    return '<nav class="toc" aria-label="On this page"><div class="toc-title">On this page</div>' + \
        "".join(f'<a href="#{i}">{re.sub("<[^>]+>", "", t)}</a>' for i, t in heads) + "</nav>"


HEAD = """<title>{title}</title>
<meta name="claude:viewport-fit-sides" content="cover">
<meta name="description" content="{desc}">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:opsz,wght@12..96,500;12..96,700&family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500;600&display=swap">
<link rel="stylesheet" href="site.css">"""


def page(name, title, desc, body):
    nav = "".join(f'<a href="{n}.html"{" aria-current=\"page\"" if n == name else ""}>{label}</a>' for n, label in NAV)
    body = expand(body)
    side = toc(body) if name != "index" else ""
    main = f'<main class="{"home" if name == "index" else "doc"}">{side}<div class="content">{body}</div></main>' \
        if name != "index" else f'<main class="home">{body}</main>'
    return f"""<header class="top"><div class="wrap">
<a class="brand" href="index.html"><span class="mark" aria-hidden="true">⊢</span>Pagda</a>
<nav class="nav" aria-label="Site">{nav}</nav>
</div></header>
{main}
<footer class="foot"><div class="wrap">
<div><span class="mark" aria-hidden="true">⊢</span> Pagda. Python, checked by Agda before it runs.</div>
<nav aria-label="Footer">{nav}</nav>
</div></footer>
<script src="site.js"></script>"""


def main():
    if not os.path.exists(PAGDA):
        sys.exit("build ./pagda first: run make")
    os.makedirs(OUT, exist_ok=True)
    for fn in sorted(os.listdir(os.path.join(HERE, "pages"))):
        if not fn.endswith(".html"):
            continue
        name = fn[:-5]
        raw = open(os.path.join(HERE, "pages", fn)).read()
        meta = re.match(r"<!--\s*title:\s*(.*?)\s*\|\s*desc:\s*(.*?)\s*-->\n", raw)
        title, desc = meta.group(1), meta.group(2)
        head = HEAD.format(title=html.escape(title), desc=html.escape(desc, quote=True))
        inner = page(name, title, desc, raw[meta.end():])
        full = f'<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n' \
               f'<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n{head}\n</head>\n<body>\n{inner}\n</body>\n</html>\n'
        open(os.path.join(OUT, fn), "w").write(full)
        if name == "index":
            # the same page without the document wrapper, for hosts that add their own
            open(os.path.join(OUT, "_home.html"), "w").write(head + "\n" + inner + "\n")
    for fn in ("site.css", "site.js"):
        open(os.path.join(OUT, fn), "w").write(open(os.path.join(HERE, fn)).read())
    print(f"built {len(os.listdir(OUT))} files in website/site, from {len(_runs)} checked examples")


if __name__ == "__main__":
    main()
