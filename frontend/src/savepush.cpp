#include "savepush.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

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
    // The new marker first, so the game is never without one; then the files
    // of its save still owed from before save channels, which this carries.
    if (cache::markPushOwed(user, p, sizeOf(p.savePath)).empty()) return false;
    for (const cache::Owed& o : cache::owed(user))
        if (o.romId == sess.romId && !o.isState && !o.isScreenshot)
            cache::clearPending(user, o.romId, o.fileName);
    std::fprintf(stderr, "[save] game %d: %s (%lld bytes) owed to RomM as a snapshot, %s%s\n",
                 sess.romId, p.saveName.c_str(), static_cast<long long>(sizeOf(p.savePath)),
                 p.channelId.empty()
                     ? ("into the channel named \"" + p.label + "\"").c_str()
                     : ("on snapshot " + std::to_string(p.expectedCurrentId) + " of channel \"" +
                        p.label + "\"").c_str(),
                 p.couldNotUseNewest ? ", never on top of the save it could not use" : "");
    return true;
}

void leave(GameSession& sess, const storage::User& user, Uploader& up) {
    if (mark(sess, user)) up.pushSnapshot(sess.romId, user);
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
    const std::vector<uint8_t> bytes =
        owed.hasSave ? cab::readBytes(owed.savePath) : std::vector<uint8_t>();
    if (inPlay && inPlay()) return stop(Sent::NotSent, "the game is being played");
    if (bytes.empty()) {
        // The file it was owed from is gone (deleted by hand, or with the
        // person). Nothing left to send, so nothing is owed.
        cache::clearPushOwed(who, romId, owed.stamp);
        std::fprintf(stderr, "[save] game %d's owed snapshot has no file left; nothing to send\n",
                     romId);
        return stop(Sent::NothingOwed, "its file is gone");
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
    {
        savemirror::Held holds;
        if (savemirror::getHeld(who, romId, &holds) && !push.channelId.empty() &&
            holds.channelId == push.channelId) {
            push.expectedCurrentId = holds.snapshotId;
            push.parentId = 0;
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
    withBytes();

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
            // could not use the newest save.
            held = answer.snapshot;
            beside = true;
            const int64_t theirs = answer.hasCurrent ? isoMicros(answer.current.createdAt) : 0;
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
                         : later ? ", and was made later, so it goes on top"
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
            if (!beside) withBytes();
            continue;
        }
        if (answer.kind == Kind::BytesMissing && !push.hasSave) {
            // RomM no longer holds the branch's bytes: sent again.
            withBytes();
            continue;
        }
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
    now.madeAt = owed.madeAt;
    now.beside = beside;
    savemirror::putHeld(who, romId, now);
    cache::clearPushOwed(who, romId, owed.stamp);
    std::fprintf(stderr, "[save] game %d: snapshot %d %s channel \"%s\" on RomM (%s, %zu bytes)\n",
                 romId, held.id, beside ? "is kept beside the current of" : "is the current of",
                 push.label.c_str(), owed.saveName.c_str(), bytes.size());

    // The copy for offline: RomM's lists as they are now, and the bytes just
    // sent kept as that snapshot's save. Should the asking fail there is no
    // copy, and offline uses this console's own save, which is this one.
    std::vector<romm::Asset> rows;
    if (server.saves(romId, &rows, &err)) savemirror::putList(who, romId, rows);
    bool gone = false;
    if (server.channels(fileId, &channels, &err, &gone)) {
        savemirror::putChannels(who, romId, channels);
        if (held.hasSave) savemirror::putSnapshotSave(who, romId, held.save, bytes);
    }
    return stop(beside ? Sent::Beside : Sent::Landed, std::string());
}

}  // namespace savepush
