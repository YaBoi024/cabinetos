#include "savepush.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>

#include "cache.h"
#include "dirsave.h"
#include "fileio.h"
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

// What a marker holds back on the disk (pendingBytes): its save and its
// states with their pictures.
int64_t owedBytes(const cache::OwedPush& p) {
    int64_t n = p.hasSave ? sizeOf(p.savePath) : 0;
    for (const cache::OwedPush::State& s : p.states) n += sizeOf(s.path) + sizeOf(s.picturePath);
    return n;
}

// THE STATES OF THE SNAPSHOT A PUSH BUILDS ON, under `core`, newest first:
// as RomM just listed it when it is a channel's current, else as this
// console noted it when that is the snapshot it holds; none when neither
// says. Ordered by the time each one's name carries, then by slot.
std::vector<romm::SnapshotState> bankOf(int snapshotId, const std::vector<romm::Channel>& channels,
                                        const savemirror::Held* held, const std::string& core) {
    std::vector<romm::SnapshotState> out;
    const std::vector<romm::SnapshotState>* from = nullptr;
    for (const romm::Channel& c : channels)
        if (snapshotId != 0 && c.currentId == snapshotId) from = &c.current.states;
    if (!from && held && snapshotId != 0 && held->snapshotId == snapshotId) from = &held->states;
    if (!from) return out;
    for (const romm::SnapshotState& s : *from)
        if (s.core == core && !s.file.contentHash.empty()) out.push_back(s);
    std::stable_sort(out.begin(), out.end(),
                     [](const romm::SnapshotState& a, const romm::SnapshotState& b) {
                         const int64_t x = stateNameMs(a.file.fileName);
                         const int64_t y = stateNameMs(b.file.fileName);
                         return x != y ? x > y : a.slot < b.slot;
                     });
    return out;
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
// False when there is no file to send.
bool gather(const GameSession& sess, cache::OwedPush* p) {
    if (!sess.dirSaveRoot.empty()) {
        p->saveName = dirSaveZipName(sess.title);
        p->savePath = sess.saveDir + "/" + p->saveName;
        p->saveShape = kFolder;
    } else if (sess.fileSaves.size() == 1) {
        const cab::FileSaveState& f = sess.fileSaves.front();
        p->saveName = nameWithExtensionOf(sess.fsStem, f.spec.path);
        p->savePath = f.kept;
        p->saveShape = kSingle;
    } else if (!sess.fileSaves.empty()) {
        std::vector<cab::ZipFile> files;
        for (const cab::FileSaveState& f : sess.fileSaves)
            if (!f.atLaunch.empty()) files.push_back({f.spec.path, f.atLaunch});
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
    } else {
        p->saveName = sanitisedStem(sess.fsStem) + ".srm";
        p->savePath = sess.saveDir + "/" + saveRowName(sess.fsStem, "srm");
        p->saveShape = kSingle;
    }
    return !p->savePath.empty() && sizeOf(p->savePath) > 0;
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
    p->romFileId = sess.channelFileId;
    p->couldNotUseNewest = sess.couldNotUseNewest;
    cache::OwedPush earlier;
    if (cache::owedPush(user, sess.romId, &earlier)) {
        if (p->romFileId == 0) p->romFileId = earlier.romFileId;
        p->couldNotUseNewest = p->couldNotUseNewest || earlier.couldNotUseNewest;
    }
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
// A save folder a power cut left is gathered at a later start, so its files'
// own newest time stands when that is earlier.
int64_t madeAt(const GameSession& sess) {
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t newest = 0;
    for (const cab::DirEntry& e : sess.dirAtLaunch)
        newest = std::max(newest, e.mtimeNs / 1000000000);
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
    p.hasSave = true;
    if (!gather(sess, &p)) {
        std::fprintf(stderr, "[save] game %d: nothing on this console to send\n", sess.romId);
        return false;
    }
    aim(sess, user, &p);
    p.madeAt = madeAt(sess);
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
    std::fprintf(stderr, "[save] game %d: %s (%lld bytes)%s owed to RomM as a snapshot, %s%s\n",
                 sess.romId, p.saveName.c_str(), static_cast<long long>(sizeOf(p.savePath)),
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
    }
    // States alone are aimed from this session, where they were made; a
    // push with a save stays where its save goes.
    if (!p.hasSave) {
        cache::OwedPush aimed;
        aim(sess, user, &aimed);
        p.romFileId = aimed.romFileId;
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
                                        bool* holds) {
    std::vector<OfferedState> out;
    auto have = [](const std::string& p) {
        struct stat st;
        return !p.empty() && ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    };
    auto listed = [&out](const std::string& name) {
        for (const OfferedState& o : out)
            if (o.fileName == name) return true;
        return false;
    };
    cache::OwedPush owed;
    if (cache::owedPush(who, romId, &owed))
        for (const cache::OwedPush::State& s : owed.states)
            if (s.core == tag && have(s.path) && !listed(s.fileName))
                out.push_back({0, s.fileName, s.path, have(s.picturePath) ? s.picturePath : "",
                               s.madeMs});
    savemirror::Held held;
    const bool holding = savemirror::getHeld(who, romId, &held);
    if (holds) *holds = holding;
    if (holding)
        for (const romm::SnapshotState& s : held.states) {
            if (s.core != tag || listed(ownName(s.file.fileName))) continue;
            OfferedState o;
            o.id = s.file.id;
            o.fileName = s.file.fileName;
            o.madeMs = stateNameMs(s.file.fileName);
            o.picture = s.file.picturePath;
            const std::string mine = ownName(o.fileName);
            if (plainName(mine) && !stateDir.empty() && have(stateDir + "/" + mine)) {
                o.path = stateDir + "/" + mine;
                const size_t dot = o.path.rfind('.');
                const std::string png =
                    (dot == std::string::npos ? o.path : o.path.substr(0, dot)) + ".png";
                if (have(png)) o.picture = png;
            }
            out.push_back(std::move(o));
        }
    std::stable_sort(out.begin(), out.end(), [](const OfferedState& a, const OfferedState& b) {
        return a.madeMs > b.madeMs;
    });
    if (out.size() > kStatesKept) out.resize(kStatesKept);
    return out;
}

Server serverOf(romm::Client& client) {
    Server s;
    s.hasChannels = [] { return romm::heardServerChannels(); };
    s.channelFile = [&client](int romId, int* fileId, std::string* err) {
        std::vector<romm::RomFile> files;
        if (!client.fetchRomFiles(romId, &files, err)) return false;
        *fileId = 0;
        for (const romm::RomFile& f : files)
            if (f.channelFile) *fileId = f.id;
        return true;
    };
    s.channels = [&client](int fileId, std::vector<romm::Channel>* out, std::string* err,
                           bool* gone) { return client.fetchChannels({fileId}, out, err, gone); };
    s.push = [&client](const romm::SnapshotPush& push, romm::PushAnswer* answer,
                       std::string* err) { return client.pushSnapshot(push, answer, err); };
    s.saves = [&client](int romId, std::vector<romm::Asset>* rows, std::string* err) {
        return client.fetchSaves(romId, rows, err);
    };
    return s;
}

Sent send(const Server& server, const storage::User& who, int romId, std::string* why,
          const std::function<bool()>& inPlay) {
    auto stop = [&](Sent sent, const std::string& reason) {
        if (why) *why = reason;
        return sent;
    };
    cache::OwedPush owed;
    if (!cache::owedPush(who, romId, &owed)) return stop(Sent::NothingOwed, "nothing is owed");
    std::vector<uint8_t> bytes =
        owed.hasSave ? cab::readBytes(owed.savePath) : std::vector<uint8_t>();
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
    if (owed.hasSave && bytes.empty() && !made.empty()) {
        // The save's file is gone, its states are not: they go, with the
        // save of the snapshot they build on.
        std::fprintf(stderr, "[save] game %d's owed save has no file left; its states go alone\n",
                     romId);
        owed.hasSave = false;
    }
    if (bytes.empty() && made.empty()) {
        // The files it was owed from are gone (deleted by hand, or with the
        // person). Nothing left to send, so nothing is owed.
        cache::clearPushOwed(who, romId, owed.stamp);
        std::fprintf(stderr, "[save] game %d's owed snapshot has no file left; nothing to send\n",
                     romId);
        return stop(Sent::NothingOwed, "its files are gone");
    }
    if (!server.hasChannels())
        return stop(Sent::NotSent, "RomM has not said it has save channels");

    // What RomM is asked could not be had: refused for good when it answered
    // so (the game is gone, or hidden from this person), else tried again.
    std::string err;
    auto unasked = [&] { return stop(refusedForGood(err) ? Sent::Refused : Sent::NotSent, err); };

    // THE FILE THE GAME'S CHANNELS ARE KEYED TO, AND THE CHANNELS ON IT. The
    // file as the marker has it, asked for when it has none, and asked for
    // once more when RomM no longer knows that one: a scan of RomM's library
    // gives a file another id.
    int fileId = owed.romFileId;
    bool askedFile = false;
    std::vector<romm::Channel> channels;
    for (;;) {
        if (fileId == 0) {
            if (!server.channelFile(romId, &fileId, &err)) return unasked();
            askedFile = true;
            if (fileId == 0)
                return stop(Sent::Refused, "RomM names no file of this game for its channels");
        }
        bool gone = false;
        if (server.channels(fileId, &channels, &err, &gone)) break;
        if (!gone || askedFile) return unasked();
        fileId = 0;
    }

    // WHERE IT GOES, now that RomM's channels are known: the channel the
    // marker names, or the person's own under the marker's name; and never a
    // hardcore one, whose place the person's "softcore" takes, the push made
    // from what it was made from.
    romm::SnapshotPush push;
    push.romFileId = fileId;
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
    auto withBytes = [&] {
        push.hasSave = true;
        push.save.hash = romm::contentHash(bytes);
        push.save.fileName = owed.saveName;
        push.save.bytes = bytes;
        const bool zip = cab::looksLikeZip(bytes);
        push.saveShape = !zip ? kSingle : owed.saveShape == kFolder ? kFolder : kMulti;
    };
    if (owed.hasSave) withBytes();
    // THE STATES IT NAMES (the head of savepush.h): the ones made since,
    // with their bytes and pictures, then the ones in the snapshot it builds
    // on, by hash; the newest kStatesKept, one per hash, in slots "1" to "3".
    // None made: none named, and the bank is carried as it is.
    const std::string core = made.empty() ? std::string() : made.front().state->core;
    const std::string stateDir = made.empty() ? std::string() : dirOf(made.front().state->path);
    if (!made.empty()) {
        std::vector<romm::PushState> picked;
        std::set<std::string> hashes;
        auto add = [&](romm::PushState s) {
            if (picked.size() == kStatesKept || !hashes.insert(s.file.hash).second) return;
            s.core = core;
            s.slot = slotOf(picked.size());
            picked.push_back(std::move(s));
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
            add(std::move(s));
        }
        for (const romm::SnapshotState& b :
             bankOf(push.parentId ? push.parentId : push.expectedCurrentId, channels,
                    holding ? &holds : nullptr, core)) {
            romm::PushState s;
            s.file.hash = b.file.contentHash;
            s.file.fileName = b.file.fileName;
            add(std::move(s));
        }
        push.states = std::move(picked);
    }
    // What this push names, which RomM holds once it lands, wherever it kept it.
    std::set<std::string> namedHashes;
    for (const romm::PushState& s : push.states) namedHashes.insert(s.file.hash);
    // RomM LACKS SOME BYTES IT WAS NOT SENT: a state of the snapshot built
    // on that RomM no longer holds goes up from this console's copy, or
    // leaves the bank when there is none; the save from the owed file.
    auto supply = [&](const std::vector<std::string>& missing) {
        bool any = false;
        for (const std::string& key : missing) {
            if (key == "save") {
                if (!owed.hasSave || push.hasSave) return false;
                withBytes();
                any = true;
                continue;
            }
            auto at = std::find_if(push.states.begin(), push.states.end(),
                                   [&](const romm::PushState& s) {
                                       return key == "state:" + s.core + ":" + s.slot;
                                   });
            if (at == push.states.end() || !at->file.bytes.empty()) return false;
            std::vector<uint8_t> mine;
            const std::string own = ownName(at->file.fileName);
            if (plainName(own) && !stateDir.empty()) mine = cab::readBytes(stateDir + "/" + own);
            if (!mine.empty() && romm::contentHash(mine) == at->file.hash) {
                at->file.bytes = std::move(mine);
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
        romm::PushAnswer answer;
        if (!server.push(push, &answer, &err)) return stop(Sent::NotSent, err);
        using Kind = romm::PushAnswer::Kind;
        if (answer.kind == Kind::Made || answer.kind == Kind::Unchanged) {
            held = answer.snapshot;
            beside = false;
            break;
        }
        if (answer.kind == Kind::MovedOn) {
            // RomM has it, beside the current. On top only when this
            // console's save was made later, and never for a session that
            // could not use the newest save, nor for states alone, whose
            // save is older than the current they missed.
            held = answer.snapshot;
            beside = true;
            // The current's save dated by its own row (savepick::savedAt): a
            // current that changed only its states is newer than its save.
            if (answer.hasCurrent && owed.hasSave) {
                std::vector<romm::Channel> theirChannel(1);
                theirChannel[0].currentId = answer.current.id;
                theirChannel[0].current = answer.current;
                std::vector<romm::Asset> rows;
                if (server.saves(romId, &rows, &err)) savepick::dateSaves(&theirChannel, rows);
                answer.current = theirChannel[0].current;
            }
            const int64_t theirs =
                answer.hasCurrent ? isoMicros(savepick::savedAt(answer.current)) : 0;
            const bool later =
                owed.hasSave && (!answer.hasCurrent || owed.madeAt * 1000000 > theirs);
            std::fprintf(stderr, "[save] game %d: RomM's channel had moved on%s; this "
                                 "console's %s kept beside its current as snapshot %d%s\n",
                         romId,
                         answer.hasCurrent
                             ? (" to snapshot " + std::to_string(answer.current.id)).c_str()
                             : " and has no current",
                         owed.hasSave ? "save is" : "states are", held.id,
                         owed.couldNotUseNewest ? ", where it stays: the newest was a save this "
                                                  "console could not use"
                         : !owed.hasSave        ? ", where they stay: they carry no newer save"
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
            else if (owed.hasSave) withBytes();
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
    savemirror::getHeld(who, romId, &before);
    savemirror::Held now = savepick::heldAfter(before, channelId, held,
                                               static_cast<int64_t>(std::time(nullptr)));
    // When its save was made: this console's note of it for a save it sent,
    // and for states alone the save's own time, which they did not change.
    if (owed.hasSave) now.madeAt = owed.madeAt;
    else if (before.madeAt != 0 && before.saveHash == now.saveHash) now.madeAt = before.madeAt;
    now.beside = beside;
    savemirror::putHeld(who, romId, now);
    cache::clearPushOwed(who, romId, owed.stamp);
    std::fprintf(stderr,
                 "[save] game %d: snapshot %d %s channel \"%s\" on RomM (%s, %zu bytes; %zu "
                 "state(s) named)\n",
                 romId, held.id, beside ? "is kept beside the current of" : "is the current of",
                 push.label.c_str(), owed.hasSave ? owed.saveName.c_str() : "its states alone",
                 bytes.size(), push.states.size());
    // This console's copies of states it no longer offers: kept are the ones
    // in the snapshot now held, the ones this push named, and the ones in a
    // push owed since (the head of savepush.h).
    if (!made.empty()) {
        std::set<std::string> names, hashes = namedHashes;
        for (const romm::SnapshotState& s : now.states)
            if (s.core == core) {
                names.insert(ownName(s.file.fileName));
                hashes.insert(s.file.contentHash);
            }
        if (cache::OwedPush since; cache::owedPush(who, romId, &since))
            for (const cache::OwedPush::State& s : since.states)
                if (dirOf(s.path) == stateDir) names.insert(s.path.substr(stateDir.size() + 1));
        trimStates(stateDir, names, hashes, romId);
    }

    // The copy for offline: RomM's lists as they are now, and the bytes just
    // sent kept as that snapshot's save. Should the asking fail there is no
    // copy, and offline uses this console's own save, which is this one.
    std::vector<romm::Asset> rows;
    if (server.saves(romId, &rows, &err)) savemirror::putList(who, romId, rows);
    bool gone = false;
    if (server.channels(fileId, &channels, &err, &gone)) {
        savemirror::putChannels(who, romId, channels);
        if (held.hasSave && owed.hasSave) savemirror::putSnapshotSave(who, romId, held.save, bytes);
    }
    return stop(beside ? Sent::Beside : Sent::Landed, std::string());
}

}  // namespace savepush
