// Downloads: downloading and keeping a game, removing one, stopping one from
// its row, and Settings' Downloads panel (#68, #188) (#398, restructure
// batch e).

#include "app.h"

#include <cstdio>
#include <utility>

#include "cache.h"
#include "storage.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// Human-readable bytes, for the one refusal a person is ever shown.
std::string App::gigabytes(int64_t b) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f GB", static_cast<double>(b) / 1e9);
    return std::string(buf);
}

// DOWNLOAD, WHICH IS THE ONLY DELIBERATE STORAGE ACT IN THE PRODUCT.
//
// The cache is invisible by decision: pressing Play fetches a game that is
// not here and says nothing about it, because the feedback that matters —
// a progress bar, and Stop on it (pressStopDownload) — already exists at the
// only moment it is useful. So this row is not "is it cached". It is "put this
// game on the machine and do not take it away again", which is a KEEP, and
// keeping is the one place the console is allowed to say no.
//
// Both floors are checked here, before a byte moves, because refusing after
// a two-gigabyte download would be the same answer at a much higher price.
auto App::downloadById(int romId) -> void {
    if (launchJob.busy()) {
        // The same answer as Play's (#184), unless it is this game's own
        // download, which the page already shows.
        if (launchJob.romId != romId)
            menuNotice.say(launchJob.title + " is still downloading", Tone::Info);
        return;    // one at a time
    }
    // Nor while a finished job waits for the frame loop (#472, launchById).
    if (launchJob.waiting()) return;
    const romm::Game* g = nullptr;
    for (const auto& x : games) if (x.id == romId) { g = &x; break; }
    if (!g) return;

    // Against the disk the game will actually occupy. A kept game going on
    // the games drive cannot threaten the internal disk's floors at all,
    // which is the quiet second benefit of the split — see open question 14.
    const cache::Placement where = cache::find(romId);
    const std::string keepOn =
        where.present ? where.location : cache::keepLocation(g->sizeBytes);
    const cache::KeepVerdict v = cache::mayKeep(keepOn, romId, g->sizeBytes);
    if (!v.allowed) {
        // The one failure the person ever sees, and the number is what makes
        // it actionable: without it "the disk is full" is a dead end.
        //
        // THE CONSTRAINT AND THE NUMBER, NOT WHAT TO DO (#110, MMagTech,
        // 2026-10-06). It said "Remove downloads or add a drive" from
        // 2026-09-26.
        detailScreen.setNotice("Needs " + gigabytes(v.shortfallBytes) + " more space");
        std::fprintf(stderr,
                     "[keep] refused %s: %lld reclaimable against a %lld floor\n",
                     g->name.c_str(), static_cast<long long>(v.reclaimableBytes),
                     static_cast<long long>(v.floorBytes));
        return;
    }

    std::string derr;
    if (!beginLaunch(launchJob, liveClient, *g, coreDir, &derr,
                     /*playWhenReady=*/false, /*keepWhenReady=*/true)) {
        detailScreen.setNotice(derr);
        std::fprintf(stderr, "[download] %s\n", derr.c_str());
        return;
    }
    detailScreen.setNotice("");
    std::fprintf(stderr, "[download] %s (%.0f MB)\n", g->name.c_str(),
                 static_cast<double>(g->sizeBytes) / 1e6);
}

// "Remove download" removes the download. If nobody else is keeping the
// game, the bytes go and the space comes back — because reclaiming space is
// why a person presses a row with that name on it.
//
// The exception is the game being played right now, which the core has open;
// that one drops into the cache and goes when the session ends.
//
// WHAT ACTUALLY HAPPENED IS NOT THIS FUNCTION'S TO SAY, and the first
// version said it anyway — it printed "released to the cache" every time,
// including on the run where the game stayed exactly where it was because a
// second person still wanted it. cache::unkeep reports what it did.
void App::removeDownload(int romId) {
    const bool nowPlaying = playing && session.romId == romId;
    cache::Release r;
    cache::unkeep(storage::currentUser(), romId, /*keepTheBytes=*/nowPlaying, &r);
    detailScreen.setKept(false);

    // SILENCE IS THE RIGHT ANSWER ONLY WHEN THE ROW DID WHAT IT SAYS. The
    // badge going out is feedback enough for a game that is gone and a
    // disk that has the room back. The other three outcomes all leave the
    // bytes where they were, and saying nothing then is the row lying —
    // the same fault as a launch refusal reaching stderr and no further.
    switch (r.what) {
        // CONFIRMATIONS GO IN THE PILL — 2026-09-23 — with the rest of
        // the console's short-lived messages. The screen's own notice is
        // kept for what a person has to ACT on, like a full disk.
        case cache::Release::What::Deleted:
            detailScreen.setNotice("");
            menuNotice.say("Download removed", Tone::Done);
            break;
        case cache::Release::What::Nothing:
            detailScreen.setNotice("");
            break;
        case cache::Release::What::StillKept:
            // Unreachable until this console had more than one account,
            // and the first thing that account switching makes real.
            detailScreen.setNotice("");
            menuNotice.say(r.otherKeepers == 1
                               ? "Removed. Another account still keeps it"
                               : "Removed. Other accounts still keep it",
                           Tone::Info);
            break;
        case cache::Release::What::Demoted:
            detailScreen.setNotice("");
            menuNotice.say("Removed. The space frees when the game closes",
                           Tone::Info);
            break;
        case cache::Release::What::DeleteFailed:
            detailScreen.setNotice("");
            menuNotice.say("Removed, but its files couldn't be deleted", Tone::Problem);
            break;
    }
}

// STOP, ON THE ROW THAT IS FILLING. A download could be stopped only by
// Escape on a keyboard (input.cpp) or by ejecting its drive (#404): on a
// pad, A on the filling row did nothing and B walked away with the download
// still going, and every other Play or Download was refused until it ended
// (launchById). On slow Wi-Fi a wrong press on a big game tied the console
// up for an hour (a player's report, 2026-11-06). The row asks first, as
// Remove download does (#188): a mis-press would cost whatever had arrived.
// Continue is focused, and B is the same answer. Stop sets the job's cancel,
// which the worker answers at every step (#472): the job ends Failed, the
// keep comes out, the half-written file goes, and the page says nothing
// (takeFinishedJob, a cancel says nothing). One question for every stage,
// unpacking and installing included: a player calls all of it the
// download, and the row's own word is under the scrim.
//
// THE USUAL BOX, AND NOTHING MORE (MMagTech at the TV, 2026-11-06). The
// first version said "Stop downloading <game>?" over the game's size, with
// Keep going and Stop: the name made the box wide for nothing, since it
// sits under the game's own title, and Keep going is not a word the
// console uses. Now it is a one-verb question, as "Sign out?" is, about
// what the row under it is doing ("this" was tried and he does not like
// the word), and no line under it: the size is on the page two lines up,
// and the bar is under the scrim. CONTINUE, NOT CANCEL, for the harmless
// answer: under "Stop downloading?" a Cancel reads as cancelling the
// download, the opposite of what it does (MMagTech, the same evening),
// and Exit is the word for leaving a game (Exit to Home). Continue says
// what happens. docs/decisions/2026-11-06-stop-a-download-from-its-row.md.
namespace {
constexpr const char* kStopTitle = "Stop downloading?";
}

void App::pressStopDownload(const screens::Result& res) {
    const int romId = res.value;
    if (!launchJob.busy() || launchJob.romId != romId) return;
    sound::play(sound::Cue::Activate);
    askChoice(kStopTitle, "", {"Continue", "Stop"}, 0, [&, romId](int answer) {
        if (answer != 1) return;
        // Ended while the question was up (followStopAsk closes the box
        // then, but the answer can land the same frame).
        if (!launchJob.busy() || launchJob.romId != romId) return;
        std::fprintf(stderr, "[download] %s stopped from its page\n", launchJob.title.c_str());
        launchJob.cancel = true;
    });
}

// THE QUESTION GOES WITH ITS DOWNLOAD. Finished (a Play about to start, a
// Download that landed) or failed while the box was up, there is nothing
// left to stop: a box left open would take the first press meant for the
// page, or sit over the game. Known by its title, as the Formatting box is
// (followDrives). Each frame, with the other follow-ups (frame.cpp).
void App::followStopAsk() {
    if (!choiceScreen.isOpen() || launchJob.busy() || choiceScreen.title() != kStopTitle)
        return;
    choiceScreen.close();
    choiceThen = nullptr;
}

std::vector<screens::DownloadItem> App::downloadItems() {
    std::unordered_map<int, const romm::Game*> byId;
    for (const romm::Game& g : games) byId[g.id] = &g;
    const std::vector<std::string> locs = storage::locations();
    const std::map<std::string, std::string> names = locationNames(locs, drives::unusable());
    const bool downloading = launchJob.busy() && launchJob.keepWhenReady;
    std::vector<screens::DownloadItem> out;
    for (const cache::Download& d : cache::downloads()) {
        if (downloading && d.romId == launchJob.romId) continue;
        // ONLY WHAT IS ON A DRIVE THAT IS HERE (MMagTech, 2026-09-26). A
        // game on an unplugged drive takes no room on this console and
        // returns to the list with its drive.
        if (!d.present) continue;
        screens::DownloadItem it;
        it.romId = d.romId;
        it.title = d.title;
        it.system = d.platform;
        int platformId = d.platformId;
        if (auto g = byId.find(d.romId); g != byId.end()) {
            if (!g->second->name.empty()) it.title = g->second->name;
            if (g->second->platformId) platformId = g->second->platformId;
        }
        if (auto pn = lib.platformNames.find(platformId); pn != lib.platformNames.end())
            it.system = pn->second;
        it.bytes = d.bytes;
        // THE DRIVE ONLY WHEN THERE IS A CHOICE OF DRIVES: with one, the
        // same word on every row says nothing.
        if (locs.size() > 1)
            if (auto n = names.find(d.location); n != names.end()) it.drive = n->second;
        out.push_back(std::move(it));
    }
    return out;
}

// DOWNLOADS' REMOVE: one question naming how many and how much, with
// Cancel focused, then everybody's keep released for each. The pill says
// how many went; the panel comes back with what is left, and goes when
// nothing is. docs/SETTINGS.md, Storage.
void App::removeDownloads() {
    const std::vector<int> ids = downloadsPanel.toRemove();
    int removed = 0, failed = 0, demoted = 0;
    int64_t freed = 0;
    for (int romId : ids) {
        const bool nowPlaying = playing && session.romId == romId;
        cache::Release r;
        cache::unkeepForEveryone(romId, /*keepTheBytes=*/nowPlaying, &r);
        switch (r.what) {
            case cache::Release::What::DeleteFailed: ++failed; break;
            case cache::Release::What::Demoted: ++demoted; ++removed; break;
            case cache::Release::What::Deleted: freed += r.bytesFreed; ++removed; break;
            default: ++removed; break;
        }
    }
    std::fprintf(stderr, "[downloads] removed %d, %lld bytes back, %d failed\n", removed,
                 static_cast<long long>(freed), failed);
    refreshKeeps();
    if (!downloadsPanel.replace(downloadItems())) downloadsPanel.close();
    buildSettings();
    if (failed > 0)
        menuNotice.say("Couldn't delete some of the files", Tone::Problem);
    else if (demoted > 0)
        menuNotice.say("Removed. The space frees when the game closes", Tone::Info);
    else
        menuNotice.say(removed == 1 ? "Download removed"
                                    : std::to_string(removed) + " downloads removed",
                       Tone::Done);
}

void App::downloadsOutcome(screens::DownloadsPanel::Outcome o) {
    using O = screens::DownloadsPanel::Outcome;
    if (o == O::Closed) {
        downloadsPanel.close();
        buildSettings();
    } else if (o == O::Remove) {
        askChoice(downloadsPanel.removeTitle(), downloadsPanel.removeDetail(),
                  {"Cancel", downloadsPanel.removingAll() ? "Remove all" : "Remove"}, 0,
                  [&](int answer) {
                      if (answer == 1) removeDownloads();
                  });
    }
}

void App::pressDownloads() {
    // AN ADMIN SCREEN: the PIN to open it, once per Settings
    // visit, so clearing several games is one PIN. MMagTech,
    // 2026-09-25. Removing your own needs none, from the
    // game's own screen.
    askPin("Enter the PIN", "To open Downloads", [&]() {
        std::vector<screens::DownloadItem> dl = downloadItems();
        if (dl.empty()) { buildSettings(); return; }
        downloadsPanel.open(std::move(dl));
    });
    sound::play(sound::Cue::Activate);
}

void App::pressRemoveDownload(const screens::Result& res) {
    // ASKS FIRST (#188): a mis-press cost a re-download, 37 GB and
    // a reinstall for a PS3 game. The same question, words and
    // order as Settings' Downloads list, focus on Cancel.
    const int romId = res.value;
    int64_t bytes = 0;
    for (const screens::DownloadItem& d : downloadItems())
        if (d.romId == romId) bytes = d.bytes;
    askChoice("Remove " + detailScreen.game().title + "?",
              bytes > 0 ? screens::sizeText(bytes) : std::string(),
              {"Cancel", "Remove"}, 0, [&, romId](int answer) {
                  if (answer != 1) return;
                  removeDownload(romId);
                  refreshKeeps();
              });
}
