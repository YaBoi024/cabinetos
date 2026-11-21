#include "savepush.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <set>

#include "cache.h"
#include "dirsave.h"
#include "fileio.h"
#include "library.h"
#include "savemirror.h"
#include "savepick.h"
#include "saves.h"
#include "timewords.h"
#include "uploader.h"

namespace savepush {
namespace {

// How a save travels, RomM's words (romm::SnapshotFile::shape).
const char kSingle[] = "SINGLE";
const char kMulti[] = "MULTI";
const char kFolder[] = "FOLDER";

// How many pushes one send makes at most. An ordinary one makes one; a
// channel that moved on, two; one that moved on again or turned hardcore
// while the console answered, three or four. Past that RomM and the console
// are not converging and the push waits, still owed.
constexpr int kMostPushes = 5;

int64_t sizeOf(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 ? static_cast<int64_t>(st.st_size) : 0;
}

std::string dirOf(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

// A name RomM gave a file that is safe to look for in a folder of this
// console's: no path in it.
bool plainName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos;
}

// THE NAME THIS CONSOLE GAVE A FILE RomM STORED. RomM adds its own time to
// every file it keeps, " [2026-11-19_13-02-02-848]" before the extension, its
// milliseconds optional (`apply_datetime_tag` in its utils/uploads.py at
// 5.5.0-alpha.2); taken off, what is left is the name the console sent, as
// RomM made it safe (rommKeptStem), and so the name of its own copy.
std::string ownName(const std::string& stored) {
    const size_t dot = stored.rfind('.');
    const size_t end = dot == std::string::npos ? stored.size() : dot;
    if (end == 0 || stored[end - 1] != ']') return stored;
    const size_t open = stored.rfind(" [", end);
    if (open == std::string::npos) return stored;
    const std::string tag = stored.substr(open + 2, end - 1 - (open + 2));
    // "YYYY-MM-DD_HH-MM-SS", then "-mmm" or nothing.
    const char* shape = "dddd-dd-dd_dd-dd-dd";
    if (tag.size() != 19 && tag.size() != 23) return stored;
    for (size_t i = 0; i < tag.size(); ++i) {
        const char want = i < 19 ? shape[i] : (i == 19 ? '-' : 'd');
        const bool ok = want == 'd' ? (tag[i] >= '0' && tag[i] <= '9') : tag[i] == want;
        if (!ok) return stored;
    }
    return stored.substr(0, open) + stored.substr(end);
}

bool isFile(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// This console's copy of a state RomM stored, found by the name the console
// gave it, and that copy's picture: empty for none.
std::string ownCopy(const std::string& stateDir, const std::string& stored) {
    const std::string mine = ownName(stored);
    if (!plainName(mine) || stateDir.empty() || !isFile(stateDir + "/" + mine)) return {};
    return stateDir + "/" + mine;
}
std::string pictureOf(const std::string& statePath) {
    const size_t dot = statePath.rfind('.');
    const std::string png = (dot == std::string::npos ? statePath : statePath.substr(0, dot)) + ".png";
    return isFile(png) ? png : std::string();
}

// THE NAME A STATE OF RomM'S LIST IS FILED UNDER: its bank's (Asset::core),
// or for one from before channels, which has none, its emulator's.
const std::string& filedUnder(const romm::Asset& row) {
    return row.core.empty() ? row.emulator : row.core;
}

// WHEN A STATE OF RomM'S LIST WAS MADE: the time the console put in its name
// (stateNameMs), else when RomM first stored it.
int64_t rowMadeMs(const romm::Asset& row) {
    const int64_t ms = stateNameMs(row.fileName);
    return ms != 0 ? ms : isoMicros(row.createdAt) / 1000;
}

// The slot a push files its n-th newest state under (the head of savepush.h).
std::string slotOf(size_t newest) { return std::to_string(newest + 1); }

// A marker's states, newest first, one per name, the newest kStatesKept.
void keepNewest(std::vector<cache::OwedPush::State>* states) {
    std::stable_sort(states->begin(), states->end(),
                     [](const cache::OwedPush::State& a, const cache::OwedPush::State& b) {
                         return a.madeMs > b.madeMs;
                     });
    std::vector<cache::OwedPush::State> kept;
    for (const cache::OwedPush::State& s : *states) {
        if (kept.size() == kStatesKept) break;
        bool dup = false;
        for (const cache::OwedPush::State& k : kept) dup = dup || k.fileName == s.fileName;
        if (!dup) kept.push_back(s);
    }
    *states = std::move(kept);
}

// What a marker holds back on the disk (pendingBytes): its save with its
// picture, and its states with theirs.
int64_t owedBytes(const cache::OwedPush& p) {
    int64_t n = p.hasSave ? sizeOf(p.savePath) + sizeOf(p.savePicturePath) : 0;
    for (const cache::OwedPush::State& s : p.states) n += sizeOf(s.path) + sizeOf(s.picturePath);
    return n;
}

// THIS CONSOLE'S COPIES OF STATES IT NO LONGER OFFERS (the head of
// savepush.h): every state file in `dir`, with its picture, whose name is
// none of `keepNames` and whose bytes are none of `keepHashes`.
void trimStates(const std::string& dir, const std::set<std::string>& keepNames,
                const std::set<std::string>& keepHashes, int romId) {
    std::vector<std::string> gone;
    if (DIR* d = dir.empty() ? nullptr : ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            const std::string name = e->d_name;
            if (name.size() < 7 || name.compare(name.size() - 6, 6, ".state") != 0) continue;
            if (keepNames.count(name)) continue;
            if (keepHashes.count(romm::contentHash(cab::readBytes(dir + "/" + name)))) continue;
            gone.push_back(name);
        }
        ::closedir(d);
    }
    for (const std::string& name : gone) {
        cab::removeFile(dir + "/" + name);
        cab::removeFile(dir + "/" + name.substr(0, name.size() - 6) + ".png");
        std::fprintf(stderr, "[state] game %d: %s is no longer among its three; RomM keeps it "
                             "in the game's history\n", romId, name.c_str());
    }
}

// Several files of a save, zipped as a snapshot holds them (saves.h), each
// under the path given, and written beside this console's copies as
// `<game> (Cabinet).zip`; the push names it `<game>.zip`. False when it
// could not be.
bool zipBeside(const GameSession& sess, const std::vector<cab::ZipFile>& files,
               cache::OwedPush* p) {
    std::vector<uint8_t> zip;
    std::string zerr;
    p->saveName = sanitisedStem(sess.fsStem) + ".zip";
    p->savePath = sess.saveDir + "/" + saveRowName(sess.fsStem, "zip");
    p->saveShape = kMulti;
    if (!cab::zipFiles(files, &zip, &zerr) || !cab::writeBytes(p->savePath, zip)) {
        std::fprintf(stderr, "[save] could not write %s: %s\n", p->savePath.c_str(),
                     zerr.empty() ? "the write failed" : zerr.c_str());
        return false;
    }
    return true;
}

// The game's name on RomM with the extension the core gave its own file:
// `Contra 4.sav` for melonDS's `<what it was handed>.sav`. The core's whole
// file name when it has no extension.
std::string nameWithExtensionOf(const std::string& fsStem, const std::string& corePath) {
    const std::string file = corePath.substr(corePath.find_last_of('/') + 1);
    const size_t dot = file.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || fsStem.empty()) return file;
    return sanitisedStem(fsStem) + file.substr(dot);
}

// THE SAVE THE SESSION LEFT, in the form a start reads a snapshot's save
// (saves.h), named after the game with the extension its emulator gives it:
// where the bytes are on this console, the name they go up under and how
// they travel. A game has one
// kind of save. By what the session was set up with, a save folder first,
// then the files a core writes, then the battery, which is the order the
// restores look in. Several files are zipped here, each under the path its
// core reads it from whatever name it was left under, and the zip is written
// beside them so a push tried again after a restart sends the same bytes.
// A BATTERY WITH ITS CLOCK OR ITS CARTRIDGE (#346, step 11; SaveForm::clock,
// SaveForm::batteryFirst) goes the same way, from this console's copies:
// `<game>.srm` beside `<game>.rtc`, or Sigil's `backup.ram` beside
// `cart.ram`; a cartridge with no save on it is never sent, and the battery
// goes alone.
// False when there is no file to send.
bool gather(const GameSession& sess, cache::OwedPush* p) {
    const catalog::SaveForm form = catalog::saveForm(sess.core.c_str(), sess.platformSlug);
    if (!sess.dirSaveRoot.empty()) {
        p->saveName = dirSaveZipName(sess.title);
        p->savePath = sess.saveDir + "/" + p->saveName;
        p->saveShape = kFolder;
    } else if (!form.batteryFirst && sess.fileSaves.size() == 1) {
        const cab::FileSaveState& f = sess.fileSaves.front();
        p->saveName = nameWithExtensionOf(sess.fsStem, f.spec.path);
        p->savePath = f.kept;
        p->saveShape = kSingle;
    } else if (!form.batteryFirst && !sess.fileSaves.empty()) {
        std::vector<cab::ZipFile> files;
        for (const cab::FileSaveState& f : sess.fileSaves)
            if (!f.atLaunch.empty()) files.push_back({f.spec.path, f.atLaunch});
        if (!zipBeside(sess, files, p)) return false;
    } else {
        const std::string stem = sanitisedStem(sess.fsStem);
        std::vector<cab::ZipFile> beside;
        if (form.clock)
            if (std::vector<uint8_t> clock =
                    cab::readBytes(sess.saveDir + "/" + saveRowName(sess.fsStem, "rtc"));
                !clock.empty())
                beside.push_back({stem + ".rtc", std::move(clock)});
        for (const cab::FileSaveState& f : sess.fileSaves)
            if (!f.atLaunch.empty() && cab::holdsASave(f.atLaunch, f.spec.untouched))
                beside.push_back(
                    {f.spec.unitName.empty() ? f.spec.path : f.spec.unitName, f.atLaunch});
        p->saveName = stem + ".srm";
        p->savePath = sess.saveDir + "/" + saveRowName(sess.fsStem, "srm");
        p->saveShape = kSingle;
        if (!beside.empty()) {
            std::vector<cab::ZipFile> files;
            if (std::vector<uint8_t> battery = cab::readBytes(p->savePath); !battery.empty())
                files.push_back(
                    {form.batteryFirst ? catalog::kInternalVolume : p->saveName, std::move(battery)});
            files.insert(files.end(), beside.begin(), beside.end());
            if (!zipBeside(sess, files, p)) return false;
        }
    }
    return !p->savePath.empty() && sizeOf(p->savePath) > 0;
}

// THE CORE'S OWN NAME, in Sigil's spelling (catalog::SaveForm), sent beside
// the tag the save travels under (answer 7 of 2026-11-17), so the same core
// in another app gets the same bytes and RomM converts the save for any
// other (#346, step 8).
std::string coreNameOf(const GameSession& sess) {
    return catalog::saveForm(sess.core.c_str(), sess.platformSlug).sigil;
}

// The name this console last saw a channel under, for a push that has to
// make it again; RomM's own when it never saw it.
std::string labelOf(const storage::User& user, int romId, const std::string& channelId) {
    std::vector<romm::Channel> seen;
    savemirror::getChannels(user, romId, &seen);
    for (const romm::Channel& c : seen)
        if (c.id == channelId && !c.label.empty()) return c.label;
    return kDefaultChannel;
}

// WHERE THE PUSH GOES (the head of savepush.h), from what this console knows
// without asking: onto the snapshot this console holds, or the one the
// session started from when the note of it was never written; else into the
// person's "default". The queue's half finds the channel a name stands for,
// a hardcore one, and the snapshot held by then (send). An earlier push of
// the game still unsent is the save this session started from (savepick.h: a
// save still owed wins), so what that one could not go on top of, this one
// cannot either.
void aim(const GameSession& sess, const storage::User& user, cache::OwedPush* p) {
    p->couldNotUseNewest = sess.couldNotUseNewest;
    cache::OwedPush earlier;
    if (cache::owedPush(user, sess.romId, &earlier))
        p->couldNotUseNewest = p->couldNotUseNewest || earlier.couldNotUseNewest;
    savemirror::Held held;
    std::string channel = sess.startChannelId;
    int base = sess.startSnapshotId;
    if (savemirror::getHeld(user, sess.romId, &held)) {
        channel = held.channelId;
        base = held.snapshotId;
    }
    if (base != 0 && sess.startHardcore && channel == sess.startChannelId) {
        p->label = kSoftcoreChannel;
        p->parentId = base;
    } else if (base != 0) {
        p->channelId = channel;
        p->label = labelOf(user, sess.romId, channel);
        p->expectedCurrentId = base;
    } else {
        p->label = kDefaultChannel;
    }
}

// WHEN THIS CONSOLE WROTE THE SAVE: now, for one gathered as the game closed.
// A save folder or a core's file a crash or a power cut left is gathered at
// a later start (finishInterruptedSave, finishInterruptedPlay), so the
// files' own newest time stands when that is earlier.
int64_t madeAt(const GameSession& sess) {
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t newest = 0;
    for (const cab::DirEntry& e : sess.dirAtLaunch)
        newest = std::max(newest, e.mtimeNs / 1000000000);
    for (const cab::FileSaveState& f : sess.fileSaves)
        if (!f.kept.empty()) newest = std::max(newest, cab::modifiedNs(f.kept) / 1000000000);
    return newest > 0 && newest < now ? newest : now;
}

// The person's channel under `label` on the file, RomM's most recently
// changed when it holds several; null for none. RomM lets two channels share
// a name.
const romm::Channel* named(const std::vector<romm::Channel>& channels, const std::string& label) {
    const romm::Channel* found = nullptr;
    for (const romm::Channel& c : channels)
        if (c.label == label && (!found || c.updatedAt > found->updatedAt)) found = &c;
    return found;
}

const romm::Channel* byId(const std::vector<romm::Channel>& channels, const std::string& id) {
    for (const romm::Channel& c : channels)
        if (c.id == id) return &c;
    return nullptr;
}

}  // namespace

bool mark(GameSession& sess, const storage::User& user) {
    if (!sess.saveChanged) return false;
    sess.saveChanged = false;
    if (sess.saveTag.empty() || sess.romId <= 0 || !user.valid()) {
        std::fprintf(stderr, "[save] game %d's save stays on this console: %s\n", sess.romId,
                     sess.saveTag.empty() ? "no tag it may travel under" : "nobody to send it as");
        return false;
    }
    cache::OwedPush p;
    p.romId = sess.romId;
    p.emulator = sess.saveTag;
    p.core = coreNameOf(sess);
    p.hasSave = true;
    if (!gather(sess, &p)) {
        std::fprintf(stderr, "[save] game %d: nothing on this console to send\n", sess.romId);
        return false;
    }
    aim(sess, user, &p);
    p.madeAt = madeAt(sess);
    // THE SAVE'S PICTURE (savepicture.h), when this sitting took one: named
    // after the save, as RomM names it after the save's row in any case.
    if (!sess.savePicture.empty() && sizeOf(sess.savePicture) > 0) {
        const size_t dot = p.saveName.find_last_of('.');
        p.savePictureName =
            (dot == std::string::npos ? p.saveName : p.saveName.substr(0, dot)) + ".png";
        p.savePicturePath = sess.savePicture;
    }
    // The states an earlier marker of the game carries go on with it: made
    // on this console, they reach RomM only inside a snapshot.
    if (cache::OwedPush earlier; cache::owedPush(user, sess.romId, &earlier))
        p.states = earlier.states;
    // The new marker first, so the game is never without one; then the files
    // of its save still owed from before save channels, which this carries.
    if (cache::markPushOwed(user, p, owedBytes(p)).empty()) return false;
    for (const cache::Owed& o : cache::owed(user))
        if (o.romId == sess.romId && !o.isState && !o.isScreenshot)
            cache::clearPending(user, o.romId, o.fileName);
    std::fprintf(stderr, "[save] game %d: %s (%lld bytes)%s%s owed to RomM as a snapshot, %s%s\n",
                 sess.romId, p.saveName.c_str(), static_cast<long long>(sizeOf(p.savePath)),
                 p.savePicturePath.empty() ? "" : " with its picture",
                 p.states.empty() ? "" : (" and " + std::to_string(p.states.size()) +
                                          " state(s)").c_str(),
                 p.channelId.empty()
                     ? ("into the channel named \"" + p.label + "\"").c_str()
                     : ("on snapshot " + std::to_string(p.expectedCurrentId) + " of channel \"" +
                        p.label + "\"").c_str(),
                 p.couldNotUseNewest ? ", never on top of the save it could not use" : "");
    return true;
}

bool markState(const GameSession& sess, const storage::User& user, const StateMade& made) {
    if (sess.stateTag.empty() || sess.romId <= 0 || !user.valid() || made.path.empty())
        return false;
    cache::OwedPush p;
    if (!cache::owedPush(user, sess.romId, &p)) {
        p = cache::OwedPush{};
        p.romId = sess.romId;
        p.emulator = sess.stateTag;
        p.core = coreNameOf(sess);
    }
    // States alone are aimed from this session, where they were made; a
    // push with a save stays where its save goes.
    if (!p.hasSave) {
        cache::OwedPush aimed;
        aim(sess, user, &aimed);
        p.channelId = aimed.channelId;
        p.label = aimed.label;
        p.expectedCurrentId = aimed.expectedCurrentId;
        p.parentId = aimed.parentId;
        p.couldNotUseNewest = aimed.couldNotUseNewest;
    }
    p.states.insert(p.states.begin(), {sess.stateTag, made.fileName, made.path, made.pictureName,
                                       made.picturePath, made.madeMs});
    keepNewest(&p.states);
    if (cache::markPushOwed(user, p, owedBytes(p)).empty()) return false;
    std::fprintf(stderr, "[state] game %d: %s owed to RomM in the game's snapshot, %zu state(s) "
                         "in it%s\n",
                 sess.romId, made.fileName.c_str(), p.states.size(),
                 p.hasSave ? ", with its save" : "");
    return true;
}

void leave(GameSession& sess, const storage::User& user, Uploader& up) {
    if (mark(sess, user) || cache::isPushOwed(user, sess.romId))
        up.pushSnapshot(sess.romId, user);
}

bool markOldSave(const GameSession& sess, const storage::User& user,
                 const savepick::OldSaveCopy& copy,
                 const std::vector<std::vector<uint8_t>>& bytes) {
    if (copy.rows.empty() || sess.saveTag.empty() || sess.romId <= 0 || !user.valid())
        return false;
    cache::OwedPush earlier;
    const bool hadEarlier = cache::owedPush(user, sess.romId, &earlier);
    // A save this console owes wins at a start (savepick.h), so a start that
    // took RomM's had none; one marked since stands.
    if (hadEarlier && earlier.hasSave && !earlier.fromOldRows()) return false;
    cache::OwedPush p;
    p.romId = sess.romId;
    p.emulator = sess.saveTag;
    p.core = coreNameOf(sess);
    p.hasSave = true;
    p.saveShape = copy.shape;
    for (const romm::Asset* r : copy.rows) {
        const int64_t at = isoMicros(r->updatedAt) / 1000000;
        p.oldRows.push_back({r->id, at});
        p.madeAt = std::max(p.madeAt, at);
    }
    if (copy.zipped) {
        // Each row as the restore put it in place, under the path its core
        // reads it at, beside the console's own copy, as gather writes one.
        std::vector<cab::ZipFile> files;
        for (size_t i = 0; i < copy.rows.size() && i < bytes.size() && i < copy.paths.size(); ++i)
            if (!bytes[i].empty()) files.push_back({copy.paths[i], bytes[i]});
        std::vector<uint8_t> zip;
        std::string zerr;
        p.saveName = sanitisedStem(sess.fsStem) + ".zip";
        p.savePath = sess.saveDir + "/" + saveRowName(sess.fsStem, "zip");
        if (files.size() != copy.rows.size() || !cab::zipFiles(files, &zip, &zerr) ||
            !cab::writeBytes(p.savePath, zip)) {
            std::fprintf(stderr, "[save] game %d: its save from before channels could not be "
                                 "zipped to %s: %s\n",
                         sess.romId, p.savePath.c_str(),
                         files.size() != copy.rows.size() ? "a row's bytes did not come"
                         : zerr.empty()                   ? "the write failed"
                                                          : zerr.c_str());
            return false;
        }
    } else {
        // For the log: RomM names its copy after the row.
        p.saveName = ownName(copy.rows.front()->fileName);
    }
    aim(sess, user, &p);
    if (hadEarlier) p.states = earlier.states;
    // THE GAME'S STATES FROM BEFORE CHANNELS GO INTO ITS FIRST SNAPSHOT
    // (answer 5 of 2026-11-17): this console's own copies of its newest, as
    // the game offers them with RomM away, beside any the marker carries.
    // RomM holds them in no snapshot, so they go as their bytes (send). One
    // per name: keepNewest keeps the marker's own.
    if (!sess.stateTag.empty() && !sess.stateDir.empty()) {
        for (const OfferedState& o :
             offeredStates(user, sess.romId, sess.stateTag, sess.stateDir, nullptr))
            p.states.push_back({sess.stateTag, o.fileName, o.path,
                                o.picture.empty() ? std::string()
                                                  : o.picture.substr(o.picture.rfind('/') + 1),
                                o.picture, o.madeMs});
        keepNewest(&p.states);
    }
    if (cache::markPushOwed(user, p, owedBytes(p)).empty()) return false;
    std::fprintf(stderr, "[save] game %d: its save from before channels, %s, owed to RomM as a "
                         "snapshot (%s)%s, %s\n",
                 sess.romId, p.saveName.c_str(),
                 copy.zipped ? (std::to_string(copy.rows.size()) + " rows zipped").c_str()
                             : ("row " + std::to_string(p.oldRows.front().id) +
                                " copied in by RomM, no bytes sent").c_str(),
                 p.states.empty() ? "" : (" and " + std::to_string(p.states.size()) +
                                          " state(s)").c_str(),
                 p.channelId.empty()
                     ? ("into the channel named \"" + p.label + "\"").c_str()
                     : ("on snapshot " + std::to_string(p.expectedCurrentId) + " of channel \"" +
                        p.label + "\"").c_str());
    return true;
}

int adoptOldSaves(const storage::User& who) {
    std::map<int, std::vector<cache::Owed>> byGame;
    for (const cache::Owed& o : cache::owed(who))
        if (!o.isState && !o.isScreenshot) byGame[o.romId].push_back(o);
    int games = 0;
    // The name this console's copy had, " (Cabinet)" taken off: the name the
    // save goes up under, as a push of the session's names it.
    auto upName = [](const std::string& path) {
        std::string name = path.substr(path.find_last_of('/') + 1);
        const size_t at = name.rfind(" (Cabinet).");
        if (at != std::string::npos) name.erase(at, std::strlen(" (Cabinet)"));
        return name;
    };
    for (const auto& [romId, list] : byGame) {
        cache::OwedPush earlier;
        const bool had = cache::owedPush(who, romId, &earlier);
        auto clearOld = [&, romId = romId] {
            for (const cache::Owed& o : list) cache::clearPending(who, romId, o.fileName);
        };
        if (had && earlier.hasSave && !earlier.fromOldRows()) {
            clearOld();
            continue;
        }
        cache::OwedPush p;
        p.romId = romId;
        p.emulator = list.front().emulator;
        p.hasSave = true;
        std::vector<cab::ZipFile> files;
        for (const cache::Owed& o : list) {
            struct stat st;
            if (::stat(o.localPath.c_str(), &st) == 0)
                p.madeAt = std::max(p.madeAt, static_cast<int64_t>(st.st_mtime));
            if (list.size() > 1)
                if (std::vector<uint8_t> b = cab::readBytes(o.localPath); !b.empty())
                    files.push_back({o.localPath.substr(o.localPath.find_last_of('/') + 1),
                                     std::move(b)});
        }
        if (p.madeAt == 0) p.madeAt = static_cast<int64_t>(std::time(nullptr));
        if (list.size() == 1) {
            p.savePath = list.front().localPath;
            p.saveName = upName(p.savePath);
            p.saveShape = cab::looksLikeZip(cab::readBytes(p.savePath)) ? kFolder : kSingle;
        } else {
            // Several files of one game (Sega CD's memory and its cartridge):
            // one zip of them, as a snapshot holds them (saves.h).
            std::string stem = upName(list.front().fileName);
            stem = stem.substr(0, stem.find_last_of('.'));
            p.saveName = stem + ".zip";
            p.savePath = dirOf(list.front().localPath) + "/" + saveRowName(stem, "zip");
            p.saveShape = kMulti;
            std::vector<uint8_t> zip;
            std::string zerr;
            if (!files.empty() &&
                (!cab::zipFiles(files, &zip, &zerr) || !cab::writeBytes(p.savePath, zip))) {
                std::fprintf(stderr, "[save] game %d: its saves from before save channels could "
                                     "not be zipped: %s\n",
                             romId, zerr.empty() ? "the write failed" : zerr.c_str());
                continue;
            }
        }
        savemirror::Held held;
        if (savemirror::getHeld(who, romId, &held)) {
            p.channelId = held.channelId;
            p.label = labelOf(who, romId, held.channelId);
            p.expectedCurrentId = held.snapshotId;
        } else {
            p.label = kDefaultChannel;
        }
        if (had) p.states = earlier.states;
        if (cache::markPushOwed(who, p, owedBytes(p)).empty()) continue;
        clearOld();
        std::fprintf(stderr, "[save] game %d: its save owed from before save channels (%zu "
                             "file(s)) goes up as its snapshot\n",
                     romId, list.size());
        ++games;
    }
    return games;
}

int adoptOldStates(const storage::User& who) {
    std::map<int, std::vector<cache::Owed>> byGame;
    for (const cache::Owed& o : cache::owed(who))
        if (o.isState) byGame[o.romId].push_back(o);
    int games = 0;
    for (const auto& [romId, list] : byGame) {
        cache::OwedPush p;
        if (!cache::owedPush(who, romId, &p)) {
            p = cache::OwedPush{};
            p.romId = romId;
            p.emulator = list.front().emulator;
            savemirror::Held held;
            if (savemirror::getHeld(who, romId, &held)) {
                p.channelId = held.channelId;
                p.label = labelOf(who, romId, held.channelId);
                p.expectedCurrentId = held.snapshotId;
            } else {
                p.label = kDefaultChannel;
            }
        }
        for (const cache::Owed& o : list) {
            struct stat st;
            int64_t ms = stateNameMs(o.fileName);
            if (ms == 0 && ::stat(o.localPath.c_str(), &st) == 0)
                ms = static_cast<int64_t>(st.st_mtime) * 1000;
            p.states.push_back({o.emulator, o.fileName, o.localPath, o.shotName, o.shotPath, ms});
        }
        keepNewest(&p.states);
        if (cache::markPushOwed(who, p, owedBytes(p)).empty()) continue;
        for (const cache::Owed& o : list) cache::clearPending(who, romId, o.fileName);
        std::fprintf(stderr, "[state] game %d: %zu state(s) owed from before save channels go "
                             "up in its snapshot\n", romId, list.size());
        ++games;
    }
    return games;
}

std::vector<OfferedState> offeredStates(const storage::User& who, int romId,
                                        const std::string& tag, const std::string& stateDir,
                                        const std::vector<romm::Asset>* rows) {
    std::vector<OfferedState> out;
    // One per name, without RomM's time: a state RomM lists twice under one
    // name (sent before channels and again in a snapshot) is one state.
    std::set<std::string> names;
    // Made here and still owed: off this console's own files.
    cache::OwedPush owed;
    if (!tag.empty() && cache::owedPush(who, romId, &owed))
        for (const cache::OwedPush::State& s : owed.states)
            if (s.core == tag && isFile(s.path) && names.insert(s.fileName).second)
                out.push_back({0, s.fileName, s.path, isFile(s.picturePath) ? s.picturePath : "",
                               s.madeMs});
    if (rows && !tag.empty()) {
        // RomM's list: every snapshot's states and the ones from before
        // channels, under the console's own name only.
        for (const romm::Asset& r : *rows) {
            if (filedUnder(r) != tag || !names.insert(ownName(r.fileName)).second) continue;
            OfferedState o;
            o.id = r.id;
            o.fileName = r.fileName;
            o.madeMs = rowMadeMs(r);
            o.picture = r.picturePath;
            o.path = ownCopy(stateDir, r.fileName);
            if (const std::string png = o.path.empty() ? "" : pictureOf(o.path); !png.empty())
                o.picture = png;
            out.push_back(std::move(o));
        }
    } else if (DIR* d = stateDir.empty() ? nullptr : ::opendir(stateDir.c_str())) {
        // RomM not asked, or not answering: every state file of the game on
        // this console, dated by its name, else by its file.
        while (dirent* e = ::readdir(d)) {
            const std::string name = e->d_name;
            if (name.size() < 7 || name.compare(name.size() - 6, 6, ".state") != 0) continue;
            const std::string full = stateDir + "/" + name;
            struct stat st;
            if (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
            if (!names.insert(name).second) continue;
            const int64_t ms = stateNameMs(name);
            out.push_back({0, name, full, pictureOf(full),
                           ms != 0 ? ms : static_cast<int64_t>(st.st_mtime) * 1000});
        }
        ::closedir(d);
    }
    std::stable_sort(out.begin(), out.end(), [](const OfferedState& a, const OfferedState& b) {
        return a.madeMs > b.madeMs;
    });
    if (out.size() > kStatesKept) out.resize(kStatesKept);
    return out;
}

bool settled(Sent sent) {
    return sent == Sent::Landed || sent == Sent::Beside || sent == Sent::NothingOwed;
}

bool triedAgain(Sent sent) { return sent == Sent::NotSent || sent == Sent::Waits; }

Server serverOf(romm::Client& client) {
    Server s;
    s.hasChannels = [] { return romm::heardServerChannels(); };
    s.files = [&client](int romId, bool ask, romm::ChannelFiles* out, std::string* err) {
        if (!ask) {
            *out = rememberedChannelFiles(romId);
            return true;
        }
        if (!client.fetchChannelFiles(romId, out, err)) return false;
        rememberChannelFiles(romId, *out);
        return true;
    };
    s.channels = [&client](const std::vector<int>& fileIds, std::vector<romm::Channel>* out,
                           std::string* err, bool* gone) {
        return client.fetchChannels(fileIds, out, err, gone);
    };
    s.push = [&client](const romm::SnapshotPush& push, romm::PushAnswer* answer,
                       std::string* err) { return client.pushSnapshot(push, answer, err); };
    s.saves = [&client](int romId, std::vector<romm::Asset>* rows, std::string* err) {
        return client.fetchSaves(romId, rows, err);
    };
    s.states = [&client](int romId, std::vector<romm::Asset>* rows, std::string* err) {
        return client.fetchStates(romId, rows, err);
    };
    s.history = [&client](const std::string& channelId, std::vector<romm::Snapshot>* out,
                          std::string* err) {
        return client.fetchChannelHistory(channelId, out, err);
    };
    return s;
}

namespace {

// ONE PUSH OF A GAME AT A TIME (send, savepush.h): the person's folder and the
// game of every push in flight, under sendingMutex.
std::mutex sendingMutex;
std::condition_variable sendingDone;
std::set<std::string> sending;

class OnePushOfAGame {
public:
    OnePushOfAGame(const storage::User& who, int romId)
        : key_(storage::userDir(who) + "|" + std::to_string(romId)) {
        std::unique_lock<std::mutex> lock(sendingMutex);
        sendingDone.wait(lock, [this] { return sending.count(key_) == 0; });
        sending.insert(key_);
    }
    ~OnePushOfAGame() {
        {
            std::lock_guard<std::mutex> lock(sendingMutex);
            sending.erase(key_);
        }
        sendingDone.notify_all();
    }
    OnePushOfAGame(const OnePushOfAGame&) = delete;
    OnePushOfAGame& operator=(const OnePushOfAGame&) = delete;

private:
    const std::string key_;
};

Sent sendOne(const Server& server, const storage::User& who, int romId, std::string* why,
             const std::function<bool()>& inPlay);

}  // namespace

Sent send(const Server& server, const storage::User& who, int romId, std::string* why,
          const std::function<bool()>& inPlay) {
    OnePushOfAGame one(who, romId);
    return sendOne(server, who, romId, why, inPlay);
}

bool settleBeforeStart(const Server& server, const storage::User& who, int romId) {
    std::string why;
    const Sent sent = send(server, who, romId, &why);
    if (!settled(sent)) {
        std::fprintf(stderr, "[save] game %d: its save waiting on this console could not go to "
                             "RomM before the start (%s); this console's stands\n",
                     romId, why.c_str());
        return false;
    }
    std::fprintf(stderr, "[save] game %d: its save waiting on this console went to RomM before "
                         "the start picked%s\n",
                 romId, sent == Sent::NothingOwed ? " (already sent)" : "");
    return true;
}

namespace {

Sent sendOne(const Server& server, const storage::User& who, int romId, std::string* why,
             const std::function<bool()>& inPlay) {
    auto stop = [&](Sent sent, const std::string& reason) {
        if (why) *why = reason;
        return sent;
    };
    cache::OwedPush owed;
    if (!cache::owedPush(who, romId, &owed)) return stop(Sent::NothingOwed, "nothing is owed");
    // A SAVE FROM BEFORE CHANNELS THAT RomM COPIES IN ITSELF (markOldSave):
    // one row, and no file of this console's to send.
    bool copyIn = owed.fromOldRows() && owed.savePath.empty();
    std::vector<uint8_t> bytes =
        owed.hasSave && !copyIn ? cab::readBytes(owed.savePath) : std::vector<uint8_t>();
    // Its picture, the moment it was saved (savepicture.h); none when it has
    // none or its file is gone.
    const std::vector<uint8_t> picture = owed.savePicturePath.empty()
                                             ? std::vector<uint8_t>()
                                             : cab::readBytes(owed.savePicturePath);
    // The states it carries, off the disk with their pictures. One whose
    // file is gone (deleted by hand) is not sent.
    struct Made {
        const cache::OwedPush::State* state;
        std::vector<uint8_t> bytes, picture;
    };
    std::vector<Made> made;
    for (const cache::OwedPush::State& s : owed.states) {
        std::vector<uint8_t> b = cab::readBytes(s.path);
        if (b.empty()) continue;
        made.push_back({&s, std::move(b),
                        s.picturePath.empty() ? std::vector<uint8_t>()
                                              : cab::readBytes(s.picturePath)});
    }
    if (inPlay && inPlay()) return stop(Sent::NotSent, "the game is being played");
    if (owed.hasSave && !copyIn && bytes.empty() && !made.empty()) {
        // The save's file is gone, its states are not: they go, with the
        // save of the snapshot they build on.
        std::fprintf(stderr, "[save] game %d's owed save has no file left; its states go alone\n",
                     romId);
        owed.hasSave = false;
    }
    if (bytes.empty() && made.empty() && !copyIn) {
        // The files it was owed from are gone (deleted by hand, or with the
        // person). Nothing left to send, so nothing is owed.
        cache::clearPushOwed(who, romId, owed.stamp);
        std::fprintf(stderr, "[save] game %d's owed snapshot has no file left; nothing to send\n",
                     romId);
        return stop(Sent::NothingOwed, "its files are gone");
    }
    if (!server.hasChannels()) return stop(Sent::Waits, "RomM has not said it has save channels");

    // What RomM is asked could not be had: refused for good when it answered
    // so (the game is gone, or hidden from this person), else tried again.
    std::string err;
    auto unasked = [&] { return stop(refusedForGood(err) ? Sent::Refused : Sent::NotSent, err); };

    // THE FILES THE GAME'S CHANNELS CAN BE ON, AND THIS PERSON'S CHANNELS ON
    // THEM (romm::ChannelFiles), as a start lists them: the files as this
    // console remembers them, asked of RomM when it has none, and asked once
    // more when RomM no longer knows one of them: a scan of RomM's library
    // gives a file another id, or takes it away.
    romm::ChannelFiles files;
    server.files(romId, false, &files, &err);
    bool askedFiles = false;
    std::vector<romm::Channel> channels;
    for (;;) {
        if (files.files.empty()) {
            if (!server.files(romId, true, &files, &err)) return unasked();
            askedFiles = true;
            if (files.files.empty())
                return stop(Sent::Refused, "RomM lists no file of this game for its channels");
        }
        bool gone = false;
        if (server.channels(files.files, &channels, &err, &gone)) break;
        if (!gone || askedFiles) return unasked();
        files = romm::ChannelFiles{};
    }
    // THE FILE A PUSH NAMES (the head of savepush.h): a channel's own, which
    // RomM checks, when it goes into one RomM lists; else the one RomM makes
    // a new channel on.
    auto fileFor = [&](const std::string& channelId) {
        const romm::Channel* c = channelId.empty() ? nullptr : byId(channels, channelId);
        return c && c->romFileId != 0 ? c->romFileId : files.channelFile;
    };

    // A SAVE FROM BEFORE CHANNELS IS BROUGHT IN ONLY AS THE START TOOK IT
    // (markOldSave): every row still RomM's, from before channels, at the
    // time it had then. One changed (a console on the old version saved
    // again) or gone (deleted on RomM's website) is not brought in: the next
    // start takes what RomM holds then, and brings that in. States the push
    // carries go on alone. The bytes of a row RomM copies, as this console
    // kept them since the start (savemirror), become the copy's for offline.
    std::vector<uint8_t> copied;
    if (owed.fromOldRows()) {
        std::vector<romm::Asset> rows;
        if (!server.saves(romId, &rows, &err)) return unasked();
        bool asTaken = true;
        for (const cache::OwedPush::OldRow& old : owed.oldRows) {
            const auto row = std::find_if(rows.begin(), rows.end(), [&](const romm::Asset& r) {
                return r.id == old.id && r.channelId.empty();
            });
            asTaken = asTaken && row != rows.end() && isoMicros(row->updatedAt) / 1000000 == old.at;
            if (asTaken && copyIn) copied = savemirror::getBytes(who, romId, *row);
        }
        if (!asTaken) {
            std::fprintf(stderr, "[save] game %d: its save from before channels changed on RomM "
                                 "since the start; not brought in%s\n",
                         romId, made.empty() ? "" : ", its states go alone");
            if (made.empty()) {
                cache::clearPushOwed(who, romId, owed.stamp);
                return stop(Sent::NothingOwed, "its save from before channels changed on RomM");
            }
            owed.hasSave = false;
            owed.oldRows.clear();
            bytes.clear();
            copyIn = false;
        }
    }

    // WHERE IT GOES, now that RomM's channels are known: the channel the
    // marker names, or the person's own under the marker's name; and never a
    // hardcore one, whose place the person's "softcore" takes, the push made
    // from what it was made from.
    romm::SnapshotPush push;
    push.channelId = owed.channelId;
    push.label = owed.label.empty() ? kDefaultChannel : owed.label;
    push.expectedCurrentId = owed.expectedCurrentId;
    push.parentId = owed.parentId;
    push.emulator = owed.emulator;
    push.core = owed.core;
    auto intoSoftcore = [&](int madeFrom) {
        const romm::Channel* soft = named(channels, kSoftcoreChannel);
        push.channelId = soft ? soft->id : std::string();
        push.label = kSoftcoreChannel;
        push.expectedCurrentId = 0;
        push.parentId = madeFrom;
    };
    {
        const romm::Channel* target = push.channelId.empty() ? named(channels, push.label)
                                                             : byId(channels, push.channelId);
        if (target && target->hardcore)
            intoSoftcore(push.parentId ? push.parentId : push.expectedCurrentId);
        else if (target)
            push.channelId = target->id;
    }
    // THE SNAPSHOT THIS CONSOLE HOLDS IN THAT CHANNEL NOW IS WHAT THE PUSH
    // BUILDS ON, whatever the marker named. They differ in one case: an
    // older push of this game landed after the marker was written (a save
    // made while the one before it was still on its way up). The marker
    // names the snapshot both were made from, RomM's current is now the
    // older save, and expecting anything else would file the newer save
    // beside the older one.
    savemirror::Held holds;
    const bool holding = savemirror::getHeld(who, romId, &holds);
    if (holding && !push.channelId.empty() && holds.channelId == push.channelId) {
        push.expectedCurrentId = holds.snapshotId;
        push.parentId = 0;
    }
    // A PUSH OF STATES ALONE GOES ONTO THE CHANNEL'S CURRENT as RomM lists it
    // now, carrying that snapshot's save, whoever made it (the head of
    // savepush.h); not when the session could not use the newest save, nor
    // onto a save of another emulator's, and then it is aimed as a save is.
    auto onTopOf = [&](const romm::Snapshot& current) {
        return !owed.hasSave && !owed.couldNotUseNewest && !current.hardcore &&
               (!current.hasSave || current.save.emulator == owed.emulator);
    };
    if (!owed.hasSave) {
        const romm::Channel* target = push.channelId.empty() ? named(channels, push.label)
                                                             : byId(channels, push.channelId);
        if (target && !target->hardcore && target->currentId != 0 && onTopOf(target->current)) {
            push.channelId = target->id;
            push.expectedCurrentId = target->currentId;
            push.parentId = 0;
        }
    }
    // STATES ALONE WITH NOTHING TO BUILD ON, while the game has a save from
    // before channels on this console or on RomM: they wait (the head of
    // savepush.h).
    if (!owed.hasSave && push.expectedCurrentId == 0 && push.parentId == 0) {
        const romm::Channel* target = push.channelId.empty() ? named(channels, push.label)
                                                             : byId(channels, push.channelId);
        if (!target || target->currentId == 0) {
            bool older = false;
            for (const cache::Owed& o : cache::owed(who))
                older = older || (o.romId == romId && !o.isState && !o.isScreenshot);
            std::vector<romm::Asset> rows;
            if (!older && !server.saves(romId, &rows, &err)) return unasked();
            for (const romm::Asset& r : rows)
                older = older || (r.channelId.empty() && r.emulator == owed.emulator);
            if (older)
                return stop(Sent::NotSent, "its states wait until the game's save is in a channel");
        }
    }
    // The bytes, named by RomM's hash of them. A zip travels as one of the
    // two zip shapes and anything else as a single file, which RomM checks.
    // A row RomM copies in is named by its id, as the start found it.
    auto withSave = [&] {
        push.hasSave = true;
        if (copyIn) {
            push.saveCopyOf = owed.oldRows.front().id;
            push.save = romm::PushFile{};
            push.saveShape = owed.saveShape.empty() ? kSingle : owed.saveShape;
            return;
        }
        push.save.hash = romm::contentHash(bytes);
        push.save.fileName = owed.saveName;
        push.save.bytes = bytes;
        if (!picture.empty()) {
            push.save.pictureName = owed.savePictureName;
            push.save.picture = picture;
        }
        const bool zip = cab::looksLikeZip(bytes);
        push.saveShape = !zip ? kSingle : owed.saveShape == kFolder ? kFolder : kMulti;
    };
    if (owed.hasSave) withSave();
    // THE STATES IT NAMES (the head of savepush.h): the ones made since, with
    // their bytes and pictures, and the newest of RomM's under the console's
    // own name, by hash; kStatesKept, one per hash, in slots "1" to "3",
    // newest first. A state made since is always named, so none made on this
    // console is let go before RomM has it. None made: none named, and the
    // bank is carried as it is.
    const std::string core = made.empty() ? std::string() : made.front().state->core;
    const std::string stateDir = made.empty() ? std::string() : dirOf(made.front().state->path);
    if (!made.empty()) {
        std::vector<romm::Asset> rows;
        if (!server.states(romId, &rows, &err)) return unasked();
        std::vector<std::pair<int64_t, romm::PushState>> picked;
        std::set<std::string> hashes, names;
        auto add = [&](int64_t ms, romm::PushState s) {
            if (picked.size() == kStatesKept || !hashes.insert(s.file.hash).second) return;
            s.core = core;
            picked.emplace_back(ms, std::move(s));
        };
        for (const Made& m : made) {
            romm::PushState s;
            s.file.hash = romm::contentHash(m.bytes);
            s.file.fileName = m.state->fileName;
            s.file.bytes = m.bytes;
            if (!m.picture.empty()) {
                s.file.pictureName = m.state->pictureName;
                s.file.picture = m.picture;
            }
            names.insert(m.state->fileName);
            add(m.state->madeMs, std::move(s));
        }
        std::vector<const romm::Asset*> theirs;
        for (const romm::Asset& r : rows)
            if (filedUnder(r) == core && names.insert(ownName(r.fileName)).second)
                theirs.push_back(&r);
        std::stable_sort(theirs.begin(), theirs.end(),
                         [](const romm::Asset* a, const romm::Asset* b) {
                             return rowMadeMs(*a) > rowMadeMs(*b);
                         });
        // A STATE'S HASH, which RomM's list does not carry: off this
        // console's copy, else as the channels' currents list it, else as
        // the history of the channel it was pushed into does, asked once.
        // One with none (from before channels, and this console has no copy)
        // is left out.
        std::map<int, std::string> known;
        for (const romm::Channel& c : channels)
            for (const romm::SnapshotState& s : c.current.states)
                if (!s.file.contentHash.empty()) known[s.file.id] = s.file.contentHash;
        std::set<std::string> asked;
        auto hashOf = [&](const romm::Asset& r) {
            if (const std::string mine = ownCopy(stateDir, r.fileName); !mine.empty())
                if (const std::vector<uint8_t> b = cab::readBytes(mine); !b.empty())
                    return romm::contentHash(b);
            if (!known.count(r.id) && !r.channelId.empty() && asked.insert(r.channelId).second) {
                std::vector<romm::Snapshot> past;
                std::string herr;
                if (server.history(r.channelId, &past, &herr)) {
                    for (const romm::Snapshot& p : past)
                        for (const romm::SnapshotState& s : p.states)
                            if (!s.file.contentHash.empty()) known[s.file.id] = s.file.contentHash;
                } else {
                    std::fprintf(stderr, "[state] game %d: the history of channel %s not had: "
                                         "%s\n", romId, r.channelId.c_str(), herr.c_str());
                }
            }
            const auto at = known.find(r.id);
            return at == known.end() ? std::string() : at->second;
        };
        for (const romm::Asset* r : theirs) {
            if (picked.size() == kStatesKept) break;
            romm::PushState s;
            s.file.hash = hashOf(*r);
            s.file.fileName = r->fileName;
            if (s.file.hash.empty()) {
                std::fprintf(stderr, "[state] game %d: %s is left out of the game's states: "
                                     "RomM gives no hash for it and this console has no copy\n",
                             romId, r->fileName.c_str());
                continue;
            }
            add(rowMadeMs(*r), std::move(s));
        }
        std::stable_sort(picked.begin(), picked.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < picked.size(); ++i) {
            picked[i].second.slot = slotOf(i);
            push.states.push_back(std::move(picked[i].second));
        }
    }
    // What this push names, which RomM holds once it lands, wherever it kept
    // it: by hash, and by the name this console gave each.
    std::set<std::string> namedHashes, namedNames;
    for (const romm::PushState& s : push.states) {
        namedHashes.insert(s.file.hash);
        namedNames.insert(ownName(s.file.fileName));
    }
    // RomM LACKS SOME BYTES IT WAS NOT SENT: a state named by hash that no
    // snapshot holds (one from before channels) goes up from this console's
    // copy, or leaves the bank when there is none; the save from the owed
    // file.
    auto supply = [&](const std::vector<std::string>& missing) {
        bool any = false;
        for (const std::string& key : missing) {
            if (key == "save") {
                if (!owed.hasSave || push.hasSave) return false;
                withSave();
                any = true;
                continue;
            }
            auto at = std::find_if(push.states.begin(), push.states.end(),
                                   [&](const romm::PushState& s) {
                                       return key == "state:" + s.core + ":" + s.slot;
                                   });
            if (at == push.states.end() || !at->file.bytes.empty()) return false;
            const std::string copy = ownCopy(stateDir, at->file.fileName);
            std::vector<uint8_t> mine = copy.empty() ? std::vector<uint8_t>() : cab::readBytes(copy);
            if (!mine.empty() && romm::contentHash(mine) == at->file.hash) {
                // Under the name this console gave it, with its picture.
                at->file.fileName = copy.substr(copy.rfind('/') + 1);
                at->file.bytes = std::move(mine);
                if (const std::string png = pictureOf(copy); !png.empty()) {
                    at->file.pictureName = png.substr(png.rfind('/') + 1);
                    at->file.picture = cab::readBytes(png);
                }
            } else {
                std::fprintf(stderr, "[state] game %d: RomM no longer holds %s, and this console "
                                     "has no copy; it leaves the game's states\n",
                             romId, at->file.fileName.c_str());
                at->file.hash.clear();   // taken out below, once every key is read
            }
            any = true;
        }
        auto dropped = [](const romm::PushState& s) { return s.file.hash.empty(); };
        push.states.erase(std::remove_if(push.states.begin(), push.states.end(), dropped),
                          push.states.end());
        for (size_t i = 0; i < push.states.size(); ++i) push.states[i].slot = slotOf(i);
        return any;
    };

    bool beside = false, retargeted = false;
    romm::Snapshot held;
    for (int pushes = 1;; ++pushes) {
        if (pushes > kMostPushes)
            return stop(Sent::NotSent, "RomM's channel kept changing while this was sent");
        push.romFileId = fileFor(push.channelId);
        if (push.romFileId == 0)
            return stop(Sent::Refused, "RomM names no file of this game for a new channel");
        romm::PushAnswer answer;
        if (!server.push(push, &answer, &err)) return stop(Sent::NotSent, err);
        using Kind = romm::PushAnswer::Kind;
        if (answer.kind == Kind::Made || answer.kind == Kind::Unchanged) {
            held = answer.snapshot;
            beside = false;
            break;
        }
        if (answer.kind == Kind::MovedOn) {
            // RomM has it, beside the current. A save goes on top only when
            // this console's save was made later, and never for a session
            // that could not use the newest save. States alone go again onto
            // the current RomM names, as onto the one it listed (onTopOf),
            // named by hash: RomM holds their bytes now, in the branch.
            held = answer.snapshot;
            beside = true;
            if (!owed.hasSave) {
                const bool again = answer.hasCurrent && onTopOf(answer.current);
                std::fprintf(stderr, "[state] game %d: RomM's channel had moved on%s; this "
                                     "console's states kept beside its current as snapshot "
                                     "%d%s\n",
                             romId,
                             answer.hasCurrent
                                 ? (" to snapshot " + std::to_string(answer.current.id)).c_str()
                                 : " and has no current",
                             held.id,
                             again ? ", and they go again onto that current"
                             : owed.couldNotUseNewest
                                 ? ", where they stay: the newest was a save this console "
                                   "could not use"
                                 : ", where they stay: that current's save is one this console "
                                   "cannot use");
                if (!again) break;
                push.channelId = held.channelId.empty() ? push.channelId : held.channelId;
                push.expectedCurrentId = answer.current.id;
                push.parentId = 0;
                for (romm::PushState& s : push.states) {
                    s.file.bytes.clear();
                    s.file.pictureName.clear();
                    s.file.picture.clear();
                }
                continue;
            }
            // The current's save dated by its own row (savepick::savedAt): a
            // current that changed only its states is newer than its save.
            if (answer.hasCurrent) {
                std::vector<romm::Channel> theirChannel(1);
                theirChannel[0].currentId = answer.current.id;
                theirChannel[0].current = answer.current;
                std::vector<romm::Asset> rows;
                if (server.saves(romId, &rows, &err)) savepick::dateSaves(&theirChannel, rows);
                answer.current = theirChannel[0].current;
            }
            const int64_t theirs =
                answer.hasCurrent ? isoMicros(savepick::savedAt(answer.current)) : 0;
            const bool later = !answer.hasCurrent || owed.madeAt * 1000000 > theirs;
            std::fprintf(stderr, "[save] game %d: RomM's channel had moved on%s; this "
                                 "console's save is kept beside its current as snapshot %d%s\n",
                         romId,
                         answer.hasCurrent
                             ? (" to snapshot " + std::to_string(answer.current.id)).c_str()
                             : " and has no current",
                         held.id,
                         owed.couldNotUseNewest ? ", where it stays: the newest was a save this "
                                                  "console could not use"
                         : later                ? ", and was made later, so it goes on top"
                                                : ", where it stays: the other was made later");
            if (owed.couldNotUseNewest || !later) break;
            // The same snapshot again as the channel's new current: the
            // branch named as what it is made from, so RomM carries its save
            // and states and no byte is sent.
            push.channelId = held.channelId.empty() ? push.channelId : held.channelId;
            push.expectedCurrentId = answer.hasCurrent ? answer.current.id : 0;
            push.parentId = held.id;
            push.hasSave = false;
            push.save = romm::PushFile{};
            push.saveCopyOf = 0;
            push.states.clear();
            continue;
        }
        if (answer.kind == Kind::HardcoreChannel) {
            // The channel's current is hardcore: nothing was written there.
            // Made from the branch RomM kept when it kept one, else from
            // what this push was made from.
            if (push.label == kSoftcoreChannel)
                return stop(Sent::Refused, "RomM calls the softcore channel hardcore");
            const int madeFrom =
                beside ? held.id : push.parentId ? push.parentId : push.expectedCurrentId;
            intoSoftcore(madeFrom);
            if (beside) push.states.clear();
            else if (owed.hasSave) withSave();
            continue;
        }
        if (answer.kind == Kind::BytesMissing && supply(answer.missing)) continue;
        // REFUSED. A channel or a snapshot the marker names that RomM no
        // longer has (404; a channel deleted on RomM's website takes its
        // older snapshots with it) is asked once more as a save of its own,
        // into the person's channel of that name. Anything else stays owed.
        if (answer.status == 404 && !retargeted && !beside) {
            retargeted = true;
            const romm::Channel* again = named(channels, push.label);
            push.channelId = again && !again->hardcore ? again->id : std::string();
            push.expectedCurrentId = 0;
            push.parentId = 0;
            if (again && again->hardcore) intoSoftcore(0);
            continue;
        }
        return stop(Sent::Refused, "HTTP " + std::to_string(answer.status) + ": " + answer.detail);
    }

    // RomM HOLDS IT. In this order (the head of savepush.h): the copy of
    // RomM's saves forgotten, the snapshot noted as the one held, the marker
    // cleared, the copy fetched again.
    const std::string channelId = held.channelId.empty() ? push.channelId : held.channelId;
    savemirror::forget(who, romId);
    savemirror::Held before;
    const bool hadNote = savemirror::getHeld(who, romId, &before);
    savemirror::Held now = savepick::heldAfter(before, channelId, held,
                                               static_cast<int64_t>(std::time(nullptr)));
    // When its save was made: this console's note of it for a save it sent,
    // and for states alone the save's own time, which they did not change.
    // STATES ALONE THAT WENT ONTO ANOTHER SAVE (the head of this file) leave
    // the note's save as the one this console last had: the save it carries
    // is another device's, and the next start still says "Continuing from
    // another device" and puts it in place.
    if (owed.hasSave) {
        now.madeAt = owed.madeAt;
    } else if (hadNote && before.saveHash != now.saveHash) {
        now.saveHash = before.saveHash;
        now.hashesHad = before.hashesHad;
        now.madeAt = before.madeAt;
    } else if (before.madeAt != 0 && before.saveHash == now.saveHash) {
        now.madeAt = before.madeAt;
    }
    now.beside = beside;
    savemirror::putHeld(who, romId, now);
    cache::clearPushOwed(who, romId, owed.stamp);
    std::fprintf(stderr,
                 "[save] game %d: snapshot %d %s channel \"%s\" on RomM (%s, %zu bytes%s; %zu "
                 "state(s) named)\n",
                 romId, held.id, beside ? "is kept beside the current of" : "is the current of",
                 push.label.c_str(), owed.hasSave ? owed.saveName.c_str() : "its states alone",
                 bytes.size(),
                 copyIn ? ", its save from before channels copied in by RomM" : "",
                 namedHashes.size());
    // This console's copies of states it no longer offers: kept are the ones
    // this push named and the ones in a push owed since (the head of
    // savepush.h).
    if (!made.empty()) {
        std::set<std::string> names = namedNames;
        if (cache::OwedPush since; cache::owedPush(who, romId, &since))
            for (const cache::OwedPush::State& s : since.states)
                if (dirOf(s.path) == stateDir) names.insert(s.path.substr(stateDir.size() + 1));
        trimStates(stateDir, names, namedHashes, romId);
    }

    // The copy for offline: RomM's lists as they are now, and the bytes just
    // sent kept as that snapshot's save. Should the asking fail there is no
    // copy, and offline uses this console's own save, which is this one.
    std::vector<romm::Asset> rows;
    if (server.saves(romId, &rows, &err)) savemirror::putList(who, romId, rows);
    bool gone = false;
    if (server.channels(files.files, &channels, &err, &gone)) {
        savemirror::putChannels(who, romId, channels);
        // A row RomM copied: this console's bytes of it, while they are the
        // copy's.
        const std::vector<uint8_t>& kept = copyIn ? copied : bytes;
        if (held.hasSave && owed.hasSave && !kept.empty() &&
            (!copyIn || romm::contentHash(kept) == held.save.contentHash))
            savemirror::putSnapshotSave(who, romId, held.save, kept);
    }
    return stop(beside ? Sent::Beside : Sent::Landed, std::string());
}

}  // namespace

}  // namespace savepush
