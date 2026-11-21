// `--save-push-test` (#346): leaving a game, with no server and no core. In a
// scratch root: what a session left, for each kind of save, as the marker a
// push is owed by; where each push is aimed; every answer RomM gives a push,
// settled against a stand-in for RomM that answers as scripted and keeps what
// it was sent; and the states a push carries (step 6): marked when made,
// offered back, named in their slots, and this console's copies kept or let
// go. Its own file, as `--romm-snapshots-test` and
// `--game-events-test` have theirs: the cases would otherwise fill
// savepush.cpp.

#include <fcntl.h>
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
#include "savepicture.h"
#include "savepush.h"
#include "saves.h"
#include "standalone.h"
#include "storage.h"
#include "timewords.h"
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
    // The game's states RomM lists (fetchStates); `statesAnswer` false when
    // that list cannot be had. A channel's history, by its id.
    std::vector<romm::Asset> stateRows;
    bool statesAnswer = true;
    std::map<std::string, std::vector<romm::Snapshot>> history;
    int stateAsks = 0, historyAsks = 0, saveAsks = 0;

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
            ++saveAsks;
            *out = rows;
            return true;
        };
        s.states = [this](int, std::vector<romm::Asset>* out, std::string* err) {
            ++stateAsks;
            if (!statesAnswer) {
                *err = "could not reach the server";
                return false;
            }
            *out = stateRows;
            return true;
        };
        s.history = [this](const std::string& channelId, std::vector<romm::Snapshot>* out,
                           std::string* err) {
            ++historyAsks;
            auto at = history.find(channelId);
            if (at == history.end()) {
                *err = "HTTP 404: Channel not found";
                return false;
            }
            *out = at->second;
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
        // DREAMCAST'S CARD, ONE PER GAME IN THE GAME'S SAVE FOLDER (#346,
        // step 8): a start copies the person's card from before once to the
        // name Flycast reads, the old file left, and takes away a card under
        // the disc's product number, which Flycast would read first; leaving
        // moves the card Flycast wrote under that number back to the game's
        // name, and that is what goes up; a start on another console puts
        // RomM's card there.
        const std::vector<catalog::SaveFile> specs =
            catalog::saveFiles("dc", "flycast", "Ikaruga (Japan).chd", false);
        GameSession s;
        s.romId = ++game;
        s.title = "Ikaruga";
        s.fsStem = "Ikaruga (Japan)";
        s.saveTag = "flycast-native";
        s.core = "flycast";
        s.platformSlug = "dc";
        s.saveDir = storage::savesDir(u, "dc", game, "flycast");
        const std::string card = s.saveDir + "/Ikaruga (Japan).A1.bin";
        const std::string numbered = s.saveDir + "/T38706M.A1.bin";
        const std::string former = s.saveDir + "/" + saveRowName(s.fsStem, "srm");
        cab::writeBytes(former, older);
        s.fileSaves = restoreFileSaves(specs, s.fsStem, s.saveDir, "flycast-native", SaveFetch{});
        check(specs.size() == 1 && specs[0].path == "Ikaruga (Japan).A1.bin" &&
                  s.fileSaves.size() == 1 && cab::readBytes(card) == older &&
                  cab::readBytes(former) == older,
              "Dreamcast: the card from before is copied once to the game's own name, and "
              "stays");
        cab::writeBytes(numbered, one);   // Flycast wrote the game's card under its number
        syncFileSaves(s);
        check(s.saveChanged && cab::readBytes(card) == one && cab::readBytes(numbered).empty() &&
                  cab::readBytes(former) == older,
              "leaving moves the card Flycast wrote under the disc's number to the game's name");
        const bool marked = mark(s, u) && cache::owedPush(u, game, &p);
        check(marked && p.savePath == card && cab::readBytes(p.savePath) == one &&
                  p.saveName == "Ikaruga (Japan).bin" && p.saveShape == "SINGLE",
              "and that card is what goes up, as one file");
        check(p.emulator == "flycast-native" && p.core == "flycast",
              "the push names the console's tag and the core in Sigil's spelling (#346, step "
              "8)");
        s.fileSaves = restoreFileSaves(specs, s.fsStem, s.saveDir, "flycast-native", SaveFetch{});
        check(cab::readBytes(card) == one && cab::readBytes(former) == older,
              "the next start reads the game's card as it is; the card from before is copied "
              "once, never over it");
        const std::vector<uint8_t> crashLeft(64, 0x5c);
        cab::writeBytes(numbered, crashLeft);   // a card under the number, left by a crash
        {   // written after the game's: a file's time moves in clock ticks
            const timespec later[2] = {{0, UTIME_OMIT}, {std::time(nullptr) + 60, 0}};
            ::utimensat(AT_FDCWD, numbered.c_str(), later, 0);
        }
        s.fileSaves = restoreFileSaves(specs, s.fsStem, s.saveDir, "flycast-native", SaveFetch{});
        check(cab::readBytes(card) == crashLeft && cab::readBytes(numbered).empty() &&
                  cab::readBytes(former) == older,
              "a start reads the card Flycast last wrote and leaves it under the game's name "
              "only, so no other is read first; the card from before is not copied again");
        const std::string other = storage::savesDir(u, "dc", game + 5000, "flycast");
        SaveFetch fetched;
        fetched.romId = game + 5000;
        fetched.asked = fetched.listed = true;
        fetched.mark = savemirror::mark();
        fetched.pick.from = savepick::Pick::From::Channel;
        fetched.pick.channel.current.save.fileName = p.saveName;
        fetched.picked = one;
        restoreFileSaves(specs, s.fsStem, other, "flycast-native", fetched);
        check(cab::readBytes(other + "/Ikaruga (Japan).A1.bin") == one,
              "a start on another console puts RomM's card under the game's name");
        const std::vector<catalog::SaveFile> discs =
            catalog::saveFiles("dc", "flycast", "Skies of Arcadia.m3u", false);
        check(discs.size() == 1 && discs[0].path == "Skies of Arcadia.A1.bin",
              "a game on several discs: the card is named after the disc list, whatever disc "
              "it starts from");
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
    {   // The save's picture (savepicture.h), with the save it was taken of.
        GameSession s = battery(++game, one);
        const int g = game;
        s.savePicture = savepicture::pathFor(u, g, 1234);
        const std::vector<uint8_t> pic = bytesOf("a picture of the save", 48);
        cab::writeBytes(s.savePicture, pic);
        mark(s, u);
        Stand romm;
        romm.answers = {made(snapshot(64, kChannel, hashOne, romTime(1)))};
        const Sent sent = send(romm.server(), u, g, &why);
        const bool single = romm.pushes.size() == 1;
        check(sent == Sent::Landed && single && romm.pushes[0].hasSave &&
                  romm.pushes[0].save.picture == pic &&
                  romm.pushes[0].save.pictureName == "Game (USA).png",
              "a save with its picture: the picture goes in the same push, named after the "
              "save");
        struct stat st;
        check(!cache::isPushOwed(u, g) && ::stat(s.savePicture.c_str(), &st) != 0,
              "it landed: the picture's file is deleted with the marker");
        // A save whose picture's file is gone goes without one.
        GameSession s2 = battery(++game, one);
        s2.savePicture = savepicture::pathFor(u, game, 1235);
        cab::writeBytes(s2.savePicture, pic);
        mark(s2, u);
        cab::removeFile(s2.savePicture);
        Stand romm2;
        romm2.answers = {made(snapshot(65, kChannel, hashOne, romTime(1)))};
        check(send(romm2.server(), u, game, &why) == Sent::Landed && romm2.pushes.size() == 1 &&
                  romm2.pushes[0].hasSave && romm2.pushes[0].save.picture.empty(),
              "its picture's file gone: the save goes without one");
        // A picture whose file went before the save was marked is not named.
        GameSession s4 = battery(++game, one);
        s4.savePicture = savepicture::pathFor(u, game, 1236);
        mark(s4, u);
        cache::OwedPush q4;
        check(cache::owedPush(u, game, &q4) && q4.savePicturePath.empty(),
              "a picture whose file is gone before the save is marked is not named");
        cache::clearPushOwed(u, game, q4.stamp);
        // A sitting that took no picture: none, and none of an older save's.
        GameSession s3 = battery(++game, one);
        mark(s3, u);
        cache::OwedPush q;
        check(cache::owedPush(u, game, &q) && q.savePicturePath.empty() &&
                  q.savePictureName.empty(),
              "a sitting that took no picture owes none");
        cache::clearPushOwed(u, game, q.stamp);
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
        romm.channelAsks = romm.fileAsks = romm.saveAsks = romm.stateAsks = 0;
        check(send(romm.server(), u, g, &why) == Sent::Waits && romm.pushes.empty() &&
                  romm.channelAsks == 0 && romm.fileAsks == 0 && romm.saveAsks == 0 &&
                  romm.stateAsks == 0 && cache::isPushOwed(u, g) &&
                  !savemirror::getHeld(u, g, &held),
              "a RomM too old for save channels is asked nothing: the push waits, still owed");
        romm.heard = true;
        romm.answers = {made(snapshot(120, kChannel, hashOne, romTime(1)))};
        check(send(romm.server(), u, g, &why, [] { return true; }) == Sent::NotSent &&
                  romm.pushes.empty() && cache::isPushOwed(u, g),
              "a game being played keeps its owed snapshot until it is left");
        check(send(romm.server(), u, g, &why) == Sent::Landed && romm.pushes.size() == 1 &&
                  romm.pushes[0].hasSave && !cache::isPushOwed(u, g) &&
                  savemirror::getHeld(u, g, &held) && held.snapshotId == 120,
              "once RomM has save channels, the push that waited goes as it was owed");
        check(!settled(Sent::Waits) && triedAgain(Sent::Waits) && !settled(Sent::NotSent) &&
                  triedAgain(Sent::NotSent) && !settled(Sent::Refused) &&
                  !triedAgain(Sent::Refused) && settled(Sent::Landed) &&
                  settled(Sent::Beside) && settled(Sent::NothingOwed) &&
                  !triedAgain(Sent::Landed),
              "a push that waits or had no answer stays owed and is tried again on the timer; "
              "a refusal waits for the next start; one RomM holds is done");
        if (cache::owedPush(u, g, &p)) cache::clearPushOwed(u, g, p.stamp);
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
    // A state in RomM's list of the game's states (fetchStates), as it lists
    // one: pushed into `channelId` and filed under `core` there, or from
    // before channels (no channel, no core).
    auto stateRow = [&](int id, int64_t ms, const char* core, const std::string& channelId) {
        romm::Asset a;
        a.id = id;
        a.romId = game;
        a.fileName = rommName(ms);
        a.emulator = kTag;
        a.core = core;
        a.channelId = channelId;
        a.createdAt = romTime(-60);
        a.picturePath = "picture-" + std::to_string(id) + ".png";
        return a;
    };
    {
        // What the game offers: of RomM's list, the three newest under the
        // console's own name wherever they are, with the ones still owed.
        GameSession s = session(++game);
        std::vector<romm::Asset> rows = {stateRow(901, base + 3000, kTag, kChannel),
                                         stateRow(902, base + 2000, kTag, kChannel),
                                         stateRow(903, base + 1000, "", ""),
                                         stateRow(904, base + 9000, "mgba", kChannel),
                                         stateRow(905, base + 500, kTag, kChannel)};
        rows[3].emulator = "retroarch";
        std::vector<OfferedState> offered = offeredStates(u, game, kTag, s.stateDir, &rows);
        check(offered.size() == 3 && offered[0].id == 901 && offered[0].path.empty() &&
                  offered[0].picture == "picture-901.png" && offered[0].madeMs == base + 3000 &&
                  offered[1].id == 902 && offered[2].id == 903 && offered[2].madeMs == base + 1000,
              "RomM's list: the three newest under the console's own name, wherever they are "
              "(a channel's, one from before channels, filed under the emulator), newest first "
              "by the time the console put in each name and not RomM's, each by its id and "
              "RomM's picture; another app's state is not offered, and the fourth drops out");
        std::vector<romm::Asset> undated = rows;
        undated.push_back(stateRow(906, 0, kTag, kChannel));
        undated.back().fileName = "quicksave.state";
        undated.back().createdAt = romTime(0);
        offered = offeredStates(u, game, kTag, s.stateDir, &undated);
        check(offered.size() == 3 && offered[0].id == 906 && offered[0].madeMs / 1000 >= now - 5,
              "a state whose name carries no time is dated when RomM first stored it");
        const std::string middle = s.stateDir + "/" + stateName(base + 2000);
        cab::writeBytes(middle, bytesOf("bank middle", 128));
        cab::writeBytes(middle.substr(0, middle.size() - 6) + ".png", bytesOf("its picture", 32));
        const StateMade mine = makeState(s, base + 4000, "made since");
        markState(s, u, mine);
        offered = offeredStates(u, game, kTag, s.stateDir, &rows);
        check(offered.size() == 3 && offered[0].fileName == mine.fileName &&
                  offered[0].id == 0 && offered[0].path == mine.path &&
                  offered[0].picture == mine.picturePath && offered[1].id == 901 &&
                  offered[2].id == 902 && offered[2].path == middle &&
                  offered[2].picture == middle.substr(0, middle.size() - 6) + ".png",
              "one made since and still owed comes first, off this console; one this console "
              "has the file of comes off it too, found by the name it gave it, without RomM's "
              "time");
        rows.push_back(stateRow(907, base + 4000, kTag, kChannel));
        offered = offeredStates(u, game, kTag, s.stateDir, &rows);
        check(offered.size() == 3 && offered[0].fileName == mine.fileName &&
                  offered[0].path == mine.path && offered[1].id == 901 && offered[2].id == 902,
              "owed and already in RomM's list under RomM's name: one state, not two");
        offered = offeredStates(u, game, kTag, s.stateDir, nullptr);
        check(offered.size() == 2 && offered[0].path == mine.path && offered[1].path == middle &&
                  offered[1].madeMs == base + 2000 && offered[1].id == 0,
              "RomM away or not answering: this console's own files, the owed one and the "
              "one in its folder, dated by their names");
        check(offeredStates(u, game, "", s.stateDir, &rows).size() == 2,
              "a core whose states never leave the console: its own files, whatever RomM "
              "lists");
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
        romm.stateRows = {stateRow(911, base + 2000, kTag, kChannel),
                          stateRow(912, base + 1000, kTag, kChannel),
                          stateRow(913, base + 500, kTag, kChannel),
                          stateRow(914, base + 9000, "mgba", kChannel)};
        romm.answers = {made(snapshot(321, kChannel, hashOlder, romTime(1)))};
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
                  sp.states[2].file.bytes.empty() && romm.stateAsks == 1 &&
                  romm.historyAsks == 0 && sp.save.picture.empty(),
              "states alone: no save named, so the snapshot's is carried with its picture; "
              "the console's own "
              "name only, the new one in slot 1 with its bytes and picture, the two newest of "
              "RomM's list by hash (the current's, and one off this console's copy) and no "
              "bytes, the oldest left out; RomM's list asked once and no history needed");
        check(!cache::isPushOwed(u, game) && savemirror::getHeld(u, game, &held) &&
                  held.snapshotId == 321 && held.madeAt == before.madeAt &&
                  held.saveHash == hashOlder && held.hashesHad == before.hashesHad,
              "it landed: not owed, the snapshot held, and its save dated as before, since "
              "states alone did not change it");
        auto there = [](const std::string& path) {
            struct stat st;
            return ::stat(path.c_str(), &st) == 0;
        };
        check(there(mine.path) && there(mine.picturePath) && there(keptB) && !there(stale) &&
                  !there(stale.substr(0, stale.size() - 6) + ".png"),
              "this console's copies: the ones the push named stay, one it did not name goes "
              "with its picture");
    }
    {   // States alone while another device's newer save went on top.
        GameSession s = session(++game);
        hold(game, kChannel, 510);
        savemirror::Held before;
        savemirror::getHeld(u, game, &before);
        s.startChannelId = kChannel;
        s.startSnapshotId = 510;
        markState(s, u, makeState(s, base + 5000, "made alone"));
        Stand romm;
        const romm::Snapshot theirs =
            withStates(snapshot(511, kChannel, "ffff", romTime(-60)),
                       {inBank("mgba", "auto", 512, base + 9000, "their app's")});
        romm.channels = {channel(kChannel, kDefaultChannel, theirs)};
        romm.answers = {made(snapshot(513, kChannel, "ffff", romTime(1)))};
        const Sent sent = send(romm.server(), u, game, &why);
        const romm::SnapshotPush sp = romm.pushes.empty() ? romm::SnapshotPush{} : romm.pushes[0];
        check(sent == Sent::Landed && romm.pushes.size() == 1 && !sp.hasSave &&
                  sp.channelId == kChannel && sp.expectedCurrentId == 511 && sp.parentId == 0 &&
                  sp.states.size() == 1 && sp.states[0].core == kTag,
              "states alone while another device's newer save went on top: onto that current "
              "as RomM lists it, its save carried, not into a branch; the other app's state "
              "not named, so carried as it is");
        check(savemirror::getHeld(u, game, &held) && held.snapshotId == 513 && !held.beside &&
                  held.saveHash == before.saveHash && held.hashesHad == before.hashesHad &&
                  held.madeAt == before.madeAt,
              "the note of the snapshot held keeps the save this console last had, its hashes "
              "and its time, not the other device's save it went onto");
        savepick::Facts f;
        f.tag = kTag;
        f.channels = {channel(kChannel, kDefaultChannel, snapshot(513, kChannel, "ffff",
                                                                  romTime(1)))};
        f.haveHeld = true;
        f.held = held;
        const savepick::Pick next = savepick::pick(f);
        check(next.from == savepick::Pick::From::Channel && next.madeElsewhere,
              "so the next start continues from the other device's save and says \"Continuing "
              "from another device\"");
    }
    {   // States alone, the channel moved on while they were sent.
        GameSession s = session(++game);
        hold(game, kChannel, 330);
        savemirror::Held before;
        savemirror::getHeld(u, game, &before);
        s.startChannelId = kChannel;
        s.startSnapshotId = 330;
        markState(s, u, makeState(s, base + 1000, "beside"));
        Stand romm;
        const romm::Snapshot theirs = snapshot(331, kChannel, "ffff", romTime(-86400));
        romm.answers = {movedOn(snapshot(332, kChannel, hashOlder, romTime(0)), &theirs),
                        made(snapshot(333, kChannel, "ffff", romTime(1)))};
        const Sent sent = send(romm.server(), u, game, &why);
        const romm::SnapshotPush again =
            romm.pushes.size() == 2 ? romm.pushes[1] : romm::SnapshotPush{};
        check(sent == Sent::Landed && romm.pushes.size() == 2 && !again.hasSave &&
                  again.channelId == kChannel && again.expectedCurrentId == 331 &&
                  again.parentId == 0 && again.states.size() == 1 &&
                  again.states[0].file.hash == stateHash("beside") &&
                  again.states[0].file.bytes.empty() && again.states[0].file.picture.empty() &&
                  !cache::isPushOwed(u, game) && savemirror::getHeld(u, game, &held) &&
                  held.snapshotId == 333 && !held.beside && held.saveHash == before.saveHash,
              "states alone, the channel moved on while they were sent: again onto the current "
              "RomM names, by hash with no bytes (RomM holds them in the branch it kept)");
    }
    {   // States alone and a current whose save is another emulator's.
        GameSession s = session(++game);
        hold(game, kChannel, 335);
        s.startChannelId = kChannel;
        s.startSnapshotId = 335;
        markState(s, u, makeState(s, base + 1000, "not over theirs"));
        Stand romm;
        romm::Snapshot theirs = snapshot(336, kChannel, "eeee", romTime(-60));
        theirs.save.emulator = "retroarch";
        romm.channels = {channel(kChannel, kDefaultChannel, theirs)};
        romm.answers = {movedOn(snapshot(337, kChannel, hashOlder, romTime(0)), &theirs)};
        check(send(romm.server(), u, game, &why) == Sent::Beside && romm.pushes.size() == 1 &&
                  romm.pushes[0].expectedCurrentId == 335 && !cache::isPushOwed(u, game) &&
                  savemirror::getHeld(u, game, &held) && held.snapshotId == 337 && held.beside,
              "states alone while the current's save is another emulator's: aimed as a save "
              "is, on the snapshot held, and left beside where RomM keeps them");
    }
    {   // States alone from a session that could not use the newest save.
        GameSession s = session(++game);
        hold(game, kChannel, 338);
        s.startChannelId = kChannel;
        s.startSnapshotId = 338;
        s.couldNotUseNewest = true;
        markState(s, u, makeState(s, base + 1000, "could not use"));
        Stand romm;
        const romm::Snapshot theirs = snapshot(339, kChannel, "dddd", romTime(-60));
        romm.channels = {channel(kChannel, kDefaultChannel, theirs)};
        romm.answers = {movedOn(snapshot(3391, kChannel, hashOlder, romTime(0)), &theirs)};
        check(send(romm.server(), u, game, &why) == Sent::Beside && romm.pushes.size() == 1 &&
                  romm.pushes[0].expectedCurrentId == 338 && savemirror::getHeld(u, game, &held) &&
                  held.beside,
              "states alone from a session that could not use the newest save: never onto it, "
              "left beside");
    }
    {   // Another console's newer state, only in a branch.
        GameSession s = session(++game);
        hold(game, kChannel, 520);
        s.startChannelId = kChannel;
        s.startSnapshotId = 520;
        const romm::Snapshot current =
            withStates(snapshot(520, kChannel, hashOlder, romTime(-600)),
                       {inBank(kTag, "1", 521, base + 1000, "on top")});
        const romm::Snapshot branch =
            withStates(snapshot(522, kChannel, hashOlder, romTime(-300), "branch"),
                       {inBank(kTag, "1", 523, base + 2000, "in a branch")});
        std::vector<romm::Asset> rows = {stateRow(521, base + 1000, kTag, kChannel),
                                         stateRow(523, base + 2000, kTag, kChannel)};
        const std::vector<OfferedState> offered = offeredStates(u, game, kTag, s.stateDir, &rows);
        check(offered.size() == 2 && offered[0].id == 523 && offered[1].id == 521,
              "another console's newer state, only in a branch: offered first");
        markState(s, u, makeState(s, base + 3000, "made here"));
        Stand romm;
        romm.channels = {channel(kChannel, kDefaultChannel, current)};
        romm.stateRows = rows;
        romm.history[kChannel] = {branch, current};
        romm.answers = {made(snapshot(524, kChannel, hashOlder, romTime(1)))};
        const Sent sent = send(romm.server(), u, game, &why);
        const romm::SnapshotPush sp = romm.pushes.empty() ? romm::SnapshotPush{} : romm.pushes[0];
        check(sent == Sent::Landed && sp.states.size() == 3 &&
                  sp.states[0].file.hash == stateHash("made here") &&
                  sp.states[1].file.hash == stateHash("in a branch") &&
                  sp.states[1].file.bytes.empty() && sp.states[1].slot == "2" &&
                  sp.states[2].file.hash == stateHash("on top") && romm.historyAsks == 1,
              "and named by the hash its channel's history gives (RomM's list carries none), "
              "with no bytes, beside the one made here and the current's; the history asked "
              "once");
    }
    {   // A state RomM's history does not hold either.
        GameSession s = session(++game);
        hold(game, kChannel, 525);
        s.startChannelId = kChannel;
        s.startSnapshotId = 525;
        markState(s, u, makeState(s, base + 3000, "made here"));
        Stand romm;
        romm.stateRows = {stateRow(526, base + 2000, kTag, kChannel),
                          stateRow(528, base + 1000, kTag, kChannel)};
        romm.answers = {made(snapshot(527, kChannel, hashOlder, romTime(1)))};
        check(send(romm.server(), u, game, &why) == Sent::Landed && romm.pushes.size() == 1 &&
                  romm.pushes[0].states.size() == 1 && romm.historyAsks == 1,
              "two states no hash can be found for, their channel's history not had: left "
              "out, the history asked once for both, and the push still goes");
    }
    {   // A state made while the push was on its way.
        GameSession s = session(++game);
        hold(game, kChannel, 570);
        s.startChannelId = kChannel;
        s.startSnapshotId = 570;
        markState(s, u, makeState(s, base + 1000, "sent"));
        StateMade meanwhile;
        Stand romm;
        romm.duringPush = [&] {
            meanwhile = makeState(s, base + 2000, "made meanwhile");
            markState(s, u, meanwhile);
        };
        romm.answers = {made(snapshot(571, kChannel, hashOlder, romTime(1)))};
        struct stat st;
        check(send(romm.server(), u, game, &why) == Sent::Landed &&
                  cache::owedPush(u, game, &p) && !p.states.empty() &&
                  p.states[0].fileName == meanwhile.fileName &&
                  ::stat(meanwhile.path.c_str(), &st) == 0,
              "a state made while the push was on its way: still owed, and its copy kept");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {   // A state made here older than RomM's three newest.
        GameSession s = session(++game);
        hold(game, kChannel, 530);
        s.startChannelId = kChannel;
        s.startSnapshotId = 530;
        const StateMade earlier = makeState(s, base + 1000, "made earlier");
        markState(s, u, earlier);
        const romm::Snapshot current =
            withStates(snapshot(530, kChannel, hashOlder, romTime(-60)),
                       {inBank(kTag, "1", 533, base + 7000, "their newest"),
                        inBank(kTag, "2", 532, base + 6000, "their middle"),
                        inBank(kTag, "3", 531, base + 5000, "their oldest")});
        Stand romm;
        romm.channels = {channel(kChannel, kDefaultChannel, current)};
        romm.stateRows = {stateRow(531, base + 5000, kTag, kChannel),
                          stateRow(532, base + 6000, kTag, kChannel),
                          stateRow(533, base + 7000, kTag, kChannel)};
        romm.answers = {made(snapshot(534, kChannel, hashOlder, romTime(1)))};
        const Sent sent = send(romm.server(), u, game, &why);
        const romm::SnapshotPush sp = romm.pushes.empty() ? romm::SnapshotPush{} : romm.pushes[0];
        struct stat st;
        check(sent == Sent::Landed && sp.states.size() == 3 &&
                  sp.states[0].file.hash == stateHash("their newest") &&
                  sp.states[1].file.hash == stateHash("their middle") &&
                  sp.states[2].slot == "3" && sp.states[2].file.hash == stateHash("made earlier") &&
                  sp.states[2].file.bytes == bytesOf("made earlier", 128) &&
                  ::stat(earlier.path.c_str(), &st) == 0,
              "a state made here older than RomM's three newest is still named, with its bytes, "
              "in the last slot, and its copy kept: none made on this console is let go before "
              "RomM has it");
    }
    {   // RomM's list of the game's states not had.
        GameSession s = session(++game);
        hold(game, kChannel, 550);
        markState(s, u, makeState(s, base + 1000, "waits"));
        Stand romm;
        romm.statesAnswer = false;
        check(send(romm.server(), u, game, &why) == Sent::NotSent && romm.pushes.empty() &&
                  cache::isPushOwed(u, game),
              "RomM's list of the game's states not had: nothing sent, still owed, tried later");
        cache::owedPush(u, game, &p);
        cache::clearPushOwed(u, game, p.stamp);
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
    {   // RomM lacks a named state's bytes.
        GameSession s = session(++game);
        const romm::Snapshot current =
            withStates(snapshot(360, kChannel, hashOlder, romTime(-60)),
                       {inBank(kTag, "1", 921, base + 2000, "nowhere"),
                        inBank(kTag, "2", 922, base + 1000, "mine on disk")});
        savemirror::putHeld(u, game,
                            savepick::heldAfter(savemirror::Held{}, kChannel, current, now));
        const std::string disk = s.stateDir + "/" + stateName(base + 1000);
        cab::writeBytes(disk, bytesOf("mine on disk", 128));
        cab::writeBytes(disk.substr(0, disk.size() - 6) + ".png", bytesOf("its picture", 32));
        markState(s, u, makeState(s, base + 3000, "new one"));
        Stand romm;
        romm.channels = {channel(kChannel, kDefaultChannel, current)};
        romm.stateRows = {stateRow(921, base + 2000, kTag, kChannel),
                          stateRow(922, base + 1000, kTag, kChannel)};
        romm::PushAnswer lacks = answerOf(romm::PushAnswer::Kind::BytesMissing, 400);
        lacks.missing = {std::string("state:") + kTag + ":2", std::string("state:") + kTag + ":3"};
        romm.answers = {lacks, made(snapshot(361, kChannel, hashOlder, romTime(1)))};
        const Sent sent = send(romm.server(), u, game, &why);
        const romm::SnapshotPush again =
            romm.pushes.size() == 2 ? romm.pushes[1] : romm::SnapshotPush{};
        check(sent == Sent::Landed && romm.pushes.size() == 2 && again.states.size() == 2 &&
                  again.states[0].slot == "1" && !again.states[0].file.bytes.empty() &&
                  again.states[1].slot == "2" &&
                  again.states[1].file.bytes == bytesOf("mine on disk", 128) &&
                  again.states[1].file.fileName == stateName(base + 1000) &&
                  again.states[1].file.picture == bytesOf("its picture", 32),
              "RomM lacks two named states' bytes: the one this console has no copy of leaves "
              "the bank, the other goes up from this console's copy under the name it gave "
              "it, with its picture, and the slots close up");
    }
    {   // States from before channels.
        GameSession s = session(++game);
        hold(game, kChannel, 560);
        s.startChannelId = kChannel;
        s.startSnapshotId = 560;
        cab::writeBytes(s.stateDir + "/" + stateName(base + 1000), bytesOf("before channels", 128));
        markState(s, u, makeState(s, base + 2000, "made now"));
        Stand romm;
        romm.stateRows = {stateRow(941, base + 1000, "", ""), stateRow(942, base + 500, "", "")};
        romm::PushAnswer lacks = answerOf(romm::PushAnswer::Kind::BytesMissing, 400);
        lacks.missing = {std::string("state:") + kTag + ":2"};
        romm.answers = {lacks, made(snapshot(561, kChannel, hashOlder, romTime(1)))};
        const Sent sent = send(romm.server(), u, game, &why);
        check(sent == Sent::Landed && romm.pushes.size() == 2 &&
                  romm.pushes[0].states.size() == 2 &&
                  romm.pushes[0].states[1].file.hash == stateHash("before channels") &&
                  romm.pushes[0].states[1].file.bytes.empty() &&
                  romm.pushes[1].states.size() == 2 &&
                  romm.pushes[1].states[1].file.bytes == bytesOf("before channels", 128) &&
                  romm.historyAsks == 0,
              "states from before channels: one this console has a copy of is named by that "
              "copy's hash and goes up from it when RomM asks; one it has no copy of is left "
              "out; no history is asked for either");
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

    // ---- Saves from before channels (#346, step 7) -----------------------------
    std::printf("\nsaves from before channels, brought into a channel\n");
    const std::string oldAt = romTime(-86400);
    const int64_t oldAtSec = isoMicros(oldAt) / 1000000;
    // RomM's row of a save from before channels, at RomM's time `at`.
    auto oldRow = [&](int id, const std::string& at) {
        romm::Asset a;
        a.id = id;
        a.fileName = "Game (USA) (Cabinet).srm";
        a.emulator = kTag;
        a.updatedAt = at;
        a.createdAt = at;
        return a;
    };
    // The push a start that took row `id` owes (markOldSave).
    auto startTook = [&](const GameSession& s, const romm::Asset& row) {
        std::vector<romm::Asset> rows = {row};
        savepick::Pick pick;
        pick.from = savepick::Pick::From::OldRows;
        savepick::OldSaveCopy copy;
        savepick::oldSaveToCopy(pick, rows,
                                {{row.id, savepick::OldRowTaken::As::Battery, "", 0}}, &copy);
        // The start kept the row's bytes, as the worker's fetch does.
        savemirror::putBytes(u, s.romId, row, older);
        return markOldSave(s, u, copy, {});
    };
    {
        GameSession s = battery(++game, older);
        s.saveChanged = false;
        const romm::Asset row = oldRow(501, oldAt);
        check(startTook(s, row) && cache::owedPush(u, game, &p) && p.fromOldRows() &&
                  p.madeAt == oldAtSec && !cache::saveOwed(u, game),
              "a start that took a battery from before channels owes its push, no save of "
              "this console's");
        Stand romm;
        romm.rows = {row};
        romm::Snapshot copied = snapshot(510, kChannel, romm::contentHash(older), romTime(0));
        romm.answers = {made(copied)};
        savemirror::Held held;
        const Sent sent = send(romm.server(), u, game, &why);
        check(sent == Sent::Landed && romm.pushes.size() == 1 && romm.pushes[0].hasSave &&
                  romm.pushes[0].saveCopyOf == 501 && romm.pushes[0].save.bytes.empty() &&
                  romm.pushes[0].save.hash.empty() && romm.pushes[0].saveShape == "SINGLE" &&
                  romm.pushes[0].label == kDefaultChannel && romm.pushes[0].expectedCurrentId == 0 &&
                  romm.pushes[0].emulator == kTag,
              "sent as RomM's copy of the row: no hash and no bytes, into \"default\"");
        check(!cache::isPushOwed(u, game) && savemirror::getHeld(u, game, &held) &&
                  held.snapshotId == 510 && held.saveHash == romm::contentHash(older) &&
                  held.madeAt == oldAtSec && !held.beside,
              "once RomM holds it: owed no more, and the console holds the snapshot, made when "
              "RomM stored the row");
        check(savemirror::getSnapshotSave(u, game, copied.save) == older,
              "and keeps the row's bytes as the copy's, for a start offline");
        // RomM's copy is not the bytes this console kept of the row.
        startTook(s, oldRow(507, oldAt));
        romm.rows = {oldRow(507, oldAt)};
        const romm::Snapshot other = snapshot(511, kChannel, "not-these-bytes", romTime(0));
        romm.answers = {made(other)};
        check(send(romm.server(), u, game, &why) == Sent::Landed &&
                  savemirror::getSnapshotSave(u, game, other.save).empty(),
              "bytes kept of a row that are not RomM's copy are not kept as the copy's");
    }
    {
        // A console on the old version saved again after the start: RomM's
        // row is no longer what the start took.
        GameSession s = battery(++game, older);
        const romm::Asset row = oldRow(502, oldAt);
        startTook(s, row);
        Stand romm;
        romm.rows = {oldRow(502, romTime(-60))};
        check(send(romm.server(), u, game, &why) == Sent::NothingOwed && romm.pushes.empty() &&
                  !cache::isPushOwed(u, game),
              "the row changed since the start: not brought in, nothing owed; the next start "
              "takes it as it is");
        startTook(s, row);
        romm.rows.clear();
        check(send(romm.server(), u, game, &why) == Sent::NothingOwed && romm.pushes.empty(),
              "the row deleted on RomM's website: the same");
        startTook(s, row);
        romm.rows = {row};
        romm.rows[0].channelId = kChannel;
        check(send(romm.server(), u, game, &why) == Sent::NothingOwed && romm.pushes.empty(),
              "a row RomM lists in a channel now: no longer a save from before channels");
    }
    {
        // The channel moved on meanwhile: RomM keeps the copy beside, and it
        // goes on top only when RomM stored the row later.
        GameSession s = battery(++game, older);
        const romm::Asset row = oldRow(503, oldAt);
        hold(game, kChannel, 520);
        startTook(s, row);
        check(cache::owedPush(u, game, &p) && p.channelId == kChannel && p.expectedCurrentId == 520,
              "aimed onto the snapshot this console holds");
        Stand romm;
        romm.rows = {row};
        romm::Snapshot theirs = snapshot(521, kChannel, "theirs", romTime(-2 * 86400));
        romm.channels = {channel(kChannel, kDefaultChannel, theirs)};
        const romm::Snapshot branch = snapshot(522, kChannel, romm::contentHash(older), romTime(0));
        romm.answers = {movedOn(branch, &theirs), made(snapshot(523, kChannel,
                                                                romm::contentHash(older),
                                                                romTime(0)))};
        check(send(romm.server(), u, game, &why) == Sent::Landed && romm.pushes.size() == 2 &&
                  romm.pushes[0].saveCopyOf == 503 && !romm.pushes[1].hasSave &&
                  romm.pushes[1].saveCopyOf == 0 && romm.pushes[1].parentId == 522 &&
                  romm.pushes[1].expectedCurrentId == 521,
              "a current stored before the row: the copy goes on top, named as the branch, "
              "nothing copied twice");
        hold(game, kChannel, 520);
        startTook(s, row);
        romm.pushes.clear();
        theirs = snapshot(524, kChannel, "theirs", romTime(-60));
        romm.channels = {channel(kChannel, kDefaultChannel, theirs)};
        romm.answers = {movedOn(branch, &theirs)};
        check(send(romm.server(), u, game, &why) == Sent::Beside && romm.pushes.size() == 1,
              "a current stored after it: the copy stays beside, the newer save on top");
    }
    {
        // States owed before the start go with the copy; one made in play
        // joins it; the session's own save at the leave takes its place.
        GameSession s = session(++game);
        s.fsStem = "Game (USA)";
        s.saveTag = kTag;
        markState(s, u, makeState(s, base, "made before"));
        // A state the old version made and sent: its file on this console,
        // no marker.
        const StateMade oldVersion = makeState(s, base - 5000, "from the old version");
        const romm::Asset row = oldRow(504, oldAt);
        startTook(s, row);
        bool both = cache::owedPush(u, game, &p) && p.fromOldRows() && p.states.size() == 2;
        for (const cache::OwedPush::State& st : p.states)
            both = both && st.core == kTag &&
                   (st.fileName == oldVersion.fileName ||
                    st.fileName == stateName(base));
        check(both, "states owed before the start, and the console's own from before channels, "
                    "go with the copy into the game's first snapshot");
        Stand romm;
        romm.rows = {row};
        romm.answers = {made(snapshot(512, kChannel, romm::contentHash(older), romTime(0)))};
        check(send(romm.server(), u, game, &why) == Sent::Landed && romm.pushes.size() == 1 &&
                  romm.pushes[0].saveCopyOf == 504 && romm.pushes[0].states.size() == 2 &&
                  romm.pushes[0].states[1].file.fileName == oldVersion.fileName &&
                  romm.pushes[0].states[1].file.bytes == bytesOf("from the old version", 128) &&
                  romm.pushes[0].states[1].slot == "2",
              "sent with the copy, the state from before channels as its bytes, newest first");
        markState(s, u, makeState(s, base + 1000, "made in play"));
        startTook(s, row);
        check(cache::owedPush(u, game, &p) && p.fromOldRows() && p.states.size() == 3 &&
                  p.label == kDefaultChannel,
              "a state made in play joins a copy still owed, aimed where the copy goes");
        cab::writeBytes(s.saveDir + "/" + saveRowName(s.fsStem, "srm"), one);
        s.saveChanged = true;
        check(mark(s, u) && cache::owedPush(u, game, &p) && !p.fromOldRows() && p.hasSave &&
                  p.states.size() == 3 && cache::saveOwed(u, game),
              "the session's own save at the leave takes the copy's place, carrying the states");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        // Sega CD's two rows: the zip the start wrote goes as bytes.
        GameSession s = battery(++game, older);
        romm::Asset internalRow = oldRow(505, oldAt), cartRow = oldRow(506, oldAt);
        cartRow.fileName = "Game (USA) (Cabinet).cart";
        std::vector<romm::Asset> rows = {internalRow, cartRow};
        savepick::Pick pick;
        pick.from = savepick::Pick::From::OldRows;
        savepick::OldSaveCopy copy;
        savepick::oldSaveToCopy(pick, rows,
                                {{505, savepick::OldRowTaken::As::File, "Game (USA).brm", 0},
                                 {506, savepick::OldRowTaken::As::File, "4Mbit_cart.brm", 1}},
                                &copy);
        check(markOldSave(s, u, copy, {one, two}) && cache::owedPush(u, game, &p) &&
                  p.savePath == s.saveDir + "/" + saveRowName(s.fsStem, "zip") &&
                  p.saveShape == "MULTI",
              "two rows: their zip beside the console's own copy");
        check(!markOldSave(s, u, copy, {one}),
              "a row whose bytes did not come: nothing marked");
        Stand romm;
        romm.rows = rows;
        const std::vector<uint8_t> zip = cab::readBytes(p.savePath);
        romm.answers = {made(snapshot(530, kChannel, romm::contentHash(zip), romTime(0)))};
        check(send(romm.server(), u, game, &why) == Sent::Landed && romm.pushes.size() == 1 &&
                  romm.pushes[0].saveCopyOf == 0 && romm.pushes[0].save.bytes == zip &&
                  romm.pushes[0].saveShape == "MULTI" &&
                  romm.pushes[0].save.hash == romm::contentHash(zip),
              "sent as the zip's bytes, named by its hash");
    }
    {
        // A save's marker from before save channels becomes its game's push.
        GameSession s = battery(++game, one);
        cache::Owed o;
        o.romId = game;
        o.emulator = kTag;
        o.fileName = "Game (USA) (Cabinet).srm";
        o.localPath = s.saveDir + "/Game (USA) (Cabinet).srm";
        cache::markPending(u, o, 64);
        // Written an hour ago, when the game saved and RomM could not be
        // reached.
        const struct timespec hourAgo[2] = {{now - 3600, 0}, {now - 3600, 0}};
        ::utimensat(AT_FDCWD, o.localPath.c_str(), hourAgo, 0);
        struct stat st;
        ::stat(o.localPath.c_str(), &st);
        check(adoptOldSaves(u) == 1 && cache::owed(u).empty() && cache::owedPush(u, game, &p) &&
                  p.hasSave && !p.fromOldRows() && p.savePath == o.localPath &&
                  p.saveName == "Game (USA).srm" && p.saveShape == "SINGLE" &&
                  p.madeAt == now - 3600 && st.st_mtime == now - 3600 &&
                  p.label == kDefaultChannel &&
                  p.emulator == kTag && cache::saveOwed(u, game),
              "a battery's: its file as the save, made when the console wrote it, into "
              "\"default\", still a save this console owes");
        Stand romm;
        romm.answers = {made(snapshot(540, kChannel, romm::contentHash(one), romTime(0)))};
        check(send(romm.server(), u, game, &why) == Sent::Landed &&
                  romm.pushes[0].save.bytes == one && romm.pushes[0].saveCopyOf == 0 &&
                  !cache::isPushOwed(u, game),
              "and it goes up with its bytes");
    }
    {
        // Sega CD's two files owed from before: one zip, each under its name.
        GameSession s = session(++game);
        const std::string dir = s.saveDir;
        cab::writeBytes(dir + "/Lunar.brm", one);
        cab::writeBytes(dir + "/4Mbit_cart.brm", two);
        cache::Owed a, b;
        a.romId = b.romId = game;
        a.emulator = b.emulator = "gpgx-native";
        a.fileName = "Lunar (Cabinet).srm";
        a.localPath = dir + "/Lunar.brm";
        b.fileName = "Lunar (Cabinet).cart";
        b.localPath = dir + "/4Mbit_cart.brm";
        cache::markPending(u, a, 64);
        cache::markPending(u, b, 96);
        markState(s, u, makeState(s, base, "carried"));
        std::vector<cab::ZipFile> files;
        std::string zerr;
        const bool adopted = adoptOldSaves(u) == 1 && cache::owedPush(u, game, &p) &&
                             cab::readZip(cab::readBytes(p.savePath), &files, &zerr);
        bool both = files.size() == 2;
        for (const cab::ZipFile& f : files)
            both = both && ((f.relPath == "Lunar.brm" && f.bytes == one) ||
                            (f.relPath == "4Mbit_cart.brm" && f.bytes == two));
        check(adopted && both && p.saveShape == "MULTI" && p.saveName == "Lunar.zip" &&
                  p.savePath == dir + "/" + saveRowName("Lunar", "zip") && p.states.size() == 1 &&
                  cache::owed(u).empty(),
              "two files of one game: one zip of both under their own names, the states owed "
              "carried, the old markers gone");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        // A push of the console's own save already owed carries the whole save.
        GameSession s = battery(++game, one);
        mark(s, u);
        cache::OwedPush before;
        cache::owedPush(u, game, &before);
        cache::Owed o;
        o.romId = game;
        o.emulator = kTag;
        o.fileName = "Game (USA) (Cabinet).srm";
        o.localPath = s.saveDir + "/Game (USA) (Cabinet).srm";
        cache::markPending(u, o, 64);
        check(adoptOldSaves(u) == 0 && cache::owed(u).empty() && cache::owedPush(u, game, &p) &&
                  p.stamp == before.stamp,
              "beside a push of the console's own save: the old marker only goes");
        cache::clearPushOwed(u, game, p.stamp);
    }

    // ---- A game in a core that never ended (saves.h, finishInterruptedPlay) ----
    std::printf("\na game in a core that never ended\n");
    // The note as notePlaying writes it, but `ago` seconds old: a file's time
    // is the kernel's coarse clock, so one written the moment after the note
    // could read as written before it.
    auto noteAgo = [&](GameSession& s, int64_t ago) {
        s.playingSince = (now - ago) * 1000000000;
        cab::standalone::writePlayingNote(s.saveDir, playingNoteFor(s, s.saveDir));
    };
    auto age = [&](const std::string& path, int64_t ago) {
        const timespec t[2] = {{now - ago, 0}, {now - ago, 0}};
        utimensat(AT_FDCWD, path.c_str(), t, 0);
    };
    {
        // A DS game: melonDS writes its `.sav` while the game runs.
        const int g = ++game;
        GameSession s;
        s.romId = g;
        s.title = "Ended Badly";
        s.saveTag = "melonds-native";
        s.core = "melonds";
        s.platformSlug = "nds";
        s.romFile = "Ended Badly.nds";
        s.fsStem = "Ended Badly";
        s.saveDir = storage::savesDir(u, "nds", g, "melonds");
        for (const catalog::SaveFile& spec : catalog::saveFiles("nds", "melonds", s.romFile, false)) {
            cab::FileSaveState f;
            f.spec = spec;
            f.path = s.saveDir + "/" + spec.path;
            s.fileSaves.push_back(f);
        }
        // A battery lives in the core: a crash loses that session, and there
        // is nothing on the console to note.
        GameSession cartridge = s;
        cartridge.romId = g + 1000;
        cartridge.saveDir = storage::savesDir(u, "gb", cartridge.romId, "gambatte");
        cartridge.fileSaves.clear();
        storage::makeDirs(cartridge.saveDir);
        notePlaying(cartridge);
        check(!cab::standalone::hasPlayingNote(cartridge.saveDir) && cartridge.playingSince == 0,
              "a game whose save is a battery notes nothing");
        const std::string sav = s.saveDir + "/Ended Badly.sav";
        const std::vector<uint8_t> started = bytesOf("the save the start put in place", 512);
        const std::vector<uint8_t> won = bytesOf("the cup won before the power went", 512);
        cab::writeBytes(sav, started);
        notePlaying(s);
        GameSession read = sessionOfPlayingNote(cab::standalone::playingNote(s.saveDir));
        check(cab::standalone::hasPlayingNote(s.saveDir) && s.playingSince > 0 &&
                  read.romId == g && read.core == "melonds" && read.platformSlug == "nds" &&
                  read.romFile == s.romFile && read.saveTag == s.saveTag &&
                  read.fsStem == s.fsStem &&
                  read.playingSince == s.playingSince,
              "a game in a core whose save is files notes it is playing, with what rebuilds its "
              "saves");
        Uploader up;
        check(!finishInterruptedPlay(s.saveDir, up) && !cache::isPushOwed(u, g) &&
                  !cab::standalone::hasPlayingNote(s.saveDir) && cab::readBytes(sav) == started,
              "it never ended, having saved nothing since its start: nothing owed, the note gone");
        // Saved an hour ago, in a session that started two hours ago.
        noteAgo(s, 7200);
        cab::writeBytes(sav, won);
        age(sav, 3600);
        check(finishInterruptedPlay(s.saveDir, up) && cache::owedPush(u, g, &p) && p.hasSave &&
                  cab::readBytes(p.savePath) == won && p.madeAt == now - 3600 &&
                  p.saveName == "Ended Badly.sav" &&
                  cache::saveOwed(u, g) && !cab::standalone::hasPlayingNote(s.saveDir) &&
                  up.pending() == 1,
              "what the core wrote after the note goes up as the session's snapshot, owed so "
              "the next start keeps it, made when the core wrote it, and the note goes");
        cache::clearPushOwed(u, g, p.stamp);
        // A note with no time (not one notePlaying writes) is taken away:
        // every file would read as written after it.
        s.playingSince = 0;
        cab::standalone::writePlayingNote(s.saveDir, playingNoteFor(s, s.saveDir));
        cab::writeBytes(sav, bytesOf("a save of unknown age", 512));
        check(!finishInterruptedPlay(s.saveDir, up) && !cache::isPushOwed(u, g) &&
                  !cab::standalone::hasPlayingNote(s.saveDir),
              "a note that says nothing of when the game started is taken away, nothing sent");
        // Half written by a power cut: the wrong size for a DS cartridge.
        noteAgo(s, 10);
        cab::writeBytes(sav, bytesOf("half a save", 300));
        check(!finishInterruptedPlay(s.saveDir, up) && !cache::isPushOwed(u, g) &&
                  !cab::standalone::hasPlayingNote(s.saveDir),
              "a file that fails the save check is not kept: nothing owed");
    }
    {
        // A PSP game: PPSSPP writes into its save folder while the game runs.
        const int g = ++game;
        GameSession s;
        s.romId = g;
        s.title = "Folder Game";
        s.saveTag = "ppsspp";
        s.core = "ppsspp";
        s.platformSlug = "psp";
        s.romFile = "Folder Game.chd";
        s.saveDir = storage::savesDir(u, "psp", g, "ppsspp");
        s.dirSaveRoot = s.saveDir + "/PSP/SAVEDATA";
        cab::writeBytes(s.dirSaveRoot + "/ULUS00000/OLD.BIN", bytesOf("from the start", 64));
        age(s.dirSaveRoot + "/ULUS00000/OLD.BIN", 60);
        noteAgo(s, 10);
        cab::writeBytes(s.dirSaveRoot + "/ULUS00000/DATA.BIN", bytesOf("saved in play", 64));
        Uploader up;
        check(finishInterruptedPlay(s.saveDir, up) && cache::owedPush(u, g, &p) && p.hasSave &&
                  cab::looksLikeZip(cab::readBytes(p.savePath)) && cache::saveOwed(u, g) &&
                  !cab::standalone::hasPlayingNote(s.saveDir),
              "a save folder written in after the note goes up whole, owed");
        cache::clearPushOwed(u, g, p.stamp);
        age(s.dirSaveRoot + "/ULUS00000/DATA.BIN", 60);
        noteAgo(s, 10);
        check(!finishInterruptedPlay(s.saveDir, up) && !cache::isPushOwed(u, g),
              "a save folder with nothing written after the note sends nothing");
    }

    // ---- A battery with its clock or its cartridge (#346, step 11) ------------
    std::printf("\na battery with its clock or its cartridge (step 11)\n");
    auto zipOf = [](const std::string& path) {
        std::vector<cab::ZipFile> in;
        std::string zerr;
        cab::readZip(cab::readBytes(path), &in, &zerr);
        return in;
    };
    // A Saturn cartridge as Beetle Saturn formats it, with a save's mark on
    // its third block when `save`.
    auto saturnCart = [](uint8_t save) {
        std::vector<uint8_t> b(524288, 0x00);
        const char* format = "BackUpRam Format";
        for (size_t i = 0; i < 512; ++i) b[i] = static_cast<uint8_t>(format[i % 16]);
        if (save) {
            b[1024] = 0x80;
            b[1028] = save;
        }
        return b;
    };
    const std::vector<uint8_t> clock = bytesOf("clock 8", 8);
    {
        // Game Boy: Gambatte's clock beside the battery, this console's copy.
        GameSession s = battery(++game, one);
        s.core = "gambatte";
        s.platformSlug = "gbc";
        cab::writeBytes(s.saveDir + "/" + saveRowName(s.fsStem, "rtc"), clock);
        std::vector<cab::ZipFile> in;
        check(mark(s, u) && cache::owedPush(u, game, &p) && p.saveName == "Game (USA).zip" &&
                  p.saveShape == "MULTI" && (in = zipOf(p.savePath)).size() == 2 &&
                  in[0].relPath == "Game (USA).srm" && in[0].bytes == one &&
                  in[1].relPath == "Game (USA).rtc" && in[1].bytes == clock,
              "Game Boy with a clock: a zip of Gambatte's own two files, `<game>.srm` and "
              "`<game>.rtc`");
        Stand romm;
        romm.answers = {made(snapshot(1101, kChannel, "x", romTime(1)))};
        check(send(romm.server(), u, game, &why) == Sent::Landed && romm.pushes.size() == 1 &&
                  romm.pushes[0].saveShape == "MULTI" && romm.pushes[0].core == "gambatte" &&
                  romm.pushes[0].save.fileName == "Game (USA).zip" &&
                  romm.pushes[0].save.bytes == cab::readBytes(p.savePath),
              "sent as a zip, naming `gambatte`, which is what makes Sigil read its clock");
        s.saveChanged = false;
        cab::writeBytes(s.saveDir + "/" + saveRowName(s.fsStem, "rtc"), bytesOf("ticked", 8));
        check(!mark(s, u) && !cache::isPushOwed(u, game),
              "the clock changed alone: no new save, nothing owed");
    }
    {
        GameSession s = battery(++game, one);
        s.core = "gambatte";
        s.platformSlug = "gb";
        check(mark(s, u) && cache::owedPush(u, game, &p) && p.saveName == "Game (USA).srm" &&
                  p.saveShape == "SINGLE" && cab::readBytes(p.savePath) == one,
              "Game Boy with no clock: the battery as it is");
        cache::clearPushOwed(u, game, p.stamp);
        GameSession snes = battery(++game, one);
        snes.core = "snes9x";
        snes.platformSlug = "snes";
        cab::writeBytes(snes.saveDir + "/" + saveRowName(snes.fsStem, "rtc"), clock);
        check(mark(snes, u) && cache::owedPush(u, game, &p) && p.saveShape == "SINGLE" &&
                  cab::readBytes(p.savePath) == one,
              "Super Nintendo: a clock on the console stays there; the battery alone");
        cache::clearPushOwed(u, game, p.stamp);
    }
    // Saturn: the battery first, the cartridge beside it.
    auto saturn = [&](int romId, const std::vector<uint8_t>& cart) {
        GameSession s = battery(romId, one);
        s.core = "beetle_saturn";
        s.platformSlug = "saturn";
        s.romFile = "Rally (USA).chd";
        s.saveDir = storage::savesDir(u, "saturn", romId, "beetle_saturn");
        cab::writeBytes(s.saveDir + "/" + saveRowName(s.fsStem, "srm"), one);
        for (const catalog::SaveFile& spec :
             catalog::saveFiles("saturn", "beetle_saturn", s.romFile, false)) {
            cab::FileSaveState f;
            f.spec = spec;
            f.path = s.saveDir + "/" + spec.path;
            if (!cart.empty()) {
                cab::writeBytes(f.path, cart);
                f.atLaunch = cart;
                f.hadOne = true;
                f.kept = f.path;
            }
            s.fileSaves.push_back(f);
        }
        return s;
    };
    {
        GameSession s = saturn(++game, saturnCart('A'));
        std::vector<cab::ZipFile> in;
        check(mark(s, u) && cache::owedPush(u, game, &p) && p.saveName == "Game (USA).zip" &&
                  p.saveShape == "MULTI" && (in = zipOf(p.savePath)).size() == 2 &&
                  in[0].relPath == "Game (USA).srm" && in[0].bytes == one &&
                  in[1].relPath == "Rally (USA).bcr" && in[1].bytes == saturnCart('A'),
              "Saturn with a cartridge holding a save: a zip of the internal memory as "
              "`<game>.srm` and the cartridge as Beetle Saturn names it");
        // A start on another console: the cartridge before the load, the
        // battery's part left to the battery.
        const std::string other = storage::savesDir(u, "saturn", game + 5000, "beetle_saturn");
        SaveFetch fetched;
        fetched.romId = game + 5000;
        fetched.asked = fetched.listed = true;
        fetched.mark = savemirror::mark();
        fetched.pick.from = savepick::Pick::From::Channel;
        fetched.pick.channel.current.save.fileName = p.saveName;
        fetched.picked = cab::readBytes(p.savePath);
        restoreFileSaves(catalog::saveFiles("saturn", "beetle_saturn", "Rally (USA).chd", false),
                         "Rally (USA)", other, "saturn-native", fetched);
        BatteryParts parts;
        check(cab::readBytes(other + "/Rally (USA).bcr") == saturnCart('A') &&
                  batteryParts(fetched.picked, &parts, &why) && parts.battery == one,
              "which a start on another console writes back: the cartridge as its file, the "
              "battery as the core's memory");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        GameSession s = saturn(++game, saturnCart(0));
        check(mark(s, u) && cache::owedPush(u, game, &p) && p.saveName == "Game (USA).srm" &&
                  p.saveShape == "SINGLE" && cab::readBytes(p.savePath) == one,
              "Saturn with a formatted, empty cartridge: never sent; the battery as it is");
        cache::clearPushOwed(u, game, p.stamp);
        GameSession none = saturn(++game, {});
        check(mark(none, u) && cache::owedPush(u, game, &p) && p.saveShape == "SINGLE",
              "Saturn with no cartridge file yet: the battery as it is");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        // The game saved to its cartridge and not its own memory.
        GameSession s = saturn(++game, saturnCart(0));
        s.saveChanged = false;
        cab::writeBytes(s.fileSaves[0].path, saturnCart('C'));
        syncFileSaves(s);
        std::vector<cab::ZipFile> in;
        check(s.saveChanged && mark(s, u) && cache::owedPush(u, game, &p) &&
                  (in = zipOf(p.savePath)).size() == 2 && in[0].bytes == one &&
                  in[1].bytes == saturnCart('C'),
              "a save to the cartridge alone: the zip, the battery as this console last had it");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        // A Saturn game that never ended: Beetle Saturn writes its cartridge
        // while the game runs, so the cartridge it wrote after the note goes
        // up beside the battery this console last had.
        GameSession s = saturn(++game, saturnCart(0));
        s.title = "Rally";
        s.saveTag = "saturn-native";
        notePlaying(s);
        check(cab::standalone::hasPlayingNote(s.saveDir),
              "Saturn notes it is playing: its cartridge is a file the core writes");
        noteAgo(s, 60);
        cab::writeBytes(s.fileSaves[0].path, saturnCart('D'));
        age(s.fileSaves[0].path, 30);
        Uploader up;
        std::vector<cab::ZipFile> in;
        check(finishInterruptedPlay(s.saveDir, up) && cache::owedPush(u, game, &p) &&
                  (in = zipOf(p.savePath)).size() == 2 && in[0].bytes == one &&
                  in[1].bytes == saturnCart('D') && cache::saveOwed(u, game),
              "a Saturn game that never ended: the cartridge it wrote goes up with the battery");
        cache::clearPushOwed(u, game, p.stamp);
    }
    {
        // A start that took a battery and its clock from before channels
        // (Cabinet's `.rtc` row): the push zips the two rows' bytes.
        GameSession s = battery(++game, older);
        s.core = "gambatte";
        s.platformSlug = "gbc";
        s.saveChanged = false;
        romm::Asset srm = oldRow(1201, oldAt);
        romm::Asset rtc = oldRow(1202, oldAt);
        rtc.fileName = "Game (USA) (Cabinet).rtc";
        savepick::Pick pick;
        pick.from = savepick::Pick::From::OldRows;
        savepick::OldSaveCopy copy;
        using As = savepick::OldRowTaken::As;
        std::vector<cab::ZipFile> in;
        const std::vector<romm::Asset> rows = {srm, rtc};
        const std::vector<savepick::OldRowTaken> taken = {
            {1201, As::Battery, "Game (USA).srm", 0}, {1202, As::Clock, "Game (USA).rtc", 0}};
        check(savepick::oldSaveToCopy(pick, rows, taken, &copy) &&
                  markOldSave(s, u, copy, {older, clock}) && cache::owedPush(u, game, &p) &&
                  p.saveShape == "MULTI" && p.oldRows.size() == 2 &&
                  (in = zipOf(p.savePath)).size() == 2 && in[0].relPath == "Game (USA).srm" &&
                  in[0].bytes == older && in[1].relPath == "Game (USA).rtc" &&
                  in[1].bytes == clock,
              "a battery and its clock from before channels: one zip of the two rows, as the "
              "session's push makes it");
        cache::clearPushOwed(u, game, p.stamp);
    }

    storage::removeEntry(root);
    std::printf("\n%s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}

}  // namespace savepush
