// The library as RomM lists it: platforms, collections and smart collections,
// games under any filter, one game with its facts and files, firmware, and
// Home's recent and favourite shelves; with the JSON field readers the whole
// client uses (jstr, jint, jstrs) and the game parser behind every list.
// Moved out of romm.cpp in the restructure (#398).

#include "romm.h"

#include "romm_internal.h"
#include "wiiu.h"

#include <json-c/json.h>

#include <cstdlib>
#include <ctime>

namespace romm {

// json-c returns borrowed pointers; these keep the call sites readable and
// make a missing field a default rather than a crash. RomM omits fields
// rather than nulling them in several places, so this is the common path and
// not an error case.
std::string jstr(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v) || !v) return {};
    if (json_object_get_type(v) == json_type_null) return {};
    const char* s = json_object_get_string(v);
    return s ? std::string(s) : std::string();
}

int64_t jint(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v) || !v) return 0;
    if (json_object_get_type(v) == json_type_null) return 0;
    return json_object_get_int64(v);
}

// A list of strings, empty when the field is missing, null or not a list.
std::vector<std::string> jstrs(json_object* o, const char* key) {
    std::vector<std::string> out;
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v) || !v) return out;
    if (json_object_get_type(v) != json_type_array) return out;
    const size_t n = json_object_array_length(v);
    for (size_t i = 0; i < n; ++i) {
        const char* s = json_object_get_string(json_object_array_get_idx(v, i));
        if (s && *s) out.emplace_back(s);
    }
    return out;
}

namespace {

bool parseGame(json_object* o, Game* g) {
    g->id = static_cast<int>(jint(o, "id"));
    if (g->id == 0) return false;
    g->platformId = static_cast<int>(jint(o, "platform_id"));
    g->platformSlug = jstr(o, "platform_slug");
    g->platformFsSlug = jstr(o, "platform_fs_slug");
    g->platformName = jstr(o, "platform_display_name");
    if (g->platformName.empty()) g->platformName = jstr(o, "platform_name");
    g->name = jstr(o, "name");
    g->fsName = jstr(o, "fs_name");
    g->sortKey = jstr(o, "name_sort_key");
    g->sizeBytes = jint(o, "fs_size_bytes");
    // A FOLDER BY EITHER OF RomM's NAMES FOR ONE (#400). Since RomM 5.0
    // `has_multiple_files` counts only the files at the top of the folder,
    // so one game file with `manual/` or `soundtrack/` beside it, which
    // RomM's own upload makes, is `has_nested_single_file`. Fetched as one
    // file, that came down as RomM's zip of the whole folder, and a core was
    // handed the soundtrack; or, alone in its folder, as the one file named
    // after the folder, without its ending, which PCSX2 cannot read (#416).
    bool multiple = false, nested = false;
    if (json_object* m = nullptr; json_object_object_get_ex(o, "has_multiple_files", &m) && m)
        multiple = json_object_get_boolean(m);
    if (json_object* m = nullptr; json_object_object_get_ex(o, "has_nested_single_file", &m) && m)
        nested = json_object_get_boolean(m);
    g->multiFile = multiple || nested;
    g->titleId = jstr(o, "title_id");
    g->raHash = jstr(o, "ra_hash");
    g->md5 = jstr(o, "md5_hash");
    // BOTH SIZES, because they are for different jobs. See romm_types.h:
    // `small` is a 162x216 thumbnail and `big` is 810x1080. Either may be
    // absent — a game the server never matched has neither — so each falls
    // back to the other and a caller can use one field without checking two.
    g->coverPath = jstr(o, "path_cover_small");
    g->coverLargePath = jstr(o, "path_cover_large");
    if (g->coverPath.empty()) g->coverPath = g->coverLargePath;
    if (g->coverLargePath.empty()) g->coverLargePath = g->coverPath;
    g->manualPath = jstr(o, "path_manual");
    g->regions = jstrs(o, "regions");
    g->languages = jstrs(o, "languages");
    g->revision = jstr(o, "revision");
    g->tags = jstrs(o, "tags");
    g->tagLabel = tagLabel(*g);
    return true;
}

}  // namespace

std::string tagLabel(const Game& g) {
    std::string out;
    auto group = [&](const std::vector<std::string>& parts) {
        if (parts.empty()) return;
        if (!out.empty()) out += ' ';
        out += '(';
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) out += ", ";
            out += parts[i];
        }
        out += ')';
    };
    group(g.regions);
    group(g.languages);
    if (!g.revision.empty()) group({"Rev " + g.revision});
    group(g.tags);
    return out;
}

// A record also carries the one thing the console reads off a file itself
// that a game needs offline: a Wii U game's product code, without which the
// Wii U rule reads it as needing the GamePad and Home leaves it off (#398,
// ROMM-31). A record written before it carried one takes the code this
// console read online (wiiu-codes.txt).
// RomM's `archive_members`, or the record's copy of them.
static std::vector<ArchiveMember> parseMembers(json_object* list) {
    std::vector<ArchiveMember> out;
    if (!list || json_object_get_type(list) != json_type_array) return out;
    const size_t n = json_object_array_length(list);
    for (size_t i = 0; i < n; ++i) {
        json_object* m = json_object_array_get_idx(list, i);
        ArchiveMember a;
        a.name = jstr(m, "name");
        a.size = jint(m, "size");
        a.crc = static_cast<uint32_t>(std::strtoul(jstr(m, "crc_hash").c_str(), nullptr, 16));
        if (!a.name.empty()) out.push_back(std::move(a));
    }
    return out;
}

bool gameFromJson(const std::string& json, Game* out) {
    json_object* o = json_tokener_parse(json.c_str());
    if (!o) return false;
    const bool ok = parseGame(o, out);
    if (ok) out->productCode = jstr(o, "product_code");
    if (json_object* a = nullptr; ok && json_object_object_get_ex(o, "archive_members", &a) && a) {
        out->membersFile = jstr(a, "file");
        out->membersSize = jint(a, "size");
        json_object* list = nullptr;
        if (json_object_object_get_ex(a, "members", &list)) out->members = parseMembers(list);
    }
    json_object_put(o);
    if (ok && out->platformSlug == "wiiu" && out->productCode.empty()) {
        bool known = false;
        out->productCode = wiiu::rememberedCode(out->id, &known);
    }
    return ok;
}

bool Client::fetchPlatforms(std::vector<Platform>* out, std::string* err) {
    std::string body;
    if (!get("/api/platforms", &body, err)) return false;
    json_object* root = json_tokener_parse(body.c_str());
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root) json_object_put(root);
        if (err) *err = "platforms response was not an array";
        return false;
    }
    const size_t n = json_object_array_length(root);
    out->clear();
    out->reserve(n);
    for (size_t i = 0; i < n; ++i) {
        json_object* o = json_object_array_get_idx(root, i);
        Platform p;
        p.id = static_cast<int>(jint(o, "id"));
        // The name the server's owner gave it, else RomM's own: what a game's
        // card already shows (`platform_display_name`, above), so a tile and
        // its games agree. MMagTech's library renames five ("Sega Genesis" for
        // "Sega Mega Drive/Genesis"). Labels only: folders on the drive come
        // from `fs_slug` and the catalogue from `slug`, so nothing moves.
        p.name = jstr(o, "display_name");
        if (p.name.empty()) p.name = jstr(o, "name");
        p.slug = jstr(o, "slug");
        p.fsSlug = jstr(o, "fs_slug");
        p.romCount = static_cast<int>(jint(o, "rom_count"));

        p.updatedAt = jstr(o, "updated_at");
        if (p.id != 0) out->push_back(std::move(p));
    }
    json_object_put(root);
    return true;
}

// Both lists, regular and smart, are the same shape.
static bool parseCollections(const std::string& body, bool smart,
                             std::vector<Collection>* out, std::string* err) {
    json_object* root = json_tokener_parse(body.c_str());
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root) json_object_put(root);
        if (err) *err = "collections response was not an array";
        return false;
    }
    const size_t n = json_object_array_length(root);
    out->clear();
    out->reserve(n);
    for (size_t i = 0; i < n; ++i) {
        json_object* o = json_object_array_get_idx(root, i);
        Collection c;
        c.id = static_cast<int>(jint(o, "id"));
        c.smart = smart;
        c.userId = static_cast<int>(jint(o, "user_id"));
        c.ownerName = jstr(o, "owner_username");
        c.name = jstr(o, "name");
        c.romCount = static_cast<int>(jint(o, "rom_count"));
        json_object* fav = nullptr;
        if (json_object_object_get_ex(o, "is_favorite", &fav) && fav)
            c.isFavorite = json_object_get_boolean(fav);

        // `path_cover_small` is null for a collection the person never gave
        // art to, which is the common case; `path_covers_small` is RomM's
        // mosaic of member covers and its first entry is a real cover from a
        // real game. Either way the tile gets a picture rather than a hole.
        c.coverPath = jstr(o, "path_cover_small");
        if (c.coverPath.empty()) {
            json_object* covers = nullptr;
            if (json_object_object_get_ex(o, "path_covers_small", &covers) &&
                json_object_get_type(covers) == json_type_array &&
                json_object_array_length(covers) > 0) {
                const char* s =
                    json_object_get_string(json_object_array_get_idx(covers, 0));
                if (s) c.coverPath = s;
            }
        }
        if (c.id != 0) out->push_back(std::move(c));
    }
    json_object_put(root);
    return true;
}

bool Client::fetchCollections(std::vector<Collection>* out, std::string* err) {
    std::string body;
    if (!get("/api/collections", &body, err)) return false;
    return parseCollections(body, false, out, err);
}

bool Client::fetchSmartCollections(std::vector<Collection>* out, std::string* err) {
    out->clear();
    std::string body, why;
    if (!get("/api/collections/smart", &body, &why)) {
        // A server from before smart collections has no such route.
        if (why.rfind("HTTP 404 ", 0) == 0) return true;
        if (err) *err = why;
        return false;
    }
    return parseCollections(body, true, out, err);
}

// THREE THINGS EVERY /api/roms RESPONSE CARRIES THAT THIS CLIENT NEVER READS.
//
// RomM returns `filter_values`, `rom_id_index` and `char_index` beside the
// items, for a web UI that draws filter menus and an A-Z rail. This parses
// `items` and `total` and has never looked at any of them.
//
// MEASURED 2026-09-22 rather than assumed, and the honest figure is smaller
// than the first one looked. On an UNFILTERED `limit=1` the three are 44.6 KB
// of a 46.3 KB response — 87% — because `filter_values` enumerates every
// genre, company and tag in the whole library. But `filter_values` is computed
// over the FILTERED set, so asking for one platform makes it small, and the
// requests this console actually makes save about 20%: the cover fill went
// 1.37 s to 1.07 s and 0.40 MB to 0.33 MB, boot's four calls 0.21 s to 0.17 s.
//
// Worth having for three query parameters, and not worth calling a
// breakthrough. The control was run: a misspelled flag changes nothing, which
// is the same trap `search_term` had.
static const char* const kLeanRoms =
    "&with_filter_values=false&with_rom_id_index=false&with_char_index=false";

std::string Client::encodeQueryValue(const std::string& in) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() + 16);
    for (unsigned char c : in) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') ||
                                c == '-' || c == '.' || c == '_' || c == '~';
        if (unreserved) { out += static_cast<char>(c); continue; }
        // NOT encodeUrl, which deliberately preserves & and = because it is
        // encoding a PATH. Here those two characters are exactly the danger:
        // a title with an ampersand in it would end the value and start a
        // parameter the server may or may not ignore.
        out += '%';
        out += kHex[c >> 4];
        out += kHex[c & 0x0F];
    }
    return out;
}

bool Client::fetchRoms(const std::string& filter, int limit,
                       std::vector<Game>* out, std::string* err, int* total) {
    out->clear();
    if (total) *total = 0;
    std::string path = "/api/roms?limit=" + std::to_string(limit) + kLeanRoms;
    if (!filter.empty()) path += "&" + filter;

    std::string body;
    if (!get(path, &body, err)) return false;
    json_object* root = json_tokener_parse(body.c_str());
    if (!root) { if (err) *err = "roms response was not JSON"; return false; }

    json_object* items = nullptr;
    if (!json_object_object_get_ex(root, "items", &items) ||
        json_object_get_type(items) != json_type_array) {
        json_object_put(root);
        if (err) *err = "roms response had no items array";
        return false;
    }
    json_object* t = nullptr;
    if (total && json_object_object_get_ex(root, "total", &t))
        *total = json_object_get_int(t);

    const size_t n = json_object_array_length(items);
    for (size_t i = 0; i < n; ++i) {
        Game g;
        if (parseGame(json_object_array_get_idx(items, i), &g))
            out->push_back(std::move(g));
    }
    json_object_put(root);
    fillWiiCodes(*out);
    return true;
}

bool Client::fetchGame(int romId, Game* out, std::string* err) {
    std::string body;
    if (!get("/api/roms/" + std::to_string(romId), &body, err)) return false;
    json_object* root = json_tokener_parse(body.c_str());
    if (!root) { if (err) *err = "rom response was not JSON"; return false; }
    const bool ok = parseGame(root, out);
    json_object_put(root);
    if (!ok && err) *err = "rom response did not parse as a game";
    if (ok) {
        std::vector<Game> one{*out};
        fillWiiCodes(one);
        out->titleId = one[0].titleId;
        out->productCode = one[0].productCode;
    }
    return ok;
}

static bool parseRomFiles(json_object* root, std::vector<RomFile>* out);

bool Client::fetchFacts(int romId, Facts* out, std::string* err) {
    *out = Facts{};
    std::string body;
    if (!get("/api/roms/" + std::to_string(romId), &body, err)) return false;
    json_object* root = json_tokener_parse(body.c_str());
    if (!root) { if (err) *err = "rom response was not JSON"; return false; }
    json_object* meta = nullptr;
    if (json_object_object_get_ex(root, "metadatum", &meta) &&
        json_object_get_type(meta) == json_type_object) {
        json_object* v = nullptr;
        if (json_object_object_get_ex(meta, "first_release_date", &v) &&
            json_object_get_type(v) == json_type_int) {
            // Milliseconds in RomM's merged record; seconds in some sources'.
            int64_t t = json_object_get_int64(v);
            if (t > 100000000000LL) t /= 1000;
            const time_t tt = static_cast<time_t>(t);
            struct tm g;
            if (t > 0 && gmtime_r(&tt, &g)) out->year = g.tm_year + 1900;
        }
        if (json_object_object_get_ex(meta, "companies", &v) &&
            json_object_get_type(v) == json_type_array && json_object_array_length(v) > 0) {
            json_object* first = json_object_array_get_idx(v, 0);
            if (json_object_get_type(first) == json_type_string)
                out->maker = json_object_get_string(first);
        }
        out->players = jstr(meta, "player_count");
    }
    parseRomFiles(root, &out->files);
    json_object_put(root);
    return true;
}

bool Client::fetchGames(int platformId, std::vector<Game>* out, std::string* err,
                        const std::function<void(int)>& onPage) {
    out->clear();
    // RomM caps a page. A library of thousands arrives truncated unless this
    // pages, and truncated-but-successful is the worst possible failure: the
    // UI would simply show fewer games and say nothing.
    constexpr int kPage = 500;
    int offset = 0;
    for (;;) {
        std::string path = "/api/roms?limit=" + std::to_string(kPage) +
                           "&offset=" + std::to_string(offset) + kLeanRoms;
        if (platformId > 0) path += "&platform_ids=" + std::to_string(platformId);

        std::string body;
        if (!get(path, &body, err)) return false;
        json_object* root = json_tokener_parse(body.c_str());
        if (!root) { if (err) *err = "games response was not JSON"; return false; }

        json_object* items = nullptr;
        if (!json_object_object_get_ex(root, "items", &items) ||
            json_object_get_type(items) != json_type_array) {
            json_object_put(root);
            if (err) *err = "games response had no items array";
            return false;
        }

        const size_t n = json_object_array_length(items);
        const size_t first = out->size();
        for (size_t i = 0; i < n; ++i) {
            Game g;
            if (parseGame(json_object_array_get_idx(items, i), &g))
                out->push_back(std::move(g));
        }
        json_object_put(root);
        fillWiiCodes(*out, first);

        // Said after each page rather than at the end: a library of sixteen
        // hundred arrives in four of these and the screen waiting on it has
        // to show something moving in between.
        if (onPage) onPage(static_cast<int>(out->size()));
        if (n < static_cast<size_t>(kPage)) break;
        offset += kPage;
    }
    return true;
}

bool Client::fetchFirmware(int platformId, std::vector<Firmware>* out, std::string* err) {
    out->clear();
    std::string body;
    if (!get("/api/firmware?platform_id=" + std::to_string(platformId), &body, err))
        return false;
    json_object* root = json_tokener_parse(body.c_str());
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root) json_object_put(root);
        if (err) *err = "firmware response was not an array";
        return false;
    }
    const size_t n = json_object_array_length(root);
    for (size_t i = 0; i < n; ++i) {
        json_object* o = json_object_array_get_idx(root, i);
        Firmware f;
        f.id = static_cast<int>(jint(o, "id"));
        f.fileName = jstr(o, "file_name");
        f.sizeBytes = jint(o, "file_size_bytes");
        if (f.id != 0 && !f.fileName.empty()) out->push_back(std::move(f));
    }
    json_object_put(root);
    return true;
}

// A game's `files`, from the one rom payload both fetchRomFiles and
// fetchFacts read. False when the payload has no files array.
static bool parseRomFiles(json_object* root, std::vector<RomFile>* out) {
    json_object* files = nullptr;
    if (!root || !json_object_object_get_ex(root, "files", &files) ||
        json_object_get_type(files) != json_type_array)
        return false;
    const size_t n = json_object_array_length(files);
    for (size_t i = 0; i < n; ++i) {
        json_object* o = json_object_array_get_idx(files, i);
        RomFile f;
        f.id = static_cast<int>(jint(o, "id"));
        f.fileName = jstr(o, "file_name");
        f.sizeBytes = jint(o, "file_size_bytes");
        f.category = jstr(o, "category");
        json_object* top = nullptr;
        if (json_object_object_get_ex(o, "is_top_level", &top) &&
            json_object_get_type(top) == json_type_boolean)
            f.topLevel = json_object_get_boolean(top);
        if (json_object* m = nullptr; json_object_object_get_ex(o, "archive_members", &m))
            f.members = parseMembers(m);
        if (f.id != 0 && !f.fileName.empty()) out->push_back(std::move(f));
    }
    return true;
}

bool Client::fetchRomFiles(int romId, std::vector<RomFile>* out, std::string* err) {
    out->clear();
    std::string body;
    if (!get("/api/roms/" + std::to_string(romId), &body, err)) return false;
    json_object* root = json_tokener_parse(body.c_str());
    const bool listed = parseRomFiles(root, out);
    if (root) json_object_put(root);
    if (!listed) {
        if (err) *err = "the game had no files array";
        return false;
    }
    if (out->empty() && err) *err = "the game lists no files";
    return !out->empty();
}

std::string Client::romFilePath(int romId, const RomFile& f) {
    // THE NAME IS ESCAPED WHOLE (encodeSegment), because encodeUrl leaves
    // `#`, `&` and `?` alone as delimiters, and a file name is not a place for
    // them to mean anything.
    return "/api/roms/" + std::to_string(romId) + "/content/" + encodeSegment(f.fileName) +
           "?file_ids=" + std::to_string(f.id);
}

bool Client::fetchFavorites(int limit, std::vector<Game>* out, std::string* err) {
    return fetchFiltered("&favorite=true", limit, out, err);
}

bool Client::fetchRecent(int limit, std::vector<Game>* out, std::string* err) {
    return fetchFiltered("&order_by=last_played&order_dir=desc&last_played=true",
                         limit, out, err);
}

bool Client::fetchFiltered(const char* filter, int limit, std::vector<Game>* out,
                           std::string* err) {
    out->clear();
    const std::string path = "/api/roms?limit=" + std::to_string(limit) + filter + kLeanRoms;
    std::string body;
    if (!get(path, &body, err)) return false;

    json_object* root = json_tokener_parse(body.c_str());
    if (!root) { if (err) *err = "response was not JSON"; return false; }
    json_object* items = nullptr;
    if (!json_object_object_get_ex(root, "items", &items) ||
        json_object_get_type(items) != json_type_array) {
        json_object_put(root);
        if (err) *err = "response had no items array";
        return false;
    }
    const size_t n = json_object_array_length(items);
    for (size_t i = 0; i < n; ++i) {
        Game g;
        if (parseGame(json_object_array_get_idx(items, i), &g))
            out->push_back(std::move(g));
    }
    json_object_put(root);
    fillWiiCodes(*out);
    return true;
}

}  // namespace romm
