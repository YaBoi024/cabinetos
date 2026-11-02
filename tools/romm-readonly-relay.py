#!/usr/bin/env python3
# Test relay for #310: 127.0.0.1:<port> -> RomM, READS ONLY.
#   * GET and HEAD are forwarded; anything else gets 403 and is logged, so a
#     test can never write to the server (saves, play sessions, last played).
#   * A platform can be shown under another folder name and display name:
#       romm-readonly-relay.py 16006 45=arcade:Arcade 22=FB%20Neo
#     rewrites fs_slug (and display_name, when given) in /api/platforms and
#     platform_fs_slug / platform_display_name in every ROM payload.
#   * A game's files can be shown under other names, or not at all (#162,
#     the disc rig): file:<fileId>=<name> renames one in every `files` list,
#     hide:<fileId> leaves it out. RomM still serves a file by its id.
#   * folder:<romId> shows a one-file game as a folder game with an update
#     beside it (has_multiple_files, and a made-up `update` file that RomM
#     would refuse to serve), for the folder download fix (#181's notes).
#   * nested:<romId> shows a one-file game as RomM 5 shows a game alone in
#     its own folder (#416): has_nested_single_file, and fs_name the
#     folder's name, with no file ending. media:<romId> adds a made-up
#     manual beside it (#400), as RomM's own upload makes: a request with no
#     file id then gets a zip of the game and the manual, as RomM serves a
#     folder, and the manual's own id gets 404 (the console never needs it).
#   * `people` adds four made-up people with public collections (#352, the
#     Collections tab's "From <name>" groups), so a household of five can be
#     tried with two real accounts. Each one opens onto a real platform's
#     games; Sam's public Favorites must never show.
#   * `formats` serves every cover in another of the formats RomM accepts
#     (#341): by game id, WebP, AVIF, GIF (two frames, as an animated one),
#     BMP, TIFF, or the PNG as it came. The name stays small.png, as RomM's
#     own WebP small covers do. Each conversion is logged with its game id.
#     Needs Pillow, and ffmpeg for AVIF.
#   * `nosmall` answers 404 for the small cover of every third game (by id),
#     as a server missing it would; the large one is still served (#376).
#   * `junk` serves the small cover of every fifth game as bytes that are no
#     picture at all, for the console's "could not read" line (#376).
#   * name:<romId>=<name> gives a game another name, and
#     tags:<romId>=<regions>;<languages>;<revision>;<tags> (each a comma list,
#     any part empty) gives it RomM's four fields, for the same-name heading
#     (#383): twins on a Home shelf and long labels, without renaming files.
#   * nosize:<romId> gives a game no size (fs_size_bytes 0), as a server
#     that has none for it would (#484, LAUNCH-11: the room check before a
#     one-file download then waits for the download's own length).
#   * hold:<what>=<seconds> answers every /api/<what> request that late
#     (hold:saves=20 holds a game's save rows and bytes), as a slow or hung
#     server would, for what the console does meanwhile (#481, LAUNCH-17: a
#     launch must not hold the picture on them).
#   * status:<path>=<code> answers a GET of exactly that path (its query
#     aside) with that HTTP status, as a failing or refusing server would:
#     status:/api/roms/identifiers=503, status:/api/roms/705=500 (#625: only
#     RomM's own 404 for a game's entry removes the console's copy).
import json, os, sys, time, urllib.parse, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# The RomM server it reads from, from the environment, so no address is
# written here: CABINETOS_RELAY_UP=http://<host>:<port> (offline-relay.py
# takes the same).
UP = os.environ.get("CABINETOS_RELAY_UP") or sys.exit(
    "CABINETOS_RELAY_UP: set it to http://<your RomM server>:<port>")
UP = (UP if "://" in UP else "http://" + UP).rstrip("/")
PORT = int(sys.argv[1])
MAP = {}
# file:<fileId>=<name> shows a game's file under another name (#162, odd names).
FILES = {}
PEOPLE = False
FORMATS = False   # `formats`
NOSMALL = False   # `nosmall`
JUNK = False      # `junk`
FOLDERS = set()   # folder:<romId>
NESTED = set()    # nested:<romId>
MEDIA = set()     # media:<romId>, also in NESTED
NAMES = {}        # name:<romId>=<name>
TAGS = {}         # tags:<romId>=<regions>;<languages>;<revision>;<tags>
HOLD = {}         # hold:<what>=<seconds>: "/api/<what>" -> seconds
NOSIZE = set()    # nosize:<romId>
STATUS = {}       # status:<path>=<code>: "/api/..." -> HTTP status
# (owner id, owner name, collection id, name, smart, favourite, platform id)
MOCK = [
    (9101, "Alex", 9001, "Saturday co-op", False, False, 8),
    (9101, "Alex", 9002, "Hard mode", True, False, 10),
    (9101, "Alex", 9003, "Handhelds", False, False, 2),
    (9102, "Jordan", 9004, "Puzzle corner", True, False, 1),
    (9103, "Riley", 9005, "Arcade nights", False, False, 22),
    (9103, "Riley", 9006, "Sega only", True, False, 11),
    (9103, "Riley", 9007, "Dreamcast", True, False, 14),
    (9103, "Riley", 9008, "PlayStation", False, False, 9),
    (9103, "Riley", 9009, "Master System", False, False, 30),
    (9104, "Sam", 9010, "Favorites", False, True, 1),
    (9104, "Sam", 9011, "Game Boy Color", False, False, 3),
]
for a in sys.argv[2:]:
    if a == "people":
        PEOPLE = True
        continue
    if a == "formats":
        FORMATS = True
        continue
    if a in ("nosmall", "junk"):
        globals()[a.upper()] = True
        continue
    if a.startswith("folder:"):
        FOLDERS.add(int(a[7:]))
        continue
    if a.startswith(("nested:", "media:")):
        kind, rid = a.split(":", 1)
        NESTED.add(int(rid))
        if kind == "media": MEDIA.add(int(rid))
        continue
    if a.startswith("name:"):
        rid, name = a[5:].split("=", 1)
        NAMES[int(rid)] = urllib.parse.unquote(name)
        continue
    if a.startswith("tags:"):
        rid, val = a[5:].split("=", 1)
        parts = (urllib.parse.unquote(val).split(";") + ["", "", "", ""])[:4]
        lst = lambda x: [t.strip() for t in x.split(",") if t.strip()]
        TAGS[int(rid)] = {"regions": lst(parts[0]), "languages": lst(parts[1]),
                          "revision": parts[2].strip(), "tags": lst(parts[3])}
        continue
    if a.startswith("nosize:"):
        NOSIZE.add(int(a[7:]))
        continue
    if a.startswith("hold:"):
        what, secs = a[5:].split("=", 1)
        HOLD["/api/" + what] = float(secs)
        continue
    if a.startswith("status:"):
        path, code = a[7:].rsplit("=", 1)
        STATUS[path] = int(code)
        continue
    if a.startswith("hide:"):
        FILES[int(a[5:])] = None
        continue
    if a.startswith("file:"):
        fid, name = a[5:].split("=", 1)
        FILES[int(fid)] = urllib.parse.unquote(name)
        continue
    pid, val = a.split("=", 1)
    fs, _, name = urllib.parse.unquote(val).partition(":")
    MAP[int(pid)] = (fs, name)

def rewrite(obj):
    if isinstance(obj, list):
        return [rewrite(x) for x in obj
                if not (isinstance(x, dict) and "file_size_bytes" in x and x.get("id") in FILES
                        and FILES[x["id"]] is None)]
    if isinstance(obj, dict):
        obj = {k: rewrite(v) for k, v in obj.items()}
        if "fs_name" in obj and "platform_id" in obj:
            if obj.get("id") in NAMES:
                obj["name"] = NAMES[obj["id"]]
            if obj.get("id") in TAGS:
                obj.update(TAGS[obj["id"]])
            if obj.get("id") in NOSIZE:
                obj["fs_size_bytes"] = 0
        if obj.get("id") in FOLDERS and "fs_name" in obj and "platform_id" in obj:
            obj["has_multiple_files"] = True
            obj["has_nested_single_file"] = False
            if isinstance(obj.get("files"), list) and obj["files"]:
                fake = dict(obj["files"][0])
                fake.update(id=990000 + obj["id"], file_name="Made-up update v9.nsp",
                            file_size_bytes=1234, category="update", is_top_level=False)
                obj["files"] = obj["files"] + [fake]
        if obj.get("id") in NESTED and "fs_name" in obj and "platform_id" in obj:
            obj.update(has_multiple_files=False, has_simple_single_file=False,
                       has_nested_single_file=True,
                       fs_name=obj.get("fs_name_no_ext") or obj["fs_name"].rsplit(".", 1)[0])
            if obj["id"] in MEDIA and isinstance(obj.get("files"), list) and obj["files"]:
                fake = dict(obj["files"][0])
                fake.update(id=MANUAL_ID + obj["id"], file_name=obj["fs_name"] + ".pdf",
                            file_size_bytes=len(MANUAL), category="manual", is_top_level=False)
                obj["files"] = obj["files"] + [fake]
        if "file_name" in obj and obj.get("id") in FILES and "file_size_bytes" in obj:
            obj["file_name"] = FILES[obj["id"]]
        if "fs_slug" in obj and obj.get("id") in MAP and "rom_count" in obj:
            fs, name = MAP[obj["id"]]
            obj["fs_slug"] = fs
            if name: obj["display_name"] = name; obj["custom_name"] = name
        if obj.get("platform_id") in MAP and "platform_fs_slug" in obj:
            fs, name = MAP[obj["platform_id"]]
            obj["platform_fs_slug"] = fs
            if name: obj["platform_display_name"] = name; obj["platform_custom_name"] = name
        return obj
    return obj

# The made-up manual (media:<romId>): larger than most cartridge games, as a
# real one is, so a console that takes the biggest file takes this.
MANUAL_ID = 980000
MANUAL = b"%PDF-1.4\n% made up by the relay (#400)\n" + b"\0" * (2 * 1024 * 1024)

def folder_zip(rid, hdrs):
    """RomM's zip of a media:<romId> game's folder: the game file and the
    manual in manual/, for a request with no file id."""
    import io, zipfile
    hdrs = {k: v for k, v in hdrs.items() if k.lower() != "range"}
    get = lambda path: urllib.request.urlopen(
        urllib.request.Request(UP + path, headers=hdrs), timeout=600).read()
    f = json.loads(get("/api/roms/%d" % rid))["files"][0]
    game = get("/api/roms/%d/content/%s?file_ids=%d"
               % (rid, urllib.parse.quote(f["file_name"]), f["id"]))
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:
        z.writestr(f["file_name"], game)
        z.writestr("manual/" + f["file_name"].rsplit(".", 1)[0] + ".pdf", MANUAL)
    sys.stderr.write(f"media: game {rid} served as a zip of its folder ({out.tell()} bytes)\n")
    return out.getvalue()

FIRST = {}   # platform id -> (rom ids, first cover), asked once with the caller's token

def platform_sample(pid, hdrs):
    if pid not in FIRST:
        req = urllib.request.Request(UP + "/api/roms?platform_ids=%d&limit=500&with_filter_values=false"
                                     "&with_rom_id_index=false&with_char_index=false" % pid, headers=hdrs)
        items = json.loads(urllib.request.urlopen(req, timeout=60).read()).get("items", [])
        FIRST[pid] = ([r["id"] for r in items],
                      next((r.get("path_cover_small") for r in items if r.get("path_cover_small")), None))
    return FIRST[pid]

def mock_collections(smart, hdrs):
    out = []
    for uid, uname, cid, name, is_smart, fav, pid in MOCK:
        if is_smart != smart:
            continue
        ids, cover = platform_sample(pid, hdrs)
        c = {"id": cid, "name": name, "description": "made up by the relay (#352)",
             "user_id": uid, "owner_username": uname, "is_public": True, "is_favorite": fav,
             "is_smart": smart, "is_virtual": False, "rom_ids": ids, "rom_count": len(ids),
             "path_cover_small": None, "path_covers_small": [cover] if cover else [],
             "path_cover_large": None, "path_covers_large": [],
             "created_at": "2026-10-12T00:00:00+00:00", "updated_at": "2026-10-12T00:00:00+00:00"}
        if smart:
            c["filter_criteria"] = {"platform_ids": [pid]}
            c["filter_summary"] = ""
        out.append(c)
    return out

FORMAT_CYCLE = ["webp", "avif", "gif", "bmp", "tiff", "png"]

def convert_cover(path, body):
    """A cover in the format its game id picks (`formats`), or as it came."""
    import io, re, subprocess, tempfile
    m = re.search(r"/roms/\d+/(\d+)/cover/", path)
    if not m or not body: return body
    fmt = FORMAT_CYCLE[int(m.group(1)) % len(FORMAT_CYCLE)]
    if fmt == "png": return body
    from PIL import Image
    img = Image.open(io.BytesIO(body)).convert("RGB")
    out = io.BytesIO()
    if fmt == "avif":
        with tempfile.TemporaryDirectory() as d:
            img.save(d + "/in.png")
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", d + "/in.png",
                            "-c:v", "libaom-av1", "-still-picture", "1", d + "/out.avif"], check=True)
            data = open(d + "/out.avif", "rb").read()
    elif fmt == "gif":
        flipped = img.transpose(Image.FLIP_LEFT_RIGHT)
        img.save(out, "GIF", save_all=True, append_images=[flipped], duration=500, loop=0)
        data = out.getvalue()
    else:
        img.save(out, {"webp": "WEBP", "bmp": "BMP", "tiff": "TIFF"}[fmt])
        data = out.getvalue()
    sys.stderr.write(f"formats: game {m.group(1)} cover as {fmt} ({len(data)} bytes) {path}\n")
    return data

def mock_path(path):
    # A made-up collection opens onto its platform.
    u = urllib.parse.urlsplit(path)
    if u.path != "/api/roms":
        return path
    q = urllib.parse.parse_qsl(u.query, keep_blank_values=True)
    for i, (k, v) in enumerate(q):
        if k in ("collection_id", "smart_collection_id") and v.isdigit():
            for m in MOCK:
                if m[2] == int(v):
                    q[i] = ("platform_ids", str(m[6]))
    return urllib.parse.urlunsplit(u._replace(query=urllib.parse.urlencode(q)))

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def refuse(self):
        sys.stderr.write(f"REFUSED {self.command} {self.path}\n"); sys.stderr.flush()
        n = int(self.headers.get("Content-Length") or 0)
        if n: self.rfile.read(n)
        self.send_response(403); self.send_header("Content-Length", "0"); self.end_headers()
    do_POST = do_PUT = do_PATCH = do_DELETE = refuse
    def do_HEAD(self): self.fwd(head=True)
    def do_GET(self): self.fwd()
    def fwd(self, head=False):
        hdrs = {k: v for k, v in self.headers.items() if k.lower() not in ("host", "connection", "accept-encoding")}
        import re as _re
        for prefix, secs in HOLD.items():
            if self.path.startswith(prefix):
                sys.stderr.write(f"hold: {self.command} {self.path} held {secs:g} s\n"); sys.stderr.flush()
                time.sleep(secs)
        code = STATUS.get(urllib.parse.urlsplit(self.path).path)
        if code:
            body = json.dumps({"detail": f"answered {code} by the relay"}).encode()
            sys.stderr.write(f"status: {self.command} {self.path} answered {code}\n"); sys.stderr.flush()
            self.send_response(code); self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body))); self.end_headers()
            if not head: self.wfile.write(body)
            return
        small = _re.search(r"/roms/\d+/(\d+)/cover/small\.", self.path)
        if small and NOSMALL and int(small.group(1)) % 3 == 0:
            sys.stderr.write(f"nosmall: game {small.group(1)} small cover answered 404\n")
            self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers()
            return
        if small and JUNK and int(small.group(1)) % 5 == 0:
            body = b"this is not a picture " * 3
            sys.stderr.write(f"junk: game {small.group(1)} small cover sent as junk\n")
            self.send_response(200); self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(body))); self.end_headers()
            if not head: self.wfile.write(body)
            return
        content = _re.match(r"/api/roms/(\d+)/content/[^?]*(?:\?(.*))?$", self.path)
        if content and int(content.group(1)) in MEDIA:
            rid = int(content.group(1))
            ids = dict(urllib.parse.parse_qsl(content.group(2) or "")).get("file_ids", "")
            if ids == str(MANUAL_ID + rid):
                sys.stderr.write(f"media: game {rid}'s made-up manual asked for; 404\n")
                self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers()
                return
            if not ids:
                body = folder_zip(rid, hdrs)
                self.send_response(200); self.send_header("Content-Type", "application/zip")
                self.send_header("Content-Length", str(len(body))); self.end_headers()
                if not head: self.wfile.write(body)
                return
        path = mock_path(self.path) if PEOPLE else self.path
        req = urllib.request.Request(UP + path, headers=hdrs, method=self.command)
        try:
            r = urllib.request.urlopen(req, timeout=120)
        except urllib.error.HTTPError as e:
            r = e
        body = r.read() if not head else b""
        ctype = r.headers.get("Content-Type", "")
        if (MAP or FILES or FOLDERS or NESTED or NAMES or TAGS or NOSIZE) and "json" in ctype and body:
            try: body = json.dumps(rewrite(json.loads(body))).encode()
            except Exception: pass
        listing = urllib.parse.urlsplit(self.path).path
        if FORMATS and "/cover/" in listing and body:
            try: body = convert_cover(listing, body)
            except Exception as e: sys.stderr.write(f"formats: {e}\n")
        if PEOPLE and "json" in ctype and body and listing in ("/api/collections", "/api/collections/smart"):
            try:
                body = json.dumps(json.loads(body) + mock_collections(listing.endswith("smart"), hdrs)).encode()
            except Exception as e:
                sys.stderr.write(f"people: {e}\n")
        self.send_response(r.status if hasattr(r, "status") else r.code)
        for k, v in r.headers.items():
            if k.lower() in ("content-length", "transfer-encoding", "connection", "content-encoding"): continue
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if not head: self.wfile.write(body)

ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
