#!/usr/bin/env python3
"""Does the console's RomM client still match RomM's published API? (#729)

  ci/check-romm-api.py                              the repository: the client's uses against the
                                                    copy of RomM's description, and against the code
  ci/check-romm-api.py --description <openapi.json> the client's uses against another description,
                                                    a whole one (romm-api.yml, RomM's newest release)
  ci/check-romm-api.py --trim <openapi.json> <out> <release> <image>
                                                    the copy: only the calls the uses list names, and
                                                    what they refer to, with where it came from

RomM publishes a description of its API (its OpenAPI document): every call, what it takes and what it
answers. Three files stand beside this script, which holds them to one another:

  ci/romm-api-uses.txt   what the console's client sends to RomM and reads from its answers, one block
                         a call, with the kind of each field. Its header says the form.
  ci/romm-api.json       the parts of RomM's own description those calls need, from the release the
                         console is tested against, made by ci/fetch-romm-api.sh. Never edited by hand.
  frontend/src           the client itself.

WHAT IT CHECKS. For each call in the list: RomM has it; each setting the client sends in the address is
declared; each field it sends is declared, and none RomM requires is left out; and each field it reads
from a good answer is declared there, with the kind the client reads it as. It checks shapes, not
behaviour. A name RomM's description does not spell out stands under `undeclared`, with the reason,
and a name that is the console's own is in OWN below.

AND THE LIST AGAINST THE CODE, so the list cannot fall behind: every RomM path, every setting in an
address and every field name that the client's files spell in a JSON read or write is in the list. It
reads the patterns the client uses (jstr, jint, jobj, jbool, jstrs, json_object_object_get_ex,
json_object_object_add, addStr, a setting in a string's address). A field spelled another way is not
seen, and a name counts wherever it stands in the list: that it is read from the right answer is the
list's word, held to RomM's description call by call.

AND THE COPY'S RELEASE: it must be a release of the RomM the console needs (ROMM_NEEDED in
cabinetos.env), so moving that version stops here until the copy is taken again.

Prints each finding as file:line and exits 1 if there is any. REPORT ONLY UNTIL A DATE, as checks.yml's
checks: while today (UTC) is not past REPORT_ONLY_UNTIL (YYYY-MM-DD, set by the workflow) the findings
are warnings and it exits 0.
"""

import json
import os
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
USES = "ci/romm-api-uses.txt"
COPY = "ci/romm-api.json"
SRC = ROOT / "frontend/src"

# The client's files, and the ones outside it that spell part of an address
# or a setting (ci/check-romm-paths.sh holds the list of those paths).
CLIENT = ["romm.h", "romm_internal.h", "romm_http.cpp", "romm_library.cpp", "romm_pairing.cpp",
          "romm_playdata.cpp", "romm_snapshots.cpp", "romm_codes.cpp"]
ADDRESSES_ONLY = ["saves.cpp", "library.cpp", "search.cpp", "gridfill.cpp", "wiimii.cpp", "launch_routes.cpp",
                  "probes.cpp", "startup.cpp"]

# Names the client reads or writes that are not RomM's.
OWN = {
    "product_code": "the console's own record of a game (romm_library.cpp, gameFromJson)",
    "file": "the console's own record of an archive's members",
    "members": "the console's own record of an archive's members",
    "platform_name": "the console's own record of a game, read back by parseGame",
}

KINDS = {"text": {"string"}, "number": {"integer", "number"}, "yesno": {"boolean"},
         "list": {"array"}, "object": {"object"}, "any": None}


class Use:
    """One line of the list that names a field."""
    def __init__(self, path, kind, line):
        self.path, self.kind, self.line = path, kind, line


class Op:
    def __init__(self, method, path, line):
        self.method, self.path, self.line = method, path, line
        self.query, self.sends, self.reads, self.undeclared = {}, [], [], {}


def read_uses(path):
    """The list, as {(method, path): Op}, and what is wrong with the file itself."""
    ops, op, bad = {}, None, []
    for n, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].rstrip() if raw.lstrip().startswith("#") else raw.rstrip()
        if not line.strip():
            continue
        if not line[0].isspace():
            m = re.match(r"(GET|POST|PUT|PATCH|DELETE) (/\S+)$", line)
            if not m:
                bad.append((n, "not a call: METHOD /path"))
                continue
            op = Op(m.group(1), m.group(2), n)
            if (op.method, op.path) in ops:
                bad.append((n, "named twice: %s %s" % (op.method, op.path)))
            ops[op.method, op.path] = op
            continue
        words = line.split()
        if op is None:
            bad.append((n, "a line under no call"))
        elif words[0] == "query":
            for w in words[1:]:
                op.query[w] = n
        elif words[0] in ("send", "read") and len(words) == 3 and words[2] in KINDS:
            (op.sends if words[0] == "send" else op.reads).append(Use(words[1], words[2], n))
        elif words[0] == "undeclared" and len(words) > 2 and words[1].endswith(":"):
            op.undeclared[words[1][:-1]] = n
        else:
            bad.append((n, "not understood: %s" % line.strip()))
    return ops, bad


class Description:
    def __init__(self, doc):
        self.doc = doc
        self.schemas = doc.get("components", {}).get("schemas", {})
        self.calls = {}
        for path, ops in doc.get("paths", {}).items():
            for method in ops:
                self.calls[method.upper(), re.sub(r"\{[^}]+\}", "{}", path)] = ops[method]

    def call(self, op):
        return self.calls.get((op.method, re.sub(r"\{[^}]+\}", "{}", op.path)))

    def expand(self, schema):
        """The schemas a schema stands for: refs followed, anyOf and allOf flattened, null dropped."""
        if not isinstance(schema, dict):
            return []
        if "$ref" in schema:
            return self.expand(self.schemas.get(schema["$ref"].split("/")[-1], {}))
        out = []
        for key in ("anyOf", "oneOf", "allOf"):
            if key in schema:
                for part in schema[key]:
                    out += self.expand(part)
        if out:
            # An allOf's parts and the object's own properties belong together.
            return [schema] + out if "properties" in schema else out
        if schema.get("type") == "null":
            return []
        return [schema]

    def step(self, schemas, seg):
        found = []
        for s in schemas:
            if seg == "[]":
                found += self.expand(s.get("items", {})) if s.get("type") == "array" else []
            elif seg == "{}":
                ap = s.get("additionalProperties")
                if isinstance(ap, dict):
                    found += self.expand(ap)
            else:
                prop = s.get("properties", {}).get(seg)
                if prop is not None:
                    found += self.expand(prop)
        return found

    def follow(self, schema, path):
        schemas = self.expand(schema)
        for seg in split(path):
            schemas = self.step(schemas, seg)
            if not schemas:
                return []
        return schemas

    def kinds(self, schemas):
        out = set()
        for s in schemas:
            t = s.get("type")
            if isinstance(t, list):
                out |= set(t) - {"null"}
            elif t:
                out.add(t)
            elif "properties" in s or "additionalProperties" in s:
                out.add("object")
            elif "const" in s:
                out.add("string" if isinstance(s["const"], str) else "boolean"
                        if isinstance(s["const"], bool) else "integer")
            elif "enum" in s:
                out |= {"string" if isinstance(v, str) else "integer" for v in s["enum"]}
        return out

    def good_answer(self, call):
        for status in sorted(call.get("responses", {})):
            if status.startswith("2"):
                for ctype, c in call["responses"][status].get("content", {}).items():
                    if "json" in ctype:
                        return c.get("schema", {})
        return None

    def body(self, call):
        content = call.get("requestBody", {}).get("content", {})
        for ctype in ("application/json", "multipart/form-data", "application/x-www-form-urlencoded"):
            if ctype in content:
                return content[ctype].get("schema", {})
        for c in content.values():
            return c.get("schema", {})
        return None


def split(path):
    """a.b, a[].b, a.{}.b, [].b as segments."""
    out = []
    for piece in re.split(r"\.(?![^\[]*\])", path.replace("[]", ".[]").lstrip(".")):
        if piece:
            out.append(piece)
    return out


def check_uses(ops, desc, uses_file):
    """The list against one description."""
    out = []

    def say(line, text):
        out.append((uses_file, line, text))

    for op in ops.values():
        name = "%s %s" % (op.method, op.path)
        call = desc.call(op)
        if call is None:
            say(op.line, "RomM has no call %s" % name)
            continue
        declared = {p["name"] for p in call.get("parameters", []) if p.get("in") == "query"}
        for q, n in op.query.items():
            if q not in declared:
                say(n, "%s: RomM declares no setting \"%s\"" % (name, q))
        body = desc.body(call)
        if op.sends and body is None:
            say(op.sends[0].line, "%s: the client sends a body, and RomM declares none" % name)
        elif body is not None:
            for u in op.sends:
                found = desc.follow(body, u.path)
                if not found:
                    say(u.line, "%s: RomM declares no field \"%s\" to send" % (name, u.path))
                elif KINDS[u.kind] and not (desc.kinds(found) & KINDS[u.kind]):
                    say(u.line, "%s: \"%s\" is sent as %s, RomM takes %s" % (
                        name, u.path, u.kind, "/".join(sorted(desc.kinds(found))) or "something else"))
            sent = {tuple(split(u.path)) for u in op.sends}
            top = {p[0] for p in sent}
            for s in desc.expand(body):
                for req in s.get("required", []):
                    if req not in top:
                        say(op.line, "%s: RomM requires \"%s\" and the client does not send it" % (name, req))
            # What a list entry or an object requires, one level down.
            for u in op.sends:
                segs = split(u.path)
                if len(segs) < 2:
                    continue
                for s in desc.follow(body, ".".join(segs[:-1]) if len(segs) > 1 else ""):
                    for req in s.get("required", []):
                        if not any(p[:len(segs) - 1] == tuple(segs[:-1]) and p[-1] == req for p in sent):
                            say(op.line, "%s: RomM requires \"%s\" in \"%s\"" % (
                                name, req, ".".join(segs[:-1])))
        good = desc.good_answer(call)
        if op.reads and good is None:
            say(op.reads[0].line, "%s: the client reads a JSON answer, and RomM declares none" % name)
        elif good is not None:
            for u in op.reads:
                found = desc.follow(good, u.path)
                if not found:
                    say(u.line, "%s: RomM's answer has no \"%s\"" % (name, u.path))
                elif KINDS[u.kind] and not (desc.kinds(found) & KINDS[u.kind]):
                    say(u.line, "%s: \"%s\" is read as %s, RomM answers %s" % (
                        name, u.path, u.kind, "/".join(sorted(desc.kinds(found))) or "something else"))
    return out


def release_of(doc):
    return (doc.get("x-cabinetos") or {}).get("release") or doc.get("info", {}).get("version", "")


def code_names(ops):
    """Every name the list has, for the code's half."""
    names = set(OWN)
    for op in ops.values():
        names |= set(op.query) | set(op.undeclared)
        for u in op.sends + op.reads:
            names |= {s for s in split(u.path) if s not in ("[]", "{}")}
    return names


# Inside a lookahead, so a call nested in another's arguments is seen too.
NAME_CALLS = re.compile(
    r"(?=\b(?:jstr|jint|jobj|jbool|jstrs|json_object_object_get_ex|json_object_object_add|addStr)"
    r"\s*\(\s*[^,()]*(?:\([^()]*\)[^,()]*)*,\s*\"([A-Za-z_][A-Za-z0-9_]*)\")")
SETTING = re.compile(r"[?&]([a-z_]+)=")
SETTING_START = re.compile(r"\"((?:platform_ids|collection_id|smart_collection_id|search_term|core)=)")
PATH_START = re.compile(r"\"(/api/[A-Za-z0-9/_-]*)")


def strip_comments(line):
    if line.lstrip().startswith(("//", "*", "/*")):
        return ""
    cut = re.search(r"\s//", line)
    return line[:cut.start()] if cut else line


def check_code(ops):
    names, out = code_names(ops), []
    # A path the code may spell: a call's own path, or the part of one before
    # its first id (`/api/roms/`, which a game's id is added to), or "/api/"
    # alone, which fetchAsset adds a kind to.
    allowed = {"/api/"}
    for _, t in ops:
        allowed.add(t)
        for m in re.finditer(r"\{", t):
            allowed.add(t[:m.start()])
    for f in CLIENT + ADDRESSES_ONLY:
        p = SRC / f
        if not p.exists():
            out.append((str(p.relative_to(ROOT)), 0, "the check names a file that is not there"))
            continue
        rel = str(p.relative_to(ROOT))
        for n, raw in enumerate(p.read_text().splitlines(), 1):
            line = strip_comments(raw)
            for m in PATH_START.finditer(line):
                lit = m.group(1)
                if lit not in allowed:
                    out.append((rel, n, "a RomM path not in the list: %s" % lit))
            for s in SETTING.findall(line):
                if s not in names:
                    out.append((rel, n, "a setting not in the list: %s" % s))
            for m in SETTING_START.finditer(line):
                if m.group(1)[:-1] not in names:
                    out.append((rel, n, "a setting not in the list: %s" % m.group(1)[:-1]))
            if f in CLIENT:
                for m in NAME_CALLS.finditer(line):
                    if m.group(1) not in names:
                        out.append((rel, n, "a field not in the list: %s" % m.group(1)))
    return out


def trim(full, ops, release, image):
    """The copy: the calls the list names and what they refer to."""
    desc = Description(full)
    norm = lambda p: re.sub(r"\{[^}]+\}", "{}", p)
    keep_paths, refs, todo = {}, set(), []
    want = {(op.method, norm(op.path)) for op in ops.values()}
    for path, methods in full["paths"].items():
        for method, body in methods.items():
            if (method.upper(), norm(path)) in want:
                keep_paths.setdefault(path, {})[method] = body
                todo.append(body)
    while todo:
        x = todo.pop()
        if isinstance(x, dict):
            r = x.get("$ref")
            if isinstance(r, str) and r.startswith("#/components/schemas/"):
                name = r.split("/")[-1]
                if name not in refs:
                    refs.add(name)
                    todo.append(full["components"]["schemas"].get(name, {}))
            todo.extend(x.values())
        elif isinstance(x, list):
            todo.extend(x)
    return {
        "x-cabinetos": {"release": release, "image": image,
                        "made by": "ci/fetch-romm-api.sh; never edited by hand"},
        "openapi": full["openapi"], "info": {"title": full["info"].get("title", ""), "version": release},
        "paths": keep_paths,
        "components": {"schemas": {k: full["components"]["schemas"][k] for k in sorted(refs)}},
    }


def finish(findings, note=""):
    level = "warning" if report_only() else "error"
    for f, n, text in findings:
        print("%s:%d: %s" % (f, n, text))
        if os.environ.get("GITHUB_ACTIONS"):
            print("::%s file=%s,line=%d::%s" % (level, f, n, text))
    if note:
        print(note)
    if not findings:
        print("The client matches RomM's description.")
        return 0
    print("%d finding(s)." % len(findings))
    if report_only():
        print("Report only until %s (UTC): passing. From the next day this fails." %
              os.environ["REPORT_ONLY_UNTIL"])
        return 0
    return 1


def report_only():
    until = os.environ.get("REPORT_ONLY_UNTIL", "")
    return bool(until) and not (datetime.now(timezone.utc).strftime("%Y-%m-%d") > until)


def needed():
    m = re.search(r'^ROMM_NEEDED="?([0-9.]+)"?$', (ROOT / "cabinetos.env").read_text(), re.M)
    return m.group(1) if m else ""


def main(argv):
    ops, bad = read_uses(ROOT / USES)
    if argv[:1] == ["--trim"] and len(argv) == 5:
        copy = trim(json.loads(Path(argv[1]).read_text()), ops, argv[3], argv[4])
        Path(argv[2]).write_text(json.dumps(copy, indent=1, sort_keys=True) + "\n")
        print("%s: %d paths, %d schemas, %d bytes" % (
            argv[2], len(copy["paths"]), len(copy["components"]["schemas"]), Path(argv[2]).stat().st_size))
        return 0
    findings = [(USES, n, t) for n, t in bad]
    if argv[:1] == ["--description"] and len(argv) == 2:
        doc = json.loads(Path(argv[1]).read_text())
        findings += check_uses(ops, Description(doc), USES)
        return finish(findings, "RomM %s." % release_of(doc))
    if argv:
        print(__doc__)
        return 2
    doc = json.loads((ROOT / COPY).read_text())
    release = release_of(doc)
    if not (release == needed() or release.startswith(needed() + ".")):
        findings.append((COPY, 1, "the copy is of RomM %s, and cabinetos.env needs RomM %s: run "
                                  "ci/fetch-romm-api.sh" % (release or "?", needed())))
    desc = Description(doc)
    findings += check_uses(ops, desc, USES)
    findings += check_code(ops)
    return finish(findings, "RomM %s." % release)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
