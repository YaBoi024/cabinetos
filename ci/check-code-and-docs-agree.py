#!/usr/bin/env python3
"""
Does a comment name something of ours that is not there, does a header declare
a function nothing defines, is a private address in a tracked file?

  ci/check-code-and-docs-agree.py <base> <head>
  ci/check-code-and-docs-agree.py --whole-tree [<revision>]

Prints each finding as file:line and a count per check, and exits 1 if there
is any; 0 when there is none. .github/workflows/checks.yml runs the first form
on every pull request; on GitHub each finding is also marked on the pull
request. REPORT ONLY UNTIL A DATE: while today (UTC) is not past
REPORT_ONLY_UNTIL (YYYY-MM-DD, set by checks.yml for this job), they are
warnings and it exits 0.

Both forms read the revision from git, never the checkout, so they can run
from any checkout without switching it; run it inside one.

WHY. The rules that a comment changes with the code (CLAUDE.md, rules 4 to 6)
held where CI checks them and slipped where only a session remembers: the
audit of 2026-11-03 found 33 places, across 695 commits, where comments or docs
had drifted (docs/lessons/README.md, rule 8). This is the mechanical half of
that audit; the skill check-code-and-docs-agree is the full read.

WHAT A PULL REQUEST IS HELD TO. Only what it touches, so the old tree never
fails it: a comment on a line it adds or changes; a declaration on a line it
adds or changes, or whose name is on a line it removes (a definition taken away
with the header left behind); an address on a line it adds. A rename that
leaves a comment stale on a line the pull request does not touch is not seen
here; --whole-tree sees it.

WHAT COUNTS. Comments are read in FOLDERS. Upstream's names (Dolphin's, PCSX2's,
MAME's functions and files) fill them, and cannot be told from a stale one of
ours by looking, so only what is clearly ours counts:
  * a path that starts with one of this repository's top-level folders must be
    there, by the rules of ci/check-doc-paths.py;
  * a bare file name that is not in the tree counts only when our own history
    has a file of that name, so it was ours and went;
  * `ns::name`, when `ns` is a namespace, struct or enum our code defines:
    `ns::name` must be in the code, or `name` in a file that defines `ns`. Not
    when the sentence gives something to another project first ("Dolphin's
    Core::Stop()"), since Core is ours too, or in frontend/ps2, which talks of
    PCSX2's namespaces, unless frontend/ps2 defines `ns`;
  * a name a comment pairs with a file of ours ("launch.cpp's startPlaying",
    "startPlaying in launch.cpp", "startPlaying (launch.cpp)", "(romfile.cpp,
    prepareFile)") must be in that file or its .h or .cpp beside it. The name
    has to look like code: camelCase, snake_case or with ();
  * a frontend flag must be one that cli.cpp reads, or --check-option-tables in
    main.cpp. A flag counts as the frontend's when it ends -test or -probe (no
    other program here names them so), follows `cabinetos-frontend`, or stands
    beside a frontend flag with only a comma, "or", "and" or "not" between.
    Every other flag is another program's: bwrap's, gamescope's, nmcli's.
SKIP below holds what is left over. A new entry says why it is there.

A bare file name said to be gone ("Moved out of screens.cpp") is true of it and
does not count, and neither does a name written as a prefix
(`design::kScrollFade*`) that something still starts with.

WHAT IT CANNOT SEE. A bare `name()` or `name` in prose, which could be anybody's.
A declaration with no return type (a constructor) or under `extern "C"`. Overloads
are told apart by name alone. A heredoc's lines that start with # read as
comments.
"""

import bisect
import collections
import datetime
import importlib.util
import os
import posixpath
import re
import subprocess
import sys
import tarfile

FOLDERS = ("frontend/src/", "frontend/ps2/", "wiibridge/", "build_files/", "ci/",
           "system_files/", "tools/", "cores/")
HEADERS = ("frontend/src/", "frontend/ps2/")
OUR_CODE = ("frontend/src/", "frontend/ps2/", "wiibridge/", "tools/")
NOT_READ = ("cores/options/", "cores/sunshine-patches/",
            "frontend/src/libretro.h", "frontend/src/libretro_vulkan.h")

C_FILES = (".c", ".cc", ".cpp", ".h", ".hpp", ".inc")
# Not code, so no comments to read and no lines to pair a name with.
NO_COMMENTS = (".md", ".json", ".xml", ".patch", ".pub", ".svg", ".csv")

# Not read for addresses: package versions, four numbers of which can start 10.
NO_ADDRESSES = ("base-manifest.txt",)

# The addresses the docs and the code use as examples, none of them a machine.
ADDRESSES = {
    "192.168.0.20",   # first run's example (setup.cpp, docs/wiki/first-run.md)
    "192.168.1.50",   # docs/wiki/file-access.md's example
    "192.168.77.20",  # the self-test's made-up server (selftests.cpp)
    # The name of each range itself.
    "10.0.0.0",
    "172.16.0.0",
    "192.168.0.0",
}

# What looks like ours and is not. Each says whose it is.
SKIP = {
    ".github/workflows/libretro.yml",  # Azahar's own workflow
    "tools/build-flycast.sh",          # Cabinet's, as its manifest says
    "tools/build-pcsx2-deps-mac.sh",   # Cabinet's
    "frontend/libretro.c",             # PCSX ReARMed's
    "docs/interop/qcow2.txt",          # QEMU's
    "docs/NAME",                       # how check-docs-list.sh writes any doc
    "docs/NAME.md",
    "Device::wait_idle_nolock",        # Granite's Device, not bluetooth's or volume's
    "RomM",                            # the server, not a camelCase name
}

PRIVATE = re.compile(
    r"(?<![\w.])(?:10(?:\.\d{1,3}){3}|172\.(?:1[6-9]|2\d|3[01])(?:\.\d{1,3}){2}"
    r"|192\.168(?:\.\d{1,3}){2})(?![\w]|\.\d)")

HUNK = re.compile(r"^@@ -\d+(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")
C_TOKEN = re.compile(
    r"//[^\n]*"
    r"|/\*.*?(?:\*/|\Z)"
    r"|(?<!\w)(?:u8|[uUL])?R\"([^(\s]*)\("
    r"|\"(?:\\.|[^\"\\\n])*\"?"
    r"|(?<![\w'])'(?:\\.|[^'\\\n])*'?", re.S)
PREPROCESSOR = re.compile(r"^[ \t]*#(?:[^\n\\]|\\.)*", re.M | re.S)

PATH = re.compile(r"(?<![\w/.~$@:%-])(?:\./)?(\.?[A-Za-z_][\w.+-]*(?:/[\w.+-]+)+/?)"
                  r"(?![\w*<{$%(\[]|/)")
# A sentence that gives something to another project. Its names are that
# project's, though a struct of ours may share them.
ANOTHER_PROJECT = re.compile(r"\b(?!RomM\b|CabinetOS\b|Cabinet\b|MMagTech\b)[A-Z][\w-]*'s\s")
# Said of a file that has gone, and true of it: "Moved out of screens.cpp".
HISTORY = re.compile(r"\b(?:out of|from|was in|were in|old|formerly|used to|until|once|moved|split|"
                     r"renamed|replaced|had)\b.{0,30}$", re.I)
FILE_WORD = r"(?:[\w.+-]+/)*[\w+-][\w.+-]*\.(?:cpp|cc|c|hpp|h|inc|sh|py|yml|yaml)"
BARE_FILE = re.compile(r"(?<![\w/.@$-])([\w+-][\w.+-]*\.(?:cpp|cc|c|hpp|h|inc|sh|py))(?![\w/-])")
QUALIFIED = re.compile(r"(?<![\w:])((?:[A-Za-z_]\w*::)+~?[A-Za-z_]\w*)(\*)?")
NAME_WORD = r"`?((?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)(\(\))?`?"
# Each with the group of its file and of its name, and whether a name the
# comment's own file has is that file's, which the pair only points beside: a
# comment in standalone_supermodel.cpp lists quality.h and that file's own
# pictureFor in one bracket.
PAIRS = (
    (re.compile(r"(?<![\w/.-])`?(" + FILE_WORD + r")`?'s\s+(?:own\s+)?" + NAME_WORD), 1, 2, False),
    (re.compile(NAME_WORD + r",?\s+in\s+`?(" + FILE_WORD + r")`?(?![\w/-])"), 3, 1, False),
    (re.compile(NAME_WORD + r"\s+\(`?(" + FILE_WORD + r")`?\)"), 3, 1, True),
    (re.compile(r"\(`?(" + FILE_WORD + r")`?,\s+" + NAME_WORD + r"[,);]"), 1, 2, True),
)
LOOKS_LIKE_CODE = re.compile(r"::|_|[a-z0-9][A-Z]")
FLAG = re.compile(r"(?<![\w-])--[a-z][a-z0-9-]*[a-z0-9](?![\w])")
FLAG_IN_CODE = re.compile(r"\"(--[a-z][\w-]*)=?\"")
FLAG_OWNER = re.compile(r"\bcabinetos-frontend\b|\bcli\.cpp's|\bfrontend's")
FLAG_END_OF_COMMAND = re.compile(r"[.;:)](?:\s|$)|[`(]")
FLAG_BESIDE = re.compile(
    r"(--[a-z][a-z0-9-]*[a-z0-9])(?:\s+[\w<>.,/-]+?)??\s*(?:,\s*(?:(?:or|and|not)\s+)?|\s(?:or|and|not)\s+)"
    r"(--[a-z][a-z0-9-]*[a-z0-9])(?![\w])")

SCOPE_NAME = re.compile(
    r"\b(?:namespace|struct|class|union|enum(?:\s+class|\s+struct)?)\s+([A-Za-z_][\w:]*)"
    r"\s*(?:final\s*)?(?:\{|:(?!:))")
CODE_QUALIFIED = re.compile(r"[A-Za-z_]\w*::~?[A-Za-z_]\w*")
SCOPE_TOKEN = re.compile(r"[{};]|\b((?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)\s*\(")
SCOPE_HEAD = re.compile(r"\b(?:namespace|struct|class|union)\s+([\w:]+)?[^;()]*$")
PLAIN_SCOPE_HEAD = re.compile(r"\bextern\s*\"\"\s*$|\bnamespace\s*$|\benum\b[^;()]*$")
AFTER_PARAMETERS = re.compile(
    r"\s*(?:const\b\s*)?(?:&{1,2}\s*)?(?:noexcept\b(?:\s*\([^)]*\))?\s*)?"
    r"(?:override\b\s*|final\b\s*)*(?:->\s*[^{;=]+?)?\s*(?:\{|:(?!:))")
NO_BODY_ON_PURPOSE = re.compile(r"\s*(?:const\s*)?(?:noexcept\s*)?=\s*(?:default|delete|0)\s*;")
NOT_A_FUNCTION = {"if", "for", "while", "switch", "return", "sizeof", "decltype", "static_assert",
                  "alignof", "catch", "noexcept", "operator", "defined", "alignas", "throw",
                  "new", "delete"}
NOT_A_DECLARATION = re.compile(r"^(?:using|typedef|friend|return|static_assert|template\s*<[^>]*>\s*using)\b")
DECLARATION = re.compile(
    r"^(?:template\s*<.*?>\s*)?(?:(?:static|inline|virtual|explicit|extern|constexpr|\[\[\w+\]\])\s+)*"
    r"[\w:<>,*&\s]+?[\s*&](~?[A-Za-z_]\w*)\s*\(")


def git(*args):
    return subprocess.run(["git", "-c", "core.quotePath=false", *args], capture_output=True,
                          encoding="utf-8", errors="replace", check=True).stdout


def read_tree(revision):
    """Every tracked file at the revision: path -> text, None when it is binary
    or a link."""
    archive = subprocess.Popen(["git", "archive", revision], stdout=subprocess.PIPE)
    tree = {}
    with tarfile.open(fileobj=archive.stdout, mode="r|") as tar:
        for member in tar:
            if member.isdir():
                continue
            text = None
            if member.isfile():
                try:
                    text = tar.extractfile(member).read().decode("utf-8")
                except UnicodeDecodeError:
                    pass
            tree[member.name] = text
    if archive.wait():
        sys.exit(f"git archive {revision} failed")
    return tree


def read_diff(base, head):
    """What the pull request changed: {file: line numbers it added}, and {file:
    the lines it removed}."""
    added, removed = collections.defaultdict(set), collections.defaultdict(list)
    new_file, old_file, line, minus, plus = "", "", 0, 0, 0
    for text in git("diff", "--unified=0", "--no-color", "--no-ext-diff", "-M", base, head).split("\n"):
        if minus or plus:
            if text.startswith("-"):
                removed[old_file].append(text[1:])
                minus -= 1
            elif text.startswith("+"):
                added[new_file].add(line)
                line += 1
                plus -= 1
        elif text.startswith("--- "):
            old_file = text[6:].rstrip("\t")
        elif text.startswith("+++ "):
            new_file = "" if text == "+++ /dev/null" else text[6:].rstrip("\t")
        elif text.startswith("@@ "):
            hunk = HUNK.match(text)
            minus, line, plus = int(hunk.group(1) or 1), int(hunk.group(2)), int(hunk.group(3) or 1)
    return added, removed


def blank(text):
    return re.sub(r"[^\n]", " ", text)


def split_c(text):
    """C or C++: (comments as (line, text), the code with comments blanked, the
    code with comments, string contents and preprocessor lines blanked). Lines
    do not move."""
    comments, code, bare = [], [], []
    at, line = 0, 1
    while True:
        m = C_TOKEN.search(text, at)
        if not m:
            code.append(text[at:])
            bare.append(text[at:])
            break
        code.append(text[at:m.start()])
        bare.append(text[at:m.start()])
        line += text.count("\n", at, m.start())
        token, end = m.group(), m.end()
        if token.startswith(("//", "/*")):
            for i, part in enumerate(token.split("\n")):
                comments.append((line + i, part.lstrip(" \t/*!<").rstrip(" \t*/")))
            code.append(blank(token))
            bare.append(blank(token))
        else:
            if m.group(1) is not None:
                close = text.find(")" + m.group(1) + '"', end)
                end = len(text) if close < 0 else close + len(m.group(1)) + 2
            whole = text[m.start():end]
            code.append(whole)
            bare.append('""' + blank(whole[2:]) if whole.startswith('"') else blank(whole))
        line += text.count("\n", m.start(), end)
        at = end
    return comments, "".join(code), PREPROCESSOR.sub(lambda p: blank(p.group()), "".join(bare))


def split_hash(text, python):
    """A script or a settings file: (comments as (line, text), the code with
    comments blanked). A Python docstring reads as a comment."""
    comments, code = [], []
    inside = None
    for line, raw in enumerate(text.split("\n"), 1):
        if inside:
            end = raw.find(inside)
            comments.append((line, (raw if end < 0 else raw[:end]).strip()))
            if end >= 0:
                inside = None
            code.append("")
            continue
        stripped = raw.strip()
        if python and stripped[:3] in ('"""', "'''"):
            quote, rest = stripped[:3], stripped[3:]
            if quote in rest:
                comments.append((line, rest[:rest.find(quote)].strip()))
            else:
                comments.append((line, rest.strip()))
                inside = quote
            code.append("")
            continue
        if stripped.startswith("#"):
            if not (line == 1 and stripped.startswith("#!")):
                comments.append((line, stripped.lstrip("#").strip()))
            code.append("")
            continue
        single = double = False
        for i, ch in enumerate(raw):
            if ch == "'" and not double:
                single = not single
            elif ch == '"' and not single:
                double = not double
            elif ch == "#" and not single and not double and i and raw[i - 1] in " \t":
                comments.append((line, raw[i + 1:].strip()))
                raw = raw[:i]
                break
        code.append(raw)
    return comments, "\n".join(code)


class Tree:
    """The files at a revision, each split into its comments and code once."""

    def __init__(self, files):
        self.files = files
        self.folders = {posixpath.dirname(f) for f in files}
        for folder in list(self.folders):
            while "/" in folder:
                folder = folder.rsplit("/", 1)[0]
                self.folders.add(folder)
        self.top = {f.split("/", 1)[0] for f in files if "/" in f}
        self.by_name = collections.defaultdict(list)
        for f in files:
            self.by_name[posixpath.basename(f)].append(f)
        self.split_files = {}

    def split(self, path):
        """(comments, code, bare), or None when the file has no comments to read."""
        if path in self.split_files:
            return self.split_files[path]
        text = self.files.get(path)
        result = None
        if text is not None and not path.endswith(NO_COMMENTS):
            if path.endswith(C_FILES):
                result = split_c(text)
            else:
                comments, code = split_hash(text, path.endswith(".py"))
                result = comments, code, code
        self.split_files[path] = result
        return result

    def code_of(self, path):
        split = self.split(path)
        return split[1] if split else ""


def word_of(name, whole=True):
    """A name as a word; -D is how a build script hands CMake one (-DX11_API=ON)."""
    return re.compile(r"(?:\b|(?<=-D))" + re.escape(name) + (r"\b" if whole else ""))


def blocks(comments):
    """Comment lines on consecutive lines, joined, since a wrapped comment
    splits what it pairs: (text, the offset each line starts at, those lines)."""
    current, starts, lines, length = [], [], [], 0
    for line, text in comments:
        if lines and line != lines[-1] + 1:
            yield " ".join(current), starts, lines
            current, starts, lines, length = [], [], [], 0
        current.append(text)
        starts.append(length)
        lines.append(line)
        length += len(text) + 1
    if lines:
        yield " ".join(current), starts, lines


class Check:
    def __init__(self, tree, added, removed):
        self.tree = tree
        self.added = added
        self.removed = removed
        self.read = collections.Counter()
        self.found = []
        self.seen = set()

    def touched(self, path, first, last):
        if self.added is None:
            return True
        lines = self.added.get(path, ())
        return any(n in lines for n in range(first, last + 1))

    def report(self, check, path, line, what):
        if (path, line, what) not in self.seen:
            self.seen.add((path, line, what))
            self.found.append((check, path, line, what))

    def comments(self, names, gone):
        for path in sorted(self.tree.files):
            if not path.startswith(FOLDERS) or path.startswith(NOT_READ):
                continue
            if self.added is not None and path not in self.added:
                continue
            split = self.tree.split(path)
            if not split:
                continue
            for text, starts, lines in blocks(split[0]):
                def spot(m):
                    first = lines[bisect.bisect_right(starts, m.start()) - 1]
                    last = lines[bisect.bisect_right(starts, m.end() - 1) - 1]
                    return first, last

                self.paths(path, text, spot)
                self.bare_files(path, text, spot, gone)
                self.qualified(path, text, spot, names)
                self.pairs(path, text, spot)
                self.flags(path, text, spot, names)

    def paths(self, path, text, spot):
        for m in PATH.finditer(text):
            named = m.group(1).rstrip(".")
            if named.split("/", 1)[0] not in self.tree.top:
                continue
            # Two words with a slash ("ci/cd") are not a path.
            if not named.endswith("/") and named.count("/") == 1 and "." not in posixpath.basename(named):
                continue
            first, last = spot(m)
            if not self.touched(path, first, last):
                continue
            self.read["files"] += 1
            if not named.endswith("-") and not self.path_is_there(named.rstrip("/")):
                self.report("files", path, first, f"names {named}, which is not in the repository")

    def path_is_there(self, named):
        tree = self.tree
        if named in tree.files or named in tree.folders or named in SKIP or named in CABINET:
            return True
        if any(f.endswith("/" + named) for f in tree.files):
            return True
        if any(d.endswith("/" + named) for d in tree.folders):
            return True
        return any(subprocess.run(["git", "check-ignore", "-q", "--no-index", p]).returncode == 0
                   for p in (named, named + "/"))

    def bare_files(self, path, text, spot, gone):
        for m in BARE_FILE.finditer(text):
            named = m.group(1)
            if named in self.tree.by_name or named in SKIP or named not in gone:
                continue
            if HISTORY.search(text[:m.start()]):
                continue
            first, last = spot(m)
            if self.touched(path, first, last):
                self.read["files"] += 1
                self.report("files", path, first,
                            f"names {named}, a file of ours that is no longer in the repository")

    def qualified(self, path, text, spot, names):
        for m in QUALIFIED.finditer(text):
            named = m.group(1)
            parts = named.split("::")
            scope, name = parts[-2], parts[-1].lstrip("~")
            if scope not in names.scopes or named in SKIP:
                continue
            if path.startswith("frontend/ps2/") and not any(f.startswith("frontend/ps2/") for f in names.scopes[scope]):
                continue
            before = text[:m.start()]
            if ANOTHER_PROJECT.search(before[before.rfind(". ") + 1:]):
                continue
            first, last = spot(m)
            if not self.touched(path, first, last):
                continue
            self.read["qualified names"] += 1
            if f"{scope}::{name}" in names.uses:
                continue
            word = word_of(name, whole=not m.group(2))
            if not any(word.search(self.tree.code_of(f)) for f in names.scopes[scope]):
                self.report("qualified names", path, first,
                            f"names {named}, and {scope} has no {name}")

    def pairs(self, path, text, spot):
        for pattern, file_group, name_group, own_file_is_enough in PAIRS:
            for m in pattern.finditer(text):
                named_file, name, call = m.group(file_group), m.group(name_group), m.group(name_group + 1)
                bare_name = name.split("::")[-1].lstrip("~")
                if not (call or LOOKS_LIKE_CODE.search(name)):
                    continue
                if name in SKIP or bare_name in SKIP or named_file in SKIP:
                    continue
                places = self.places(named_file)
                if not places:
                    continue
                first, last = spot(m)
                if not self.touched(path, first, last):
                    continue
                self.read["names paired with a file"] += 1
                word = word_of(bare_name)
                if own_file_is_enough and word.search(self.tree.code_of(path)):
                    continue
                if not any(word.search(self.tree.code_of(f)) for f in places):
                    self.report("names paired with a file", path, first,
                                f"names {name} with {named_file}, which has no {bare_name}")

    def places(self, named_file):
        """The files a name a comment pairs with could be in: that file, or
        every file of that name, each with the .h or .cpp beside it."""
        found = [f for f in self.tree.by_name.get(posixpath.basename(named_file), ())
                 if f == named_file or f.endswith("/" + named_file)]
        for f in list(found):
            stem, ext = posixpath.splitext(f)
            if ext in (".h", ".cpp"):
                other = stem + (".cpp" if ext == ".h" else ".h")
                if other in self.tree.files:
                    found.append(other)
        return found

    def flags(self, path, text, spot, names):
        ours = set()
        for m in FLAG_OWNER.finditer(text):
            command = FLAG_END_OF_COMMAND.split(text[m.end():], maxsplit=1)[0]
            ours.update(FLAG.findall(command))
        for m in FLAG_BESIDE.finditer(text):
            one, other = m.groups()
            if one in names.flags:
                ours.add(other)
            if other in names.flags:
                ours.add(one)
        for m in FLAG.finditer(text):
            flag = m.group()
            if not (flag in ours or flag.endswith(("-test", "-probe"))):
                continue
            first, last = spot(m)
            if not self.touched(path, first, last):
                continue
            self.read["frontend flags"] += 1
            if flag not in names.flags and flag not in SKIP:
                self.report("frontend flags", path, first, f"names {flag}, which the frontend does not take")

    def declarations(self, names):
        words_removed = set()
        for path, lines in (self.removed or {}).items():
            if path.endswith(C_FILES):
                for line in lines:
                    words_removed.update(re.findall(r"\b\w+\b", line))
        for path in sorted(self.tree.files):
            if not path.startswith(HEADERS) or path.startswith(NOT_READ) or not path.endswith(".h"):
                continue
            for first, last, key, short, name in scan_scopes(self.tree.split(path)[2])[1]:
                if self.added is not None and not (self.touched(path, first, last) or name in words_removed):
                    continue
                self.read["declarations"] += 1
                if key not in names.definitions and short not in names.definitions and key not in SKIP:
                    self.report("declarations", path, first, f"declares {key}, which nothing defines")

    def addresses(self):
        for path, text in sorted(self.tree.files.items()):
            if text is None or path in NO_ADDRESSES:
                continue
            if self.added is not None and path not in self.added:
                continue
            for line, content in enumerate(text.split("\n"), 1):
                for m in PRIVATE.finditer(content):
                    if not self.touched(path, line, line):
                        continue
                    self.read["private addresses"] += 1
                    if m.group() not in ADDRESSES:
                        self.report("private addresses", path, line,
                                    "a private address in a tracked file: use a documented example "
                                    "(ADDRESSES in this script)")


def scan_scopes(bare):
    """Walk the braces of code with comments and strings blanked: (the functions
    it defines, as every form of their name; the functions it declares at
    namespace or class scope, as (first line, last line, full name, name with
    its last scope, name))."""
    definitions, declarations = set(), []
    stack = []  # (is it a body, how many names it pushed) for each open brace
    scope = []
    start = 0
    for m in SCOPE_TOKEN.finditer(bare):
        token = m.group()
        if token == "{":
            head = bare[start:m.start()]
            named = SCOPE_HEAD.search(head)
            if named:
                parts = [p for p in (named.group(1) or "").split("::") if p]
                scope.extend(parts)
                stack.append((False, len(parts)))
            elif PLAIN_SCOPE_HEAD.search(head):
                stack.append((False, 0))
            else:
                stack.append((True, 0))
            start = m.end()
        elif token == "}":
            if stack:
                _, pushed = stack.pop()
                if pushed:
                    del scope[-pushed:]
            start = m.end()
        elif token == ";":
            if not any(body for body, _ in stack):
                declaration = declared(bare, start, m.start(), scope)
                if declaration:
                    declarations.append(declaration)
            start = m.end()
        elif not any(body for body, _ in stack):
            name = m.group(1).split("::")
            if name[-1] in NOT_A_FUNCTION:
                continue
            close = matching_paren(bare, m.end() - 1)
            if close < 0:
                continue
            after = bare[close + 1:close + 200]
            if AFTER_PARAMETERS.match(after) or NO_BODY_ON_PURPOSE.match(after):
                full = scope + name[:-1]
                definitions.add("::".join(full + name[-1:]))
                for i in range(len(full)):
                    definitions.add("::".join(full[i:] + name[-1:]))
                if not full:
                    definitions.add(name[-1])
    return definitions, declarations


def matching_paren(text, at):
    depth = 0
    for i in range(at, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i
    return -1


def declared(bare, start, end, scope):
    statement = bare[start:end]
    flat = " ".join(statement.split())
    if "(" not in flat or NOT_A_DECLARATION.match(flat):
        return None
    if re.search(r"=\s*(?:default|delete|0)\s*$", flat):
        return None
    m = DECLARATION.match(flat)
    if not m or m.group(1) in NOT_A_FUNCTION:
        return None
    name = m.group(1)
    first = bare.count("\n", 0, start + len(statement) - len(statement.lstrip())) + 1
    last = bare.count("\n", 0, end) + 1
    key = "::".join(scope + [name])
    return first, last, key, "::".join(scope[-1:] + [name]), name


class Names:
    """What our code defines: the frontend's flags, each namespace, struct and
    enum with the files that define it, the qualified names it uses and the
    functions it defines."""

    def __init__(self, tree):
        self.flags = set()
        for path in ("frontend/src/cli.cpp", "frontend/src/main.cpp"):
            self.flags.update(FLAG_IN_CODE.findall(tree.code_of(path)))
        self.scopes = collections.defaultdict(set)
        self.uses = set()
        self.definitions = set()
        for path in sorted(tree.files):
            if not path.startswith(OUR_CODE) or path.startswith(NOT_READ) or not path.endswith(C_FILES):
                continue
            split = tree.split(path)
            if not split:
                continue
            _, code, bare = split
            for m in SCOPE_NAME.finditer(bare):
                for part in m.group(1).split("::"):
                    self.scopes[part].add(path)
            self.uses.update(q.replace("~", "") for q in CODE_QUALIFIED.findall(code))
            self.definitions.update(scan_scopes(bare)[0])


def gone_files(revision, tree):
    """The name of each file our history once had and the tree does not."""
    gone = git("log", "--diff-filter=D", "--no-renames", "--name-only", "--format=", revision)
    named = {posixpath.basename(f) for f in gone.split("\n") if f.endswith(tuple(C_FILES) + (".sh", ".py"))}
    return {n for n in named if n not in tree.by_name}


def cabinet_files():
    """ci/check-doc-paths.py's list of the files in Cabinet's repository, which
    shares this one's folder names."""
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("check_doc_paths", os.path.join(here, "check-doc-paths.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.CABINET


CABINET = cabinet_files()


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] != "--whole-tree":
        os.chdir(git("rev-parse", "--show-toplevel").strip())
        revision = args[1]
        added, removed = read_diff(git("merge-base", args[0], revision).strip(), revision)
        what = "what this pull request touches"
    elif args[:1] == ["--whole-tree"] and len(args) <= 2:
        os.chdir(git("rev-parse", "--show-toplevel").strip())
        revision = args[1] if len(args) == 2 else "HEAD"
        added, removed = None, None
        what = f"the whole tree at {revision}"
    else:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2

    tree = Tree(read_tree(revision))
    names = Names(tree)
    check = Check(tree, added, removed)
    check.comments(names, gone_files(revision, tree))
    check.declarations(names)
    check.addresses()

    until = os.environ.get("REPORT_ONLY_UNTIL", "")
    today = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")
    level = "warning" if until and today <= until else "error"
    on_github = bool(os.environ.get("GITHUB_ACTIONS"))
    for kind, path, line, message in check.found:
        print(f"{path}:{line}: {message}")
        if on_github:
            print(f"::{level} file={path},line={line}::{message}. See ci/check-code-and-docs-agree.py.")

    print(f"Read {what}:")
    found = collections.Counter(kind for kind, *_ in check.found)
    for kind in ("files", "qualified names", "names paired with a file", "frontend flags",
                 "declarations", "private addresses"):
        print(f"  {kind}: {check.read[kind]} read, {found[kind]} found")
    if not check.found:
        return 0
    print(f"{len(check.found)} finding(s).")
    if level == "warning":
        print(f"Report only until {until} (UTC): passing. From the next day this fails.")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
