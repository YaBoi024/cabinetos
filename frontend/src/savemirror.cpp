#include "savemirror.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>

#include <json-c/json.h>

#include "fileio.h"
#include "romm.h"

namespace savemirror {
namespace {

std::mutex gLock;
uint64_t gSeq = 0;
std::map<int, uint64_t> gForgotAt;   // rom id -> gSeq when last forgotten
std::map<int, uint64_t> gOnTopAt;    // rom id -> gSeq when this console's save last went on top

const char kHeld[] = "held.json";
const char kSnapshotPrefix[] = "snapshot-";

std::string dirFor(const storage::User& u, int romId) {
    return storage::userDir(u) + "/server-saves/" + std::to_string(romId);
}

std::string bytesName(const romm::Asset& a) {
    return std::to_string(a.id) + "-" + storage::safeSegment(a.updatedAt) + ".bin";
}

// Empty for a save RomM gave no hash.
std::string snapshotName(const romm::SnapshotFile& f) {
    if (f.contentHash.empty()) return "";
    return kSnapshotPrefix + storage::safeSegment(f.contentHash) + ".bin";
}

bool isSnapshotBytes(const std::string& name) {
    return name.compare(0, sizeof kSnapshotPrefix - 1, kSnapshotPrefix) == 0;
}

bool endsWithBin(const std::string& name) {
    return name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0;
}

// Deletes the kept bytes `mine` says are its own and `keep` does not list.
template <typename Mine>
void dropBytes(const std::string& dir, const std::set<std::string>& keep, Mine mine) {
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            const std::string name = e->d_name;
            if (endsWithBin(name) && mine(name) && !keep.count(name))
                ::unlink((dir + "/" + name).c_str());
        }
        ::closedir(d);
    }
}

// A LIST THAT HAS NOT CHANGED IS NOT WRITTEN AGAIN. Every start asks RomM for
// the game's saves and its channels and keeps what it answered; most starts
// it answers what it did last time, and writing that again costs the start a
// sync to the drive for each list (cab::writeBytes) and tells it nothing.
bool writeIfChanged(const std::string& path, const std::string& text) {
    const std::vector<uint8_t> was = cab::readBytes(path);
    if (was.size() == text.size() && std::equal(was.begin(), was.end(), text.begin()))
        return true;
    return cab::writeBytes(path, text);
}

bool writeJson(const std::string& path, json_object* o) {
    const std::string text = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
    json_object_put(o);
    return writeIfChanged(path, text);
}

bool sizedFile(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

}  // namespace

void putList(const storage::User& u, int romId, const std::vector<romm::Asset>& rows) {
    if (!u.valid() || romId <= 0) return;
    const std::string dir = dirFor(u, romId);
    json_object* arr = json_object_new_array();
    std::set<std::string> keep;
    for (const romm::Asset& a : rows) {
        json_object* o = json_object_new_object();
        json_object_object_add(o, "id", json_object_new_int(a.id));
        json_object_object_add(o, "file_name", json_object_new_string(a.fileName.c_str()));
        json_object_object_add(o, "file_size_bytes", json_object_new_int64(a.sizeBytes));
        json_object_object_add(o, "emulator", json_object_new_string(a.emulator.c_str()));
        json_object_object_add(o, "updated_at", json_object_new_string(a.updatedAt.c_str()));
        json_object_object_add(o, "channel_id", json_object_new_string(a.channelId.c_str()));
        json_object_object_add(o, "slot", json_object_new_string(a.slot.c_str()));
        json_object_array_add(arr, o);
        keep.insert(bytesName(a));
    }
    writeJson(dir + "/saves.json", arr);
    // Bytes of a row that is gone, or of an older version of one.
    dropBytes(dir, keep, [](const std::string& name) { return !isSnapshotBytes(name); });
}

bool getList(const storage::User& u, int romId, std::vector<romm::Asset>* rows) {
    rows->clear();
    if (!u.valid()) return false;
    json_object* arr = json_object_from_file((dirFor(u, romId) + "/saves.json").c_str());
    if (!arr) return false;
    const bool ok = json_object_is_type(arr, json_type_array);
    if (ok) {
        for (size_t i = 0; i < json_object_array_length(arr); ++i) {
            json_object* o = json_object_array_get_idx(arr, i);
            json_object* v = nullptr;
            romm::Asset a;
            if (json_object_object_get_ex(o, "id", &v)) a.id = json_object_get_int(v);
            if (json_object_object_get_ex(o, "file_name", &v)) a.fileName = json_object_get_string(v);
            if (json_object_object_get_ex(o, "file_size_bytes", &v)) a.sizeBytes = json_object_get_int64(v);
            if (json_object_object_get_ex(o, "emulator", &v)) a.emulator = json_object_get_string(v);
            if (json_object_object_get_ex(o, "updated_at", &v)) a.updatedAt = json_object_get_string(v);
            if (json_object_object_get_ex(o, "channel_id", &v)) a.channelId = json_object_get_string(v);
            if (json_object_object_get_ex(o, "slot", &v)) a.slot = json_object_get_string(v);
            a.romId = romId;
            if (a.id != 0) rows->push_back(std::move(a));
        }
    }
    json_object_put(arr);
    return ok;
}

void putBytes(const storage::User& u, int romId, const romm::Asset& row,
              const std::vector<uint8_t>& bytes) {
    if (!u.valid() || bytes.empty()) return;
    cab::writeBytes(dirFor(u, romId) + "/" + bytesName(row), bytes);
}

std::vector<uint8_t> getBytes(const storage::User& u, int romId, const romm::Asset& row) {
    if (!u.valid()) return {};
    return cab::readBytes(dirFor(u, romId) + "/" + bytesName(row));
}

bool hasBytes(const storage::User& u, int romId, const romm::Asset& row) {
    return u.valid() && sizedFile(dirFor(u, romId) + "/" + bytesName(row));
}

void putChannels(const storage::User& u, int romId, const std::vector<romm::Channel>& channels) {
    if (!u.valid() || romId <= 0) return;
    const std::string dir = dirFor(u, romId);
    if (!writeIfChanged(dir + "/channels.json", romm::channelsJson(channels))) return;
    std::set<std::string> keep;
    for (const romm::Channel& c : channels)
        if (c.currentId != 0 && c.current.hasSave) keep.insert(snapshotName(c.current.save));
    dropBytes(dir, keep, isSnapshotBytes);
}

bool getChannels(const storage::User& u, int romId, std::vector<romm::Channel>* channels) {
    channels->clear();
    if (!u.valid()) return false;
    const std::vector<uint8_t> text = cab::readBytes(dirFor(u, romId) + "/channels.json");
    if (text.empty()) return false;
    if (romm::readChannels(std::string(text.begin(), text.end()), channels, nullptr)) return true;
    channels->clear();
    return false;
}

void putSnapshotSave(const storage::User& u, int romId, const romm::SnapshotFile& save,
                     const std::vector<uint8_t>& bytes) {
    const std::string name = snapshotName(save);
    if (!u.valid() || romId <= 0 || name.empty() || bytes.empty()) return;
    cab::writeBytes(dirFor(u, romId) + "/" + name, bytes);
}

std::vector<uint8_t> getSnapshotSave(const storage::User& u, int romId,
                                     const romm::SnapshotFile& save) {
    const std::string name = snapshotName(save);
    if (!u.valid() || name.empty()) return {};
    return cab::readBytes(dirFor(u, romId) + "/" + name);
}

bool hasSnapshotSave(const storage::User& u, int romId, const romm::SnapshotFile& save) {
    const std::string name = snapshotName(save);
    return u.valid() && !name.empty() && sizedFile(dirFor(u, romId) + "/" + name);
}

void putHeld(const storage::User& u, int romId, const Held& held) {
    if (!u.valid() || romId <= 0 || held.snapshotId <= 0) return;
    json_object* o = json_object_new_object();
    json_object_object_add(o, "channel_id", json_object_new_string(held.channelId.c_str()));
    json_object_object_add(o, "snapshot_id", json_object_new_int(held.snapshotId));
    json_object_object_add(o, "digest", json_object_new_string(held.digest.c_str()));
    json_object_object_add(o, "save_hash", json_object_new_string(held.saveHash.c_str()));
    json_object_object_add(o, "at", json_object_new_int64(held.at));
    json_object_object_add(o, "made_at", json_object_new_int64(held.madeAt));
    json_object_object_add(o, "beside", json_object_new_boolean(held.beside));
    json_object* had = json_object_new_array();
    for (const std::string& h : held.hashesHad)
        json_object_array_add(had, json_object_new_string(h.c_str()));
    json_object_object_add(o, "save_hashes_had", had);
    if (!writeJson(dirFor(u, romId) + "/" + kHeld, o))
        std::fprintf(stderr, "[save] could not note the snapshot held of game %d\n", romId);
}

bool getHeld(const storage::User& u, int romId, Held* held) {
    *held = Held{};
    if (!u.valid()) return false;
    json_object* o = json_object_from_file((dirFor(u, romId) + "/" + kHeld).c_str());
    if (!o) return false;
    json_object* v = nullptr;
    if (json_object_object_get_ex(o, "channel_id", &v)) held->channelId = json_object_get_string(v);
    if (json_object_object_get_ex(o, "snapshot_id", &v)) held->snapshotId = json_object_get_int(v);
    if (json_object_object_get_ex(o, "digest", &v)) held->digest = json_object_get_string(v);
    if (json_object_object_get_ex(o, "save_hash", &v)) held->saveHash = json_object_get_string(v);
    if (json_object_object_get_ex(o, "at", &v)) held->at = json_object_get_int64(v);
    if (json_object_object_get_ex(o, "made_at", &v)) held->madeAt = json_object_get_int64(v);
    if (json_object_object_get_ex(o, "beside", &v)) held->beside = json_object_get_boolean(v);
    if (json_object_object_get_ex(o, "save_hashes_had", &v) &&
        json_object_is_type(v, json_type_array))
        for (size_t i = 0; i < json_object_array_length(v); ++i)
            if (const char* h = json_object_get_string(json_object_array_get_idx(v, i)))
                held->hashesHad.push_back(h);
    json_object_put(o);
    if (held->snapshotId > 0) return true;
    *held = Held{};
    return false;
}

uint64_t mark() {
    std::lock_guard<std::mutex> lk(gLock);
    return gSeq;
}

bool forgottenSince(int romId, uint64_t m) {
    std::lock_guard<std::mutex> lk(gLock);
    auto it = gForgotAt.find(romId);
    return it != gForgotAt.end() && it->second > m;
}

void ownSaveWentOnTop(int romId) {
    std::lock_guard<std::mutex> lk(gLock);
    gOnTopAt[romId] = ++gSeq;
}

bool ownSaveOnTopSince(int romId, uint64_t m) {
    std::lock_guard<std::mutex> lk(gLock);
    auto it = gOnTopAt.find(romId);
    return it != gOnTopAt.end() && it->second > m;
}

void forget(const storage::User& u, int romId) {
    {
        std::lock_guard<std::mutex> lk(gLock);
        gForgotAt[romId] = ++gSeq;
    }
    if (!u.valid()) return;
    const std::string dir = dirFor(u, romId);
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.' || std::string(e->d_name) == kHeld) continue;
            ::unlink((dir + "/" + e->d_name).c_str());
        }
        ::closedir(d);
    }
    ::rmdir(dir.c_str());
}

void forgetHeld(const storage::User& u, int romId) {
    if (!u.valid()) return;
    const std::string dir = dirFor(u, romId);
    cab::removeFile(dir + "/" + kHeld);
    ::rmdir(dir.c_str());
}

}  // namespace savemirror
