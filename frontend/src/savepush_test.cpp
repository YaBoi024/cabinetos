// `--save-push-test` (#346): leaving a game, with no server and no core. In a
// scratch root: what a session left, for each kind of save, as the marker a
// push is owed by; where each push is aimed; and every answer RomM gives a
// push, settled against a stand-in for RomM that answers as scripted and
// keeps what it was sent. Its own file, as `--romm-snapshots-test` and
// `--game-events-test` have theirs: the cases would otherwise fill
// savepush.cpp.

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>

#include "cache.h"
#include "dirsave.h"
#include "fileio.h"
#include "savemirror.h"
#include "savepick.h"
#include "savepush.h"
#include "saves.h"
#include "storage.h"

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
        s.saves = [](int, std::vector<romm::Asset>* rows, std::string*) {
            rows->clear();
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

    storage::removeEntry(root);
    std::printf("\n%s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}

}  // namespace savepush
