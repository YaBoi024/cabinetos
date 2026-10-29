// A game's page: opening it, its states and their pictures, its manual,
// and what the page shows of a download (#398, restructure batch d).

#include "app.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include "cache.h"
#include "catalog.h"
#include "favorites.h"
#include "manual.h"
#include "saves.h"
#include "storage.h"
#include "timewords.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

void App::startDetailStates() {
    if (detailStates.busy.load() || !detailStates.wantRom) return;
    if (detailStates.worker.joinable()) detailStates.worker.join();
    const int romId = detailStates.wantRom;
    const std::string tag = detailStates.wantTag;
    const std::string saveTag = detailStates.wantSaveTag;
    const std::string stateDir = detailStates.wantStateDir;
    const std::string slug = detailStates.wantSlug;
    detailStates.wantRom = 0;
    detailStates.busy = true;
    const storage::User who = storage::currentUser();
    detailStates.worker = std::thread([this, romId, tag, saveTag, stateDir, slug, who]() {
        std::vector<romm::Asset> all;
        std::vector<screens::StateChoice> out;
        std::string err, saveWhen;
        // YEAR, MAKER, PLAYERS, under the cover. Whatever RomM has.
        screens::DetailScreen::Facts facts;
        {
            romm::Client::Facts f;
            std::string ferr;
            // Offline (#88): as last seen, kept beside the game's record.
            const bool fresh = !romm::serverAway() && liveClient.fetchFacts(romId, &f, &ferr);
            if (fresh) rememberFacts(romId, f);
            if (fresh || knownFacts(romId, &f)) {
                if (f.year > 0) facts.year = std::to_string(f.year);
                facts.maker = f.maker;
                if (!f.players.empty())
                    facts.players = f.players + (f.players == "1" ? " player" : " players");
            }
            // DISC N OF M (#162), only on a game with several discs: the
            // disc Play starts on. RomM's files say how many; with no
            // server, the count the last launch here saw.
            if (disc::handles(slug) &&
                disc::systemFor(slug).change != disc::Change::Automatic) {
                // By the discs' own numbers (disc.h), so a disc missing on
                // RomM reads "Disc 1 of 4  ·  Disc 2 missing".
                const std::vector<disc::Disc> ds =
                    fresh ? disc::fromNames(disc::plan(f.files).discNames)
                          : std::vector<disc::Disc>{};
                const int count = fresh ? (ds.size() >= 2 ? disc::highest(ds) : 0)
                                        : disc::knownCount(who, romId);
                int n = disc::lastDisc(who, romId);
                if (fresh ? !disc::byNumber(ds, n) : (n < 1 || n > count))
                    n = ds.empty() ? 1 : ds.front().number;
                if (count >= 2) facts.disc = disc::ofText(n, count);
                // A DISC THE NAMES NUMBER AND RomM DOES NOT HAVE, said:
                // "Disc 2 missing". Only when RomM answered.
                if (fresh && count >= 1) {
                    const std::vector<int> gone = disc::missing(disc::plan(f.files).discNames);
                    if (!gone.empty()) {
                        std::string m = gone.size() == 1 ? "Disc " : "Discs ";
                        for (size_t k = 0; k < gone.size(); ++k)
                            m += (k ? ", " : "") + std::to_string(gone[k]);
                        m += " missing";
                        facts.disc += (facts.disc.empty() ? "" : "  \xC2\xB7  ") + m;
                    }
                }
            }
        }
        // TIME PLAYED (#128): RomM's total plus what this console has not
        // sent yet; with no server, the last total RomM gave. Nothing when
        // neither is known, rather than a number that is only part of it.
        {
            int64_t ms = 0;
            std::string perr;
            bool have = !romm::serverAway() && liveClient.fetchPlayedMs(romId, &ms, &perr);
            if (have) playtime::remember(who, romId, ms);
            else have = playtime::known(who, romId, &ms);
            if (have) facts.played = playtime::describe(ms + playtime::owedMs(who, romId));
        }
        // THE GAME'S OWN SAVE, the newest under the tag it travels by, as
        // the launch reads it. "Saved today, 8:17 PM". RomM's, or its copy
        // offline (savesFor); or this console's own when that is newer and
        // still waiting to upload (#88).
        std::string savedAt;
        if (!saveTag.empty() && savesFor(liveClient, who, romId, &all, &err))
            for (const romm::Asset& a : all)
                if (a.emulator == saveTag && a.updatedAt > savedAt) savedAt = a.updatedAt;
        const std::vector<cache::Owed> owedHere = cache::owed(who);
        auto owedNamed = [&](const std::string& name, bool state) {
            for (const cache::Owed& o : owedHere)
                if (o.romId == romId && o.isState == state && !o.isScreenshot &&
                    o.fileName == name)
                    return true;
            return false;
        };
        for (const cache::Owed& o : owedHere) {
            if (o.romId != romId || o.isState || o.isScreenshot) continue;
            struct stat st;
            if (::stat(o.localPath.c_str(), &st) == 0 && isoUtc(st.st_mtime) > savedAt)
                savedAt = isoUtc(st.st_mtime);
        }
        if (!savedAt.empty()) {
            std::string w = stateWhen(savedAt);
            if (w.rfind("Today", 0) == 0 || w.rfind("Yesterday", 0) == 0)
                w[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(w[0])));
            if (!w.empty()) saveWhen = "Saved " + w;
        }
        // THE NEWEST THREE STATES. RomM's, and with them this console's
        // own (#88): all of them when RomM could not be asked (offline,
        // or a system whose states never leave the console), and only the
        // ones still waiting to upload when it could. One file name is one
        // state, shown once.
        struct Shown { int id; std::string picture, at, name, path; };
        std::vector<Shown> shown;
        bool askedRomm = false;
        all.clear();
        if (!tag.empty() && !romm::serverAway() && liveClient.fetchStates(romId, &all, &err)) {
            askedRomm = true;
            for (const romm::Asset& a : all)
                if (a.emulator == tag)
                    shown.push_back({a.id, a.picturePath, a.updatedAt, a.fileName, ""});
        } else if (!err.empty()) {
            std::fprintf(stderr, "[detail] states and saves: %s\n", err.c_str());
        }
        if (DIR* d = stateDir.empty() ? nullptr : ::opendir(stateDir.c_str())) {
            while (dirent* e = ::readdir(d)) {
                const std::string name = e->d_name;
                if (name.size() < 7 || name.compare(name.size() - 6, 6, ".state") != 0) continue;
                if (askedRomm && !owedNamed(name, true)) continue;
                bool dup = false;
                for (const Shown& x : shown) dup = dup || x.name == name;
                if (dup) continue;
                const std::string full = stateDir + "/" + name;
                struct stat st;
                if (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                const std::string png = full.substr(0, full.size() - 6) + ".png";
                shown.push_back({0, ::access(png.c_str(), R_OK) == 0 ? png : std::string(),
                                 isoUtc(st.st_mtime), name, full});
            }
            ::closedir(d);
        }
        std::sort(shown.begin(), shown.end(),
                  [](const Shown& a, const Shown& b) { return a.at > b.at; });
        std::vector<std::string> localPaths;
        for (size_t i = 0; i < shown.size() && i < 3; ++i) {
            int id = shown[i].id;
            if (!shown[i].path.empty()) {
                localPaths.push_back(shown[i].path);
                id = -static_cast<int>(localPaths.size());
            }
            out.push_back({id, shown[i].picture, stateWhen(shown[i].at)});
        }
        std::lock_guard<std::mutex> lk(detailStates.m);
        detailStates.romId = romId;
        detailStates.list = std::move(out);
        detailStates.localPaths = std::move(localPaths);
        detailStates.saveWhen = std::move(saveWhen);
        detailStates.facts = std::move(facts);
        detailStates.ready = true;
        detailStates.busy = false;
    });
}

// Which emulator Play would run for this game decides whose states are
// offered: another emulator's state does not load, it hangs.
void App::loadDetailStates(int romId) {
    for (const auto& g : games) {
        if (g.id != romId) continue;
        const catalog::Coverage cov = catalog::coverageFor(g);
        if (!liveClient.haveToken()) return;
        // The facts are asked for every game, playable or not; the states
        // and the save only where there is a core to have written them.
        const char* tag = cov.core && catalog::snapshotsAllowed(cov.core)
            ? catalog::emulatorTag(cov.core) : nullptr;
        const char* saveTag = cov.core ? catalog::saveTag(cov.core) : nullptr;
        detailStates.wantRom = romId;
        detailStates.wantTag = tag ? tag : "";
        detailStates.wantSaveTag = saveTag ? saveTag : "";
        detailStates.wantSlug = g.platformSlug;
        detailStates.wantStateDir =
            cov.core && catalog::snapshotsAllowed(cov.core)
                ? storage::statesDir(storage::currentUser(), g.platformFsSlug, romId, cov.core)
                : std::string();
        startDetailStates();
        return;
    }
}

void App::pumpDetailStates() {
    if (detailStates.ready.exchange(false)) {
        std::lock_guard<std::mutex> lk(detailStates.m);
        if (here() == Screen::Detail && detailScreen.game().romId == detailStates.romId) {
            detailScreen.setStates(detailStates.list);
            detailLocalStates = detailStates.localPaths;
            detailScreen.setSaveWhen(detailStates.saveWhen);
            detailScreen.setFacts(detailStates.facts);
            detailScreen.detailsArrived();
        }
    }
    startDetailStates();
    if (detailStates.stale && uploader.pending() == 0) {
        detailStates.stale = false;
        if (here() == Screen::Detail) loadDetailStates(detailScreen.game().romId);
    }
}

void App::fetchManual(int romId, const std::string& manualPath, const std::string& to,
                      bool forViewer) {
    {
        std::lock_guard<std::mutex> lk(manualFetch.m);
        manualFetch.jobs.push_back({romId, manualPath, to, forViewer});
    }
    manualFetch.cv.notify_all();
    if (manualFetch.th.joinable()) return;
    manualFetch.th = std::thread([this]() {
        std::unique_lock<std::mutex> lk(manualFetch.m);
        for (;;) {
            manualFetch.cv.wait(lk, [this] { return manualFetch.stop || !manualFetch.jobs.empty(); });
            if (manualFetch.stop) return;
            const ManualFetch::Job job = manualFetch.jobs.front();
            manualFetch.jobs.pop_front();
            lk.unlock();
            bool ok = storage::exists(job.to) && manual::looksLikePdf(job.to);
            if (!ok) {
                std::string err;
                ok = fetchManualFile(
                    liveClient, job.from, job.to,
                    [this] {
                        std::lock_guard<std::mutex> g(manualFetch.m);
                        return !manualFetch.stop;
                    },
                    &err);
                if (ok)
                    std::fprintf(stderr, "[manual] fetched %s\n", job.to.c_str());
                else
                    std::fprintf(stderr, "[manual] could not fetch the manual of %d: %s\n",
                                 job.romId, err.c_str());
            }
            lk.lock();
            if (job.forViewer) {
                manualFetch.done = true;
                manualFetch.doneRom = job.romId;
                manualFetch.donePath = job.to;
                manualFetch.doneOk = ok;
            }
        }
    });
}

// A MANUAL IS A PDF (#484, ROMM-06): RomM's docs say so (using/uploads), and
// the viewer reads nothing else. A .md or .txt one, which RomM's own code also
// takes, was offered on the game's page and never opened, and a game on a
// drive fetched it and threw it away at every launch. So it is no manual.
static bool pdfManual(const std::string& path) {
    if (path.size() < 4) return false;
    std::string end = path.substr(path.size() - 4);
    for (char& ch : end) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return end == ".pdf";
}

// A GAME THAT IS ON A DRIVE GETS ITS MANUAL BESIDE IT, when it lands and
// whenever it starts without one (a game downloaded before #364), so it
// can be read with no server. Only while the server is here.
void App::manualBeside(int romId, const std::string& entryPath) {
    if (romm::serverAway() || entryPath.empty()) return;
    for (const auto& g : games) {
        if (g.id != romId) continue;
        if (!pdfManual(g.manualPath) || !cache::findManual(romId).empty()) return;
        const std::string title = g.name.empty() ? g.fsName : g.name;
        fetchManual(romId, g.manualPath, cache::manualPathFor(entryPath, romId, title),
                    false);
        return;
    }
}

void App::openManual() {
    const screens::GameDetail& g = detailScreen.game();
    if (!g.hasManual) {
        sound::play(sound::Cue::Edge);
        return;
    }
    if (manualView.romId() != g.romId) {
        manualView.open(g.romId, renderer.scale());
        const std::string onDisk = cache::findManual(g.romId);
        if (!onDisk.empty()) {
            manualView.setFile(onDisk);
        } else if (!detailManualPath.empty() && !romm::serverAway()) {
            const cache::Placement placed = cache::find(g.romId);
            fetchManual(g.romId, detailManualPath,
                        placed.present ? cache::manualPathFor(placed.entryPath, g.romId,
                                                              detailManualTitle)
                                       : kManualTemp,
                        true);
        } else {
            manualView.setFailed();
        }
        std::fprintf(stderr, "[manual] opened for %d (%s)\n", g.romId,
                     onDisk.empty() ? "fetching" : onDisk.c_str());
    }
    manualShown = true;
    manualSlide.retarget(1.0f, kManualSlide);
    sound::play(sound::Cue::Activate);
}

void App::closeManual() {
    manualShown = false;
    manualSlide.retarget(0.0f, kManualSlide);
    sound::play(sound::Cue::Back);
}

// The page was left, or a game started: the pages go, and a manual
// fetched only to be read goes with them.
void App::forgetManual() {
    manualView.close();
    manualShown = false;
    manualSlide.settle(0.0f);
    detailScreen.setSlide(0.0f);
    ::unlink(kManualTemp.c_str());
}

void App::openDetail(int cardIndex) {
    if (cardIndex < 0 || cardIndex >= static_cast<int>(cards.size())) return;
    screens::GameDetail d;
    d.cardIndex = cardIndex;
    d.romId = cards[cardIndex].id;
    d.title = cards[cardIndex].title;
    d.cover = cards[cardIndex].cover;
    d.coverLarge = cards[cardIndex].coverLarge;
    d.art = cards[cardIndex].art;
    std::string raHash, raPlatform;
    bool raReadable = true;
    for (const auto& g : games) {
        if (g.id != d.romId) continue;
        raHash = g.raHash;
        raPlatform = g.platformSlug;
        d.platform = g.platformName;
        d.sizeBytes = g.sizeBytes;
        const catalog::Coverage cov = catalog::coverageFor(g);
        raReadable = !cov.core || ra::coreReadable(cov.core);
        d.playable = cov.support == catalog::Support::Playable;
        d.reason = cov.reason ? cov.reason : "Not playable on this console";
        detailManualPath = pdfManual(g.manualPath) ? g.manualPath : std::string();
        detailManualTitle = g.name.empty() ? g.fsName : g.name;
        break;
    }
    d.kept = cache::isKeptBy(storage::currentUser(), d.romId);
    // A MANUAL (#364): RomM has one and can be asked, or one is on a
    // drive beside the game.
    d.hasManual = (!detailManualPath.empty() && !romm::serverAway()) ||
                  !cache::findManual(d.romId).empty();
    // A favourite if Home has it, or the person's list does (a game this
    // console cannot play is on the list but not on Home), or a press not
    // yet sent says so.
    d.favorite = std::find(favorites.begin(), favorites.end(), cardIndex) != favorites.end();
    if (const storage::User& who = storage::currentUser(); who.valid()) {
        const std::vector<int> list = favoriteList(who);
        if (std::find(list.begin(), list.end(), d.romId) != list.end()) d.favorite = true;
        const std::map<int, bool> owedFav = favOwed(who);
        if (auto it = owedFav.find(d.romId); it != owedFav.end()) d.favorite = it->second;
    }
    const int romId = d.romId;
    detailScreen.open(std::move(d));
    stack.push_back(Screen::Detail);
    loadDetailStates(romId);

    // RETROACHIEVEMENTS (#74), only for somebody signed in. A platform
    // RetroAchievements does not cover, or a game RomM has no hash for,
    // has none, and no cup, without asking anybody.
    detailAch = ra::GameList{};
    detailAchRom = romId;
    if (ra::signedIn()) {
        screens::DetailScreen::Achievements a;
        a.show = true;
        if (raHash.empty() || ra::consoleFor(raPlatform) == 0 || !raReadable) {
            a.known = a.none = true;
            detailScreen.setAchievements(a);
        } else {
            detailScreen.setAchievements(a);
            ra::fetchList(raHash, [&, romId](const ra::GameList& l) {
                if (here() != Screen::Detail || detailScreen.game().romId != romId) return;
                detailAch = l;
                screens::DetailScreen::Achievements got;
                got.show = true;
                // Not asked and nothing kept (offline, first visit): no
                // cup, rather than a guess.
                got.known = true;
                got.unavailable = !l.known;
                got.none = !l.known || l.none || l.total == 0;
                got.unlocked = l.unlocked;
                got.total = l.total;
                detailScreen.setAchievements(got);
            });
        }
    }
}

void App::openDetailSoon(int cardIndex) {
    if (shotMode) openDetail(cardIndex);
    else pendingDetail = cardIndex;
}

// ON A GAME'S PAGE THE SHOULDERS ARE THE MANUAL'S (#364): R1 slides it
// in, L1 slides it back, and the other way is the edge. Nothing while a
// question or a panel is over the page. False off a game's page.
auto App::shoulderOnPage(int dir) -> bool {
    if (here() != Screen::Detail) return false;
    if (pinScreen.isOpen() || choiceScreen.isOpen() || raList.isOpen() ||
        downloadsPanel.isOpen())
        return true;
    if (dir > 0) {
        if (manualShown) sound::play(sound::Cue::Edge);
        else openManual();
    } else {
        if (manualShown) closeManual();
        else sound::play(sound::Cue::Edge);
    }
    return true;
}

// The launch screen is told what its own game is doing. Only its
// own: a background download of something else belongs in the bar's
// corner, not on this game's Play button.
void App::showDetailProgress() {
    screens::DetailScreen::Progress prog;
    if (launchJob.busy() && launchJob.busyFor >= kProgressDelay &&
        launchJob.romId == detailScreen.game().romId) {
        prog.active = true;
        // WHICH ROW LIT UP IS DECIDED BY WHAT WAS PRESSED. A download
        // started by "Download and keep" must not make Play look busy,
        // and a Play that joined a running download must.
        prog.action = launchJob.playWhenReady ? screens::Action::Play
                                              : screens::Action::Download;
        prog.got = launchJob.got.load();
        prog.total = launchJob.total.load();
        prog.unpacking =
            launchJob.stage.load() == LaunchJob::Stage::Unpacking;
        prog.installing =
            launchJob.stage.load() == LaunchJob::Stage::Installing;
    }
    detailScreen.setProgress(prog);
}

void App::tickDetail(float dt) {
    detailScreen.tick(dt);
    // THE MANUAL (#364): gone with its page, or when a game starts;
    // otherwise it slides, and its file arrives.
    if (manualView.isOpen() &&
        (playing || here() != Screen::Detail ||
         detailScreen.game().romId != manualView.romId()))
        forgetManual();
    {
        std::lock_guard<std::mutex> lk(manualFetch.m);
        if (manualFetch.done) {
            manualFetch.done = false;
            if (manualView.isOpen() && manualFetch.doneRom == manualView.romId()) {
                if (manualFetch.doneOk) manualView.setFile(manualFetch.donePath);
                else manualView.setFailed();
            } else if (manualFetch.donePath == kManualTemp) {
                ::unlink(kManualTemp.c_str());
            }
        }
    }
    manualSlide.tick(dt);
    manualView.tick(dt);
    detailScreen.setSlide(-manualSlide.value() * ui::kCanvasWidth);
}
