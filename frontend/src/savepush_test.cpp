// `--save-push-test` (#346): leaving a game, with no server and no core. In a
// scratch root: what a session left, for each kind of save, as the marker a
// push is owed by; where each push is aimed; every answer RomM gives a push,
// settled against a stand-in for RomM that answers as scripted and keeps what
// it was sent; and the states a push carries (step 6): marked when made,
// offered back, named in their slots, and this console's copies kept or let
// go. Its own file, as `--romm-snapshots-test` and
// `--game-events-test` have theirs: the cases would otherwise fill
// savepush.cpp.

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <map>

#include "cache.h"
#include "dirsave.h"
#include "fileio.h"
#include "savemirror.h"
#include "savepick.h"
#include "savepush.h"
#include "saves.h"
#include "storage.h"
#include "uploader.h"

namespace savepush {
namespace {

const char kTag[] = "gambatte-native";
const char kChannel[] = "01937a3c-0000-7000-8000-00000000000a";
const char kSoftId[] = "01937a3c-0000-7000-8000-0000000000cc";
const int kFile = 4410;

std::vector<uint8_t> bytesOf(const char* text, size_t size) {
    std::vector<uint8_t> b(size, 0x00);
    for (size_t i = 0; text[i] && i < size; ++i) b[i] = static_cast<uint8_t>(text[i]);
    return b;
}

// RomM's time for a moment `seconds` from now, as it writes one.
std::string romTime(int64_t seconds) {
    const time_t t = std::time(nullptr) + static_cast<time_t>(seconds);
    struct tm utc;
    gmtime_r(&t, &utc);
    char buf[40];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S+00:00", &utc);
    return buf;
}

romm::Snapshot snapshot(int id, const std::string& channelId, const std::string& saveHash,
                        const std::string& at, const char* kind = "channel") {
    romm::Snapshot s;
    s.id = id;
    s.digest = "sha256:" + std::to_string(id);
    s.kind = kind;
    s.channelId = channelId;
    s.createdAt = at;
    s.deviceOwn = true;
    s.deviceClient = romm::kConsoleClient;
    s.hasSave = true;
    s.save.id = id * 10;
    s.save.fileName = "save";
    s.save.sizeBytes = 64;
    s.save.contentHash = saveHash;
    s.save.emulator = kTag;
    return s;
}

romm::Channel channel(const std::string& id, const char* label, const romm::Snapshot& current,
                      bool hardcore = false) {
    romm::Channel c;
    c.id = id;
    c.label = label;
    c.hardcore = hardcore;
    c.romFileId = kFile;
    c.updatedAt = current.createdAt;
    c.currentId = current.id;
    c.current = current;
    c.current.channelId = id;
    c.current.hardcore = hardcore;
    return c;
}

romm::PushAnswer made(const romm::Snapshot& s, bool unchanged = false) {
    romm::PushAnswer a;
    a.kind = unchanged ? romm::PushAnswer::Kind::Unchanged : romm::PushAnswer::Kind::Made;
    a.status = unchanged ? 200 : 201;
    a.snapshot = s;
    return a;
}

romm::PushAnswer movedOn(const romm::Snapshot& branch, const romm::Snapshot* current) {
    romm::PushAnswer a;
    a.kind = romm::PushAnswer::Kind::MovedOn;
    a.status = 409;
    a.snapshot = branch;
    a.snapshot.kind = "branch";
    a.hasCurrent = current != nullptr;
    if (current) a.current = *current;
    return a;
}

romm::PushAnswer answerOf(romm::PushAnswer::Kind kind, long status, const char* detail = "") {
    romm::PushAnswer a;
    a.kind = kind;
    a.status = status;
    a.detail = detail;
    if (kind == romm::PushAnswer::Kind::BytesMissing) a.missing = {"save"};
    return a;
}

// RomM, standing in: it answers each push with the next scripted answer (no
// answer at all once they run out), and keeps every push it was sent.
struct Stand {
    bool heard = true;
    int fileId = kFile;
    int staleFile = 0;   // a file id RomM no longer knows
    bool fileAnswers = true;
    std::vector<romm::Channel> channels;
    std::deque<romm::PushAnswer> answers;
    std::vector<romm::SnapshotPush> pushes;
    int fileAsks = 0, channelAsks = 0;
    std::function<void()> duringPush;
    std::vector<romm::Asset> rows;   // the game's saves RomM lists

    Server server() {
        Server s;
        s.hasChannels = [this] { return heard; };
        s.channelFile = [this](int, int* out, std::string* err) {
            ++fileAsks;
            if (!fileAnswers) {
                *err = "HTTP 404: Rom not found";
                return false;
            }
            *out = fileId;
            return true;
        };
        s.channels = [this](int file, std::vector<romm::Channel>* out, std::string* err,
                            bool* gone) {
            ++channelAsks;
            if (file == staleFile) {
                *gone = true;
                *err = "HTTP 404: ROM file not found";
                return false;
            }
            *out = channels;
            return true;
        };
        s.push = [this](const romm::SnapshotPush& push, romm::PushAnswer* answer,
                        std::string* err) {
            pushes.push_back(push);
            if (duringPush) duringPush();
            if (answers.empty()) {
                *err = "could not reach the server";
                return false;
            }
            *answer = answers.front();
            answers.pop_front();
            return true;
        };
        s.saves = [this](int, std::vector<romm::Asset>* out, std::string*) {
            *out = rows;
            return true;
        };
        return s;
    }
};

}  // namespace

int test() {
    std::printf("leaving a game\n\n");
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    char root[] = "/tmp/cabinetos-save-push-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[save push] could not make a scratch root\n");
        return 1;
    }
    setenv("CABINETOS_DRIVES", (std::string(root) + "/drives").c_str(), 1);
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[save push] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    storage::User u;
    u.id = 1;
    u.name = "push-test";
    storage::setCurrentUser(u);
    const std::vector<uint8_t> one = bytesOf("the save the session left", 64);
    const std::vector<uint8_t> two = bytesOf("the cartridge beside it", 96);
    const std::vector<uint8_t> older = bytesOf("the save it started from", 64);
    const int64_t now = static_cast<int64_t>(std::time(nullptr));

    // ---- What a session left -------------------------------------------------
    std::printf("what a session left, in %s\n", root);
    int game = 100;
    // A session of a core with a battery, its copy written as syncSave does.
    auto battery = [&](int romId, const std::vector<uint8_t>& save) {
        GameSession s;
        s.romId = romId;
        s.title = "Game";
        s.fsStem = "Game (USA)";
        s.saveTag = kTag;
        s.saveDir = storage::savesDir(u, "gb", romId, "gambatte");
        s.channelFileId = kFile;
        cab::writeBytes(s.saveDir + "/" + saveRowName(s.fsStem, "srm"), save);
        s.saveAtLaunch = save;
        s.saveChanged = true;
        return s;
    };
    cache::OwedPush p;
    {
        GameSession s = battery(game, one);
        check(mark(s, u) && cache::owedPush(u, game, &p) && p.hasSave &&
                  p.saveName == "Game (USA).srm" && p.saveShape == "SINGLE" &&
                  cab::readBytes(p.savePath) == one && p.emulator == kTag &&
                  p.romFileId == kFile && p.channelId.empty() && p.label == kDefaultChannel &&
                  p.expectedCurrentId == 0 && p.parentId == 0 && !p.couldNotUseNewest &&
                  p.madeAt >= now && p.madeAt <= now + 5 && !s.saveChanged,
              "a battery: the console's copy as it is, under the emulator's own name, into "
              "the person's \"default\" when the console holds no snapshot");
        check(!mark(s, u), "gathered once: a second call with nothing new marks nothing");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        GameSession s = battery(++game, one);
        s.saveChanged = false;
        check(!mark(s, u) && !cache::isPushOwed(u, game),
              "nothing changed: nothing owed, nothing sent");
        s.saveChanged = true;
        s.saveTag.clear();
        check(!mark(s, u) && !cache::isPushOwed(u, game),
              "a save with no tag it may travel under stays on the console");
    }
    {
        // One file a core writes (FinalBurn Neo's), gathered by syncFileSaves.
        GameSession s;
        s.romId = ++game;
        s.title = "The King of Fighters '98";
        s.fsStem = "kof98 (World)";
        s.saveTag = "fbneo-native";
        s.saveDir = storage::savesDir(u, "arcade", game, "fbneo");
        cab::FileSaveState f;
        f.spec.path = "fbneo/kof98.fs";
        f.spec.untouched = catalog::Untouched::Uniform;
        f.path = s.saveDir + "/fbneo/kof98.fs";
        f.atLaunch = older;
        f.hadOne = true;
        s.fileSaves = {f};
        cab::writeBytes(f.path, older);
        syncFileSaves(s);
        check(!s.saveChanged && !mark(s, u), "a core's file left as it was: nothing owed");
        cab::writeBytes(f.path, one);
        syncFileSaves(s);
        check(s.fileSaves[0].atLaunch == one && mark(s, u) && cache::owedPush(u, game, &p) &&
                  p.saveName == "kof98 (World).fs" && p.saveShape == "SINGLE" &&
                  p.savePath == f.path && cab::readBytes(p.savePath) == one,
              "one file a core wrote: the file itself, named after the game with the core's "
              "own extension");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        // Two files (Sega CD's backup RAM and its cartridge): one zip.
        GameSession s;
        s.romId = ++game;
        s.title = "Lunar";
        s.fsStem = "Lunar";
        s.saveTag = "gpgx-native";
        s.saveDir = storage::savesDir(u, "segacd", game, "genesis_plus_gx");
        cab::FileSaveState internal, cart;
        internal.spec.path = "Lunar.brm";
        internal.spec.captureSuffix = ".brm";
        internal.spec.captureExclude = "cart.brm";
        internal.path = s.saveDir + "/Lunar.brm";
        internal.atLaunch = older;
        internal.hadOne = true;
        cart.spec.path = "4Mbit_cart.brm";
        cart.spec.captureSuffix = "cart.brm";
        cart.spec.region = "cart";
        cart.path = s.saveDir + "/4Mbit_cart.brm";
        cart.atLaunch = two;
        cart.hadOne = true;
        s.fileSaves = {internal, cart};
        cab::writeBytes(internal.path, one);   // the game saved to its backup RAM
        cab::writeBytes(cart.path, two);       // and left the cartridge alone
        syncFileSaves(s);
        const bool marked = mark(s, u) && cache::owedPush(u, game, &p);
        std::vector<cab::ZipFile> in;
        std::string zerr;
        const bool read = marked && cab::readZip(cab::readBytes(p.savePath), &in, &zerr);
        check(read && p.saveName == "Lunar.zip" && p.saveShape == "MULTI" && in.size() == 2 &&
                  in[0].relPath == "Lunar.brm" && in[0].bytes == one &&
                  in[1].relPath == "4Mbit_cart.brm" && in[1].bytes == two,
              "two files: one zip holding both, each under the path its core reads it from, "
              "the one that did not change with it");
        // And a start reads that zip back into the two files.
        const std::string other = storage::savesDir(u, "segacd", game + 5000, "genesis_plus_gx");
        SaveFetch fetched;
        fetched.romId = game + 5000;
        fetched.asked = fetched.listed = true;
        fetched.mark = savemirror::mark();
        fetched.pick.from = savepick::Pick::From::Channel;
        fetched.pick.channel.current.save.fileName = p.saveName;
        fetched.picked = cab::readBytes(p.savePath);
        restoreFileSaves({internal.spec, cart.spec}, "Lunar", other, "gpgx-native", fetched);
        check(cab::readBytes(other + "/Lunar.brm") == one &&
                  cab::readBytes(other + "/4Mbit_cart.brm") == two,
              "which a start on another console writes back as the two files");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        // DREAMCAST'S CARD, the one save kept in the system folder, where
        // Flycast alone looks (the console's own entry, catalog::saveFiles):
        // a start puts the person's copy there; leaving copies the card back
        // to the person's folder and takes it out of the system folder; that
        // copy is what goes up; and a start on another console puts RomM's
        // card back in the system folder.
        const std::vector<catalog::SaveFile> specs =
            catalog::saveFiles("dc", "flycast", "Ikaruga (Japan).chd", false);
        GameSession s;
        s.romId = ++game;
        s.title = "Ikaruga";
        s.fsStem = "Ikaruga (Japan)";
        s.saveTag = "flycast-native";
        s.saveDir = storage::savesDir(u, "dc", game, "flycast");
        const std::string card = storage::biosDir() + "/dc/vmu_save_A1.bin";
        const std::string mine = s.saveDir + "/" + saveRowName(s.fsStem, "srm");
        cab::writeBytes(mine, older);
        s.fileSaves = restoreFileSaves(specs, s.fsStem, s.saveDir, "flycast-native", SaveFetch{});
        check(specs.size() == 1 && specs[0].inSystemDir && s.fileSaves.size() == 1 &&
                  cab::readBytes(card) == older,
              "Dreamcast: a start puts the person's card in the system folder");
        cab::writeBytes(card, one);   // the game saved to its card
        syncFileSaves(s);
        check(s.saveChanged && cab::readBytes(mine) == one && cab::readBytes(card).empty(),
              "leaving copies the card to the person's folder and takes it out of the system "
              "folder");
        const bool marked = mark(s, u) && cache::owedPush(u, game, &p);
        check(marked && p.savePath == mine && cab::readBytes(p.savePath) == one &&
                  p.saveName == "Ikaruga (Japan).bin" && p.saveShape == "SINGLE",
              "and that copy is what goes up, as one file");
        const std::string other = storage::savesDir(u, "dc", game + 5000, "flycast");
        SaveFetch fetched;
        fetched.romId = game + 5000;
        fetched.asked = fetched.listed = true;
        fetched.mark = savemirror::mark();
        fetched.pick.from = savepick::Pick::From::Channel;
        fetched.pick.channel.current.save.fileName = p.saveName;
        fetched.picked = cab::readBytes(p.savePath);
        restoreFileSaves(specs, s.fsStem, other, "flycast-native", fetched);
        check(cab::readBytes(card) == one &&
                  cab::readBytes(other + "/" + saveRowName(s.fsStem, "srm")) == one,
              "a start on another console puts RomM's card in the system folder, and keeps "
              "the person's copy beside the game");
        cab::removeFile(card);
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        // A save folder (PSP's memory stick), gathered by syncDirSave.
        GameSession s;
        s.romId = ++game;
        s.title = "Lumines";
        s.fsStem = "Lumines (USA)";
        s.saveTag = "ppsspp-native";
        s.saveDir = storage::savesDir(u, "psp", game, "ppsspp");
        s.dirSaveRoot = s.saveDir + "/PSP/SAVEDATA";
        s.dirSaveCore = "ppsspp";
        s.dirSavePlatform = "psp";
        cab::writeBytes(s.dirSaveRoot + "/ULUS10002/PARAM.SFO", one);
        cab::writeBytes(s.dirSaveRoot + "/ULUS10002/DATA.BIN", two);
        syncDirSave(s);
        const bool marked = s.saveChanged && mark(s, u) && cache::owedPush(u, game, &p);
        std::vector<cab::ZipFile> in;
        std::string zerr;
        const bool read = marked && cab::readZip(cab::readBytes(p.savePath), &in, &zerr);
        check(read && p.saveName == "Lumines.zip" && p.saveShape == "FOLDER" && in.size() == 2 &&
                  in[0].relPath == "ULUS10002/DATA.BIN" && in[0].bytes == two &&
                  in[1].relPath == "ULUS10002/PARAM.SFO" && in[1].bytes == one,
              "a save folder: the zip of the whole folder");
        syncDirSave(s);
        check(!s.saveChanged && !mark(s, u), "the folder untouched since: nothing new owed");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        // A zip's names outside ASCII are marked UTF-8, so RomM hashes the
        // names this console does.
        std::vector<uint8_t> zip;
        std::string zerr;
        const std::string accent = "Pok\xC3\xA9mon/save.bin";
        const bool zipped = cab::zipFiles({{accent, one}, {"plain.bin", two}}, &zip, &zerr);
        const bool marked = zipped && zip.size() > 8 && (zip[7] & 0x08) != 0;
        const std::string lines = accent + ":" + romm::contentHash(one) + "\n" + "plain.bin:" +
                                  romm::contentHash(two);
        check(zipped && marked &&
                  romm::contentHash(zip) ==
                      romm::contentHash(std::vector<uint8_t>(lines.begin(), lines.end())),
              "a zip the console makes marks a name outside ASCII as UTF-8, and hashes as "
              "RomM hashes it");
        std::vector<cab::ZipFile> back;
        const std::string into = std::string(root) + "/unzipped";
        const bool read = cab::readZip(zip, &back, &zerr);
        const bool unpacked = cab::unzipTree(zip, into, &zerr);
        check(read && back.size() == 2 && back[0].relPath == accent && back[0].bytes == one &&
                  back[1].relPath == "plain.bin" && unpacked &&
                  cab::readBytes(into + "/" + accent) == one &&
                  cab::readBytes(into + "/plain.bin") == two,
              "and reads such a zip back, and unpacks it, with every file under its name");
        // The same zip with the marked name made no UTF-8 at all (two bytes
        // changed, in both places a zip keeps a name): read whole or refused
        // with a reason, never read as an archive with fewer files.
        std::vector<uint8_t> bad = zip;
        int changed = 0;
        for (size_t i = 0; i + 1 < bad.size(); ++i)
            if (bad[i] == 0xC3 && bad[i + 1] == 0xA9) {
                bad[i] = 0xFF;
                bad[i + 1] = 0xFE;
                ++changed;
            }
        zerr.clear();
        const bool readBad = cab::readZip(bad, &back, &zerr);
        check(changed == 2 && (readBad ? back.size() == 2 : !zerr.empty()),
              "a zip whose marked name is not UTF-8 is read whole or refused with a reason, "
              "never taken for a shorter archive");
    }
    {
        // A save folder file named in Shift-JIS (日本.gci, in a folder) goes
        // into the zip unmarked, as its bytes; RomM reads that name as CP437
        // ("ô·û{"), and the console reads it back, and unpacks it, as the
        // same bytes.
        std::vector<uint8_t> zip;
        std::string zerr;
        const std::string sjis = "MemoryCard/\x93\xFA\x96\x7B.gci";
        const bool zipped = cab::zipFiles({{sjis, one}, {"plain.bin", two}}, &zip, &zerr);
        std::map<std::string, bool> marks;
        const bool unmarked = zipped && cab::zipNameMarks(zip, &marks) && marks.size() == 2 &&
                              marks.count(sjis) == 1 && !marks[sjis];
        const std::string lines = "MemoryCard/\xC3\xB4\xC2\xB7\xC3\xBB{.gci:" +
                                  romm::contentHash(one) + "\n" + "plain.bin:" +
                                  romm::contentHash(two);
        check(unmarked && romm::contentHash(zip) ==
                              romm::contentHash(std::vector<uint8_t>(lines.begin(), lines.end())),
              "a name that is not UTF-8 goes into the zip unmarked, as its bytes, and hashes "
              "as RomM reads it, as CP437");
        std::vector<cab::ZipFile> back;
        const std::string into = std::string(root) + "/unzipped-sjis";
        const bool read = cab::readZip(zip, &back, &zerr);
        const bool unpacked = cab::unzipTree(zip, into, &zerr);
        check(read && back.size() == 2 && back[0].relPath == sjis && back[0].bytes == one &&
                  unpacked && cab::readBytes(into + "/" + sjis) == one &&
                  cab::readBytes(into + "/plain.bin") == two,
              "and comes back out of readZip and unzipTree as the same bytes");
        // Marked only when Python's strict decoder, RomM's, would read it:
        // an overlong "/", a surrogate and a code point past U+10FFFF stay
        // unmarked; a four-byte character is marked.
        const std::vector<cab::ZipFile> odd = {{"\xC0\xAF.sav", one},
                                               {"\xED\xA0\x80.sav", one},
                                               {"\xF4\x90\x80\x80.sav", one},
                                               {"\xF0\x9F\x8E\xAE.sav", one}};
        marks.clear();
        check(cab::zipFiles(odd, &zip, &zerr) && cab::zipNameMarks(zip, &marks) &&
                  marks.size() == 4 && !marks["\xC0\xAF.sav"] && !marks["\xED\xA0\x80.sav"] &&
                  !marks["\xF4\x90\x80\x80.sav"] && marks["\xF0\x9F\x8E\xAE.sav"],
              "a name is marked UTF-8 only when it is UTF-8 by the rule RomM reads it by");
    }
    {
        // A save file of the game still owed from before save channels is
        // carried by the snapshot; a state's marker and another game's stay.
        GameSession s = battery(++game, one);
        cache::Owed oldSave, state, otherGame;
        oldSave.romId = game;
        oldSave.emulator = kTag;
        oldSave.fileName = "Game (USA) (Cabinet).srm";
        oldSave.localPath = s.saveDir + "/Game (USA) (Cabinet).srm";
        state = oldSave;
        state.isState = true;
        state.fileName = "Game.state1";
        otherGame = oldSave;
        otherGame.romId = game + 9000;
        cache::markPending(u, oldSave, 64);
        cache::markPending(u, state, 64);
        cache::markPending(u, otherGame, 64);
        auto owes = [&](int romId, const std::string& name) {
            for (const cache::Owed& o : cache::owed(u))
                if (o.romId == romId && o.fileName == name) return true;
            return false;
        };
        check(mark(s, u) && cache::isPushOwed(u, game) && !owes(game, oldSave.fileName) &&
                  owes(game, state.fileName) && owes(game + 9000, otherGame.fileName),
              "a save file of the game owed from before save channels stops being owed once "
              "its snapshot is; a state's marker and another game's stay");
        cache::owedPush(u, game, &p);
        cache::clearPushOwed(u, game, p.stamp);
        cache::clearPending(u, game, state.fileName);
        cache::clearPending(u, game + 9000, otherGame.fileName);
    }

    // ---- Where it is aimed ------------------------------------------------------
    std::printf("\nwhere a push is aimed\n");
    const std::string hashOne = romm::contentHash(one);
    const std::string hashOlder = romm::contentHash(older);
    // The note of the snapshot the console holds: `id` in `channelId`.
    auto hold = [&](int romId, const std::string& channelId, int id) {
        savemirror::putHeld(u, romId,
                            savepick::heldAfter(savemirror::Held{}, channelId,
                                                snapshot(id, channelId, hashOlder, romTime(-3600)),
                                                now - 3600));
    };
    {
        GameSession s = battery(++game, one);
        hold(game, kChannel, 40);
        savemirror::putChannels(u, game,
                                {channel(kChannel, "second run",
                                         snapshot(40, kChannel, hashOlder, romTime(-3600)))});
        s.startChannelId = kChannel;
        s.startSnapshotId = 40;
        check(mark(s, u) && cache::owedPush(u, game, &p) && p.channelId == kChannel &&
                  p.label == "second run" && p.expectedCurrentId == 40 && p.parentId == 0,
              "started from a channel's snapshot: into that channel, on that snapshot, under "
              "the channel's own name");
        // The next session starts from the console's own save, still owed,
        // and could not use the newest on RomM.
        GameSession next = battery(game, two);
        next.couldNotUseNewest = true;
        const std::string first = p.stamp;
        check(mark(next, u) && cache::owedPush(u, game, &p) && p.stamp != first &&
                  p.channelId == kChannel && p.expectedCurrentId == 40 &&
                  p.label == "second run" && p.couldNotUseNewest &&
                  cab::readBytes(p.savePath) == two,
              "a later session while that is unsent: the newer save, on the same snapshot");
        GameSession third = battery(game, one);
        check(mark(third, u) && cache::owedPush(u, game, &p) && p.couldNotUseNewest,
              "and what an unsent one could not go on top of, the next cannot either");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        GameSession s = battery(++game, one);
        hold(game, kChannel, 41);
        check(mark(s, u) && cache::owedPush(u, game, &p) && p.channelId == kChannel &&
                  p.expectedCurrentId == 41 && p.label == kDefaultChannel,
              "started from the console's own save: on the snapshot the console holds");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        GameSession s = battery(++game, one);
        hold(game, kChannel, 42);
        s.startChannelId = kChannel;
        s.startSnapshotId = 42;
        s.startHardcore = true;
        check(mark(s, u) && cache::owedPush(u, game, &p) && p.channelId.empty() &&
                  p.label == kSoftcoreChannel && p.expectedCurrentId == 0 && p.parentId == 42,
              "started from a hardcore channel: never into it, into \"softcore\", made from "
              "the snapshot it started from");
        cache::clearPushOwed(u, game, p.stamp);
    }

    // ---- RomM's answers ---------------------------------------------------------
    std::printf("\nRomM's answers\n");
    // A game with a battery save owed, its session set up by `aimed`.
    auto owe = [&](const std::function<void(GameSession&)>& aimed = {}) {
        GameSession s = battery(++game, one);
        if (aimed) aimed(s);
        mark(s, u);
        cache::owedPush(u, game, &p);
        return game;
    };
    savemirror::Held held;
    std::string why;
    {   // Made, into a channel RomM makes.
        const int g = owe();
        Stand romm;
        romm.answers = {made(snapshot(50, kChannel, hashOne, romTime(1)))};
        const uint64_t before = savemirror::mark();
        const Sent sent = send(romm.server(), u, g, &why);
        const bool single = romm.pushes.size() == 1;
        const romm::SnapshotPush sentPush = single ? romm.pushes[0] : romm::SnapshotPush{};
        check(sent == Sent::Landed && single && sentPush.romFileId == kFile &&
                  sentPush.channelId.empty() && sentPush.label == kDefaultChannel &&
                  sentPush.expectedCurrentId == 0 && sentPush.parentId == 0 &&
                  sentPush.hasSave && sentPush.save.bytes == one &&
                  sentPush.save.hash == hashOne && sentPush.save.fileName == "Game (USA).srm" &&
                  sentPush.saveShape == "SINGLE" && sentPush.emulator == kTag &&
                  sentPush.states.empty(),
              "no channel yet: one push, the save's bytes named by RomM's hash, under the "
              "console's tag, into a channel named \"default\" for RomM to make");
        check(!cache::isPushOwed(u, g) && savemirror::getHeld(u, g, &held) &&
                  held.channelId == kChannel && held.snapshotId == 50 &&
                  held.saveHash == hashOne && !held.beside && held.madeAt == p.madeAt &&
                  held.hashesHad.size() == 1 && savemirror::forgottenSince(g, before),
              "it landed: no longer owed, the snapshot is the one this console holds with its "
              "save's hash, and the copy of RomM's saves a start may be reading is stale");
        check(send(romm.server(), u, g, &why) == Sent::NothingOwed && romm.pushes.size() == 1,
              "asked again with nothing owed: nothing is sent");
    }
    {   // Made, on the snapshot held; the copy for offline.
        const int g = owe([&](GameSession& s) { hold(s.romId, kChannel, 60); });
        Stand romm;
        const romm::Snapshot top = snapshot(61, kChannel, hashOne, romTime(1));
        romm.channels = {channel(kChannel, kDefaultChannel,
                                 snapshot(60, kChannel, hashOlder, romTime(-3600)))};
        romm.answers = {made(top)};
        romm.duringPush = [&] { romm.channels = {channel(kChannel, kDefaultChannel, top)}; };
        const Sent sent = send(romm.server(), u, g, &why);
        std::vector<romm::Channel> seen;
        check(sent == Sent::Landed && romm.pushes.size() == 1 &&
                  romm.pushes[0].channelId == kChannel && romm.pushes[0].expectedCurrentId == 60 &&
                  romm.pushes[0].parentId == 0 && savemirror::getHeld(u, g, &held) &&
                  held.snapshotId == 61 && held.hashesHad.size() == 2 &&
                  held.hashesHad[0] == hashOlder,
              "on the snapshot the console holds: that channel, that current expected, and "
              "the hash it held before kept among those it has had");
        check(savemirror::getChannels(u, g, &seen) && seen.size() == 1 && seen[0].currentId == 61 &&
                  savemirror::getSnapshotSave(u, g, top.save) == one,
              "the copy for offline is RomM's channels as they are now, with the bytes just sent");
    }
    {   // Unchanged.
        const int g = owe([&](GameSession& s) { hold(s.romId, kChannel, 62); });
        Stand romm;
        romm.answers = {made(snapshot(63, kChannel, hashOne, romTime(-5)), /*unchanged=*/true)};
        check(send(romm.server(), u, g, &why) == Sent::Landed && romm.pushes.size() == 1 &&
                  !cache::isPushOwed(u, g) && savemirror::getHeld(u, g, &held) &&
                  held.snapshotId == 63,
              "RomM already holds exactly this (200): landed, and that current is the one held");
    }
    {   // Moved on, this console's save made later: on top, no bytes again.
        const int g = owe([&](GameSession& s) { hold(s.romId, kChannel, 70); });
        Stand romm;
        const romm::Snapshot theirs = snapshot(71, kChannel, "ffff", romTime(-600));
        romm.answers = {movedOn(snapshot(72, kChannel, hashOne, romTime(0)), &theirs),
                        made(snapshot(73, kChannel, hashOne, romTime(1)))};
        const Sent sent = send(romm.server(), u, g, &why);
        check(sent == Sent::Landed && romm.pushes.size() == 2 &&
                  romm.pushes[0].expectedCurrentId == 70 &&
                  romm.pushes[0].hasSave && romm.pushes[1].channelId == kChannel &&
                  romm.pushes[1].expectedCurrentId == 71 && romm.pushes[1].parentId == 72 &&
                  !romm.pushes[1].hasSave && romm.pushes[1].save.bytes.empty() &&
                  romm.pushes[1].states.empty(),
              "the channel moved on and this console's save was made later: a second push "
              "puts the kept one on top, naming it and the current it missed, with no bytes");
        check(!cache::isPushOwed(u, g) && savemirror::getHeld(u, g, &held) &&
                  held.snapshotId == 73 && !held.beside && held.saveHash == hashOne,
              "and the one on top is the one held");
    }
    {   // Moved on, the other save made later: left beside.
        const int g = owe([&](GameSession& s) { hold(s.romId, kChannel, 74); });
        Stand romm;
        const romm::Snapshot theirs = snapshot(75, kChannel, "ffff", romTime(600));
        romm.answers = {movedOn(snapshot(76, kChannel, hashOne, romTime(601)), &theirs)};
        const Sent sent = send(romm.server(), u, g, &why);
        check(sent == Sent::Beside && romm.pushes.size() == 1 && !cache::isPushOwed(u, g) &&
                  savemirror::getHeld(u, g, &held) && held.snapshotId == 76 && held.beside &&
                  held.madeAt == p.madeAt && held.saveHash == hashOne,
              "the channel moved on and the other save was made later: left as RomM kept it, "
              "no second push, no longer owed, and noted as kept beside the current");
        // What the next start makes of it (savepick.h).
        savepick::Facts f;
        f.tag = kTag;
        f.haveHeld = true;
        f.held = held;
        f.channels = {channel(kChannel, kDefaultChannel, theirs)};
        check(savepick::pick(f).from == savepick::Pick::From::Channel,
              "and the next start takes the one made later");
        f.channels = {channel(kChannel, kDefaultChannel,
                              snapshot(75, kChannel, "ffff", romTime(-600)))};
        f.oldRowAt = romTime(-7200);
        check(savepick::pick(f).from == savepick::Pick::From::Own,
              "but never a save made before this console's own, kept beside");
    }
    {   // Moved on, a session that could not use the newest: always left.
        const int g = owe([&](GameSession& s) {
            hold(s.romId, kChannel, 77);
            s.couldNotUseNewest = true;
        });
        Stand romm;
        const romm::Snapshot theirs = snapshot(78, kChannel, "ffff", romTime(-600));
        romm.answers = {movedOn(snapshot(79, kChannel, hashOne, romTime(0)), &theirs)};
        check(send(romm.server(), u, g, &why) == Sent::Beside && romm.pushes.size() == 1 &&
                  savemirror::getHeld(u, g, &held) && held.snapshotId == 79 && held.beside,
              "a session that could not use the newest save: left as RomM kept it, though "
              "made later");
    }
    {   // Moved on, and no current to compare: on top.
        const int g = owe([&](GameSession& s) { hold(s.romId, kChannel, 80); });
        Stand romm;
        romm.answers = {movedOn(snapshot(81, kChannel, hashOne, romTime(0)), nullptr),
                        made(snapshot(82, kChannel, hashOne, romTime(1)))};
        check(send(romm.server(), u, g, &why) == Sent::Landed && romm.pushes.size() == 2 &&
                  romm.pushes[1].expectedCurrentId == 0 && romm.pushes[1].parentId == 81,
              "the channel has no current any more: the kept one goes on top of nothing");
    }
    {   // Moved on twice: the second current was made later.
        const int g = owe([&](GameSession& s) { hold(s.romId, kChannel, 83); });
        Stand romm;
        const romm::Snapshot first = snapshot(84, kChannel, "ffff", romTime(-600));
        const romm::Snapshot second = snapshot(86, kChannel, "eeee", romTime(900));
        romm.answers = {movedOn(snapshot(85, kChannel, hashOne, romTime(0)), &first),
                        movedOn(snapshot(87, kChannel, hashOne, romTime(901)), &second)};
        check(send(romm.server(), u, g, &why) == Sent::Beside && romm.pushes.size() == 2 &&
                  savemirror::getHeld(u, g, &held) && held.snapshotId == 87 && held.beside,
              "the channel moved on again while it went on top, to a save made later: left "
              "beside that one");
    }
    {   // Bytes missing for the push with none.
        const int g = owe([&](GameSession& s) { hold(s.romId, kChannel, 88); });
        Stand romm;
        const romm::Snapshot theirs = snapshot(89, kChannel, "ffff", romTime(-600));
        romm.answers = {movedOn(snapshot(90, kChannel, hashOne, romTime(0)), &theirs),
                        answerOf(romm::PushAnswer::Kind::BytesMissing, 400),
                        made(snapshot(91, kChannel, hashOne, romTime(1)))};
        check(send(romm.server(), u, g, &why) == Sent::Landed && romm.pushes.size() == 3 &&
                  romm.pushes[2].hasSave && romm.pushes[2].save.bytes == one &&
                  romm.pushes[2].parentId == 90,
              "RomM no longer holds the kept one's bytes (400): sent again with them");
    }
    {   // A hardcore channel is never saved into.
        const int g = owe([&](GameSession& s) { hold(s.romId, kChannel, 92); });
        Stand romm;
        romm.channels = {channel(kChannel, kDefaultChannel,
                                 snapshot(93, kChannel, "ffff", romTime(-600)), /*hardcore=*/true)};
        romm.answers = {made(snapshot(94, kSoftId, hashOne, romTime(1)))};
        check(send(romm.server(), u, g, &why) == Sent::Landed && romm.pushes.size() == 1 &&
                  romm.pushes[0].channelId.empty() && romm.pushes[0].label == kSoftcoreChannel &&
                  romm.pushes[0].expectedCurrentId == 0 && romm.pushes[0].parentId == 92 &&
                  romm.pushes[0].hasSave && savemirror::getHeld(u, g, &held) &&
                  held.channelId == kSoftId,
              "the channel has turned hardcore: nothing is pushed into it; the save goes into "
              "a channel named \"softcore\", made from what it was made from");
        const int g2 = owe([&](GameSession& s) {
            hold(s.romId, kChannel, 95);
            s.startChannelId = kChannel;
            s.startSnapshotId = 95;
            s.startHardcore = true;
        });
        romm.channels.push_back(
            channel(kSoftId, kSoftcoreChannel, snapshot(94, kSoftId, hashOlder, romTime(-7200))));
        const romm::Snapshot softWas = romm.channels.back().current;
        romm.pushes.clear();
        romm.answers = {movedOn(snapshot(96, kSoftId, hashOne, romTime(0)), &softWas),
                        made(snapshot(97, kSoftId, hashOne, romTime(1)))};
        check(send(romm.server(), u, g2, &why) == Sent::Landed && romm.pushes.size() == 2 &&
                  romm.pushes[0].channelId == kSoftId && romm.pushes[0].parentId == 95 &&
                  romm.pushes[0].expectedCurrentId == 0 && romm.pushes[1].channelId == kSoftId &&
                  romm.pushes[1].expectedCurrentId == 94 && romm.pushes[1].parentId == 96,
              "a session that started from a hardcore channel, with a \"softcore\" there "
              "already: into that one, and on top of its older save");
    }
    {   // RomM refuses an ordinary save over a hardcore current (409).
        const int g = owe([&](GameSession& s) { hold(s.romId, kChannel, 98); });
        Stand romm;
        romm.answers = {answerOf(romm::PushAnswer::Kind::HardcoreChannel, 409),
                        made(snapshot(99, kSoftId, hashOne, romTime(1)))};
        check(send(romm.server(), u, g, &why) == Sent::Landed && romm.pushes.size() == 2 &&
                  romm.pushes[1].label == kSoftcoreChannel && romm.pushes[1].channelId.empty() &&
                  romm.pushes[1].parentId == 98 && romm.pushes[1].hasSave &&
                  romm.pushes[1].save.bytes == one,
              "RomM says the channel's current is hardcore (409): the save goes into "
              "\"softcore\" instead, with its bytes");
    }
    {   // Refused for good: still owed.
        const int g = owe([&](GameSession& s) { hold(s.romId, kChannel, 110); });
        Stand romm;
        romm.answers = {answerOf(romm::PushAnswer::Kind::Refused, 422, "the hash does not match")};
        check(send(romm.server(), u, g, &why) == Sent::Refused && cache::isPushOwed(u, g) &&
                  savemirror::getHeld(u, g, &held) && held.snapshotId == 110 &&
                  why.find("422") != std::string::npos,
              "RomM refuses it (422): still owed, the snapshot held as it was, and the "
              "reason said");
        cache::owedPush(u, g, &p);
        cache::clearPushOwed(u, g, p.stamp);
    }
    {   // A channel or snapshot RomM no longer has.
        const int g = owe([&](GameSession& s) { hold(s.romId, kChannel, 111); });
        Stand romm;
        romm.answers = {answerOf(romm::PushAnswer::Kind::Refused, 404, "Parent snapshot not found"),
                        made(snapshot(112, kSoftId, hashOne, romTime(1)))};
        check(send(romm.server(), u, g, &why) == Sent::Landed && romm.pushes.size() == 2 &&
                  romm.pushes[1].channelId.empty() && romm.pushes[1].label == kDefaultChannel &&
                  romm.pushes[1].expectedCurrentId == 0 && romm.pushes[1].parentId == 0 &&
                  romm.pushes[1].hasSave,
              "the snapshot it was made from is gone from RomM (404): sent once more as a save "
              "of its own, into the person's channel of that name");
        const int g2 = owe([&](GameSession& s) { hold(s.romId, kChannel, 113); });
        romm.pushes.clear();
        romm.answers = {answerOf(romm::PushAnswer::Kind::Refused, 404, "Channel not found"),
                        answerOf(romm::PushAnswer::Kind::Refused, 404, "Channel not found")};
        check(send(romm.server(), u, g2, &why) == Sent::Refused && romm.pushes.size() == 2 &&
                  cache::isPushOwed(u, g2),
              "refused that way twice: not asked a third time, still owed");
        cache::owedPush(u, g2, &p);
        cache::clearPushOwed(u, g2, p.stamp);
    }
    {   // No answer; no channels on the server; a game in play.
        const int g = owe();
        Stand romm;
        check(send(romm.server(), u, g, &why) == Sent::NotSent && cache::isPushOwed(u, g) &&
                  !savemirror::getHeld(u, g, &held),
              "no answer from the server: still owed, to be tried again");
        romm.heard = false;
        romm.pushes.clear();
        romm.channelAsks = 0;
        check(send(romm.server(), u, g, &why) == Sent::NotSent && romm.pushes.empty() &&
                  romm.channelAsks == 0 && cache::isPushOwed(u, g),
              "a server that has not said it has save channels is asked nothing: still owed");
        romm.heard = true;
        romm.answers = {made(snapshot(120, kChannel, hashOne, romTime(1)))};
        check(send(romm.server(), u, g, &why, [] { return true; }) == Sent::NotSent &&
                  romm.pushes.empty() && cache::isPushOwed(u, g),
              "a game being played keeps its owed snapshot until it is left");
        cache::owedPush(u, g, &p);
        cache::clearPushOwed(u, g, p.stamp);
    }
    {   // The file RomM keys the channels to.
        const int g = owe([&](GameSession& s) { s.channelFileId = 0; });
        Stand romm;
        romm.fileId = 4477;
        romm.answers = {made(snapshot(121, kChannel, hashOne, romTime(1)))};
        check(send(romm.server(), u, g, &why) == Sent::Landed && romm.fileAsks == 1 &&
                  romm.pushes.size() == 1 && romm.pushes[0].romFileId == 4477,
              "a game whose channel file is not known: RomM is asked, and the push names it");
        const int g2 = owe([&](GameSession& s) { s.channelFileId = 4400; });
        romm.staleFile = 4400;
        romm.fileAsks = 0;
        romm.pushes.clear();
        romm.answers = {made(snapshot(122, kChannel, hashOne, romTime(1)))};
        check(send(romm.server(), u, g2, &why) == Sent::Landed && romm.fileAsks == 1 &&
                  romm.pushes[0].romFileId == 4477,
              "a file id RomM no longer knows (its library was scanned): asked again, once");
        const int g3 = owe([&](GameSession& s) { s.channelFileId = 0; });
        romm.fileAnswers = false;
        romm.pushes.clear();
        check(send(romm.server(), u, g3, &why) == Sent::Refused && romm.pushes.empty() &&
                  cache::isPushOwed(u, g3),
              "a game RomM no longer has (404): refused, nothing pushed, still owed");
        cache::owedPush(u, g3, &p);
        cache::clearPushOwed(u, g3, p.stamp);
    }
    {   // The save's file gone; a newer save marked while this one was sent.
        const int g = owe();
        cab::removeFile(p.savePath);
        Stand romm;
        check(send(romm.server(), u, g, &why) == Sent::NothingOwed && romm.pushes.empty() &&
                  !cache::isPushOwed(u, g),
              "the save's file is gone from the console: nothing left to send, nothing owed");
        const int g2 = owe();
        romm.answers = {made(snapshot(130, kChannel, hashOne, romTime(1)))};
        romm.duringPush = [&] {
            GameSession newer = battery(g2, two);
            mark(newer, u);
        };
        const Sent sent = send(romm.server(), u, g2, &why);
        cache::OwedPush still;
        check(sent == Sent::Landed && cache::owedPush(u, g2, &still) &&
                  cab::readBytes(still.savePath) == two && still.channelId.empty(),
              "a newer save marked while the older one was on its way stays owed");
        romm.duringPush = {};
        romm.pushes.clear();
        romm.channels = {channel(kChannel, kDefaultChannel,
                                 snapshot(130, kChannel, hashOne, romTime(1)))};
        romm.answers = {made(snapshot(131, kChannel, romm::contentHash(two), romTime(2)))};
        check(send(romm.server(), u, g2, &why) == Sent::Landed && romm.pushes.size() == 1 &&
                  romm.pushes[0].channelId == kChannel && romm.pushes[0].expectedCurrentId == 130 &&
                  romm.pushes[0].save.bytes == two && !cache::isPushOwed(u, g2),
              "and goes up next on the older one, which RomM now has as the current: never "
              "beside it");
    }

    // ---- States (#346, step 6) --------------------------------------------------
    std::printf("\nstates, inside the snapshot\n");
    // A state Save state wrote, with its picture, in the session's states
    // folder, named as states.cpp names one; `ms` its time.
    const int64_t base = 1763560000123;   // 2025-11-19 13:46:40.123 UTC
    auto stateName = [](int64_t ms) {
        const time_t t = static_cast<time_t>(ms / 1000);
        struct tm g;
        gmtime_r(&t, &g);
        char stamp[48];
        const size_t n = std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &g);
        std::snprintf(stamp + n, sizeof stamp - n, "-%03lld", static_cast<long long>(ms % 1000));
        return std::string("Game (USA) [") + stamp + "].state";
    };
    // And as RomM stores it: its own time added before the extension, with
    // an underscore between the day and the time (its `apply_datetime_tag`).
    auto rommName = [&](int64_t ms) {
        const std::string own = stateName(ms);
        return own.substr(0, own.size() - 6) + " [2025-11-19_15-00-00-000].state";
    };
    auto session = [&](int romId) {
        GameSession s = battery(romId, one);
        s.saveChanged = false;
        s.stateTag = kTag;
        s.stateDir = storage::statesDir(u, "gb", romId, "gambatte");
        storage::makeDirs(s.stateDir);
        return s;
    };
    auto makeState = [&](const GameSession& s, int64_t ms, const std::string& text,
                    bool picture = true) {
        StateMade m;
        m.fileName = stateName(ms);
        m.path = s.stateDir + "/" + m.fileName;
        m.madeMs = ms;
        cab::writeBytes(m.path, bytesOf(text.c_str(), 128));
        if (picture) {
            m.pictureName = m.fileName.substr(0, m.fileName.size() - 6) + ".png";
            m.picturePath = s.stateDir + "/" + m.pictureName;
            cab::writeBytes(m.picturePath, bytesOf(("picture of " + text).c_str(), 32));
        }
        return m;
    };
    auto stateHash = [&](const std::string& text) {
        return romm::contentHash(bytesOf(text.c_str(), 128));
    };
    // A state in a snapshot's bank, as RomM lists it.
    auto inBank = [&](const char* core, const char* slot, int id, int64_t ms,
                      const std::string& text) {
        romm::SnapshotState st;
        st.core = core;
        st.slot = slot;
        st.file.id = id;
        st.file.fileName = rommName(ms);
        st.file.sizeBytes = 128;
        st.file.contentHash = stateHash(text);
        st.file.picturePath = "picture-" + std::to_string(id) + ".png";
        return st;
    };
    auto withStates = [](romm::Snapshot s, std::vector<romm::SnapshotState> states) {
        s.states = std::move(states);
        return s;
    };
    {
        GameSession s = session(++game);
        const StateMade first = makeState(s, base + 1000, "first state");
        check(markState(s, u, first) && cache::owedPush(u, game, &p) && !p.hasSave &&
                  p.states.size() == 1 && p.states[0].core == kTag &&
                  p.states[0].fileName == first.fileName && p.states[0].path == first.path &&
                  p.states[0].pictureName == first.pictureName &&
                  p.states[0].picturePath == first.picturePath &&
                  p.states[0].madeMs == base + 1000 && p.label == kDefaultChannel &&
                  p.romFileId == kFile && !cache::saveOwed(u, game),
              "Save state: owed at once in the game's push, states alone, under the console's "
              "own name, with its picture; no save owed, so a start is not held back");
        for (int k = 2; k <= 4; ++k)
            markState(s, u, makeState(s, base + k * 1000, "state " + std::to_string(k)));
        check(cache::owedPush(u, game, &p) && p.states.size() == kStatesKept &&
                  p.states[0].madeMs == base + 4000 && p.states[2].madeMs == base + 2000,
              "four made: the newest three owed, newest first");
        s.saveAtLaunch.clear();
        cab::writeBytes(s.saveDir + "/" + saveRowName(s.fsStem, "srm"), two);
        s.saveChanged = true;
        check(mark(s, u) && cache::owedPush(u, game, &p) && p.hasSave &&
                  p.states.size() == kStatesKept && p.states[0].madeMs == base + 4000 &&
                  cache::saveOwed(u, game),
              "the save changed at the leave: its push carries the states marked before it");
        const StateMade fifth = makeState(s, base + 5000, "state 5");
        check(markState(s, u, fifth) && cache::owedPush(u, game, &p) && p.hasSave &&
                  cab::readBytes(p.savePath) == two && !p.states.empty() &&
                  p.states[0].fileName == fifth.fileName,
              "a state made after it keeps the push's save and where it goes");
        Uploader up;
        GameSession left = session(game);
        leave(left, u, up);
        check(up.pending() == 1, "leaving a game that owes states queues its push, though the "
                                 "save did not change");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        GameSession s = session(++game);
        hold(game, kChannel, 300);
        s.startChannelId = kChannel;
        s.startSnapshotId = 300;
        check(markState(s, u, makeState(s, base, "held")) && cache::owedPush(u, game, &p) &&
                  p.channelId == kChannel && p.expectedCurrentId == 300 && !p.hasSave,
              "states alone are aimed as a save would be: on the snapshot the console holds");
        cache::clearPushOwed(u, game, p.stamp);
        s.stateTag.clear();
        check(!markState(s, u, makeState(s, base, "untagged")) && !cache::isPushOwed(u, game),
              "a core with no state tag marks nothing: its states stay on the console");
    }
    {
        // What the game offers: owed first, then the snapshot held, under
        // the console's own name only, off the console when the file is here.
        GameSession s = session(++game);
        const romm::Snapshot top =
            withStates(snapshot(310, kChannel, hashOlder, romTime(-60)),
                       {inBank(kTag, "1", 901, base + 3000, "bank newest"),
                        inBank(kTag, "2", 902, base + 2000, "bank middle"),
                        inBank(kTag, "3", 903, base + 1000, "bank oldest"),
                        inBank("mgba", "auto", 904, base + 9000, "another app's")});
        savemirror::putHeld(u, game, savepick::heldAfter(savemirror::Held{}, kChannel, top, now));
        bool holds = false;
        std::vector<OfferedState> offered = offeredStates(u, game, kTag, s.stateDir, &holds);
        check(holds && offered.size() == 3 && offered[0].id == 901 && offered[0].path.empty() &&
                  offered[0].picture == "picture-901.png" &&
                  offered[0].madeMs == base + 3000 && offered[2].id == 903,
              "the snapshot held: its three under the console's name, newest first by the time "
              "the console put in each name and not RomM's, each by its id and RomM's picture; "
              "another app's state is not offered");
        const std::string middle = s.stateDir + "/" + stateName(base + 2000);
        cab::writeBytes(middle, bytesOf("bank middle", 128));
        cab::writeBytes(middle.substr(0, middle.size() - 6) + ".png", bytesOf("its picture", 32));
        const StateMade mine = makeState(s, base + 4000, "made since");
        markState(s, u, mine);
        offered = offeredStates(u, game, kTag, s.stateDir, &holds);
        check(offered.size() == 3 && offered[0].fileName == mine.fileName &&
                  offered[0].id == 0 && offered[0].path == mine.path &&
                  offered[0].picture == mine.picturePath && offered[1].id == 901 &&
                  offered[2].id == 902 && offered[2].path == middle &&
                  offered[2].picture == middle.substr(0, middle.size() - 6) + ".png",
              "one made since comes first, off this console; one this console has the file of "
              "comes off it too, found by the name it gave it, without RomM's time; the oldest "
              "drops out of the three");
        savemirror::forgetHeld(u, game);
        offered = offeredStates(u, game, kTag, s.stateDir, &holds);
        check(!holds && offered.size() == 1 && offered[0].fileName == mine.fileName,
              "no snapshot held: only what is owed, and the caller asks RomM for the rest");
        cache::owedPush(u, game, &p);
        cache::clearPushOwed(u, game, p.stamp);
    }
    {   // States alone, on the snapshot held: the three newest named, no save.
        GameSession s = session(++game);
        const romm::Snapshot current =
            withStates(snapshot(320, kChannel, hashOlder, romTime(-60)),
                       {inBank(kTag, "1", 911, base + 2000, "kept a"),
                        inBank(kTag, "2", 912, base + 1000, "kept b"),
                        inBank(kTag, "3", 913, base + 500, "kept c"),
                        inBank("mgba", "auto", 914, base + 9000, "another app's")});
        savemirror::putHeld(u, game,
                            savepick::heldAfter(savemirror::Held{}, kChannel, current, now - 60));
        savemirror::Held before;
        savemirror::getHeld(u, game, &before);
        s.startChannelId = kChannel;
        s.startSnapshotId = 320;
        // This console's own copy of "kept b", and one of a state no longer offered.
        const std::string keptB = s.stateDir + "/" + stateName(base + 1000);
        cab::writeBytes(keptB, bytesOf("kept b", 128));
        const std::string stale = s.stateDir + "/" + stateName(base - 5000);
        cab::writeBytes(stale, bytesOf("long gone", 128));
        cab::writeBytes(stale.substr(0, stale.size() - 6) + ".png", bytesOf("its picture", 32));
        const StateMade mine = makeState(s, base + 3000, "made in play");
        markState(s, u, mine);
        Stand romm;
        romm.channels = {channel(kChannel, kDefaultChannel, current)};
        const romm::Snapshot landed =
            withStates(snapshot(321, kChannel, hashOlder, romTime(1)),
                       {inBank(kTag, "1", 915, base + 3000, "made in play"),
                        inBank(kTag, "2", 911, base + 2000, "kept a"),
                        inBank(kTag, "3", 912, base + 1000, "kept b"),
                        inBank("mgba", "auto", 914, base + 9000, "another app's")});
        romm.answers = {made(landed)};
        const Sent sent = send(romm.server(), u, game, &why);
        const romm::SnapshotPush sp = romm.pushes.empty() ? romm::SnapshotPush{} : romm.pushes[0];
        check(sent == Sent::Landed && romm.pushes.size() == 1 && !sp.hasSave &&
                  sp.channelId == kChannel && sp.expectedCurrentId == 320 &&
                  sp.states.size() == 3 && sp.states[0].core == kTag &&
                  sp.states[0].slot == "1" && sp.states[0].file.hash == stateHash("made in play") &&
                  sp.states[0].file.bytes == bytesOf("made in play", 128) &&
                  sp.states[0].file.pictureName == mine.pictureName &&
                  !sp.states[0].file.picture.empty() && sp.states[1].slot == "2" &&
                  sp.states[1].file.hash == stateHash("kept a") &&
                  sp.states[1].file.bytes.empty() &&
                  sp.states[2].slot == "3" && sp.states[2].file.hash == stateHash("kept b") &&
                  sp.states[2].file.bytes.empty(),
              "states alone: no save named, so the snapshot's is carried; the console's own "
              "name only, the new one in slot 1 with its bytes and picture, the two newest "
              "kept ones by hash and no bytes, the oldest dropped");
        check(!cache::isPushOwed(u, game) && savemirror::getHeld(u, game, &held) &&
                  held.snapshotId == 321 && held.states.size() == 4 &&
                  held.madeAt == before.madeAt && held.saveHash == hashOlder,
              "it landed: not owed, the snapshot held with its states, and its save dated as "
              "before, since states alone did not change it");
        auto there = [](const std::string& path) {
            struct stat st;
            return ::stat(path.c_str(), &st) == 0;
        };
        check(there(mine.path) && there(mine.picturePath) && there(keptB) && !there(stale) &&
                  !there(stale.substr(0, stale.size() - 6) + ".png"),
              "this console's copies: the ones in the snapshot stay, one no longer among the "
              "three goes with its picture");
        savemirror::Held reread;
        check(savemirror::getHeld(u, game, &reread) && reread.states.size() == 4 &&
                  reread.states[0].core == kTag && reread.states[0].slot == "1" &&
                  reread.states[0].file.id == 915 &&
                  reread.states[0].file.contentHash == stateHash("made in play") &&
                  reread.states[0].file.fileName == rommName(base + 3000) &&
                  reread.states[0].file.picturePath == "picture-915.png",
              "the note of the snapshot held keeps each state's core, slot, id, name, hash "
              "and picture");
    }
    {   // States alone and the channel moved on: never on top.
        GameSession s = session(++game);
        hold(game, kChannel, 330);
        s.startChannelId = kChannel;
        s.startSnapshotId = 330;
        markState(s, u, makeState(s, base + 1000, "beside"));
        Stand romm;
        const romm::Snapshot theirs = snapshot(331, kChannel, "ffff", romTime(-86400));
        romm.answers = {movedOn(snapshot(332, kChannel, hashOlder, romTime(0)), &theirs)};
        check(send(romm.server(), u, game, &why) == Sent::Beside && romm.pushes.size() == 1 &&
                  !cache::isPushOwed(u, game) && savemirror::getHeld(u, game, &held) &&
                  held.snapshotId == 332 && held.beside,
              "states alone, the channel moved on: left beside as RomM kept it, though newer "
              "than the current; they carry no newer save");
    }
    {   // A save and states, moved on, made later: on top, the bank carried.
        GameSession s = session(++game);
        hold(game, kChannel, 340);
        markState(s, u, makeState(s, base + 1000, "with a save"));
        s.saveAtLaunch.clear();
        s.saveChanged = true;
        mark(s, u);
        Stand romm;
        const romm::Snapshot theirs = snapshot(341, kChannel, "ffff", romTime(-600));
        romm.answers = {movedOn(snapshot(342, kChannel, hashOne, romTime(0)), &theirs),
                        made(snapshot(343, kChannel, hashOne, romTime(1)))};
        check(send(romm.server(), u, game, &why) == Sent::Landed && romm.pushes.size() == 2 &&
                  romm.pushes[0].hasSave && romm.pushes[0].states.size() == 1 &&
                  !romm.pushes[1].hasSave && romm.pushes[1].states.empty() &&
                  romm.pushes[1].parentId == 342,
              "a save and a state, the channel moved on, made later: the second push names "
              "neither, so RomM carries both from the one it kept");
    }
    {   // States alone with nothing to build on.
        GameSession s = session(++game);
        markState(s, u, makeState(s, base, "waiting"));
        Stand romm;
        romm::Asset old;
        old.id = 77;
        old.emulator = kTag;
        old.fileName = "Game (USA) (Cabinet).srm";
        romm.rows = {old};
        check(send(romm.server(), u, game, &why) == Sent::NotSent && romm.pushes.empty() &&
                  cache::isPushOwed(u, game),
              "states alone, nothing held, and RomM has the game's save from before channels: "
              "they wait, owed, rather than make a snapshot with no save");
        romm.rows.clear();
        cache::Owed oldSave;
        oldSave.romId = game;
        oldSave.emulator = kTag;
        oldSave.fileName = "Game (USA) (Cabinet).srm";
        oldSave.localPath = s.saveDir + "/Game (USA) (Cabinet).srm";
        cache::markPending(u, oldSave, 64);
        check(send(romm.server(), u, game, &why) == Sent::NotSent && romm.pushes.empty(),
              "and so while this console owes a save of it from before channels");
        cache::clearPending(u, game, oldSave.fileName);
        romm.answers = {made(snapshot(350, kChannel, "", romTime(1)))};
        check(send(romm.server(), u, game, &why) == Sent::Landed && romm.pushes.size() == 1 &&
                  !romm.pushes[0].hasSave && romm.pushes[0].label == kDefaultChannel &&
                  romm.pushes[0].states.size() == 1,
              "a game with no save anywhere: its states go in a snapshot with none");
    }
    {   // RomM lacks a kept state's bytes.
        GameSession s = session(++game);
        const romm::Snapshot current =
            withStates(snapshot(360, kChannel, hashOlder, romTime(-60)),
                       {inBank(kTag, "1", 921, base + 2000, "nowhere"),
                        inBank(kTag, "2", 922, base + 1000, "mine on disk")});
        savemirror::putHeld(u, game,
                            savepick::heldAfter(savemirror::Held{}, kChannel, current, now));
        cab::writeBytes(s.stateDir + "/" + stateName(base + 1000), bytesOf("mine on disk", 128));
        markState(s, u, makeState(s, base + 3000, "new one"));
        Stand romm;
        romm.channels = {channel(kChannel, kDefaultChannel, current)};
        romm::PushAnswer lacks = answerOf(romm::PushAnswer::Kind::BytesMissing, 400);
        lacks.missing = {std::string("state:") + kTag + ":2", std::string("state:") + kTag + ":3"};
        romm.answers = {lacks, made(snapshot(361, kChannel, hashOlder, romTime(1)))};
        const Sent sent = send(romm.server(), u, game, &why);
        const romm::SnapshotPush again =
            romm.pushes.size() == 2 ? romm.pushes[1] : romm::SnapshotPush{};
        check(sent == Sent::Landed && romm.pushes.size() == 2 && again.states.size() == 2 &&
                  again.states[0].slot == "1" && !again.states[0].file.bytes.empty() &&
                  again.states[1].slot == "2" &&
                  again.states[1].file.bytes == bytesOf("mine on disk", 128),
              "RomM lacks two kept states' bytes: the one it has no copy of leaves the bank, "
              "the one after goes up from this console's copy, and the slots close up");
    }
    {   // Moved on to a current that changed only its states.
        GameSession s = battery(++game, one);
        hold(game, kChannel, 380);
        mark(s, u);
        Stand romm;
        // Their current was pushed after this console's save was made, but
        // its save first reached RomM an hour before.
        const romm::Snapshot theirs = snapshot(381, kChannel, "ffff", romTime(600));
        romm::Asset row;
        row.id = theirs.save.id;
        row.createdAt = romTime(-3600);
        romm.rows = {row};
        romm.answers = {movedOn(snapshot(382, kChannel, hashOne, romTime(601)), &theirs),
                        made(snapshot(383, kChannel, hashOne, romTime(602)))};
        check(send(romm.server(), u, game, &why) == Sent::Landed && romm.pushes.size() == 2 &&
                  romm.pushes[1].parentId == 382,
              "the channel moved on to a snapshot that changed only its states: its save is "
              "dated by its own row, older than this console's, so this one goes on top");
    }
    {   // A state's marker from before save channels.
        GameSession s = session(++game);
        hold(game, kChannel, 370);
        const StateMade oldOne = makeState(s, base + 1000, "from before");
        cache::Owed o;
        o.romId = game;
        o.isState = true;
        o.emulator = kTag;
        o.fileName = oldOne.fileName;
        o.localPath = oldOne.path;
        o.shotName = oldOne.pictureName;
        o.shotPath = oldOne.picturePath;
        cache::markPending(u, o, 128);
        check(adoptOldStates(u) == 1 && cache::owed(u).empty() && cache::owedPush(u, game, &p) &&
                  !p.hasSave && p.states.size() == 1 && p.states[0].fileName == oldOne.fileName &&
                  p.states[0].picturePath == oldOne.picturePath &&
                  p.states[0].madeMs == base + 1000 && p.channelId == kChannel &&
                  p.expectedCurrentId == 370,
              "a state's marker from before save channels becomes a state of its game's push, "
              "on the snapshot held, and the old marker goes");
        cab::removeFile(oldOne.path);
        Stand romm;
        check(send(romm.server(), u, game, &why) == Sent::NothingOwed && romm.pushes.empty() &&
                  !cache::isPushOwed(u, game),
              "its file deleted by hand: nothing left to send, nothing owed");
    }

    storage::removeEntry(root);
    std::printf("\n%s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}

}  // namespace savepush
