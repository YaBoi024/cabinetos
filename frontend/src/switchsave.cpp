// Switch saves through Sigil, on this console: the game's ID read with the
// player's keys, and another app's save put in Eden's form (switchsave.h).

#include "switchsave.h"

#include <sigil.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace switchsave {
namespace {

// THE SAVE ROOT SIGIL RESTORES INTO, in memory: an empty one, as RomM gives
// it a temporary folder (sigil_restore.py, _restore_per_game). Sigil writes
// each file through `write` and reads it back through `open` to check it.
using Files = std::map<std::string, std::vector<uint8_t>>;

struct Reading {
    std::vector<uint8_t> bytes;
};

int readAt(void* ctx, uint64_t off, void* buf, size_t len) {
    const auto* r = static_cast<const Reading*>(ctx);
    if (off >= r->bytes.size()) return 0;
    const size_t n = std::min(len, r->bytes.size() - static_cast<size_t>(off));
    std::memcpy(buf, r->bytes.data() + off, n);
    return static_cast<int>(n);
}

int64_t sizeOf(void* ctx) { return static_cast<int64_t>(static_cast<Reading*>(ctx)->bytes.size()); }

void closeReading(void* ctx) { delete static_cast<Reading*>(ctx); }

// sigil_io_close frees the stream itself with free(), so it is malloc'd.
sigil_io* openFile(void* ctx, const char* relPath) {
    const Files& files = *static_cast<const Files*>(ctx);
    const auto it = files.find(relPath);
    if (it == files.end()) return nullptr;
    auto* io = static_cast<sigil_io*>(std::malloc(sizeof(sigil_io)));
    if (!io) return nullptr;
    io->read = readAt;
    io->size = sizeOf;
    io->close = closeReading;
    io->ctx = new Reading{it->second};
    return io;
}

int writeFile(void* ctx, const char* relPath, const uint8_t* data, size_t len) {
    (*static_cast<Files*>(ctx))[relPath].assign(data, data + len);
    return 0;
}

int removeFile(void* ctx, const char* relPath) {
    static_cast<Files*>(ctx)->erase(relPath);
    return 0;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string errorOf(int rc, const sigil_sync_result* r) {
    std::string w = std::string("Sigil: ") + sigil_strerror(rc);
    if (r && r->problem[0]) w += std::string(" (") + r->problem + ")";
    return w;
}

}  // namespace

bool readGame(const std::string& dump, const std::string& keys, Game* out, std::string* why) {
    sigil_support support{};
    support.struct_version = SIGIL_SUPPORT_V1;
    if (!keys.empty()) support.switch_prod_keys_path = keys.c_str();
    sigil_options opts{};
    opts.struct_version = SIGIL_OPTIONS_V1;
    opts.support = &support;
    opts.flags = SIGIL_FLAG_FILENAME_FALLBACK;
    sigil_result r{};
    r.struct_version = SIGIL_RESULT_V4;
    const int rc = sigil_extract_from_path(dump.c_str(), SIGIL_PLATFORM_SWITCH, &opts, &r);
    if (rc != SIGIL_OK) {
        *why = std::string("Sigil could not read the game's ID: ") + sigil_strerror(rc);
        return false;
    }
    if (!r.title_id[0]) {
        *why = "Sigil found no title ID in the game's file";
        return false;
    }
    out->titleId = upper(r.title_id);
    out->saveId = upper(r.save_id[0] ? r.save_id : r.title_id);
    return true;
}

bool fromOtherApp(const std::vector<uint8_t>& stored, const Game& game,
                  const std::string& contentName, const std::string& profile,
                  std::vector<cab::ZipFile>* out, std::string* why) {
    out->clear();
    if (stored.empty()) {
        *why = "RomM's copy is empty";
        return false;
    }
    sigil_result r{};
    r.struct_version = SIGIL_RESULT_V4;
    std::snprintf(r.title_id, sizeof r.title_id, "%s", game.titleId.c_str());
    std::snprintf(r.save_id, sizeof r.save_id, "%s", game.saveId.c_str());
    r.platform = SIGIL_PLATFORM_SWITCH;
    r.source = SIGIL_SOURCE_BINARY;
    r.usage = SIGIL_USAGE_FOLDER_EXACT;

    Files files;
    const char* noListing[] = {nullptr};
    const char* ids[] = {game.titleId.c_str()};
    sigil_sync_request req{};
    req.struct_version = SIGIL_SYNC_REQUEST_V1;
    req.save.struct_version = SIGIL_SAVE_REQUEST_V1;
    req.save.layout = "eden";
    req.save.platform = "switch";
    req.save.content_path = contentName.c_str();
    req.save.result = &r;
    req.save.listing = noListing;
    req.save.listing_count = 0;
    req.save.open = openFile;
    req.save.open_ctx = &files;
    req.save.profile = profile.c_str();
    req.game_ids = ids;
    req.game_id_count = 1;
    req.mode = SIGIL_SYNC_MANAGED;
    req.write = writeFile;
    req.write_ctx = &files;
    req.remove = removeFile;

    sigil_sync_result* res = nullptr;
    const int rc = sigil_restore(&req, stored.data(), stored.size(), &res);
    if (rc != SIGIL_OK) {
        *why = errorOf(rc, res);
        sigil_sync_result_free(res);
        return false;
    }
    sigil_sync_result_free(res);

    // What Sigil wrote, `nand/user/save/0000000000000000/<profile or the
    // device's>/<title>/...`, under the unit's names.
    const std::string under = "nand/user/save/0000000000000000/";
    const std::string device(32, '0');
    for (auto& [path, bytes] : files) {
        std::string rest = path.compare(0, under.size(), under) == 0 ? path.substr(under.size())
                                                                      : std::string();
        const bool own = rest.compare(0, 33, profile + "/") == 0;
        const bool shared = rest.compare(0, 33, device + "/") == 0;
        const std::string inGame = rest.size() > 33 ? rest.substr(33) : std::string();
        if ((!own && !shared) || inGame.empty()) {
            *why = "Sigil put " + path + " outside this game's save folders";
            out->clear();
            return false;
        }
        out->push_back({(shared ? "device/" : "") + inGame, std::move(bytes)});
    }
    if (out->empty()) {
        *why = "Sigil found no save of this game in it";
        return false;
    }
    return true;
}

}  // namespace switchsave
