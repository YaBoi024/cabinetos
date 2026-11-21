#include "saves.h"

#include <sys/stat.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <sstream>
#include <thread>

#include "cache.h"
#include "core.h"
#include "fileio.h"
#include "savemirror.h"
#include "savepush.h"
#include "storage.h"
#include "wii.h"

using From = savepick::Pick::From;

// The pick's save comes as `picked`: a channel's, or another app's in no
// channel (#346, step 8).
static bool usesPicked(From f) { return f == From::Channel || f == From::OtherRow; }
static std::string pickedName(const SaveFetch& f) {
    return f.pick.from == From::OtherRow ? f.pick.row.fileName
                                         : f.pick.channel.current.save.fileName;
}

// Why RomM is not asked for a save: away, or too old for save channels
// (romm::serverTakesSaves).
static const char* notAsked() {
    return romm::serverAway() ? "the server is away" : "RomM has no save channels";
}

// RomM'S SAVES FROM BEFORE CHANNELS FOR A GAME, OR THE COPY OF THEM (#88,
// savemirror.h). Online: asked, and the copy refreshed. Offline, and on a
// RomM too old for save channels, which is neither read nor written (#346,
// step 9): the copy, as last seen, so a launch decides exactly as it would
// online. Every fetch asks through these, on a worker (listSaves and
// fetchSaves below, for the game's page and for a launch): the person is
// handed in, as the worker's threads do not read the current one.
static bool savesFor(romm::Client& client, const storage::User& u, int romId,
                     std::vector<romm::Asset>* rows, std::string* err) {
    if (romm::serverTakesSaves()) {
        const uint64_t asked = savemirror::mark();
        if (client.fetchSaves(romId, rows, err)) {
            if (!savemirror::forgottenSince(romId, asked)) savemirror::putList(u, romId, *rows);
            return true;
        }
        if (!romm::serverAway()) return false;
    }
    if (savemirror::getList(u, romId, rows)) {
        std::fprintf(stderr, "[save] %s; using its saves as last seen\n", notAsked());
        return true;
    }
    if (err) *err = std::string(notAsked()) + ", and its saves were never seen here";
    return false;
}

static std::vector<uint8_t> saveBytesFor(romm::Client& client, const storage::User& u,
                                         int romId, const romm::Asset& row) {
    if (romm::serverTakesSaves()) {
        std::vector<uint8_t> b = client.fetchAsset("saves", row.id);
        if (!b.empty()) {
            savemirror::putBytes(u, romId, row, b);
            return b;
        }
        if (!romm::serverAway()) return b;
    }
    return savemirror::getBytes(u, romId, row);
}

// THE GAME'S SAVE CHANNELS ON RomM, OR THE COPY OF THEM (#346), as savesFor
// is for the old rows. Online, on a RomM that has channels: asked by the file
// RomM keys them to, and the copy refreshed. `*fileId` is that file as the
// caller knows it (0 for not known) and comes back as RomM gave it when RomM
// had to be asked: the first time, and again when the id no longer answers
// (RomM's library was scanned since and the file has another). Away, or on a
// RomM too old for them: the copy, as last seen, or none when they were
// never seen. False only when RomM was asked and gave nothing to go by.
static bool channelsFor(romm::Client& client, const storage::User& u, int romId, int* fileId,
                        std::vector<romm::Channel>* channels, std::string* err) {
    channels->clear();
    if (romm::serverTakesSaves()) {
        const uint64_t asked = savemirror::mark();
        auto askFile = [&]() {
            std::vector<romm::RomFile> files;
            if (!client.fetchRomFiles(romId, &files, err)) return false;
            *fileId = 0;
            for (const romm::RomFile& f : files)
                if (f.channelFile) *fileId = f.id;
            return true;
        };
        bool known = *fileId != 0;
        bool ok = known || askFile();
        // RomM names no file for the game: nothing of it is in a channel.
        if (ok && *fileId == 0) return true;
        if (ok) {
            bool gone = false;
            ok = client.fetchChannels({*fileId}, channels, err, &gone);
            if (!ok && gone && known) {
                known = false;
                ok = askFile();
                if (ok && *fileId == 0) return true;
                if (ok) ok = client.fetchChannels({*fileId}, channels, err);
            }
        }
        if (ok) {
            if (!savemirror::forgottenSince(romId, asked)) savemirror::putChannels(u, romId, *channels);
            return true;
        }
        channels->clear();
        if (!romm::serverAway()) return false;
    }
    if (savemirror::getChannels(u, romId, channels))
        std::fprintf(stderr, "[save] %s; using its save channels as last seen\n", notAsked());
    return true;
}

// A snapshot's save. A save in a snapshot is named by its content hash, and
// the console's copy is kept under that name (savemirror.h), so a copy whose
// bytes still hash to it is the save itself and is not fetched again. Else
// RomM's, kept for offline. Away, or on a RomM too old for save channels,
// the copy as it is, or nothing.
static std::vector<uint8_t> snapshotSaveFor(romm::Client& client, const storage::User& u,
                                            int romId, const romm::SnapshotFile& save) {
    std::vector<uint8_t> kept = savemirror::getSnapshotSave(u, romId, save);
    if (!romm::serverTakesSaves()) return kept;
    if (!kept.empty() && romm::contentHash(kept) == save.contentHash) return kept;
    std::vector<uint8_t> b = client.fetchAsset("saves", save.id);
    if (!b.empty()) savemirror::putSnapshotSave(u, romId, save, b);
    return b;
}

// WHAT RomM HOLDS OF A GAME'S SAVE, AND WHICH OF IT A START CONTINUES FROM
// (#346, savepick.h): its saves from before channels, its channels, what this
// console still owes and the snapshot it holds, handed to the one rule. When
// either list could not be had from a server that is there, nothing is known
// for sure and the pick stays at the console's own save: half a view could
// put an older save over a newer one.
//
// THE TWO LISTS ARE ASKED FOR AT THE SAME TIME, the channels on a thread of
// their own for the length of this call, so a start waits for the slower of
// the two and not for both. Measured on 2026-11-18 against the reference
// server: the saves list answers in about 12 ms and the channels list in
// about 15, so a start waits about 3 ms longer than before channels, where
// one after the other it would wait 15. The client is made for it (romm.h),
// and the two write different files of the console's copy.
void listSaves(romm::Client& client, const storage::User& user, int romId, int channelFileId,
               const char* tag, SaveFetch* out) {
    *out = SaveFetch{};
    out->romId = romId;
    out->channelFileId = channelFileId;
    if (!tag || !client.haveToken()) return;
    out->asked = true;
    out->mark = savemirror::mark();
    savepick::Facts facts;
    std::string channelsErr;
    bool channelsOk = false;
    int channelFile = channelFileId;
    std::thread channelsAsk([&]() {
        channelsOk = channelsFor(client, user, romId, &channelFile, &facts.channels, &channelsErr);
    });
    out->listed = savesFor(client, user, romId, &out->rows, &out->error);
    channelsAsk.join();
    out->channelFileId = channelFile;
    if (!out->listed) return;
    if (!channelsOk) {
        out->listed = false;
        out->error = channelsErr;
        return;
    }
    facts.tag = tag;
    savepick::dateSaves(&facts.channels, out->rows);
    if (const romm::Asset* newest = savepick::newestOldRow(out->rows, tag))
        facts.oldRowAt = newest->updatedAt;
    facts.otherRows = savepick::otherRows(out->rows, tag);
    facts.owed = cache::saveOwed(user, romId);
    facts.haveHeld = savemirror::getHeld(user, romId, &facts.held);
    out->oldRowAt = facts.oldRowAt;
    out->facts = facts;
    out->pick = savepick::pick(facts);
    std::fprintf(stderr, "[save] game %d: %s\n", romId, out->pick.why.c_str());
}

namespace {

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

bool isHex(const std::string& s, size_t from, size_t n) {
    if (s.size() < from + n) return false;
    for (size_t i = from; i < from + n; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

// `path` with `root` taken off its front, each `*` in `root` one folder of
// any name; false when `path` is not under it.
bool underRoot(const std::string& root, const std::string& path, std::string* rest) {
    size_t r = 0, p = 0;
    while (r < root.size()) {
        if (root[r] == '*') {
            const size_t slash = path.find('/', p);
            if (slash == std::string::npos || slash == p) return false;
            p = slash;
            ++r;
            continue;
        }
        if (p >= path.size() || path[p] != root[r]) return false;
        ++r;
        ++p;
    }
    *rest = path.substr(p);
    return !rest->empty();
}

bool sfoAt(const std::vector<cab::ZipFile>& files, const std::string& path) {
    for (const cab::ZipFile& f : files)
        if (f.relPath == path)
            return f.bytes.size() >= 4 && std::memcmp(f.bytes.data(), "\0PSF", 4) == 0;
    return false;
}

// A FOLDER SAVE IN THIS CONSOLE'S SHAPE FOR ITS SYSTEM (the head of
// dirsave.h, and each emulator's own layout), the check of another
// emulator's (#346, step 8): every file where the system keeps a save, with
// the marks its format carries.
bool folderFits(const OwnForm& own, const std::vector<cab::ZipFile>& files, std::string* why) {
    const std::string& core = own.form.sigil;
    auto no = [why](const std::string& w) {
        *why = w;
        return false;
    };
    if (files.empty()) return no("it holds no file");
    for (const cab::ZipFile& f : files) {
        const std::string& p = f.relPath;
        const size_t slash = p.find('/');
        const std::string top = slash == std::string::npos ? std::string() : p.substr(0, slash);
        if (core == "ppsspp" || core == "rpcs3") {
            // A PSP's or a PS3's save folder holds PARAM.SFO, which starts
            // with `\0PSF`.
            if (top.empty()) return no(p + " is in no save folder");
            if (!sfoAt(files, top + "/PARAM.SFO")) return no(top + " holds no PARAM.SFO with its mark");
        } else if (core == "dolphin" && own.platform == "ngc") {
            // A GameCube save is a `.gci`: a 64-byte header, then whole
            // 8 KB blocks, its game's four-letter code first.
            if (!top.empty() || p.size() < 5 || p.compare(p.size() - 4, 4, ".gci") != 0)
                return no(p + " is not a GameCube save");
            if (f.bytes.size() < 64 + 8192 || (f.bytes.size() - 64) % 8192 != 0)
                return no(p + " is " + std::to_string(f.bytes.size()) +
                          " bytes, no size a GameCube save has");
            for (int i = 0; i < 4; ++i)
                if (!std::isalnum(f.bytes[i])) return no(p + " does not carry a game's code");
        } else if (core == "dolphin") {
            // Wii: `<kind>/<title>/data/...` under Dolphin's `title/`.
            if (!isHex(p, 0, 8) || p.size() < 23 || p[8] != '/' || !isHex(p, 9, 8) ||
                p.compare(17, 6, "/data/") != 0)
                return no(p + " is not in a Wii title's data folder");
        } else if (core == "azahar") {
            // `<id0>/<id1>/title/<high>/<low>/data/...` or `.../extdata/...`
            // under the SD card's `Nintendo 3DS/`.
            if (!isHex(p, 0, 32) || p.size() < 66 || p[32] != '/' || !isHex(p, 33, 32) ||
                p[65] != '/' ||
                (!startsWith(p.substr(66), "title/") && !startsWith(p.substr(66), "extdata/")))
                return no(p + " is not in a 3DS title's save");
        } else if (core == "eden") {
            if (!isHex(p, 0, 16) || p.size() < 18 || p[16] != '/')
                return no(p + " is not in a Switch title's save folder");
        } else if (core == "cemu") {
            if (!isHex(p, 0, 8) || p.size() < 10 || p[8] != '/' ||
                (!startsWith(p.substr(9), "user/") && !startsWith(p.substr(9), "meta/")))
                return no(p + " is not in a Wii U title's save");
        } else if (top.empty()) {
            return no(p + " is in no save folder");
        }
    }
    return true;
}

}  // namespace

// OwnForm::arcade: another app's save for an arcade game, refused with why.
static bool arcadeRefuses(const OwnForm& own, const std::string& madeBy, std::string* why) {
    if (!own.arcade) return false;
    *why = "an arcade game takes only CabinetOS's saves, and " +
           (madeBy.empty() ? std::string("another app") : madeBy) + " made it";
    return true;
}

bool toOwnForm(const romm::SaveAnswer& answer, bool plain, const OwnForm& own,
               const std::string& madeBy, const std::string& madeByCore,
               std::vector<uint8_t>* out, std::string* why) {
    out->clear();
    if (arcadeRefuses(own, madeBy, why)) return false;
    const bool asStored =
        plain || (answer.savePath.empty() && !cab::looksLikeZip(answer.bytes));
    if (asStored && (own.form.sameCoreOnly || own.folder) &&
        !savepick::mayLoad(madeBy, madeByCore, own.tag, own.form.sigil)) {
        *why = "RomM hands it over as stored, and " + (madeBy.empty() ? "another" : madeBy) +
               " made it, not this console's emulator";
        return false;
    }
    // What RomM sent: a zip of several files by their paths in the
    // emulator's save folder, or one file named by `X-Save-Path` (none when
    // handed over as stored).
    std::vector<cab::ZipFile> sent;
    const bool zipped = cab::looksLikeZip(answer.bytes);
    if (zipped) {
        std::string zerr;
        if (!cab::readZip(answer.bytes, &sent, &zerr)) {
            *why = "RomM's answer would not unpack: " + zerr;
            return false;
        }
    } else if (!answer.bytes.empty()) {
        sent.push_back({answer.savePath, answer.bytes});
    }
    if (sent.empty()) {
        *why = "RomM's answer is empty";
        return false;
    }
    auto base = [](const std::string& p) { return p.substr(p.find_last_of('/') + 1); };
    auto endsWith = [](const std::string& s, const std::string& end) {
        return !end.empty() && s.size() >= end.size() &&
               s.compare(s.size() - end.size(), end.size(), end) == 0;
    };

    // A FOLDER: each file under this console's root for it, the shape a
    // push of its own makes. As stored, the paths are the other app's own.
    if (own.folder) {
        std::vector<cab::ZipFile> mine;
        for (cab::ZipFile& f : sent) {
            std::string rel = f.relPath;
            if (!asStored && !own.form.answerRoot.empty() &&
                !underRoot(own.form.answerRoot, f.relPath, &rel)) {
                *why = f.relPath + " is outside where this emulator keeps its saves";
                return false;
            }
            mine.push_back({rel, std::move(f.bytes)});
        }
        if (!folderFits(own, mine, why)) return false;
        std::string zerr;
        if (!cab::zipFiles(mine, out, &zerr)) {
            *why = "could not zip it: " + zerr;
            return false;
        }
        return true;
    }

    // THE BATTERY: the one file, or RomM's zip's `.srm`, with the
    // cartridge's clock beside it where it travels (SaveForm::clock, #346,
    // step 11): then a zip of the two under the names RomM gave them, the
    // shape a push of this console's makes. Its size, and the clock's, are
    // the loaded core's to check (saves.cpp, restoreBatterySave).
    if (own.files.empty()) {
        if (asStored && zipped) {
            *why = "it is a zip, and RomM converted nothing";
            return false;
        }
        const cab::ZipFile* battery = sent.size() == 1 ? &sent.front() : nullptr;
        const cab::ZipFile* clock = nullptr;
        for (const cab::ZipFile& f : sent) {
            if (sent.size() > 1 && endsWith(f.relPath, ".srm")) battery = &f;
            if (sent.size() > 1 && endsWith(f.relPath, ".rtc")) clock = &f;
        }
        if (!battery) {
            *why = "RomM's answer holds no battery save";
            return false;
        }
        if (!cab::passesCheck(battery->bytes, catalog::batteryCheck(own.platform),
                              catalog::Untouched::Blank, why))
            return false;
        if (!own.form.clock || !clock || clock->bytes.empty()) {
            *out = battery->bytes;
            return true;
        }
        std::string zerr;
        if (!cab::zipFiles({{base(battery->relPath), battery->bytes},
                            {base(clock->relPath), clock->bytes}},
                           out, &zerr)) {
            *why = "could not zip it: " + zerr;
            return false;
        }
        return true;
    }

    // THE BATTERY FIRST, THE CORE'S FILES BESIDE IT (SaveForm::batteryFirst,
    // #346, step 11): Saturn's own memory, RomM's `.srm` (one file handed
    // over as stored is it), and its cartridge, found by the end of its name.
    // A cartridge with no save on it is left out, as a push of this
    // console's leaves it; one this core does not read (its size, its mark)
    // refuses the save. The battery alone goes as it is; else a zip, the
    // battery under RomM's name and each file under its path in the save
    // folder.
    if (own.form.batteryFirst) {
        auto isFile = [&](const cab::ZipFile& f) {
            for (const catalog::SaveFile& spec : own.files)
                if (endsWith(base(f.relPath), spec.captureSuffix) &&
                    !endsWith(base(f.relPath), spec.captureExclude))
                    return true;
            return false;
        };
        const cab::ZipFile* battery = nullptr;
        for (const cab::ZipFile& f : sent)
            if (endsWith(f.relPath, ".srm") || (!zipped && !isFile(f))) battery = &f;
        if (battery && !cab::passesCheck(battery->bytes, catalog::batteryCheck(own.platform),
                                         catalog::Untouched::Blank, why))
            return false;
        std::vector<cab::ZipFile> mine;
        if (battery) mine.push_back({base(battery->relPath), battery->bytes});
        for (const catalog::SaveFile& spec : own.files) {
            const cab::ZipFile* found = nullptr;
            for (const cab::ZipFile& f : sent)
                if (!found && &f != battery && endsWith(base(f.relPath), spec.captureSuffix) &&
                    !endsWith(base(f.relPath), spec.captureExclude))
                    found = &f;
            if (!found) continue;
            std::string w;
            if (!cab::passesCheck(found->bytes, spec.check, catalog::Untouched::Blank, &w)) {
                *why = base(found->relPath) + ": " + w;
                return false;
            }
            if (cab::holdsASave(found->bytes, spec.untouched))
                mine.push_back({spec.path, found->bytes});
        }
        if (mine.empty()) {
            *why = "nothing in RomM's answer is a save this emulator reads";
            return false;
        }
        if (battery && mine.size() == 1) {
            *out = battery->bytes;
            return true;
        }
        std::string zerr;
        if (!cab::zipFiles(mine, out, &zerr)) {
            *why = "could not zip it: " + zerr;
            return false;
        }
        return true;
    }

    // THE FILES A CORE WRITES, each found by the end of its name (the scan
    // after a session uses the same: cab::writtenFile); a file handed over
    // as stored is the core's first.
    std::vector<cab::ZipFile> mine;
    for (const catalog::SaveFile& spec : own.files) {
        const cab::ZipFile* found = nullptr;
        for (const cab::ZipFile& f : sent) {
            const std::string name = base(f.relPath);
            const bool fits =
                asStored && !zipped
                    ? &spec == &own.files.front()
                    : !spec.captureSuffix.empty()
                          ? endsWith(name, spec.captureSuffix) &&
                                !endsWith(name, spec.captureExclude)
                          : name == base(spec.path);
            if (fits && !found) found = &f;
        }
        if (!found) continue;
        std::string w;
        if (!cab::passesCheck(found->bytes, spec.check, spec.untouched, &w)) {
            *why = base(found->relPath) + ": " + w;
            return false;
        }
        mine.push_back({spec.path, found->bytes});
    }
    if (mine.empty()) {
        *why = "nothing in RomM's answer is a file this emulator reads";
        return false;
    }
    // The first file alone as it is; else a zip, each under its path in the
    // save folder, as a push of this console's makes them (savepush.h).
    if (mine.size() == 1 && mine.front().relPath == own.files.front().path) {
        *out = mine.front().bytes;
        return true;
    }
    std::string zerr;
    if (!cab::zipFiles(mine, out, &zerr)) {
        *why = "could not zip it: " + zerr;
        return false;
    }
    return true;
}

// ANOTHER EMULATOR'S SAVE, ASKED OF RomM IN THIS CONSOLE'S FORM AND CHECKED
// (#346, step 8; decided 2026-10-12; savepick.h). Which systems convert is
// RomM's own list: it converts a save through Sigil for the emulator named,
// hands a cartridge system's over as stored, and says a system it cannot
// convert "can't be converted for a core", whose file then travels as it is,
// checked the same way. Where Sigil names a difference RomM does not convert
// yet, or for a folder RomM cannot convert, a save handed over as stored is
// used only when this core made it (savepick::mayLoad). An arcade game's is
// refused before RomM is asked (OwnForm::arcade). False, with why, when it
// cannot be used.
static bool takeOther(romm::Client& client, const OwnForm& own, SaveFetch* out,
                      std::string* why) {
    const savepick::Pick& p = out->pick;
    const bool row = p.from == From::OtherRow;
    const int saveId = row ? p.row.id : p.channel.current.save.id;
    const std::string emulator = row ? p.row.emulator : p.channel.current.save.emulator;
    const std::string core = row ? std::string() : p.channel.current.save.core;
    if (arcadeRefuses(own, emulator, why)) return false;
    std::string query = "core=" + romm::encodeSegment(own.form.sigil);
    for (const auto& [key, value] : own.form.options)
        query += "&option=" + romm::encodeSegment(key + ":" + value);
    if (!own.form.profile.empty()) query += "&profile=" + romm::encodeSegment(own.form.profile);
    romm::SaveAnswer a = client.fetchSaveAs(saveId, query);
    const bool plain = a.status == 400 && a.error.find("can't be converted") != std::string::npos;
    if (plain) {
        std::fprintf(stderr, "[save] RomM converts no save of this system (%s); asked as stored\n",
                     a.error.c_str());
        a = client.fetchSaveAs(saveId, std::string());
    }
    if (a.status != 200) {
        *why = a.status == 0 ? "RomM did not answer: " + a.error
                             : "RomM answered " + std::to_string(a.status) + " (" + a.error + ")";
        return false;
    }
    std::vector<uint8_t> mine;
    if (!toOwnForm(a, plain, own, emulator, core, &mine, why)) return false;
    std::fprintf(stderr, "[save] %s from RomM in this console's form (%s, %zu bytes)%s\n",
                 pickedName(*out).c_str(), own.form.sigil.c_str(), mine.size(),
                 plain || (a.savePath.empty() && !cab::looksLikeZip(a.bytes)) ? ", as stored"
                                                                            : "");
    out->picked = std::move(mine);
    out->converted = true;
    return true;
}

// THE WORKER'S HALF OF EVERY RESTORE (#481, LAUNCH-17): what RomM holds and
// the pick (listSaves), then the bytes of the save the pick names, so
// whichever restore takes it has them: the snapshot's, or of the old rows the
// ones the restores choose below (the newest under the tag: of any kind for a
// folder save, in each region for a file save and the battery). Until #481
// each restore asked RomM on the frame thread behind the curtain, and a
// server that was slow or hung held a black screen with no way out.
void fetchSaves(romm::Client& client, const storage::User& user, int romId, int channelFileId,
                const char* tag, const OwnForm* own, const std::atomic<bool>* cancel,
                SaveFetch* out) {
    listSaves(client, user, romId, channelFileId, tag, out);
    if (!out->listed) return;
    if (out->pick.other) {
        if (cancel && cancel->load()) return;
        std::string why = !own                        ? "this start reads no other emulator's save"
                          : !romm::serverTakesSaves() ? notAsked()
                                                      : std::string();
        if (why.empty() && takeOther(client, *own, out, &why)) return;
        // THE NEWEST THIS CONSOLE CAN USE, and the start says it could not
        // use the newest (savepick.h).
        std::fprintf(stderr, "[save] game %d: could not use %s: %s\n", romId,
                     pickedName(*out).c_str(), why.c_str());
        out->pick = savepick::pick(out->facts, false);
        std::fprintf(stderr, "[save] game %d: %s\n", romId, out->pick.why.c_str());
    }
    if (out->pick.from == From::Channel) {
        if (cancel && cancel->load()) return;
        out->picked = snapshotSaveFor(client, user, romId, out->pick.channel.current.save);
    } else if (out->pick.from == From::OldRows) {
        for (const romm::Asset* a : savepick::oldRowsAStartMayTake(out->rows, tag)) {
            if (cancel && cancel->load()) return;
            out->bytes[a->id] = saveBytesFor(client, user, romId, *a);
        }
    }
}

// WHAT WAS FETCHED IS OLDER THAN A SAVE OF THIS GAME THAT LEFT THIS CONSOLE.
// The game's copy of RomM's saves is forgotten as each push of its save
// lands (savepush.cpp, send), so one landing after the worker's mark makes
// what was fetched older than what this console just sent: a game left and started
// again at once, its save still on its way up when the worker asked and there
// by the time the curtain was down. The console's own copy stands, as it
// would have had the upload still been owed; asked again, the server would
// only hand that copy back.
static bool fetchIsStale(const SaveFetch& f) {
    if (!savemirror::forgottenSince(f.romId, f.mark)) return false;
    std::fprintf(stderr, "[save] a save of this game reached the server after its saves were "
                 "fetched; this console's copy is the newer\n");
    return true;
}

// Where a restore's save comes from: the pick, unless nothing was asked or
// listed, or what was fetched has gone stale; then the console's own.
static From sourceOf(const SaveFetch& f) {
    if (!f.asked || !f.listed || fetchIsStale(f)) return From::Own;
    return f.pick.from;
}

// THE SAVE A START REPLACES IS KEPT ASIDE FIRST (#346; decided 2026-10-11
// with "no conflict screen"): one copy per person and game, in
// `saves/<platform>/<romId>/replaced/` under the name it has in the save
// folder, replaced by the next start that replaces one. A start writes over
// a save of this console's only when RomM holds a newer one that differs, so
// a console that is the only place a game is played never has a copy. Never
// sent anywhere; it is put back by hand. A file already there with these
// bytes is left.
static void keepAside(StartNotes* notes, const std::string& name,
                      const std::vector<uint8_t>& bytes) {
    if (!notes || notes->asideDir.empty() || bytes.empty()) return;
    const std::string path = notes->asideDir + "/" + name;
    const bool first = notes->keptAside.empty();
    notes->keptAside.push_back(name);
    if (cab::readBytes(path) != bytes) {
        if (!cab::writeBytes(path, bytes)) {
            std::fprintf(stderr, "[save] could not keep %s aside\n", path.c_str());
            notes->keptAside.pop_back();
            return;
        }
        std::fprintf(stderr, "[save] a newer save from RomM replaces this console's; its own "
                             "(%zu bytes) is kept at %s\n", bytes.size(), path.c_str());
    }
    // What an earlier start kept, and this one has not: one copy, the newest.
    if (!first) return;
    for (const cab::DirEntry& e : cab::listTree(notes->asideDir))
        if (e.relPath != name) cab::removeFile(notes->asideDir + "/" + e.relPath);
}

// What unpacking a save's files over `root` would change: `present`, the
// files there now that the archive names; how many of those hold other
// bytes; and how many it names that are not there.
struct TreeChange {
    std::vector<std::string> present;
    int differing = 0;
    int added = 0;
};
static TreeChange changeToTree(const std::vector<cab::ZipFile>& files, const std::string& root) {
    TreeChange c;
    for (const cab::ZipFile& f : files) {
        const std::string path = root + "/" + f.relPath;
        struct stat st;
        if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            ++c.added;
            continue;
        }
        c.present.push_back(f.relPath);
        if (static_cast<size_t>(st.st_size) != f.bytes.size() || cab::readBytes(path) != f.bytes)
            ++c.differing;
    }
    return c;
}

// The row's bytes as the worker fetched them; empty when it got none, which
// reads as the row coming back empty, as a failed fetch always has.
static const std::vector<uint8_t>& fetchedBytes(const SaveFetch& f, const romm::Asset& row) {
    static const std::vector<uint8_t> none;
    const auto it = f.bytes.find(row.id);
    return it == f.bytes.end() ? none : it->second;
}

// A DIRECTORY SAVE COMES DOWN before the game starts: the zip the pick names
// (savepick.h: a channel's snapshot, or the newest RomM holds under `tag`
// from before channels), unpacked into `root`. PSP's memory stick and a
// Switch game's save folder both travel this way. The files it would write
// over with other bytes are kept aside first (keepAside), and a folder that
// already holds exactly this save is left as it is.
//
// NEVER OVER A SAVE THIS CONSOLE HAS NOT SENT YET, which the pick sees to
// (savepick.h: a save still owed wins). Found 2026-09-29: an Xbox save
// recovered after a power cut was queued, the game started again at once, and
// the server's older zip came down over it.
//
// GAMECUBE'S CARDS FROM BEFORE (#344): `oldCard` is where Dolphin reads a raw
// card into its `.gci` folder when the folder is missing (writeDolphinConfig).
// When RomM's newest save for the game is still such a card, it is put there
// and Dolphin does the rest at boot. Only when the folder does not exist yet,
// and never over a card already on this console: that one is at least as new,
// and Dolphin reads it in itself.
//
// From what the launch worker fetched (fetchSaves); nothing is asked here.
cab::ZipShape zipShapeOf(const std::string& core, const std::string& platform,
                         const std::string& titleId) {
    if (core == "dolphin" && platform == "wii" && !wii::isMiiChannel(titleId))
        return cab::ZipShape::Wii;
    if (core == "azahar") return cab::ZipShape::ThreeDS;
    return cab::ZipShape::Folder;
}

std::string wiiKindOf(const std::string& titleId) {
    if (titleId.size() == 16) {
        std::string kind = titleId.substr(0, 8);
        for (char& c : kind) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return kind;
    }
    return "00010000";
}

void restoreDirSave(const SaveFetch& fetched, const char* tag, const std::string& root,
                    const std::string& title, const std::string& oldCard, StartNotes* notes,
                    cab::ZipShape shape, const std::string& wiiKind) {
    const From from = sourceOf(fetched);
    const std::vector<uint8_t>* picked = nullptr;
    std::string fileName;
    const romm::Asset* newest = nullptr;
    if (usesPicked(from)) {
        picked = &fetched.picked;
        fileName = pickedName(fetched);
    } else if (from == From::OldRows) {
        newest = savepick::newestOldRow(fetched.rows, tag);
        if (!newest) return;
        picked = &fetchedBytes(fetched, *newest);
        fileName = newest->fileName;
    } else {
        return;
    }
    const std::vector<uint8_t>& data = *picked;
    // SNIFFED, never taken from the name. The reference implementation's PSP
    // saves are an Apple directory archive wearing an `.srm` extension, so the
    // filename says nothing at all about what is inside. Anything that is not
    // a zip is left alone rather than guessed at — better a missing save than
    // a corrupted memory stick.
    if (cab::looksLikeZip(data)) {
        std::vector<cab::ZipFile> files;
        std::string uerr;
        if (!cab::readZip(data, &files, &uerr) || files.empty()) {
            std::fprintf(stderr, "[save] %s would not unpack: %s\n", fileName.c_str(),
                         files.empty() && uerr.empty() ? "archive held no files" : uerr.c_str());
            return;
        }
        // Sigil's shape back to the folder's own (dirsave.h).
        std::vector<uint8_t> reshaped;
        if (shape != cab::ZipShape::Folder) {
            files = cab::fromZipShape(shape, wiiKind, std::move(files));
            if (!cab::zipFiles(files, &reshaped, &uerr)) {
                std::fprintf(stderr, "[save] %s would not unpack: %s\n", fileName.c_str(),
                             uerr.c_str());
                return;
            }
        }
        const std::vector<uint8_t>& unpack = shape != cab::ZipShape::Folder ? reshaped : data;
        const TreeChange change = changeToTree(files, root);
        if (change.differing == 0 && change.added == 0) {
            std::fprintf(stderr, "[save] %s already holds %s (%zu file(s)); nothing to "
                                 "unpack\n", root.c_str(), fileName.c_str(), files.size());
        } else {
            if (change.differing > 0) {
                std::vector<uint8_t> own;
                std::string zerr;
                if (cab::zipTree(root, change.present, &own, &zerr))
                    keepAside(notes, dirSaveZipName(title), own);
                else
                    std::fprintf(stderr, "[save] could not archive the save folder to keep it "
                                         "aside: %s\n", zerr.c_str());
            }
            if (!cab::unzipTree(unpack, root, &uerr)) {
                std::fprintf(stderr, "[save] %s would not unpack: %s\n", fileName.c_str(),
                             uerr.c_str());
                return;
            }
            std::fprintf(stderr, "[save] unpacked %s (%zu bytes) into %s\n", fileName.c_str(),
                         data.size(), root.c_str());
        }
        if (usesPicked(from) && notes) {
            notes->tookPick = true;
            if (change.differing > 0 || change.added > 0) notes->changedOwn = true;
        }
        if (from == From::OldRows && notes)
            notes->oldRowsTaken.push_back({newest->id, savepick::OldRowTaken::As::Folder, "", 0});
    } else if (from == From::OldRows && !oldCard.empty() && !data.empty()) {
        if (storage::exists(root)) {
            std::fprintf(stderr, "[save] %s is a card from before; %s already holds this game's "
                         "saves, so it is left alone\n", fileName.c_str(), root.c_str());
        } else if (storage::exists(oldCard)) {
            std::fprintf(stderr, "[save] %s is a card from before; this console's own %s is read "
                         "in instead\n", fileName.c_str(), oldCard.c_str());
        } else if (!cab::holdsASave(data, catalog::Untouched::GameCubeDirectory)) {
            std::fprintf(stderr, "[save] %s is a card with no saves on it; left alone\n",
                         fileName.c_str());
        } else if (cab::writeBytes(oldCard, data)) {
            std::fprintf(stderr, "[save] %s (%zu bytes) placed at %s; Dolphin reads it into "
                         "%s at boot\n", fileName.c_str(), data.size(),
                         oldCard.c_str(), root.c_str());
        } else {
            std::fprintf(stderr, "[save] could not write %s\n", oldCard.c_str());
        }
    } else if (!data.empty()) {
        std::fprintf(stderr, "[save] %s is not a zip (%02x %02x %02x %02x) — left alone\n",
                     fileName.c_str(), data[0], data[1],
                     data.size() > 2 ? data[2] : 0, data.size() > 3 ? data[3] : 0);
    }
}

// The save's files under a directory-save root, and only those.
std::vector<cab::DirEntry> listDirSave(const GameSession& sess) {
    std::vector<cab::DirEntry> all = cab::listTree(sess.dirSaveRoot);
    std::vector<cab::DirEntry> out;
    for (cab::DirEntry& e : all)
        if (catalog::inDirectorySave(sess.dirSaveCore.c_str(), sess.dirSavePlatform,
                                     sess.dirSaveTitleId, e.relPath))
            out.push_back(std::move(e));
    return out;
}

// The name a folder save's zip has on this console, in the person's save
// folder for the game and in the copy a start keeps aside.
std::string dirSaveZipName(const std::string& title) { return sanitisedStem(title) + ".zip"; }

// A directory save, zipped: PSP, Wii, GameCube, and the folder saves of the
// five separate emulators (Switch, PS3, Xbox, Xbox 360, Wii U). See dirsave.h
// for why the archive is a zip and where it is rooted. The zip is written to
// this person's save folder for the game, which is the copy the snapshot
// pushed when the game is left sends (savepush.h).
//
// THE WHOLE TREE, whenever anything in it changed. It used to be only the
// top-level folders created or touched since the game started, which was how
// one shared PSP save directory told its games apart. The directory is one
// game's now (GameSession), and a save on RomM is the whole save, so sending
// only the touched folders left RomM with part of it: found 2026-09-30 when
// an Xbox 360 upload of Forza's profile folder dropped the game's shared
// `0000000000000000` folder from RomM, and a PSP game with two save folders
// would lose the one not written that session. Another console restoring
// that zip never got the rest. The baseline still decides whether anything
// was written at all.
void syncDirSave(GameSession& sess) {
    std::map<std::string, cab::DirEntry> before;
    for (const cab::DirEntry& e : sess.dirAtLaunch) before[e.relPath] = e;

    const std::vector<cab::DirEntry> now = listDirSave(sess);
    std::set<std::string> touchedFolders;
    for (const cab::DirEntry& e : now) {
        auto it = before.find(e.relPath);
        if (it != before.end() && it->second == e) continue;
        const size_t slash = e.relPath.find('/');
        touchedFolders.insert(slash == std::string::npos ? e.relPath
                                                         : e.relPath.substr(0, slash));
    }
    if (touchedFolders.empty()) return;   // the game saved nothing. Normal.

    std::vector<std::string> paths;
    for (const cab::DirEntry& e : now) paths.push_back(e.relPath);

    std::vector<uint8_t> zip;
    std::string zerr;
    // In RomM's shape for the system (dirsave.h, ZipShape), else as the
    // folder is.
    const cab::ZipShape shape =
        zipShapeOf(sess.dirSaveCore, sess.dirSavePlatform, sess.dirSaveTitleId);
    bool zipped = false;
    if (shape == cab::ZipShape::Folder) {
        zipped = cab::zipTree(sess.dirSaveRoot, paths, &zip, &zerr);
    } else {
        std::vector<cab::ZipFile> files;
        for (const std::string& rel : paths)
            files.push_back({rel, cab::readBytes(sess.dirSaveRoot + "/" + rel)});
        zipped = cab::zipFiles(cab::toZipShape(shape, std::move(files)), &zip, &zerr);
    }
    if (!zipped) {
        std::fprintf(stderr, "[save] could not archive the save folder: %s\n", zerr.c_str());
        return;
    }
    // Local first, always. The network is the part that can fail, and a save
    // that only exists in a pending upload is a save that can be lost.
    const std::string path = sess.saveDir + "/" + dirSaveZipName(sess.title);
    if (!cab::writeBytes(path, zip)) {
        std::fprintf(stderr, "[save] could not write %s\n", path.c_str());
        return;
    }
    std::fprintf(stderr, "[save] %zu file(s), %zu folder(s) changed, %zu bytes zipped to %s\n",
                 paths.size(), touchedFolders.size(), zip.size(), path.c_str());
    sess.dirAtLaunch = now;   // this is the new baseline; do not gather it twice
    sess.saveChanged = true;
}

// A GAME WHOSE SAVES NEVER GOT ZIPPED: Xbox 360, Wii U, Switch and PS3, where
// the emulator writes straight into the person's folder and a note says a game is running there
// (standalone::beforeStart). The console went down, or the frontend did,
// before the end of the game. The whole folder is zipped now, as if the game
// had just ended with everything changed, and goes up as that game's snapshot
// (savepush.h, leave), owed until it lands, so the start that follows keeps
// it rather than unpacking the server's older zip over it (savepick.h). The
// note names the game and the tag, and says when the game had started from
// this console's own save because the newest on RomM could not be used, so
// its save is still never put on top of that one. The same note, in the same
// place, is how an Xbox game's saves reach the server when another person's
// start brought them off the drive into this person's folder
// (App::recoverDriveSaves, #484).
void finishInterruptedSave(const cab::standalone::Emulator& emu,
                           const std::string& saveDir, Uploader& up) {
    GameSession owed = sessionOfPlayingNote(cab::standalone::playingNote(saveDir));
    owed.saveDir = saveDir;
    owed.dirSaveRoot = cab::standalone::saveRoot(emu, saveDir);   // no baseline: all of it
    std::fprintf(stderr, "[save] %s was never zipped after its last game; sending it all\n",
                 saveDir.c_str());
    if (owed.romId > 0 && !owed.title.empty()) {
        syncDirSave(owed);
        savepush::leave(owed, storage::currentUser(), up);
    }
    cab::standalone::finished(emu, saveDir);
}

void notePlaying(GameSession& sess) {
    if (sess.saveTag.empty() || (sess.fileSaves.empty() && sess.dirSaveRoot.empty())) return;
    // To the nanosecond, after the restores: a file's time is the kernel's
    // coarse clock, never ahead of this one, so a restore's write is older.
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    sess.playingSince = static_cast<int64_t>(now.tv_sec) * 1000000000 + now.tv_nsec;
    if (!cab::standalone::writePlayingNote(sess.saveDir, playingNoteFor(sess, sess.saveDir)))
        std::fprintf(stderr, "[save] could not note game %d as playing in %s\n", sess.romId,
                     sess.saveDir.c_str());
}

bool finishInterruptedPlay(const std::string& saveDir, Uploader& up) {
    GameSession sess = sessionOfPlayingNote(cab::standalone::playingNote(saveDir));
    sess.saveDir = saveDir;
    const int64_t since = sess.playingSince;
    if (sess.romId <= 0 || since <= 0) {
        cab::standalone::removePlayingNote(saveDir);
        return false;
    }
    // The core's own files, each counted as written this session only when
    // it was written after the note; the rest stand as their own baseline.
    for (const catalog::SaveFile& spec :
         catalog::saveFiles(sess.platformSlug, sess.core, sess.romFile, sess.arcade)) {
        cab::FileSaveState f;
        f.spec = spec;
        f.path = saveDir + "/" + spec.path;
        f.hadOne = true;
        const std::string written = cab::writtenFile(spec, f.path);
        std::vector<uint8_t> data =
            written.empty() ? std::vector<uint8_t>() : cab::readBytes(written);
        std::string why;
        if (!data.empty() && cab::modifiedNs(written) > since &&
            !cab::passesCheck(data, spec.check, spec.untouched, &why)) {
            std::fprintf(stderr, "[save] %s, written before the game ended, is not kept: %s\n",
                         written.c_str(), why.c_str());
            f.atLaunch = std::move(data);
        } else if (data.empty() || cab::modifiedNs(written) <= since) {
            f.atLaunch = std::move(data);
        }
        sess.fileSaves.push_back(std::move(f));
    }
    // A save folder: its files written after the note are this session's.
    if (const char* sub = catalog::directorySaveRoot(sess.core.c_str(), sess.platformSlug,
                                                     sess.dirSaveTitleId)) {
        sess.dirSaveRoot = saveDir + "/" + sub;
        sess.dirSaveCore = sess.core;
        sess.dirSavePlatform = sess.platformSlug;
        for (const cab::DirEntry& e : listDirSave(sess))
            if (e.mtimeNs <= since) sess.dirAtLaunch.push_back(e);
        syncDirSave(sess);
    }
    syncFileSaves(sess);
    const bool changed = sess.saveChanged;
    std::fprintf(stderr, "[save] game %d's last session in %s never ended; %s\n", sess.romId,
                 sess.core.c_str(),
                 changed ? "what it saved goes up as its snapshot" : "it saved nothing");
    const storage::User who = storage::currentUser();
    // A START ALREADY FETCHING SEES ITS LIST IS STALE (fetchIsStale), as when
    // a push lands meanwhile: this console's save is the newer.
    if (changed) savemirror::forget(who, sess.romId);
    savepush::leave(sess, who, up);
    cab::standalone::removePlayingNote(saveDir);
    return changed;
}

// THE NOTE AN EMULATOR RUN AS A PROGRAM IS STARTED WITH (standalone.h), and
// a game in a core whose saves are files (notePlaying), one fact a line: the
// game's RomM id, this person's save folder for it, its title, the tag its
// saves travel under, "1" when the start could not use the newest save on
// RomM (GameSession::couldNotUseNewest), and the save's picture the sitting
// took (GameSession::savePicture), written again as each is taken
// (savepicture.h) and empty until then. Then what finishInterruptedPlay
// rebuilds a core's saves from: the core, the platform, the file the core
// was handed, "1" for an arcade game, the game's title ID, when the note was
// first written, and the stem a save goes up named after (GameSession::fsStem).
std::string playingNoteFor(const GameSession& sess, const std::string& saveDir) {
    return std::to_string(sess.romId) + "\n" + saveDir + "\n" + sess.title + "\n" +
           sess.saveTag + "\n" + (sess.couldNotUseNewest ? "1" : "0") + "\n" +
           sess.savePicture + "\n" + sess.core + "\n" + sess.platformSlug + "\n" +
           sess.romFile + "\n" + (sess.arcade ? "1" : "0") + "\n" + sess.dirSaveTitleId +
           "\n" + std::to_string(sess.playingSince) + "\n" + sess.fsStem + "\n";
}

// And read back, into a session with nothing else in it. Its second line,
// the folder, is the caller's to read: it decides whose the saves are.
GameSession sessionOfPlayingNote(const std::string& text, std::string* saveDir) {
    std::istringstream note(text);
    std::string id, dir, title, tag, couldNot, picture, arcade, since;
    GameSession sess;
    std::getline(note, id);
    std::getline(note, dir);
    std::getline(note, title);
    std::getline(note, tag);
    std::getline(note, couldNot);
    std::getline(note, picture);
    std::getline(note, sess.core);
    std::getline(note, sess.platformSlug);
    std::getline(note, sess.romFile);
    std::getline(note, arcade);
    std::getline(note, sess.dirSaveTitleId);
    std::getline(note, since);
    std::getline(note, sess.fsStem);
    sess.romId = std::atoi(id.c_str());
    sess.title = title;
    sess.saveTag = tag;
    sess.couldNotUseNewest = couldNot == "1";
    sess.savePicture = picture;
    sess.arcade = arcade == "1";
    sess.playingSince = std::atoll(since.c_str());
    if (saveDir) *saveDir = dir;
    return sess;
}

// THE NAME THIS CONSOLE KEEPS ITS OWN COPY OF A SAVE UNDER, in the person's
// save folder for the game: `<the server's file name without its extension>
// (Cabinet).<region>`. A battery's copy, which the core never writes itself;
// and until #346, step 8, a Dreamcast card's, when Flycast kept it in the
// system directory, which a start now copies once to the game's own card
// (catalog::SaveFile::fromFormerCopy). Until
// save channels (#346) it was also the name each save's row had on RomM, the
// retired Apple apps' own, where RomM replaced a row by file name alone; a
// snapshot's save is named by its hash, and the name it goes up with is the
// emulator's own (savepush.h). The copies already on a console keep this
// name, so it does not change. The region is the extension: "srm" for the
// game's own save.
std::string saveRowName(const std::string& fsStem, const std::string& region) {
    return sanitisedStem(fsStem) + " (Cabinet)." + region;
}

// Puts a game's save files where the core will look for them, and remembers
// what was in them.
//
// RUN BEFORE `retro_load_game` AND NOWHERE ELSE. Every core in this class
// reads its save file once while the machine is being built and never looks
// again, so a file placed afterwards is a file the game has already decided is
// not there. That is the same rule PSP's directory save taught, and it is the
// reason this cannot be folded into the battery restore below, which happens
// after the load because the core can be handed bytes directly.
//
// WHICH COPY WINS. A save this console still owes the server wins outright —
// it is strictly newer than anything the server has, and without that rule a
// launch after an offline session would fetch the older copy and write it over
// the top; the pick says so (savepick.h), and then nothing comes from RomM.
// Otherwise the save the pick names wins, because that is how a card made on
// another device arrives; this console's own file that it replaces is kept
// aside first (keepAside). Failing both, whatever is on this disk plays.
//
// A SNAPSHOT'S SAVE, FOR THESE CORES (saves.h): one file as it is, which is
// the first of the core's save files and never a cartridge (Saturn's one
// file is its battery, restoreBatterySave's); or a zip, each of the core's
// files in it under its path in the save folder (or under its name alone).
//
// RomM's saves are what the launch worker fetched (fetchSaves); nothing is
// asked here.
std::vector<cab::FileSaveState> restoreFileSaves(
        const std::vector<catalog::SaveFile>& specs, const std::string& fsStem,
        const std::string& saveDir, const char* tag, const SaveFetch& fetched,
        StartNotes* notes) {
    std::vector<cab::FileSaveState> out;
    if (specs.empty()) return out;

    if (tag && fetched.asked && !fetched.listed)
        std::fprintf(stderr, "[save] could not ask the server: %s\n", fetched.error.c_str());
    From source = tag ? sourceOf(fetched) : From::Own;
    // The picked snapshot's save, when it is a zip: the files in it.
    std::vector<cab::ZipFile> pickedFiles;
    const bool pickedIsZip = usesPicked(source) && cab::looksLikeZip(fetched.picked);
    if (pickedIsZip) {
        std::string zerr;
        if (!cab::readZip(fetched.picked, &pickedFiles, &zerr)) {
            std::fprintf(stderr, "[save] %s would not unpack: %s; this console's saves stand\n",
                         pickedName(fetched).c_str(), zerr.c_str());
            source = From::Own;
        }
    }
    for (const catalog::SaveFile& spec : specs) {
        cab::FileSaveState f;
        f.spec = spec;
        f.path = saveDir + "/" + spec.path;

        // THIS PERSON'S CARD FROM BEFORE (savetable.h, fromFormerCopy), copied
        // once to the name the core reads, the old file left as it is.
        if (spec.fromFormerCopy && cab::writtenFile(spec, f.path).empty()) {
            const std::string former = saveDir + "/" + saveRowName(fsStem, spec.region);
            const std::vector<uint8_t> was = cab::readBytes(former);
            if (!was.empty() && cab::writeBytes(f.path, was))
                std::fprintf(stderr, "[save] this console's card from before, %s, copied to %s; "
                                     "the old file stays\n", former.c_str(), f.path.c_str());
        }

        // What this console already has. Found by scanning rather than by the
        // predicted name, because the name the core chose last time is the one
        // that is actually on the disk.
        const std::string existing = cab::writtenFile(spec, f.path);
        std::vector<uint8_t> chosen =
            existing.empty() ? std::vector<uint8_t>() : cab::readBytes(existing);
        const bool haveLocal = !chosen.empty();
        const char* from = haveLocal ? "this console" : nullptr;

        // RomM's save for this file, by the pick: of the old rows the newest
        // in this file's region; of a snapshot's save, the file itself or
        // this file's entry in its zip.
        const std::vector<uint8_t>* incoming = nullptr;
        std::string incomingName;
        const romm::Asset* oldRow = nullptr;
        if (source == From::OldRows) {
            if ((oldRow = savepick::newestOldRow(fetched.rows, tag, spec.region.c_str()))) {
                incoming = &fetchedBytes(fetched, *oldRow);
                incomingName = oldRow->fileName;
            }
        } else if (usesPicked(source)) {
            incomingName = pickedName(fetched);
            if (!pickedIsZip) {
                if (&spec == &specs.front() && spec.region != "cart") incoming = &fetched.picked;
            } else {
                const std::string base = spec.path.substr(spec.path.find_last_of('/') + 1);
                for (const cab::ZipFile& z : pickedFiles)
                    if (z.relPath == spec.path) incoming = &z.bytes;
                for (const cab::ZipFile& z : pickedFiles)
                    if (!incoming && z.relPath.substr(z.relPath.find_last_of('/') + 1) == base)
                        incoming = &z.bytes;
                // By the end of its name, as the scan after a session finds
                // it (cab::writtenFile): Saturn's cartridge is named after
                // the file the core was handed, which another console's may
                // name otherwise.
                auto endsWith = [](const std::string& s, const std::string& end) {
                    return !end.empty() && s.size() >= end.size() &&
                           s.compare(s.size() - end.size(), end.size(), end) == 0;
                };
                for (const cab::ZipFile& z : pickedFiles)
                    if (!incoming && endsWith(z.relPath, spec.captureSuffix) &&
                        !endsWith(z.relPath, spec.captureExclude))
                        incoming = &z.bytes;
                incomingName += ", " + spec.path;
            }
        }
        bool fromRomM = false, changed = false;
        if (incoming) {
            const std::vector<uint8_t>& data = *incoming;
            // A save that holds nothing a game wrote does not get to
            // replace one that does. Three of the thirteen Dreamcast cards
            // on the reference server are formatted and empty, uploaded by
            // an implementation with no freshness rule, and restoring one
            // of those over a real card would be this feature losing a
            // save rather than moving one. One that is byte for byte this
            // console's own copy replaces nothing, and is what is in place.
            if (data.empty()) {
                std::fprintf(stderr, "[save] %s came back empty\n", incomingName.c_str());
            } else if (haveLocal && data != chosen && !cab::holdsASave(data, spec.untouched)) {
                std::fprintf(stderr,
                             "[save] %s holds no save — keeping this console's copy\n",
                             incomingName.c_str());
            } else {
                changed = chosen != data;
                // This console's own file, under its name in the save
                // folder, before RomM's goes over it.
                if (haveLocal && changed) {
                    const bool inSaveDir = existing.compare(0, saveDir.size() + 1,
                                                            saveDir + "/") == 0;
                    keepAside(notes, inSaveDir ? existing.substr(saveDir.size() + 1)
                                               : existing.substr(existing.find_last_of('/') + 1),
                              chosen);
                }
                chosen = data;
                from = "the server";
                fromRomM = true;
            }
        }

        if (!chosen.empty()) {
            if (!cab::writeBytes(f.path, chosen)) {
                std::fprintf(stderr, "[save] could not write %s\n", f.path.c_str());
            } else {
                std::fprintf(stderr, "[save] %zu bytes from %s into %s\n", chosen.size(),
                             from ? from : "nowhere", f.path.c_str());
                // A file of the core's own naming would be read before this
                // one (savetable.h, coreNamesItself); what it held is in
                // `chosen`, or kept aside.
                if (spec.coreNamesItself) cab::removeOtherCaptures(spec, f.path);
                f.atLaunch = chosen;
                f.hadOne = true;
                f.kept = f.path;
                if (fromRomM && usesPicked(source) && notes) {
                    notes->tookPick = true;
                    if (changed) notes->changedOwn = true;
                }
                if (fromRomM && oldRow && notes)
                    notes->oldRowsTaken.push_back(
                        {oldRow->id, savepick::OldRowTaken::As::File, spec.path,
                         static_cast<size_t>(&spec - &specs.front()), spec.region == "cart"});
            }
        } else {
            std::fprintf(stderr, "[save] no save anywhere for %s yet\n", spec.path.c_str());
        }
        out.push_back(std::move(f));
    }
    return out;
}

// The saves a core wrote as files, read back after it shut down.
//
// RUN AFTER `retro_unload_game` AND NOWHERE ELSE. Every core in this class
// buffers, and Flycast does not even close the VMU until its device is
// destroyed at teardown, so a capture taken mid-session reads a partially
// written card. The reference implementation captured Dreamcast on pause once
// and uploaded exactly such a half-written image; the game that later restored
// it reported the file corrupt. That is the whole reason this is a separate
// function called from one place rather than part of syncSave.
//
// WHAT COUNTS AS A CHANGE. Only a file whose bytes differ from the baseline
// taken at launch, so a session that looked at a title screen and quit sends
// nothing; and, when there was no save anywhere to begin with, only one that
// holds something a game actually wrote. Once a real save has existed every
// later change travels, erasing one included, because losing history is worse
// than an empty save. A change is left in the session (`saveChanged`, and
// each file's bytes and where they are kept) for the snapshot that goes up
// once the game has closed (savepush.h).
void syncFileSaves(GameSession& sess) {
    for (cab::FileSaveState& f : sess.fileSaves) {
        std::string written = cab::writtenFile(f.spec, f.path);
        std::vector<uint8_t> data =
            written.empty() ? std::vector<uint8_t>() : cab::readBytes(written);

        // THE FILE THE CORE NAMED ITSELF goes back to the name a start puts it
        // at (savetable.h, coreNamesItself), there before anything else, so
        // what is on this console between games is one card under one name.
        if (f.spec.coreNamesItself && !written.empty() && written != f.path && !data.empty()) {
            if (!cab::writeBytes(f.path, data)) {
                std::fprintf(stderr, "[save] could not write %s; the card stays at %s\n",
                             f.path.c_str(), written.c_str());
                continue;
            }
            cab::removeFile(written);
            written = f.path;
        }

        if (data.empty() || data == f.atLaunch) continue;
        if (!f.hadOne && !cab::holdsASave(data, f.spec.untouched)) {
            std::fprintf(stderr,
                         "[save] %s is what the core writes by starting up, not a save — "
                         "not sending it\n", written.c_str());
            continue;
        }

        // LOCAL FIRST, ALWAYS: the file is already in this person's save
        // folder, the folder the core was handed, before a byte is offered to
        // the network.
        std::fprintf(stderr, "[save] %zu bytes from %s\n", data.size(),
                     f.spec.path.c_str());
        f.atLaunch = std::move(data);    // the new baseline; do not gather it twice
        f.hadOne = true;
        f.kept = written;
        sess.saveChanged = true;
    }
}

// Reads the game's own save out of the core and writes it to this console,
// but only when it has actually changed. A cartridge with no battery returns
// nothing, which is a normal answer and not a failure. A change is left in
// the session for the snapshot that goes up once the game has closed
// (savepush.h); the copy written here is what that sends.
//
// THE CARTRIDGE'S CLOCK (#346, step 11; catalog::SaveForm::clock) is read
// out beside it, and this console's copy of it written when it changed. A
// change to the clock alone is no new save, so it sends nothing: it goes up
// with the game's next save, and this console's next start keeps it.
void syncSave(GameSession& sess) {
    // The one platform whose save is a tree. It never reaches the save-RAM path
    // below, because PPSSPP answers RETRO_MEMORY_SAVE_RAM with nothing at all.
    if (!sess.dirSaveRoot.empty()) { syncDirSave(sess); return; }
    cab::Core& core = cab::Core::shared();
    if (std::vector<uint8_t> clock;
        catalog::saveForm(sess.core.c_str(), sess.platformSlug).clock && core.readClock(clock) &&
        !clock.empty() && clock != sess.clockAtLaunch) {
        const std::string path = sess.saveDir + "/" + saveRowName(sess.fsStem, "rtc");
        if (cab::writeBytes(path, clock)) {
            std::fprintf(stderr, "[save] the clock (%zu bytes) to %s; no new save by itself\n",
                         clock.size(), path.c_str());
            sess.clockAtLaunch = std::move(clock);
        } else {
            std::fprintf(stderr, "[save] could not write %s\n", path.c_str());
        }
    }
    std::vector<uint8_t> ram;
    if (!core.readSaveRam(ram) || ram.empty()) return;
    if (ram == sess.saveAtLaunch) return;     // nothing happened worth sending

    const std::string path = sess.saveDir + "/" + saveRowName(sess.fsStem, "srm");
    if (!cab::writeBytes(path, ram)) {
        std::fprintf(stderr, "[save] could not write %s\n", path.c_str());
        return;
    }
    std::fprintf(stderr, "[save] %zu bytes to %s\n", ram.size(), path.c_str());
    sess.saveAtLaunch = std::move(ram);
    sess.saveChanged = true;
}

bool batteryParts(const std::vector<uint8_t>& save, BatteryParts* out, std::string* why) {
    *out = BatteryParts{};
    if (!cab::looksLikeZip(save)) {
        out->battery = save;
        return true;
    }
    out->zipped = true;
    std::vector<cab::ZipFile> files;
    if (!cab::readZip(save, &files, why)) return false;
    auto endsWith = [](const std::string& s, const char* end) {
        const size_t n = std::strlen(end);
        return s.size() > n && s.compare(s.size() - n, n, end) == 0;
    };
    for (cab::ZipFile& f : files) {
        if (endsWith(f.relPath, ".srm")) out->battery = std::move(f.bytes);
        else if (endsWith(f.relPath, ".rtc")) out->clock = std::move(f.bytes);
    }
    return true;
}

const std::vector<uint8_t>* clockToSet(const std::vector<uint8_t>& incoming,
                                       const std::vector<uint8_t>& local, size_t size,
                                       bool tookNewBattery) {
    const bool localFits = size > 0 && local.size() == size;
    if (size > 0 && incoming.size() == size && (tookNewBattery || !localFits)) return &incoming;
    return localFits ? &local : nullptr;
}

// THE CARTRIDGE'S CLOCK, beside the battery (#346, step 11): the one
// clockToSet names, written into the core, and this console's copy of it
// following it when it came with RomM's battery; the copy it replaces is
// kept aside with its battery.
static void restoreClock(cab::Core& core, GameSession& session, const std::string& saveDir,
                         const std::vector<uint8_t>& incoming, const std::string& incomingName,
                         bool tookNewBattery, StartNotes* notes) {
    const std::string name = saveRowName(session.fsStem, "rtc");
    const std::vector<uint8_t> local = cab::readBytes(saveDir + "/" + name);
    const size_t size = core.clockSize();
    if (!incoming.empty() && incoming.size() != size)
        std::fprintf(stderr, "[save] %s's clock is %zu bytes where this core's is %zu: left "
                             "out\n", incomingName.c_str(), incoming.size(), size);
    const std::vector<uint8_t>* set = clockToSet(incoming, local, size, tookNewBattery);
    if (set && core.writeClock(*set)) {
        if (set == &incoming && incoming != local) {
            if (!local.empty()) keepAside(notes, name, local);
            if (!cab::writeBytes(saveDir + "/" + name, incoming))
                std::fprintf(stderr, "[save] could not write %s/%s\n", saveDir.c_str(),
                             name.c_str());
        }
        std::fprintf(stderr, "[save] the clock from %s\n",
                     set == &incoming ? incomingName.c_str() : name.c_str());
    }
    core.readClock(session.clockAtLaunch);
}

// THE BATTERY SAVE: the save the pick names (savepick.h), which is none while
// this console's own copy is still owed, and this console's copy whenever
// RomM has none to give (#259). Until 2026-10-05 the local `.srm` syncSave
// writes at every quit was never read back, so a game started with the
// server away began with an empty battery, and one started while its upload
// was still owed got RomM's OLDER copy over the newer one. What RetroArch
// does: the frontend loads the local `.srm`. This console's own `.srm`, when
// RomM's differs from it, is kept aside first (keepAside). The cartridge's
// clock with it (restoreClock), where it travels.
// Stage 8 of a launch (launch.cpp), after the load and before the first frame.
// RomM's saves are what the launch worker fetched (fetchSaves); nothing is
// asked here.
void restoreBatterySave(cab::Core& core, GameSession& session, const std::string& saveDir,
                        const SaveFetch& fetched, StartNotes* notes) {
    const bool clock = catalog::saveForm(session.core.c_str(), session.platformSlug).clock &&
                       core.clockSize() > 0;
    // The names a battery and its clock have in a zip of them (savepush.cpp,
    // gather), for a start that brings rows from before channels in.
    const std::string zipStem = sanitisedStem(session.fsStem);
    std::vector<uint8_t> incomingClock;
    std::string incomingName;
    bool tookNewBattery = false;
    if (core.saveRamSize() > 0) {
        const std::string name = saveRowName(session.fsStem, "srm");
        const std::vector<uint8_t> local = cab::readBytes(saveDir + "/" + name);
        BatteryParts parts;
        const std::vector<uint8_t>* incoming = nullptr;
        const romm::Asset* oldRow = nullptr;
        const romm::Asset* clockRow = nullptr;
        From source = From::Own;
        if (!session.saveTag.empty() && fetched.asked) {
            if (!fetched.listed)
                std::fprintf(stderr, "[save] could not ask the server: %s\n",
                             fetched.error.c_str());
            source = sourceOf(fetched);
            if (source == From::OldRows) {
                // THE BATTERY ROW, and the clock's beside it. Cabinet filed a
                // Game Boy clock as its own 8-byte `.rtc` row under the same
                // tag; taken as the battery, its bytes went over the start of
                // the save (#346, found in the audit). It is Gambatte's own
                // clock, which goes in as the clock, the newest of its row as
                // the battery is (#346, step 11).
                if ((oldRow = savepick::newestOldRow(fetched.rows, session.saveTag, "srm"))) {
                    incoming = &fetchedBytes(fetched, *oldRow);
                    incomingName = oldRow->fileName;
                    if (clock &&
                        (clockRow = savepick::newestOldRow(fetched.rows, session.saveTag, "rtc")))
                        incomingClock = fetchedBytes(fetched, *clockRow);
                }
            } else if (usesPicked(source)) {
                incomingName = pickedName(fetched);
                std::string why;
                // A battery travels as it is, or in a zip with its clock or
                // its cartridge (saves.h). A zip copied into the core's
                // memory as it is would be the save lost.
                if (!batteryParts(fetched.picked, &parts, &why))
                    std::fprintf(stderr, "[save] %s would not unpack: %s; this console's save "
                                         "stands\n", incomingName.c_str(), why.c_str());
                else if (parts.zipped && parts.battery.empty())
                    std::fprintf(stderr, "[save] %s holds no battery; this console's stands\n",
                                 incomingName.c_str());
                // ANOTHER EMULATOR'S IS THE CORE'S OWN SIZE OR NOTHING (#346,
                // step 8): the rest of its check was the worker's
                // (toOwnForm), and this is the part only the loaded core can
                // say. This console's own saves keep the rule below.
                else if (fetched.converted && parts.battery.size() != core.saveRamSize())
                    std::fprintf(stderr, "[save] %s is %zu bytes where this core's save is "
                                         "%zu: not a save it reads; this console's stands\n",
                                 incomingName.c_str(), parts.battery.size(),
                                 core.saveRamSize());
                else
                    incoming = &parts.battery;
                if (incoming) incomingClock = parts.clock;
            }
        }
        bool fromServer = false;
        if (incoming && !incoming->empty()) {
            const bool changed = local != *incoming;
            if (!local.empty() && changed) keepAside(notes, name, local);
            if (core.writeSaveRam(*incoming)) {
                session.saveAtLaunch = *incoming;
                fromServer = true;
                tookNewBattery = changed;
                std::fprintf(stderr, "[save] restored %s (%zu bytes)\n", incomingName.c_str(),
                             incoming->size());
                // THIS CONSOLE'S COPY FOLLOWS THE SAVE IT STARTED FROM, as a
                // core's own file and a save folder do by being written on
                // the disk. A battery is only in the core's memory, and its
                // copy here was written only when a game saved: a start from
                // another device's save that saved nothing left the older
                // copy, and the next start that could take nothing from RomM
                // (the server away and never seen, or its newest a save this
                // console cannot use) went back to it.
                if (changed && !cab::writeBytes(saveDir + "/" + name, *incoming))
                    std::fprintf(stderr, "[save] could not write %s/%s\n", saveDir.c_str(),
                                 name.c_str());
                if (usesPicked(source) && notes) {
                    notes->tookPick = true;
                    if (changed) notes->changedOwn = true;
                }
                if (oldRow && notes)
                    notes->oldRowsTaken.push_back({oldRow->id, savepick::OldRowTaken::As::Battery,
                                                   zipStem + ".srm", 0});
                if (clockRow && notes && incomingClock.size() == core.clockSize())
                    notes->oldRowsTaken.push_back({clockRow->id, savepick::OldRowTaken::As::Clock,
                                                   zipStem + ".rtc", 0});
            }
        }
        if (!fromServer) incomingClock.clear();
        if (!fromServer && !local.empty() && core.writeSaveRam(local)) {
            session.saveAtLaunch = local;
            std::fprintf(stderr, "[save] restored this console's %s (%zu bytes)\n",
                         name.c_str(), local.size());
        }
    }
    if (session.saveAtLaunch.empty()) core.readSaveRam(session.saveAtLaunch);
    if (clock)
        restoreClock(core, session, saveDir, incomingClock, incomingName, tookNewBattery, notes);
    std::fprintf(stderr, "[save] battery is %zu bytes%s\n", core.saveRamSize(),
                 clock ? (", clock " + std::to_string(core.clockSize())).c_str() : "");
}

// WHAT A START LEAVES IN THE NOTES (#346), once its restores have run. In the
// session: the file the game's channels are keyed to, the channel and
// snapshot the start continued from when one was put in place, and whether
// the newest save on RomM could not be used, which is also so when the
// picked snapshot's save did not come or could not be read. On the console:
// that snapshot becomes the one it holds of the game, with its save's hash
// among those it has had (savepick::heldAfter). A start that put RomM's save
// from before channels in place marks the game's push that brings it into a
// channel (savepick::oldSaveToCopy, savepush::markOldSave).
bool noteStart(const SaveFetch& fetched, const StartNotes& notes, const storage::User& user,
               GameSession* session, bool* bringsOldSave) {
    session->channelFileId = fetched.channelFileId;
    if (bringsOldSave) *bringsOldSave = false;
    // Nothing listed, or a save of this console's landed since: its own save
    // stood, and nothing on RomM was turned down.
    if (!fetched.asked || !fetched.listed ||
        savemirror::forgottenSince(fetched.romId, fetched.mark))
        return false;
    const bool picked = usesPicked(fetched.pick.from);
    session->couldNotUseNewest =
        fetched.pick.couldNotUseNewest || (picked && !notes.tookPick);
    if (savepick::OldSaveCopy copy;
        savepick::oldSaveToCopy(fetched.pick, fetched.rows, notes.oldRowsTaken, &copy)) {
        std::vector<std::vector<uint8_t>> bytes;
        if (copy.zipped)
            for (const romm::Asset* r : copy.rows) bytes.push_back(fetchedBytes(fetched, *r));
        const bool marked = savepush::markOldSave(*session, user, copy, bytes);
        if (bringsOldSave) *bringsOldSave = marked;
    }
    if (!picked || !notes.tookPick) return false;
    // ANOTHER APP'S SAVE IN NO CHANNEL (savepick.h): no snapshot to hold, so
    // what the game saves goes where this console's saves go, onto the
    // snapshot it holds or into "default", and the row stays as it is.
    if (fetched.pick.from == From::OtherRow) {
        const bool elsewhere = savepick::continuingElsewhere(fetched.pick, !notes.changedOwn);
        std::fprintf(stderr, "[save] started from save %d (%s), another app's in no channel%s%s\n",
                     fetched.pick.row.id, fetched.pick.row.emulator.c_str(),
                     notes.changedOwn ? "" : ", which this console already had",
                     elsewhere ? "; it was made on another device" : "");
        return elsewhere;
    }
    const romm::Channel& channel = fetched.pick.channel;
    session->startChannelId = channel.id;
    session->startSnapshotId = channel.current.id;
    session->startHardcore = channel.hardcore;
    savemirror::Held before;
    savemirror::getHeld(user, fetched.romId, &before);
    savemirror::putHeld(user, fetched.romId,
                        savepick::heldAfter(before, channel.id, channel.current,
                                            static_cast<int64_t>(std::time(nullptr))));
    const bool elsewhere = savepick::continuingElsewhere(fetched.pick, !notes.changedOwn);
    std::fprintf(stderr, "[save] started from snapshot %d in channel \"%s\"%s%s\n",
                 channel.current.id, channel.label.c_str(),
                 notes.changedOwn ? "" : ", which this console already had",
                 elsewhere ? "; it was made on another device" : "");
    return elsewhere;
}
