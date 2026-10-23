// The worker a launch runs, off the frame thread: the firmware step every
// launch runs first, then one of four routes in a fixed order: a game on
// several discs, a game installed before it runs (PS3), a folder of files,
// one file. The disc route falls through to the others below two discs; the
// other three always end the worker, by fetching the game's saves from RomM
// (finish) before the job is Ready. beginLaunch (launch.cpp) starts it.
// Moved out of main.cpp in the restructure (#398).

#include "launch.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "cache.h"
#include "catalog.h"
#include "disc.h"
#include "fileio.h"
#include "ncch.h"
#include "romfile.h"
#include "romm.h"
#include "saves.h"
#include "standalone.h"
#include "storage.h"
#include "wii.h"
#include "wiimii.h"

// How many things are in a directory. Used once, to decide whether a download
// left anything behind it when the core turned out to read its own archive.
static int countEntries(const std::string& dir) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) return -1;
    int n = 0;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        ++n;
    }
    ::closedir(d);
    return n;
}

// A CANCEL IS ANSWERED AT EVERY STEP, not only inside a transfer (#472):
// Escape on a keyboard, an Eject of the drive the game goes to, or a new
// launch. It was seen only by the transfers, so a cancelled job went on
// listing, evicting other games and unpacking, and one cancelled while it
// unpacked still ended Ready and started. A cancelled job ends Failed, and
// takeFinishedJob says nothing for it.
static bool cancelled(LaunchJob& job) {
    if (!job.cancel.load()) return false;
    job.message = "cancelled";
    job.stage = LaunchJob::Stage::Failed;
    return true;
}

// THE GAME'S SAVES COME DOWN HERE, ON THE WORKER (#481, LAUNCH-17), once the
// game is on the disk: the rows RomM holds for it and the bytes of each row a
// restore may pick (saves.h, fetchSaves), and for a Wii game the Mii
// Channel's, found as its own launch would find it. The stages behind the
// curtain (launch.cpp, pumpLaunch) restore from these and ask the server
// nothing, so a slow or hanging server holds this job's busy clock, with the
// screen alive, and never a black curtain. Each fetch asks only with a token
// and a tag the core's saves travel under, as the restores did.
static void fetchLaunchSaves(LaunchJob& job, romm::Client& client, const storage::User& user) {
    const char* tag = catalog::saveTag(job.coreName.c_str());
    fetchSaves(client, user, job.romId, tag, &job.cancel, &job.saves);
    job.miiSaves = SaveFetch{};
    if (job.cancel.load() || job.platformSlug != "wii" || job.coreName != "dolphin" ||
        wii::isMiiChannel(job.titleId))
        return;
    int miiRom = 0;
    std::string miiTitle;
    if (!findMiiChannel(client, job.platformId, &miiRom, &miiTitle)) return;
    fetchSaves(client, user, miiRom, tag, &job.cancel, &job.miiSaves);
    job.miiSaves.title = miiTitle;
}

// The end of every route: the saves fetched, then Ready, unless a cancel
// came during either.
//
// A 3DS FILE AZAHAR CANNOT PLAY ENDS HERE, with the reason, before the core
// is handed it (ncch.h): an encrypted dump or a .cia. The file stays, as for
// any launch that fails after its download.
static void finish(LaunchJob& job, romm::Client& client, const storage::User& user) {
    if (cancelled(job)) return;
    if (job.coreName == "azahar") {
        if (std::string why = ncch::refusal(job.romPath); !why.empty()) {
            job.message = std::move(why);
            job.stage = LaunchJob::Stage::Failed;
            return;
        }
    }
    fetchLaunchSaves(job, client, user);
    if (cancelled(job)) return;
    job.stage = LaunchJob::Stage::Ready;
}

// The copies are const, as the lambda's were.
void launchWorker(LaunchJob& job, romm::Client& client, const int id, const int platformId,
                  const std::string slug, const std::string fsSlug, const std::string fsName,
                  const cab::standalone::Emulator* const emu, const std::string entryPath,
                  const std::string location, const std::string validExts,
                  const bool blockExtract, const int64_t expectedSize, const bool multiFile,
                  const storage::User user) {
    // FIRMWARE FIRST, and EVERY file the platform lists rather than
    // whichever one this game looks like it needs. A core looks BIOS up by
    // name in the system directory and ignores what it does not want, so
    // extra files cost a little disk and a missing one costs the launch.
    // Cabinet's rule (docs/history/CABINET.md).
    //
    // Shared by every game on the platform and by every person on the
    // console, so it lives at the root in `bios/` rather than inside one
    // game's directory, and a file already present at the right size is not
    // fetched again.
    {
        job.stage = LaunchJob::Stage::Firmware;
        const std::string biosDir = storage::biosDir();
        storage::makeDirs(biosDir);
        std::vector<romm::Firmware> firmware;
        std::string ferr;
        // THIS PLATFORM'S OWN FILES, and only these, are candidates for
        // the aliasing below. `bios/` is shared by the whole console, so a
        // scan of it by size would reach across platforms — and it very
        // nearly did: the PlayStation BIOS is 524288 bytes, exactly the
        // size of Saturn's, and the first version of this picked between
        // them on alphabetical order. It happened to choose correctly and
        // that is not a property anyone should rely on.
        std::vector<std::string> platformFirmware;
        // NOT ASKED WHEN THE SERVER IS AWAY (#88): what an earlier
        // launch fetched is in bios/ and the core finds it there; asking
        // would only hold the launch for a timeout.
        const bool away = romm::serverAway();
        if (away) ferr = "the server is away; using what is in bios/";
        const bool listed = !away && client.fetchFirmware(platformId, &firmware, &ferr);
        if (listed) {
            for (const auto& f : firmware) {
                if (job.cancel.load()) break;
                const std::string fdest = biosDir + "/" + storage::safeSegment(f.fileName);
                platformFirmware.push_back(fdest);
                struct stat fst;
                if (f.sizeBytes > 0 && ::stat(fdest.c_str(), &fst) == 0 &&
                    fst.st_size == f.sizeBytes) {
                    std::fprintf(stderr, "[firmware] %s already here\n",
                                 f.fileName.c_str());
                    continue;
                }
                const std::string fpath = "/api/firmware/" + std::to_string(f.id) +
                                          "/content/" + romm::encodeSegment(f.fileName);
                std::string derr;
                if (client.fetchToFile(fpath, fdest,
                        [&job](int64_t got, int64_t total) {
                            job.got = got;
                            job.total = total;
                            return !job.cancel.load();
                        }, &derr)) {
                    std::fprintf(stderr, "[firmware] %s (%lld bytes)\n",
                                 f.fileName.c_str(),
                                 static_cast<long long>(f.sizeBytes));
                } else {
                    // Not fatal here. WHICH BIOS a core needs is the core's
                    // business and most platforms need none, so a refusal
                    // to launch is the honest place to find out.
                    std::fprintf(stderr, "[firmware] %s: %s\n",
                                 f.fileName.c_str(), derr.c_str());
                }
            }
        } else {
            std::fprintf(stderr, "[firmware] none listed: %s\n", ferr.c_str());
        }

        // AND THEN UNDER THE NAME THE CORE WILL ACTUALLY OPEN.
        //
        // Downloading the file is only half of it. A core looks its BIOS
        // up by a fixed name and RomM serves it under whatever name
        // somebody chose, so the file can be sitting right there and the
        // core still say it has no BIOS — which is what stopped every
        // Saturn game on this console from starting. See
        // catalog::firmwareAliases for why the match is on SIZE and why
        // the copy goes under every name rather than the likeliest one.
        // Only from a list the server gave: offline, the copies an earlier
        // launch placed are already there.
        if (const catalog::FirmwareAliases fa = catalog::firmwareAliases(slug, fsSlug);
            fa.sizeBytes > 0 && listed) {
            // The source is whichever of THIS PLATFORM's firmware files
            // is exactly the right length — never just whatever in `bios/`
            // happens to match, which would reach into another platform's
            // BIOS of the same size. Checked on the disk rather than
            // against what RomM said, so a file already present from an
            // earlier launch counts and a failed download does not.
            std::string source;
            for (const std::string& cand : platformFirmware) {
                struct stat st;
                if (::lstat(cand.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                if (st.st_size != fa.sizeBytes) continue;
                if (source.empty() || cand < source) source = cand;
            }
            if (source.empty()) {
                // Not fatal, and not silent either: the core is about to
                // say the same thing less clearly.
                std::fprintf(stderr,
                             "[firmware] this platform lists no file of %lld bytes — "
                             "its core will not find a BIOS\n",
                             static_cast<long long>(fa.sizeBytes));
            } else {
                std::vector<std::string> targets;
                for (const char* n : fa.names) targets.push_back(biosDir + "/" + n);
                if (fa.subDir) {
                    const std::string sub = biosDir + "/" + fa.subDir;
                    storage::makeDirs(sub);
                    for (const char* n : fa.names) targets.push_back(sub + "/" + n);
                }
                const std::vector<uint8_t> bytes = cab::readBytes(source);
                for (const std::string& t : targets) {
                    if (t == source) continue;
                    struct stat st;
                    if (::stat(t.c_str(), &st) == 0 && st.st_size == fa.sizeBytes) continue;
                    if (bytes.empty() || !cab::writeBytes(t, bytes)) {
                        std::fprintf(stderr, "[firmware] could not place %s\n", t.c_str());
                        continue;
                    }
                    std::fprintf(stderr, "[firmware] %s also placed as %s\n",
                                 source.c_str(), t.c_str());
                }
            }
        }
        // A SYSTEM THAT CANNOT START WITHOUT ITS BIOS, AND NONE CAME (#90).
        // Only when the server ANSWERED: offline, the list is unknown and
        // whatever is already on the console may be enough. "None" means
        // no file this platform lists is on the disk after the fetch, or,
        // where the core wants a known size, none of that size.
        if (listed && catalog::needsBios(slug)) {
            const catalog::FirmwareAliases fa = catalog::firmwareAliases(slug, fsSlug);
            bool have = false;
            for (const std::string& cand : platformFirmware) {
                struct stat st;
                if (::stat(cand.c_str(), &st) != 0 || st.st_size <= 0) continue;
                if (fa.sizeBytes > 0 && st.st_size != fa.sizeBytes) continue;
                have = true;
            }
            job.biosMissing = !have;
            if (!have)
                std::fprintf(stderr, "[firmware] no BIOS for %s on the server\n",
                             slug.c_str());
        }
        job.got = 0;
        job.total = 0;
    }
    if (cancelled(job)) return;

    // A GAME ON SEVERAL DISCS (#162): every disc fetched by its file id,
    // not as the zip RomM makes of the folder (which carries manuals,
    // soundtracks and DLC, and a list that may name them), into the
    // game's one folder, with one list the console writes. Only on the
    // systems disc.h takes, and only when the folder holds two discs or
    // more: one disc and its tracks is the ordinary path below.
    if (!emu && disc::handles(slug)) {
        std::vector<romm::RomFile> files;
        std::string lerr;
        if (romm::serverAway()) lerr = "the server is away";
        const bool listed =
            !romm::serverAway() && client.fetchRomFiles(id, &files, &lerr);
        std::vector<disc::Disc> discs;
        std::vector<std::pair<romm::RomFile, std::string>> fetch;
        int64_t fetchBytes = 0;
        if (listed) {
            const disc::Plan plan = disc::plan(files);
            if (plan.discNames.size() >= 2) {
                std::vector<std::string> names, listedNames;
                for (const std::string& n : plan.discNames)
                    names.push_back(storage::safeSegment(n));
                // THE LIBRARY'S OWN LIST, read for the order and nothing
                // else: it is never handed to a core.
                if (plan.listFileId) {
                    romm::RomFile lf;
                    lf.id = plan.listFileId;
                    lf.fileName = plan.listFileName;
                    const std::vector<uint8_t> b =
                        client.fetchBytes(romm::Client::romFilePath(id, lf));
                    for (std::string l : disc::readList(std::string(b.begin(), b.end()))) {
                        if (const size_t sl = l.find_last_of("/\\"); sl != std::string::npos)
                            l.erase(0, sl + 1);
                        listedNames.push_back(storage::safeSegment(l));
                    }
                }
                discs = disc::fromNames(names, listedNames);
                // EVERY DISC THERE AT ITS SIZE, each checked on its own,
                // so one missing or cut short is the only one fetched.
                for (const romm::RomFile& f : plan.fetch) {
                    const std::string d = entryPath + "/" + storage::safeSegment(f.fileName);
                    struct stat st;
                    if (::stat(d.c_str(), &st) == 0 && st.st_size == f.sizeBytes) continue;
                    fetch.emplace_back(f, d);
                    fetchBytes += f.sizeBytes;
                }
            }
        } else {
            // NO SERVER: the discs as this console last wrote them, and
            // every one of them must be here.
            discs = disc::readRecord(entryPath);
            if (discs.size() < 2) discs.clear();
            for (const disc::Disc& d : discs) {
                if (storage::exists(entryPath + "/" + d.file)) continue;
                std::fprintf(stderr, "[disc] %s is not here and the server cannot be asked: %s\n",
                             d.file.c_str(), lerr.c_str());
                job.message = "Couldn't download this game";
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
            if (!discs.empty())
                std::fprintf(stderr, "[disc] no server (%s): the %zu discs as last written\n",
                             lerr.c_str(), discs.size());
        }
        if (!discs.empty()) {
            // ONE FOLDER. A one-file entry left by a download from before
            // this (the folder as RomM's zip) goes; the folder replaces it.
            struct stat est;
            if (::lstat(entryPath.c_str(), &est) == 0 && !S_ISDIR(est.st_mode))
                storage::removeEntry(entryPath);
            storage::makeDirs(entryPath);
            if (cancelled(job)) return;
            const int64_t need =
                fetchBytes +
                static_cast<int64_t>(static_cast<double>(fetchBytes) * cache::kOverheadFraction);
            if (need > 0 && cache::freeBytes(location) < need) {
                cache::evictUntilFree(location, need, id);
                if (cache::freeBytes(location) < need) {
                    job.message = "Not enough space";
                    job.stage = LaunchJob::Stage::Failed;
                    return;
                }
            }
            job.stage = LaunchJob::Stage::Downloading;
            int64_t done = 0;
            for (const auto& [f, d] : fetch) {
                std::string derr;
                const int64_t before = done;
                if (!client.fetchToFile(romm::Client::romFilePath(id, f), d,
                        [&job, before, fetchBytes](int64_t got, int64_t) {
                            job.got = before + got;
                            job.total = fetchBytes;
                            return !job.cancel.load();
                        }, &derr)) {
                    std::fprintf(stderr, "[launch] download %s: %s\n", f.fileName.c_str(),
                                 derr.c_str());
                    job.message = "Couldn't download this game";
                    job.stage = LaunchJob::Stage::Failed;
                    return;
                }
                done += f.sizeBytes;
                std::fprintf(stderr, "[disc] fetched %s (%lld bytes)\n", f.fileName.c_str(),
                             static_cast<long long>(f.sizeBytes));
            }
            if (fetch.empty())
                std::fprintf(stderr, "[launch] already downloaded, %zu discs\n", discs.size());
            const std::string list = entryPath + "/" + disc::listFileName(fsName);
            const std::string text = disc::listText(discs);
            if (!disc::writeRecord(entryPath, discs) ||
                !cab::writeBytes(list, std::vector<uint8_t>(text.begin(), text.end()))) {
                std::fprintf(stderr, "[disc] could not write the list in %s\n",
                             entryPath.c_str());
                job.message = "Couldn't download this game";
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
            for (const disc::Disc& d : discs)
                std::fprintf(stderr, "[disc] %s: %s\n", d.label.c_str(), d.file.c_str());
            job.discs = discs;
            job.romPath = list;
            job.got = 0;
            job.total = 0;
            finish(job, client, user);
            return;
        }
    }

    // A GAME THAT IS INSTALLED BEFORE IT RUNS (a PS3 PKG; the row's
    // `installs`). Its folder is fetched FILE BY FILE rather than as the
    // zip RomM makes of a folder, because the zip route would hold the
    // zip, the PKG out of it and the installed game at once: three times
    // a 20 GB game. This way it is twice, and only while the install
    // runs; the PKG is deleted once the game it became is in place
    // (measured: the installed game is the PKG's size, open question
    // 19). The folder is never collapsed to one file, because the
    // emulator keeps its compile cache in it, so removing the game
    // removes both.
    if (emu && emu->installs) {
        storage::makeDirs(entryPath);
        std::vector<romm::RomFile> files;
        std::string lerr;
        if (romm::serverAway()) lerr = "the server is away";
        const bool listed =
            !romm::serverAway() && client.fetchRomFiles(id, &files, &lerr);
        if (!listed) {
            // OFFLINE, OR THE SERVER SAID NOTHING: a game already installed
            // here still plays, which is all the list was for.
            std::fprintf(stderr, "[launch] files of %d: %s\n", id, lerr.c_str());
            if (cab::standalone::bootPath(entryPath).empty()) {
                job.message = "Couldn't download this game";
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
        }
        // What is missing, and what the install will need on top: each
        // file not yet installed may cost its size again while it goes
        // in. Counted for every large file rather than by a `.pkg` name,
        // which a PKG does not always have (standalone_rpcs3.cpp, isPkg).
        std::vector<std::pair<const romm::RomFile*, std::string>> fetch;
        int64_t fetchBytes = 0, installBytes = 0;
        for (const romm::RomFile& f : files) {
            const std::string name = storage::safeSegment(f.fileName);
            if (cab::standalone::installedFile(entryPath, name, f.sizeBytes)) continue;
            const bool iso = name.size() > 4 &&
                             strcasecmp(name.c_str() + name.size() - 4, ".iso") == 0;
            if (!iso) installBytes += f.sizeBytes;
            struct stat st;
            const std::string d = entryPath + "/" + name;
            if (::stat(d.c_str(), &st) == 0 && st.st_size == f.sizeBytes) continue;
            fetch.emplace_back(&f, d);
            fetchBytes += f.sizeBytes;
        }
        if (cancelled(job)) return;
        const int64_t need =
            fetchBytes + installBytes +
            static_cast<int64_t>(static_cast<double>(fetchBytes + installBytes) *
                                 cache::kOverheadFraction);
        if (need > 0 && cache::freeBytes(location) < need) {
            cache::evictUntilFree(location, need, id);
            if (cache::freeBytes(location) < need) {
                job.message = "Not enough space";
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
        }
        job.stage = LaunchJob::Stage::Downloading;
        int64_t done = 0;
        for (const auto& [f, d] : fetch) {
            std::string derr;
            const int64_t before = done;
            if (!client.fetchToFile(romm::Client::romFilePath(id, *f), d,
                    [&job, before, fetchBytes](int64_t got, int64_t) {
                        job.got = before + got;
                        job.total = fetchBytes;
                        return !job.cancel.load();
                    }, &derr)) {
                std::fprintf(stderr, "[launch] download %s: %s\n", f->fileName.c_str(),
                             derr.c_str());
                job.message = "Couldn't download this game";
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
            done += f->sizeBytes;
        }
        job.got = 0;
        job.total = 0;
        if (cancelled(job)) return;
        job.stage = LaunchJob::Stage::Installing;
        std::string rom, why;
        if (!cab::standalone::installGame(*emu, entryPath, job.cancel, &rom, &why)) {
            job.message = why;
            job.stage = LaunchJob::Stage::Failed;
            return;
        }
        job.romPath = rom;
        finish(job, client, user);
        return;
    }

    // A GAME KEPT AS A FOLDER, FETCHED FILE BY FILE: several files, or one
    // game file with media beside it or alone (romm_types.h, multiFile).
    // RomM serves such a folder as a zip it makes, a little larger than
    // the game's `fs_size_bytes` (Star Ocean: 7,832,230,048 against
    // 7,832,229,235), so the size check below never matched and the whole
    // game was fetched again at every launch: a player's Switch game with
    // `update/` and `dlc/` folders, 2026-10-12. The zip was also kept
    // beside what came out of it, and its subfolders flattened, so an
    // update could be what booted. Now each of the game's own files is
    // fetched by its id and checked by its own size, as games on several
    // discs and PS3 games already are. Only the game's files, the top of
    // the folder (disc::plan): `update/`, `dlc/`, `manual/` and the rest
    // stay on the server (updates and DLC are #181).
    if (multiFile) {
        std::vector<romm::RomFile> files;
        std::string lerr;
        if (romm::serverAway()) lerr = "the server is away";
        const bool listed =
            !romm::serverAway() && client.fetchRomFiles(id, &files, &lerr);
        // A GAME THAT WAS ONE FILE AND IS NOW A FOLDER: somebody added an
        // `update/` beside it on the server after it was downloaded, or it
        // was one file in a folder, downloaded before such a game counted
        // as a folder (#416). Its entry here is still that one file.
        // Offline it still plays as it is; online the file moves into the
        // game's new folder under the name of the game file its size
        // matches, rather than coming down again, and the folder takes the
        // name a new download would (storage::entryName), which the entry's
        // own name, cut at its last dot, was not when that name had no
        // ending: `.00)` from "(v2.00)".
        std::string entry = entryPath;
        struct stat est;
        if (::lstat(entry.c_str(), &est) == 0 && S_ISREG(est.st_mode)) {
            if (!listed) {
                std::fprintf(stderr, "[launch] files of %d: %s; playing the one file "
                                     "here\n", id, lerr.c_str());
                job.romPath = entry;
                finish(job, client, user);
                return;
            }
            const size_t slash = entry.find_last_of('/');
            const std::string folder =
                entry.substr(0, slash == std::string::npos ? 0 : slash + 1) +
                storage::entryName(id, job.title);
            std::string as;
            for (const romm::RomFile& f : disc::plan(files).fetch)
                if (f.sizeBytes == est.st_size) { as = storage::safeSegment(f.fileName); break; }
            // An entry collapsed from a name with no ending already has the
            // folder's name, so the file steps aside first.
            std::string from = entry;
            if (folder == entry && ::rename(entry.c_str(), (entry + ".moving").c_str()) == 0)
                from = entry + ".moving";
            storage::makeDirs(folder);
            if (!as.empty() && ::rename(from.c_str(), (folder + "/" + as).c_str()) == 0)
                std::fprintf(stderr, "[launch] %s is a folder on the server now; moved "
                                     "into %s\n", entry.c_str(), folder.c_str());
            else if (::unlink(from.c_str()) == 0)
                std::fprintf(stderr, "[launch] %s is a folder on the server now and "
                                     "matches none of its files; removed\n", entry.c_str());
            entry = folder;
            job.entryPath = folder;
        }
        storage::makeDirs(entry);
        std::vector<std::pair<std::string, int64_t>> mine;   // on the disk, size
        if (listed) {
            const disc::Plan plan = disc::plan(files);
            std::vector<std::pair<const romm::RomFile*, std::string>> fetch;
            int64_t fetchBytes = 0;
            for (const romm::RomFile& f : plan.fetch) {
                const std::string name = storage::safeSegment(f.fileName);
                mine.emplace_back(name, f.sizeBytes);
                const std::string d = entry + "/" + name;
                struct stat st;
                if (::stat(d.c_str(), &st) == 0 && st.st_size == f.sizeBytes) continue;
                fetch.emplace_back(&f, d);
                fetchBytes += f.sizeBytes;
            }
            if (cancelled(job)) return;
            const int64_t need =
                fetchBytes +
                static_cast<int64_t>(static_cast<double>(fetchBytes) * cache::kOverheadFraction);
            if (need > 0 && cache::freeBytes(location) < need) {
                cache::evictUntilFree(location, need, id);
                if (cache::freeBytes(location) < need) {
                    job.message = "Not enough space";
                    job.stage = LaunchJob::Stage::Failed;
                    return;
                }
            }
            int64_t done = 0;
            for (const auto& [f, d] : fetch) {
                std::string derr;
                const int64_t before = done;
                if (!client.fetchToFile(romm::Client::romFilePath(id, *f), d,
                        [&job, before, fetchBytes](int64_t got, int64_t) {
                            job.got = before + got;
                            job.total = fetchBytes;
                            return !job.cancel.load();
                        }, &derr)) {
                    std::fprintf(stderr, "[launch] download %s: %s\n",
                                 f->fileName.c_str(), derr.c_str());
                    job.message = "Couldn't download this game";
                    job.stage = LaunchJob::Stage::Failed;
                    return;
                }
                done += f->sizeBytes;
            }
            job.got = 0;
            job.total = 0;
            if (fetch.empty())
                std::fprintf(stderr, "[launch] already downloaded, %zu files\n", mine.size());
            else
                std::fprintf(stderr, "[launch] fetched %zu of the game's %zu files\n",
                             fetch.size(), mine.size());
            // THE ZIP THE OLD ROUTE KEPT, named after the folder, once
            // every file it held is here on its own. Only a zip.
            const std::string old = entry + "/" + storage::safeSegment(fsName);
            bool ours = false;
            for (const auto& m : mine) ours = ours || m.first == storage::safeSegment(fsName);
            if (!ours && romfile::sniffFile(old) == romfile::Kind::Zip &&
                ::unlink(old.c_str()) == 0)
                std::fprintf(stderr, "[launch] removed %s, the folder's zip from before\n",
                             old.c_str());
            // AND WHAT CAME OUT OF IT that is not the game: the zip route
            // unpacked `update/`, `dlc/` and the rest flat into the
            // folder. Each is removed by its name in RomM's own list,
            // and only when no game file shares the name.
            for (const romm::RomFile& f : files) {
                const std::string n = storage::safeSegment(f.fileName);
                bool game = false;
                for (const auto& m : mine) game = game || m.first == n;
                struct stat st;
                const std::string d = entry + "/" + n;
                if (!game && ::lstat(d.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
                    ::unlink(d.c_str()) == 0)
                    std::fprintf(stderr, "[launch] removed %s, unpacked from the folder's "
                                         "zip before (%s)\n", d.c_str(),
                                 f.category.empty() ? "not the game" : f.category.c_str());
            }
        } else {
            // OFFLINE, OR THE SERVER SAID NOTHING: the files already here.
            std::fprintf(stderr, "[launch] files of %d: %s\n", id, lerr.c_str());
            if (DIR* dir = ::opendir(entry.c_str())) {
                while (struct dirent* e = ::readdir(dir)) {
                    const std::string n = e->d_name;
                    if (n[0] == '.' || (n.size() > 5 && n.compare(n.size() - 5, 5, ".part") == 0))
                        continue;
                    struct stat st;
                    const std::string d = entry + "/" + n;
                    if (::stat(d.c_str(), &st) == 0 && S_ISREG(st.st_mode))
                        mine.emplace_back(n, st.st_size);
                }
                ::closedir(dir);
            }
        }
        // WHAT TO START: a disc's sheet (.cue, .gdi) over its tracks, then
        // a file the emulator lists, then the largest.
        std::string boot;
        int rank = -1;
        int64_t bootSize = -1;
        for (const auto& [n, size] : mine) {
            std::string ext = n.substr(n.find_last_of('.') == std::string::npos
                                           ? n.size() : n.find_last_of('.') + 1);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const bool sheet = ext == "cue" || ext == "gdi" || ext == "ccd" ||
                               ext == "toc" || ext == "mds";
            const int r = sheet && romfile::accepts(validExts, n) ? 2
                          : romfile::accepts(validExts, n)        ? 1
                                                                   : 0;
            if (r > rank || (r == rank && size > bootSize)) {
                boot = entry + "/" + n;
                rank = r;
                bootSize = size;
            }
        }
        if (boot.empty()) {
            job.message = "Couldn't download this game";
            job.stage = LaunchJob::Stage::Failed;
            return;
        }
        std::string primary, err;
        romfile::Kind kind = romfile::Kind::Plain;
        if (cancelled(job)) return;
        job.stage = LaunchJob::Stage::Unpacking;
        // A boot file that is an archive an earlier start unpacked is not
        // unpacked again (#530), as for a game of one file below.
        if (romfile::alreadyUnpacked(boot, entry, validExts, blockExtract, &primary)) {
            std::fprintf(stderr, "[launch] already unpacked\n");
        } else if (!romfile::prepareFile(boot, entry, validExts, blockExtract, &primary,
                                         &kind, &err, job.cancel)) {
            std::fprintf(stderr, "[launch] file: %s\n", err.c_str());
            job.message = "Couldn't open this game's file";
            job.stage = LaunchJob::Stage::Failed;
            return;
        }
        job.romPath = primary;
        finish(job, client, user);
        return;
    }

    // A GAME IS ONE ENTRY, AND THE ENTRY IS A FILE WHEN THE GAME IS ONE
    // FILE. `cache/Sony Playstation/321 - Crash Bandicoot.chd` is what 18
    // asked for and what somebody browsing over SFTP wants to find. An
    // archive that unpacks into a .cue and its .bin cannot be one file, so
    // it becomes a directory of the same name — and because the two are
    // renamed identically, keeping and releasing do not care which it is.
    //
    // The download always builds the directory form first, because whether
    // it collapses is not knowable until the core has been asked whether it
    // opens its own archives.
    struct stat est;
    const bool entryIsFile =
        ::lstat(entryPath.c_str(), &est) == 0 && !S_ISDIR(est.st_mode);
    std::string dest = entryPath;
    if (!entryIsFile) {
        storage::makeDirs(entryPath);
        dest = entryPath + "/" + storage::safeSegment(fsName);
    }

    // Already here and the right size? Then it is the same ROM: RomM told
    // us how big it is, and a partial download was never renamed into
    // place. Re-fetching 112 MB to play the same game twice is not a
    // caching subtlety, it is just wrong.
    bool haveIt = false;
    if (struct stat st; expectedSize > 0 && ::stat(dest.c_str(), &st) == 0)
        haveIt = st.st_size == expectedSize;

    std::string err;
    if (cancelled(job)) return;
    if (!haveIt) {
        // ROOM FOR IT FIRST, which until the cache existed nothing checked:
        // the disk filled and stayed full. See cache.h for the policy.
        //
        // expectedSize is RomM's `fs_size_bytes`, so for an archived game
        // it is the COMPRESSED size and this is only the first of two
        // checks. What it unpacks to cannot be known until the archive is
        // here and its own index has been read — see the second check
        // below, after the download.
        if (expectedSize > 0) {
            const int64_t want =
                expectedSize +
                static_cast<int64_t>(static_cast<double>(expectedSize) *
                                     cache::kOverheadFraction);
            if (cache::freeBytes(location) < want) {
                cache::evictUntilFree(location, want, id);
                if (cache::freeBytes(location) < want) {
                    // THE CONSTRAINT, NOT WHAT TO DO ABOUT IT (#110,
                    // MMagTech, 2026-10-06). It said "Remove downloads or
                    // add a drive" from 2026-09-26.
                    job.message = "Not enough space";
                    job.stage = LaunchJob::Stage::Failed;
                    return;
                }
            }
        }
        // Escaped whole (#402): a name beginning with # or ? was cut before the
        // server saw it, and the download answered 404.
        const std::string path =
            "/api/roms/" + std::to_string(id) + "/content/" + romm::encodeSegment(fsName);
        // NO SIZE FROM RomM, NO ROOM CHECK ABOVE (#484, LAUNCH-11's other
        // half): the same check, once, when the server declares the length.
        bool roomAsked = expectedSize > 0, noRoom = false;
        const bool ok = client.fetchToFile(path, dest,
            [&job, &roomAsked, &noRoom, &location, id](int64_t got, int64_t total) {
                job.got = got;
                job.total = total;
                if (!roomAsked && total > 0) {
                    roomAsked = true;
                    const int64_t want =
                        total + static_cast<int64_t>(static_cast<double>(total) *
                                                     cache::kOverheadFraction);
                    if (cache::freeBytes(location) < want) {
                        cache::evictUntilFree(location, want, id);
                        noRoom = cache::freeBytes(location) < want;
                    }
                }
                return !job.cancel.load() && !noRoom;
            }, &err);
        if (!ok) {
            std::fprintf(stderr, "[launch] download: %s\n", err.c_str());
            job.message = noRoom ? "Not enough space" : "Couldn't download this game";
            job.stage = LaunchJob::Stage::Failed;
            return;
        }
        // SAID, so a game downloaded again at every launch shows in the
        // log and the diagnostic report as a line, not as a missing one.
        struct stat dst;
        std::fprintf(stderr, "[launch] downloaded %lld bytes\n",
                     ::stat(dest.c_str(), &dst) == 0 ? static_cast<long long>(dst.st_size)
                                                     : -1LL);
    } else {
        std::fprintf(stderr, "[launch] already downloaded, %lld bytes\n",
                     static_cast<long long>(expectedSize));
    }

    // IS IT A GAME AT ALL? Asked of the bytes, before a core is ever
    // handed them, because the alternative is what this console did until
    // now: pass a web page to an emulator, watch it accept it, report
    // correct geometry, run, and draw black for as long as anyone cares to
    // wait. See romfile.h, Kind::NotAGame, for the three real files that
    // do this on the reference library.
    //
    // AND THE BAD FILE IS DELETED. Without that the size check above
    // matches it on every later launch — "already downloaded" — and the
    // game is permanently broken with no way back from inside the product.
    // A download that is not a game is not a download.
    {
        std::string serr;
        if (romfile::sniffFile(dest, &serr) == romfile::Kind::NotAGame) {
            ::unlink(dest.c_str());
            if (!entryIsFile) ::rmdir(entryPath.c_str());
            job.message =
                "this game's file is a web page, not a game. It needs "
                "replacing on the server";
            std::fprintf(stderr,
                         "[launch] %s is a web page, not a game; deleted rather than "
                         "kept, because a cached one would fail the same way for "
                         "ever\n", dest.c_str());
            job.stage = LaunchJob::Stage::Failed;
            return;
        }
    }

    if (cancelled(job)) return;
    job.stage = LaunchJob::Stage::Unpacking;
    std::string primary;
    romfile::Kind kind = romfile::Kind::Plain;

    if (entryIsFile) {
        // A file entry is one the console already collapsed, which only
        // happens for a payload no core wanted unpacked. Nothing to do.
        job.romPath = dest;
        finish(job, client, user);
        return;
    }

    // ALREADY UNPACKED BY AN EARLIER START (#530): every file the archive
    // holds is beside it as that unpacking left it. Before #530 each start
    // wrote it all again, 37 s for Super Smash Bros. on the 3DS, and the room
    // check below asked for its whole size again, which on a nearly full
    // drive could evict other games or say "Not enough space" for a game
    // already on it. Neither happens now. Anything the check is unsure of
    // unpacks, as before.
    if (romfile::alreadyUnpacked(dest, entryPath, validExts, blockExtract, &primary)) {
        std::fprintf(stderr, "[launch] already unpacked\n");
        job.romPath = primary;
        finish(job, client, user);
        return;
    }

    // THE SECOND CHECK, and the compression ratio is why it exists: 868 KB
    // of Space Harrier becomes 2 MB, and a DS ROM padded with empty space
    // compresses far harder than that. No multiplier is safe, so the
    // archive is asked what it holds rather than guessed at.
    //
    // Both files exist at once during extraction, so the room needed is
    // what comes out, on top of the archive already on disk. Nothing to do
    // for a .chd or an arcade set handed over unextracted, which is most of
    // the large files in a library.
    const int64_t unpacked = romfile::unpackedSize(dest, validExts, blockExtract);
    const int64_t need =
        unpacked + static_cast<int64_t>(static_cast<double>(unpacked) * cache::kOverheadFraction);
    if (cancelled(job)) return;
    if (need > 0 && cache::freeBytes(location) < need) {
        cache::evictUntilFree(location, need, id);
        if (cache::freeBytes(location) < need) {
            job.message = "Not enough space";
            job.stage = LaunchJob::Stage::Failed;
            return;
        }
    }

    if (!romfile::prepareFile(dest, entryPath, validExts, blockExtract, &primary,
                              &kind, &err, job.cancel)) {
        std::fprintf(stderr, "[launch] file: %s\n", err.c_str());
        job.message = "Couldn't open this game's file";
        job.stage = LaunchJob::Stage::Failed;
        return;
    }

    // Nothing came out of it, so the directory holds one file and the
    // layout says it should not be a directory at all. Two renames inside
    // one filesystem, whatever the game weighs.
    //
    // EXCEPT WHEN THE CORE OPENS THE ARCHIVE ITSELF, and this cost every
    // arcade game on the console. A core that answers `block_extract` is
    // handed the file whole, and for MAME 2003-Plus and FBNeo the file's
    // own NAME is how the machine is chosen — `lethalen.zip` is the Lethal
    // Enforcers driver. Collapsing renames it to `3022 - Lethal
    // Enforcers.zip`, the core looks that up in its driver table, finds
    // nothing, and the launch ends at "Game driver not found". That has
    // been true of all 223 arcade games since the folder layout landed on
    // 2026-09-18 and nobody ran one; found by running one, 2026-09-19.
    //
    // So the entry stays a directory and the file inside keeps the name
    // the server gave it. The layout already allows either shape and
    // renames both identically, so keeping and releasing do not care —
    // and the save filed under `nvram/lethalen.nv` now matches what every
    // other MAME frontend, and the reference implementation, already
    // writes.
    if (primary == dest && countEntries(entryPath) == 1 && !blockExtract) {
        const size_t dot = fsName.find_last_of('.');
        const std::string ext = dot == std::string::npos ? "" : fsName.substr(dot);
        const std::string tmp = entryPath + ".collapsing";
        const std::string flat = entryPath + ext;
        if (::rename(dest.c_str(), tmp.c_str()) == 0 &&
            ::rmdir(entryPath.c_str()) == 0 &&
            ::rename(tmp.c_str(), flat.c_str()) == 0) {
            primary = flat;
            job.entryPath = flat;
        } else {
            // Not worth failing a launch over: the game is playable exactly
            // where it is, it is simply a directory holding one file. Said
            // out loud because a layout that quietly does not hold is worse
            // than one that does not hold.
            std::fprintf(stderr, "[storage] could not flatten %s; leaving it a "
                                 "directory\n", entryPath.c_str());
            ::rename(tmp.c_str(), dest.c_str());
        }
    }

    job.romPath = primary;
    finish(job, client, user);
}
