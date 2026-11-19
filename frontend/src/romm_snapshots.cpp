// Save channels and snapshots on RomM 5.5 (#346): a game's channels listed
// (fetchChannels), a snapshot pushed into one (pushSnapshot), what RomM
// answers a push read (readPushAnswer), RomM's content hash, the name a
// snapshot gives each save and state (contentHash), and a game's channels
// written in RomM's own form for the copy the console keeps of them
// (channelsJson, read back by readChannels; savemirror.h). The words are in
// romm_types.h. Its own file because it is a job of its own: the old save
// and state requests stay in romm_playdata.cpp until the console stops
// making them.
//
// FOLLOWS RomM'S CODE AT 5.5.0-alpha.2, NOT A DOCUMENT. RomM's documentation
// (rommapp/docs) still describes 5.4's sync by slot, so every field and
// answer here is read from `endpoints/channels.py`, `endpoints/snapshots.py`,
// `endpoints/responses/snapshots.py` and `handler/snapshots/write.py` at that
// tag, and recorded from the write-test RomM (romm_snapshots_test.cpp).
// Checked against RomM's own pages when they come
// (docs/ARCHITECTURE.md, section 8).
//
// WHAT A PUSH CAN BE ANSWERED (`push_snapshot`, `_respond`):
//   201  made: the snapshot is the channel's new current.
//   200  unchanged: the current already holds exactly these hashes, or
//        differs only in a save's clock; nothing was written.
//   400  `missing`: RomM holds no bytes for these hashes and the push did
//        not send them; nothing was written. Sent again with them.
//   409  `branch`: the channel's current was not the one the push expected.
//        RomM kept the push beside it as a branch (30 days by default), and
//        recorded this device as holding the branch.
//   409  `hardcore_downgrade`: the channel's current was saved in hardcore
//        and the push is not; nothing was written. The console never
//        approves that downgrade (#346, decision 10).
//   Anything else (404 for a game or channel this person cannot see, 422
//   for a hash the bytes do not match) is refused, and nothing was written.

#include "romm.h"

#include "dirsave.h"
#include "romm_internal.h"

#include "rhash/md5.h"

#include <archive.h>
#include <archive_entry.h>
#include <json-c/json.h>

#include <algorithm>
#include <climits>
#include <map>
#include <utility>

namespace romm {

namespace {

// rcheevos' MD5 (third_party/rcheevos/src/rhash/md5.c, compiled into the
// program for RetroAchievements' hashes), fed in pieces it can count.
struct Md5 {
    md5_state_t state;
    Md5() { md5_init(&state); }
    void add(const void* data, size_t size) {
        const auto* p = static_cast<const md5_byte_t*>(data);
        while (size > 0) {
            const int n = size > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(size);
            md5_append(&state, p, n);
            p += n;
            size -= static_cast<size_t>(n);
        }
    }
    std::string hex() {
        md5_byte_t digest[16];
        md5_finish(&state, digest);
        static const char* kHex = "0123456789abcdef";
        std::string out;
        for (const md5_byte_t b : digest) {
            out.push_back(kHex[b >> 4]);
            out.push_back(kHex[b & 0x0F]);
        }
        return out;
    }
};

std::string md5Of(const std::vector<uint8_t>& bytes) {
    Md5 m;
    m.add(bytes.data(), bytes.size());
    return m.hex();
}

// Python's CP437, for the bytes from 0x80 up (those below are ASCII): what
// RomM's zipfile makes of a name a zip does not mark as UTF-8.
constexpr uint16_t kCp437High[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

// A name's bytes read as CP437, written as UTF-8.
std::string fromCp437(const std::string& bytes) {
    std::string out;
    for (const char ch : bytes) {
        const auto b = static_cast<unsigned char>(ch);
        if (b < 0x80) {
            out += ch;
            continue;
        }
        const uint16_t cp = kCp437High[b - 0x80];
        if (cp < 0x800) {
            out += static_cast<char>(0xC0 | cp >> 6);
        } else {
            out += static_cast<char>(0xE0 | cp >> 12);
            out += static_cast<char>(0x80 | (cp >> 6 & 0x3F));
        }
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

// RomM's `hash_zip_contents`: each file's MD5 under its name, sorted by
// name. False when libarchive cannot read the zip, or reads a name its
// central directory does not hold. A name is what RomM's zipfile makes of
// it: the zip's bytes as UTF-8 when the zip marks the name so, and as CP437
// when it does not (a Shift-JIS name, say), whatever libarchive would make
// of them. libarchive gives each name as the bytes the zip holds
// (cab::Utf8Names), and the central directory says which were marked
// (cab::zipNameMarks).
bool zipHash(const std::vector<uint8_t>& bytes, std::string* out) {
    std::map<std::string, bool> marks;
    if (!cab::zipNameMarks(bytes, &marks)) return false;
    const cab::Utf8Names names;
    struct archive* a = archive_read_new();
    if (!a) return false;
    archive_read_support_format_zip(a);
    if (archive_read_open_memory(a, bytes.data(), bytes.size()) != ARCHIVE_OK) {
        archive_read_free(a);
        return false;
    }
    std::vector<std::pair<std::string, std::string>> files;
    struct archive_entry* e = nullptr;
    int rc = ARCHIVE_OK;
    while ((rc = archive_read_next_header(a, &e)) == ARCHIVE_OK) {
        const char* raw = archive_entry_pathname(e);
        const auto mark = marks.find(raw ? raw : "");
        if (mark == marks.end()) {
            archive_read_free(a);
            return false;
        }
        const std::string name = mark->second ? mark->first : fromCp437(mark->first);
        // A folder is a name ending in "/" in a zip, which RomM leaves out.
        if (name.empty() || name.back() == '/' || archive_entry_filetype(e) == AE_IFDIR) {
            archive_read_data_skip(a);
            continue;
        }
        Md5 m;
        char chunk[65536];
        la_ssize_t n = 0;
        while ((n = archive_read_data(a, chunk, sizeof chunk)) > 0) m.add(chunk, static_cast<size_t>(n));
        if (n < 0) { archive_read_free(a); return false; }
        files.emplace_back(name, m.hex());
    }
    archive_read_free(a);
    if (rc != ARCHIVE_EOF) return false;
    // Python's `sorted` on the names: by code point, which is the order of
    // their UTF-8 bytes, which is std::string's.
    std::sort(files.begin(), files.end());
    std::string lines;
    for (size_t i = 0; i < files.size(); ++i) {
        if (i) lines += '\n';
        lines += files[i].first + ":" + files[i].second;
    }
    Md5 m;
    m.add(lines.data(), lines.size());
    *out = m.hex();
    return true;
}

bool jbool(json_object* o, const char* key) {
    json_object* v = nullptr;
    return o && json_object_object_get_ex(o, key, &v) && v &&
           json_object_get_type(v) == json_type_boolean && json_object_get_boolean(v);
}

// The object under `key`, or null when it is missing, null or not an object.
json_object* jobj(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v) || !v ||
        json_object_get_type(v) != json_type_object)
        return nullptr;
    return v;
}

// A save or a state in a snapshot (SnapshotSaveSchema, SnapshotStateSchema).
SnapshotFile readFile(json_object* o) {
    SnapshotFile f;
    f.id = static_cast<int>(jint(o, "id"));
    f.fileName = jstr(o, "file_name");
    f.sizeBytes = jint(o, "file_size_bytes");
    f.contentHash = jstr(o, "content_hash");
    if (json_object* shot = jobj(o, "screenshot")) f.picturePath = jstr(shot, "download_path");
    f.shape = jstr(o, "shape");
    f.format = jstr(o, "format");
    f.emulator = jstr(o, "emulator");
    f.core = jstr(o, "core");
    return f;
}

// SnapshotSchema. False when it carries no id.
bool readSnapshot(json_object* o, Snapshot* s) {
    *s = Snapshot{};
    if (!o) return false;
    s->id = static_cast<int>(jint(o, "id"));
    s->digest = jstr(o, "digest");
    s->kind = jstr(o, "kind");
    s->parentId = static_cast<int>(jint(o, "parent_snapshot_id"));
    s->channelId = jstr(o, "channel_id");
    s->hardcore = jbool(o, "is_hardcore");
    s->createdAt = jstr(o, "created_at");
    if (json_object* d = jobj(o, "device")) {
        s->deviceOwn = jbool(d, "is_own");
        s->deviceId = jstr(d, "id");
        s->deviceClient = jstr(d, "client");
    }
    json_object* held = nullptr;
    if (json_object_object_get_ex(o, "held_by", &held) &&
        json_object_get_type(held) == json_type_array) {
        for (size_t i = 0; i < json_object_array_length(held); ++i) {
            json_object* h = json_object_array_get_idx(held, i);
            json_object* d = jobj(h, "device");
            s->heldBy.push_back({jstr(d, "id"), jstr(d, "client"), jstr(h, "synced_at")});
        }
    }
    if (json_object* save = jobj(o, "save")) {
        s->hasSave = true;
        s->save = readFile(save);
    }
    if (json_object* bank = jobj(o, "states")) {
        json_object_object_foreach(bank, core, slots) {
            if (json_object_get_type(slots) != json_type_object) continue;
            json_object_object_foreach(slots, slot, state) {
                if (json_object_get_type(state) != json_type_object) continue;
                s->states.push_back({core, slot, readFile(state)});
            }
        }
    }
    return s->id != 0;
}

// ChannelSchema. False when it carries no id.
bool readChannel(json_object* o, Channel* c) {
    *c = Channel{};
    c->id = jstr(o, "id");
    c->label = jstr(o, "label");
    c->hardcore = jbool(o, "is_hardcore");
    c->romId = static_cast<int>(jint(o, "rom_id"));
    c->romFileId = static_cast<int>(jint(o, "rom_file_id"));
    c->snapshotCount = static_cast<int>(jint(o, "snapshot_count"));
    c->updatedAt = jstr(o, "updated_at");
    c->currentId = static_cast<int>(jint(o, "current_snapshot_id"));
    if (c->currentId != 0) readSnapshot(jobj(o, "current"), &c->current);
    return !c->id.empty();
}

// THE THREE READERS ABOVE, BACKWARDS (channelsJson): every field each reads,
// under RomM's name, so a channel kept on the console reads back as it came.
void addStr(json_object* o, const char* key, const std::string& v) {
    json_object_object_add(o, key, json_object_new_string(v.c_str()));
}

json_object* fileJson(const SnapshotFile& f) {
    json_object* o = json_object_new_object();
    json_object_object_add(o, "id", json_object_new_int(f.id));
    addStr(o, "file_name", f.fileName);
    json_object_object_add(o, "file_size_bytes", json_object_new_int64(f.sizeBytes));
    addStr(o, "content_hash", f.contentHash);
    if (!f.picturePath.empty()) {
        json_object* shot = json_object_new_object();
        addStr(shot, "download_path", f.picturePath);
        json_object_object_add(o, "screenshot", shot);
    }
    addStr(o, "shape", f.shape);
    addStr(o, "format", f.format);
    addStr(o, "emulator", f.emulator);
    addStr(o, "core", f.core);
    return o;
}

json_object* deviceJson(const std::string& id, const std::string& client) {
    json_object* d = json_object_new_object();
    addStr(d, "id", id);
    addStr(d, "client", client);
    return d;
}

json_object* snapshotJson(const Snapshot& s) {
    json_object* o = json_object_new_object();
    json_object_object_add(o, "id", json_object_new_int(s.id));
    addStr(o, "digest", s.digest);
    addStr(o, "kind", s.kind);
    json_object_object_add(o, "parent_snapshot_id",
                           s.parentId ? json_object_new_int(s.parentId) : nullptr);
    addStr(o, "channel_id", s.channelId);
    json_object_object_add(o, "is_hardcore", json_object_new_boolean(s.hardcore));
    addStr(o, "created_at", s.createdAt);
    json_object* device = deviceJson(s.deviceId, s.deviceClient);
    json_object_object_add(device, "is_own", json_object_new_boolean(s.deviceOwn));
    json_object_object_add(o, "device", device);
    json_object* held = json_object_new_array();
    for (const SnapshotHolder& h : s.heldBy) {
        json_object* one = json_object_new_object();
        json_object_object_add(one, "device", deviceJson(h.deviceId, h.client));
        addStr(one, "synced_at", h.syncedAt);
        json_object_array_add(held, one);
    }
    json_object_object_add(o, "held_by", held);
    if (s.hasSave) json_object_object_add(o, "save", fileJson(s.save));
    json_object* bank = json_object_new_object();
    for (const SnapshotState& st : s.states) {
        json_object* slots = jobj(bank, st.core.c_str());
        if (!slots) {
            slots = json_object_new_object();
            json_object_object_add(bank, st.core.c_str(), slots);
        }
        json_object_object_add(slots, st.slot.c_str(), fileJson(st.file));
    }
    json_object_object_add(o, "states", bank);
    return o;
}

json_object* channelJson(const Channel& c) {
    json_object* o = json_object_new_object();
    addStr(o, "id", c.id);
    addStr(o, "label", c.label);
    json_object_object_add(o, "is_hardcore", json_object_new_boolean(c.hardcore));
    json_object_object_add(o, "rom_id", json_object_new_int(c.romId));
    json_object_object_add(o, "rom_file_id", json_object_new_int(c.romFileId));
    json_object_object_add(o, "snapshot_count", json_object_new_int(c.snapshotCount));
    addStr(o, "updated_at", c.updatedAt);
    json_object_object_add(o, "current_snapshot_id",
                           c.currentId ? json_object_new_int(c.currentId) : nullptr);
    if (c.currentId != 0) json_object_object_add(o, "current", snapshotJson(c.current));
    return o;
}

// A push's states, a core at a time, as the manifest names them.
std::vector<std::pair<std::string, std::vector<const PushState*>>>
statesByCore(const SnapshotPush& push) {
    std::vector<std::pair<std::string, std::vector<const PushState*>>> cores;
    for (const PushState& s : push.states) {
        auto at = std::find_if(cores.begin(), cores.end(),
                               [&](const auto& c) { return c.first == s.core; });
        if (at == cores.end()) {
            cores.push_back({s.core, {}});
            at = cores.end() - 1;
        }
        at->second.push_back(&s);
    }
    return cores;
}

}  // namespace

std::string contentHash(const std::vector<uint8_t>& bytes) {
    std::string hash;
    if (cab::looksLikeZip(bytes) && zipHash(bytes, &hash)) return hash;
    return md5Of(bytes);
}

bool readChannels(const std::string& body, std::vector<Channel>* out, std::string* err) {
    json_object* root = json_tokener_parse(body.c_str());
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root) json_object_put(root);
        if (err) *err = "the channels did not come back as a list";
        return false;
    }
    for (size_t i = 0; i < json_object_array_length(root); ++i) {
        Channel c;
        if (readChannel(json_object_array_get_idx(root, i), &c)) out->push_back(std::move(c));
    }
    json_object_put(root);
    return true;
}

std::string channelsJson(const std::vector<Channel>& channels) {
    json_object* list = json_object_new_array();
    for (const Channel& c : channels) json_object_array_add(list, channelJson(c));
    std::string text = json_object_to_json_string_ext(list, JSON_C_TO_STRING_PRETTY);
    json_object_put(list);
    return text;
}

bool Client::fetchChannels(const std::vector<int>& romFileIds, std::vector<Channel>* out,
                           std::string* err, bool* notFound) const {
    out->clear();
    if (notFound) *notFound = false;
    constexpr size_t kPerCall = 100;
    for (size_t at = 0; at < romFileIds.size(); at += kPerCall) {
        std::string q;
        for (size_t i = at; i < romFileIds.size() && i < at + kPerCall; ++i)
            q += (q.empty() ? "?rom_file_id=" : "&rom_file_id=") + std::to_string(romFileIds[i]);
        std::string body;
        long status = 0;
        if (!get("/api/channels" + q, &body, err, &status)) {
            if (notFound) *notFound = status == 404;
            return false;
        }
        if (!readChannels(body, out, err)) return false;
    }
    return true;
}

std::string pushManifest(const SnapshotPush& push) {
    json_object* m = json_object_new_object();
    json_object_object_add(m, "rom_file_id", json_object_new_int(push.romFileId));
    if (!push.channelId.empty())
        json_object_object_add(m, "channel_id", json_object_new_string(push.channelId.c_str()));
    if (!push.label.empty())
        json_object_object_add(m, "label", json_object_new_string(push.label.c_str()));
    // Always named, as null for an empty channel: left out, RomM would take
    // the current it last saw this device read.
    json_object_object_add(m, "expected_current_id",
                           push.expectedCurrentId ? json_object_new_int(push.expectedCurrentId)
                                                  : nullptr);
    if (push.parentId)
        json_object_object_add(m, "parent_snapshot_id", json_object_new_int(push.parentId));
    if (push.hasSave) {
        json_object* save = json_object_new_object();
        json_object_object_add(save, "hash", json_object_new_string(push.save.hash.c_str()));
        json_object_object_add(save, "shape", json_object_new_string(push.saveShape.c_str()));
        // The emulator's own files: the console converts nothing (#346, step 8).
        json_object_object_add(save, "format", json_object_new_string("native"));
        json_object_object_add(m, "save", save);
    }
    if (!push.states.empty()) {
        json_object* bank = json_object_new_object();
        for (const auto& [core, slots] : statesByCore(push)) {
            json_object* named = json_object_new_object();
            for (const PushState* s : slots)
                json_object_object_add(named, s->slot.c_str(),
                                       json_object_new_string(s->file.hash.c_str()));
            json_object_object_add(bank, core.c_str(), named);
        }
        json_object_object_add(m, "states", bank);
    }
    if (!push.emulator.empty())
        json_object_object_add(m, "emulator", json_object_new_string(push.emulator.c_str()));
    if (!push.core.empty())
        json_object_object_add(m, "core", json_object_new_string(push.core.c_str()));
    std::string text = json_object_to_json_string_ext(m, JSON_C_TO_STRING_PLAIN);
    json_object_put(m);
    return text;
}

void readPushAnswer(long status, const std::string& body, PushAnswer* out) {
    *out = PushAnswer{};
    out->status = status;
    json_object* root = json_tokener_parse(body.c_str());
    const bool object = root && json_object_get_type(root) == json_type_object;
    bool read = false;
    if (object && (status == 200 || status == 201)) {
        out->kind = status == 201 ? PushAnswer::Kind::Made : PushAnswer::Kind::Unchanged;
        read = readSnapshot(root, &out->snapshot);
    } else if (object && status == 400) {
        json_object* missing = nullptr;
        if (json_object_object_get_ex(root, "missing", &missing) &&
            json_object_get_type(missing) == json_type_array) {
            out->kind = PushAnswer::Kind::BytesMissing;
            for (size_t i = 0; i < json_object_array_length(missing); ++i) {
                const char* key = json_object_get_string(json_object_array_get_idx(missing, i));
                if (key && *key) out->missing.emplace_back(key);
            }
            read = !out->missing.empty();
        }
    } else if (object && status == 409) {
        if (jbool(root, "hardcore_downgrade")) {
            out->kind = PushAnswer::Kind::HardcoreChannel;
            read = true;
        } else if (readSnapshot(jobj(root, "branch"), &out->snapshot)) {
            out->kind = PushAnswer::Kind::MovedOn;
            out->hasCurrent = readSnapshot(jobj(root, "current"), &out->current);
            out->fromOlder = jstr(root, "reason") == "moved_from_older";
            read = true;
        }
    }
    if (root) json_object_put(root);
    if (!read) {
        out->kind = PushAnswer::Kind::Refused;
        out->snapshot = Snapshot{};
        out->missing.clear();
        out->detail = body.substr(0, 180);
    }
}

bool Client::pushSnapshot(const SnapshotPush& push, PushAnswer* answer, std::string* err) const {
    const std::string manifest = pushManifest(push);
    std::vector<Part> parts{{"manifest", "", "application/json", manifest.data(), manifest.size()}};
    if (push.hasSave && !push.save.bytes.empty())
        parts.push_back({"save", push.save.fileName, "application/octet-stream",
                         push.save.bytes.data(), push.save.bytes.size()});
    // `state:<core>:<slot>`, and its picture as `state:<core>:<slot>:screenshot`.
    std::vector<std::string> names;
    names.reserve(push.states.size());
    for (const PushState& s : push.states) names.push_back("state:" + s.core + ":" + s.slot);
    for (size_t i = 0; i < push.states.size(); ++i) {
        const PushFile& f = push.states[i].file;
        if (!f.bytes.empty())
            parts.push_back({names[i], f.fileName, "application/octet-stream", f.bytes.data(),
                             f.bytes.size()});
        if (!f.picture.empty())
            parts.push_back({names[i] + ":screenshot", f.pictureName, "image/png",
                             f.picture.data(), f.picture.size()});
    }
    std::string reply, why;
    long status = 0;
    postMultipart("/api/snapshots", parts, &why, &reply, &status);
    if (status == 0) {
        if (err) *err = why;
        return false;
    }
    readPushAnswer(status, reply, answer);
    return true;
}

}  // namespace romm
