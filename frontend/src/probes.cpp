#include "probes.h"

#include <SDL3/SDL.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "accountremove.h"
#include "accounts.h"
#include "battery.h"
#include "cache.h"
#include "catalog.h"
#include "core.h"
#include "disc.h"
#include "fileio.h"
#include "firstrun.h"
#include "gpu.h"
#include "net.h"
#include "players.h"
#include "playtime.h"
#include "qr.h"
#include "quality.h"
#include "romfile.h"
#include "romm.h"
#include "samenames.h"
#include "selftests.h"
#include "server.h"
#include "storage.h"
#include "vectrexsheets.h"
#include "volume.h"

// Downloads one ROM and says what a core would actually be given.
static int romProbe(const char* address, int romId, const char* validExts) {
    romm::Client client;
    std::string err;
    if (!client.setAddress(address, &err)) {
        std::fprintf(stderr, "[romm] %s\n", err.c_str());
        return 1;
    }
    if (!accounts::loadActiveToken(client)) {
        std::fprintf(stderr, "[romm] no account on this console\n");
        return 1;
    }

    // The one game by its id, not the whole library paged to find it, and
    // downloaded as a launch downloads it: to a file, with no time limit and
    // the name escaped (#488, FIRSTRUN-11's faults).
    romm::Game game;
    if (!client.fetchGame(romId, &game, &err)) {
        std::fprintf(stderr, "[romm] no rom with id %d: %s\n", romId, err.c_str());
        return 1;
    }
    const romm::Game* g = &game;

    std::printf("game        %s\n", g->name.c_str());
    std::printf("platform    %s\n", g->platformName.c_str());
    std::printf("file        %s (%lld bytes)\n", g->fsName.c_str(),
                static_cast<long long>(g->sizeBytes));

    const std::string path =
        "/api/roms/" + std::to_string(g->id) + "/content/" + romm::encodeSegment(g->fsName);
    char tmp[] = "/tmp/cabinetos-rom-probe-XXXXXX";
    const int fd = ::mkstemp(tmp);
    if (fd < 0) { std::fprintf(stderr, "[romm] no scratch file\n"); return 1; }
    ::close(fd);
    const bool got = client.fetchToFile(path, tmp, nullptr, &err);
    std::vector<uint8_t> bytes = got ? cab::readBytes(tmp) : std::vector<uint8_t>{};
    ::unlink(tmp);
    if (bytes.empty()) {
        std::fprintf(stderr, "[romm] download returned nothing%s%s\n", err.empty() ? "" : ": ",
                     err.c_str());
        return 1;
    }
    std::printf("downloaded  %zu bytes\n", bytes.size());

    // What it IS, from the bytes — not from the name it happens to have.
    const romfile::Kind k = romfile::sniff(bytes);
    std::printf("sniffed     %s\n", romfile::kindName(k));

    romfile::Prepared prep;
    if (!romfile::prepare(bytes, validExts, false, &prep, &err)) {
        std::fprintf(stderr, "[romfile] %s\n", err.c_str());
        return 1;
    }
    if (prep.passThrough) {
        std::printf("decision    hand it over untouched\n");
        return 0;
    }
    std::printf("decision    unpacked %zu file(s) from the %s\n",
                prep.members.size(), romfile::kindName(prep.kind));
    for (size_t i = 0; i < prep.members.size(); ++i) {
        std::printf("            %s %-44s %zu bytes\n",
                    static_cast<int>(i) == prep.primary ? "->" : "  ",
                    prep.members[i].name.c_str(), prep.members[i].bytes.size());
    }
    return 0;
}

// --- The first-run mechanisms, each reportable without a screen -------------
//
// None of the four things first run needs is a picture — a state machine, a QR
// renderer, NetworkManager plumbing, and a way to know it is the first run at
// all — which is why they can be built before the look is settled. But
// "not a picture" only helps if there is a way to SEE each of them work, and on
// this project that means a command that prints a fact rather than a screen
// somebody has to describe over the telephone.
//
// So each has a flag, in the same shape as --storage and --core-options
// (--first-run with its step and server check, --network and --network-scan,
// --qr): they run before SDL, need no window, no GL and no controller, and can be run over
// SSH against the real console without disturbing the session on the television.

// What the network is, what is on the air, and whether this console is still
// allowed to save a connection.
//
// THE LAST LINE IS THE POINT OF THE WHOLE COMMAND. Wi-Fi saving rests on
// 60-cabinetos-network.rules (and, on installed consoles, on `wheel` too). If
// that grant is ever lost the failure presents as a network bug on a machine
// whose network is fine, so the console answers the question directly.
static int networkProbe(bool doScan) {
    if (!net::available()) {
        std::printf("NetworkManager  NOT RUNNING — nothing below can be answered\n");
        return 1;
    }
    const net::Status s = net::status();
    std::printf("NetworkManager  running\n");
    std::printf("online          %s\n", s.online ? "yes" : "NO");
    if (s.online) {
        std::printf("link            %s %s%s%s\n",
                    s.link == net::Link::Ethernet ? "ethernet" : "wi-fi",
                    s.device.c_str(),
                    s.ipv4.empty() ? "" : "  ",
                    s.ipv4.c_str());
        if (!s.connection.empty())
            std::printf("connection      %s\n", s.connection.c_str());
    }
    std::printf("ethernet        %s\n",
                !s.ethernetPresent ? "no wired port on this machine"
                : s.ethernetUp     ? "up"
                                   : "present, no link");
    std::printf("wi-fi           %s\n",
                !s.wifiPresent  ? "no radio on this machine"
                : !s.wifiEnabled ? "PRESENT BUT THE RADIO IS OFF"
                : s.wifiUp       ? "connected"
                                 : "on, not connected");

    // Asked of this very process, because the grant depends on the session the
    // caller is in — which is why the answer over SSH and the answer on the
    // television can legitimately differ, and why this line names which it is.
    const std::string verdict = net::polkitVerdict();
    std::printf("save a network  polkit says %s%s\n", verdict.c_str(),
                verdict == "yes" ? "" :
                "  <-- Wi-Fi CANNOT be saved; see 60-cabinetos-network.rules");
    std::printf("asked as        %s\n",
                getenv("SSH_CONNECTION") ? "an SSH session (not the console's own)"
                                         : "a local session");

    if (!doScan) return 0;
    if (!s.wifiPresent) {
        std::printf("\nno radio, so nothing to scan for\n");
        return 0;
    }
    std::vector<net::Network> found;
    std::string err;
    std::printf("\nscanning…\n");
    if (!net::scan(&found, &err)) {
        std::printf("scan failed: %s\n", err.c_str());
        return 1;
    }
    if (found.empty()) {
        // A real answer, not an error. A console in a cupboard hears nothing.
        std::printf("nothing on the air\n");
        return 0;
    }
    std::printf("\n%zu network%s\n\n", found.size(), found.size() == 1 ? "" : "s");
    for (const net::Network& n : found) {
        std::printf("  %s%s %3d%%  %-14s %s%s\n",
                    n.active ? "*" : " ",
                    n.known ? " saved" : "      ",
                    n.signal,
                    n.security.empty() ? "open" : n.security.c_str(),
                    n.ssid.c_str(),
                    n.enterprise ? "   [802.1X — not supported]" : "");
    }
    return 0;
}

// A QR code, on a terminal, at a size a phone will read straight off the
// screen. That is the whole test: the encoder is proved end to end — string in,
// URL back out of a real camera — on a machine with no console and no server.
static int qrProbe(const char* text, const char* pbmPath) {
    std::string err;
    const qr::Code code = qr::encode(text, &err);
    if (!code.valid()) {
        std::fprintf(stderr, "[qr] %s\n", err.c_str());
        return 1;
    }
    std::printf("%s", qr::toText(code).c_str());
    std::printf("\n%d x %d modules, version %d — %s\n", code.size, code.size,
                (code.size - 17) / 4, text);
    // THE QUIET ZONE IS THE RENDERER'S, AND IT IS NOT OPTIONAL. Measured:
    // the same code drawn flush to the edge of an image does not decode at all,
    // while with four modules of margin it decodes every time. It is the
    // commonest reason a perfectly correct code will not scan.
    std::printf("four modules of quiet zone are included above, and whatever "
                "draws this owes it the same\n");
    if (pbmPath) {
        if (!qr::writePbm(code, pbmPath)) {
            std::fprintf(stderr, "[qr] could not write %s\n", pbmPath);
            return 1;
        }
        std::printf("wrote %s\n", pbmPath);
    }
    return 0;
}

// One game: who keeps it, and how many copies of it are on this machine.
//
// EXISTS TO ANSWER ONE QUESTION AND IT IS THE RIGHT ONE TO BE ABLE TO ASK:
// when two people keep the same game, is it on the disk twice? It must not be.
// The bytes live at `<loc>/roms/<platform>/<romId> - <title>` with no user
// anywhere in the path, so one game is one copy however many people want it —
// but that is a structural argument, and this prints the fact instead.
static int keepersProbe(int romId) {
    const std::vector<int> who = cache::keepers(romId);
    const std::vector<cache::Placement> copies = cache::findAll(romId);

    std::printf("rom           %d\n", romId);
    std::printf("keepers       %zu", who.size());
    for (int id : who) std::printf(" %d", id);
    std::printf("\n");

    std::printf("copies        %zu\n", copies.size());
    int64_t total = 0;
    for (const cache::Placement& p : copies) {
        const int64_t bytes = storage::treeBytes(p.entryPath);
        total += bytes;
        std::printf("  %-6s %s (%lld bytes)\n", p.kept ? "kept" : "cache",
                    p.entryPath.c_str(), static_cast<long long>(bytes));
    }
    std::printf("on disk       %lld bytes\n", static_cast<long long>(total));

    if (copies.size() > 1) {
        // findAll's own comment says more than one is nobody's mistake — the
        // drive-unplug case makes a second copy legitimately, and dedupe is
        // what collapses it. So this is a finding to act on, not a failure.
        std::printf("\nMORE THAN ONE COPY. See cache::dedupe — this is the "
                    "unplugged-drive case, not a keep fault.\n");
    } else if (who.size() > 1 && copies.size() == 1) {
        std::printf("\n%zu people keep this game and there is ONE copy of it, "
                    "which is the whole rule.\n", who.size());
    }
    return 0;
}

// Who this console knows, and which one it is acting as. Read-only: it writes
// nothing and is safe on a machine somebody is playing on.
static int accountsProbe() {
    std::printf("list          %s\n", accounts::listPath().c_str());
    {
        // The shape, not a real path: 0 is never an account id.
        const std::string one = accounts::tokenPath(0);
        std::printf("tokens        %s<id>.json\n",
                    one.substr(0, one.rfind('/') + 1).c_str());
    }

    const std::vector<accounts::Account> list = accounts::all();
    const int active = accounts::activeId();
    if (list.empty()) {
        std::printf("\nno accounts — this console has not been paired\n");
        return 0;
    }

    std::printf("\n%zu account%s:\n", list.size(), list.size() == 1 ? "" : "s");
    for (const accounts::Account& a : list) {
        struct stat st{};
        const bool haveToken = ::stat(accounts::tokenPath(a.id).c_str(), &st) == 0;
        std::printf("  %s %d - %s%s%s\n",
                    a.id == active ? "*" : " ", a.id, a.name.c_str(),
                    a.avatar.empty() ? "  (no avatar)" : "",
                    haveToken ? "" : "  NO TOKEN ON DISK");
    }
    if (storage::currentUser().valid())
        std::printf("\n* is the account this console acts as. Saves go to %s\n",
                    storage::userDir(storage::currentUser()).c_str());
    else
        std::printf("\n* is the account this console acts as. No user is "
                    "resolved in this process, so no save path to show.\n");
    std::printf("PIN           %s\n", accounts::pinIsSet() ? "set" : "not set");
    return 0;
}

// Where a real console would be in setup right now, and where it would stop.
//
// It WALKS the chain rather than describing it, advancing and skipping exactly
// as the rules allow, and halts at the first gate that refuses — which is the
// same thing a person would hit. A description can drift from the rules; a walk
// cannot.
static int firstRunProbe(romm::Client& client, const char* startAt, bool tryServer) {
    const firstrun::Completion done = firstrun::completion();
    std::printf("first run       %s\n",
                done.why == firstrun::Why::SignedOut
                      ? (done.clearing ? "NEEDED, signed out; games and accounts not cleared yet"
                                       : "NEEDED, signed out")
                : done.why == firstrun::Why::NoServer
                      ? "NEEDED, set up but no server address is set; opens at the server"
                : !done.done             ? "NEEDED — this console is not set up"
                : done.why == firstrun::Why::AdoptedExisting
                      ? "not needed — this machine is already configured, so it "
                        "is treated as set up"
                      : "not needed — setup was completed here");
    if (!done.when.empty()) std::printf("marked          %s\n", done.when.c_str());

    const std::string address = firstrun::serverAddress();
    std::printf("server address  %s  (from %s)\n",
                address.empty() ? "none" : address.c_str(),
                firstrun::serverAddressSource().c_str());

    firstrun::Facts f = firstrun::observe(client, 0);

    // Only if asked: it is the one part of this command that talks to anything
    // over the network, and a report should not quietly cost a round trip.
    if (tryServer && !address.empty()) {
        std::string err;
        romm::Client probe;
        f.serverAnswered = probe.setAddress(address, &err);
        std::printf("server answers  %s%s%s\n", f.serverAnswered ? "yes" : "no",
                    f.serverAnswered ? " — RomM " : " — ",
                    f.serverAnswered ? probe.serverVersion().c_str() : err.c_str());
    }

    std::printf("\nwhat it sees\n");
    std::printf("  online              %s\n", f.online ? "yes" : "no");
    std::printf("  online over a cable %s\n", f.wiredOnline ? "yes" : "no");
    std::printf("  wi-fi radio         %s\n", f.wifiPresent ? "present" : "none");
    std::printf("  wi-fi connected     %s\n", f.wifiConfigured ? "yes" : "no");
    std::printf("  server address      %s\n", f.haveServerAddress ? "yes" : "no");
    std::printf("  server answered     %s\n",
                f.serverAnswered ? "yes" : tryServer ? "no" : "not asked");
    std::printf("  paired token        %s\n", f.havePairedToken ? "yes" : "no");
    // NOT COUNTED HERE, and said so rather than printed as a zero: this whole
    // command runs before SDL, so there is nothing to ask. A report that shows
    // "0" for a question it never put is the kind of instrument that costs an
    // afternoon.
    std::printf("  gamepads            not counted — this runs before SDL\n");

    firstrun::Machine m;
    m.update(f);
    if (startAt && !m.openAt(startAt)) {
        std::fprintf(stderr, "[first-run] no step called '%s'\n", startAt);
        return 1;
    }

    std::printf("\nwalking the chain\n\n");
    for (int guard = 0; guard < 16; ++guard) {
        const firstrun::Gate g = m.gate();
        const std::string why = m.because();
        std::printf("  %-11s %s\n", firstrun::name(m.step()),
                    g == firstrun::Gate::Ready       ? "ready"
                    : g == firstrun::Gate::Skippable ? "may be skipped"
                                                     : "BLOCKED");
        if (!why.empty()) std::printf("              \"%s\"\n", why.c_str());
        if (m.finished()) {
            std::printf("\nsetup would complete.\n");
            return 0;
        }
        if (g == firstrun::Gate::Ready) {
            m.advance();
        } else if (g == firstrun::Gate::Skippable) {
            m.skip();
        } else {
            std::printf("\nsetup would stop here, and this is the correct "
                        "behaviour: there is no way past this step.\n");
            return 0;
        }
    }
    std::fprintf(stderr, "[first-run] the chain did not terminate — that is a bug\n");
    return 1;
}

// Talks to a RomM server and reports, without opening a window.
//
//   --romm <address>              connect and list the library
//   --romm <address> --romm-pair  pair first, if there is no token yet
//
// The token lives in the account's ~/.config/cabinetos/accounts/<id>.json at
// 0600 (accounts.h). It is a credential: it is never printed here, and it does
// not belong in the repository.
static int rommProbe(const char* address, bool allowPairing) {
    romm::Client client;
    std::string err;

    if (!client.setAddress(address, &err)) {
        std::fprintf(stderr, "[romm] %s\n", err.c_str());
        return 1;
    }
    std::printf("server      %s (RomM %s)\n", client.baseUrl().c_str(),
                client.serverVersion().c_str());

    // `--romm-pair` MEANS PAIR, and since 2026-09-21 that means ADD SOMEBODY.
    //
    // It used to load `~/.config/cabinetos/romm.json` first and skip pairing
    // whenever that file existed — which the handover recorded as "it does
    // nothing if a token already exists", with a workaround of pointing HOME
    // at an empty directory. That was a sensible shape when a console had one
    // token and re-pairing was a mistake. With accounts it is exactly backwards:
    // adding a second person to a console that already has one is the whole
    // point of the flag, and every console that has been set up already has an
    // account.
    //
    // So the flag pairs unconditionally, and without it the probe acts as
    // whoever the console is acting as.
    if (!allowPairing) {
        if (!accounts::loadActiveToken(client)) {
            std::fprintf(stderr,
                         "[romm] no account on this console — run again with "
                         "--romm-pair\n");
            return 1;
        }
    } else {
        // WHOEVER APPROVES THIS IS WHO GETS ADDED. The console does not choose;
        // the browser session that approves the code does, and `recordPairing`
        // then asks the server which user that was. Approving as somebody who
        // already has an account here re-pairs them rather than making a second
        // row — see `accounts::add`.
        std::printf("\nSign in to RomM as the person you are ADDING before you\n"
                    "approve this — whoever approves it is who gets added.\n");
        romm::Pairing p;
        if (!client.beginPairing(&p, &err)) {
            std::fprintf(stderr, "[romm] pairing failed: %s\n", err.c_str());
            return 1;
        }
        // This is what the first-run screen will show as a QR code. On a
        // television it is the only thing anyone has to act on.
        std::printf("\napprove at  %s\ncode        %s\nexpires in  %d seconds\n\n",
                    p.verificationUrl.c_str(), p.userCode.c_str(), p.expiresIn);
        std::fflush(stdout);

        const int deadline = p.expiresIn > 0 ? p.expiresIn : 600;
        int waited = 0;
        int state = 0;
        while (waited < deadline) {
            SDL_Delay(static_cast<Uint32>(p.intervalSeconds) * 1000);
            waited += p.intervalSeconds;
            state = client.pollPairing(p, &err);
            if (state != 0) break;
            // \r only makes sense on a terminal. Redirected to a file — which
            // is how this runs on the console — it concatenates every tick
            // onto one unreadable line.
            std::printf("waiting…    %ds%s", waited, isatty(1) ? "\r" : "\n");
            std::fflush(stdout);
        }
        if (state != 1) {
            std::fprintf(stderr, "\n[romm] not paired: %s\n",
                         err.empty() ? "timed out" : err.c_str());
            return 1;
        }
        // SAY IT LOUDLY WHEN THE TOKEN DOES NOT LAND, and do not call it
        // "paired". The old wording was `paired (could not write ...)` — a
        // success word with the failure in brackets after it — which is
        // exactly how the first console ever installed came up on the
        // stand-in library with nobody able to say why. The pairing itself
        // genuinely succeeded; what failed is the only part that lasts.
        std::string aerr;
        accounts::Paired paired;
        if (accounts::recordPairing(client, &paired, &aerr)) {
            // WHO WAS ACTUALLY ADDED, asked rather than assumed. Reporting
            // `activeId()` here would name the wrong person for every account
            // after the first, because adding somebody deliberately does not
            // switch to them.
            std::printf("\n%s   %d - %s\n",
                        paired.isNew ? "added      " : "RE-PAIRED  ",
                        paired.id, paired.name.c_str());
            if (!paired.isNew)
                std::printf("            NOBODY WAS ADDED — that account was already\n"
                            "            here, so its token was refreshed instead.\n"
                            "            Approve as the person you are ADDING.\n");
            std::printf("token       %s\n", accounts::tokenPath(paired.id).c_str());
            if (paired.id == accounts::activeId())
                std::printf("active      yes — this console was already acting as them\n");
            else
                std::printf("active      no  — still acting as %d. Switch from the chip.\n",
                            accounts::activeId());
        } else {
            std::printf("\n");
            std::fflush(stdout);
            std::fprintf(stderr,
                         "[romm] PAIRED, BUT IT WAS NOT RECORDED: %s\n"
                         "[romm] This console will not stay paired. Fix that and\n"
                         "[romm] run --romm-probe --romm-pair again.\n",
                         aerr.empty() ? std::strerror(errno) : aerr.c_str());
            return 1;
        }
    }

    std::vector<romm::Platform> platforms;
    if (!client.fetchPlatforms(&platforms, &err)) {
        std::fprintf(stderr, "[romm] platforms: %s\n", err.c_str());
        return 1;
    }

    int total = 0;
    for (const auto& p : platforms) total += p.romCount;
    std::printf("\n%zu platforms, %d games\n\n", platforms.size(), total);

    // fs_slug is printed next to the name because it is the field that
    // distinguishes two platforms the name and the slug cannot: RomM calls
    // the FBNeo, MAME, Naomi and Atomiswave folders all "Arcade", and a
    // client keying on slug loses all but one of them.
    for (const auto& p : platforms) {
        std::printf("  %5d  %-34s id=%-4d slug=%-22s fs=%s\n", p.romCount,
                    p.name.c_str(), p.id, p.slug.c_str(), p.fsSlug.c_str());
    }

    // Page the largest platform in full, because silent truncation is the
    // failure this has to rule out: a client that quietly returns the first
    // page looks like it works.
    const romm::Platform* biggest = nullptr;
    for (const auto& p : platforms)
        if (!biggest || p.romCount > biggest->romCount) biggest = &p;
    if (biggest) {
        std::vector<romm::Game> games;
        if (!client.fetchGames(biggest->id, &games, &err)) {
            std::fprintf(stderr, "\n[romm] games: %s\n", err.c_str());
            return 1;
        }
        std::printf("\npaged %s: fetched %zu, server said %d — %s\n",
                    biggest->name.c_str(), games.size(), biggest->romCount,
                    games.size() == static_cast<size_t>(biggest->romCount)
                        ? "match"
                        : "MISMATCH, something is truncating");
        int withCover = 0;
        for (const auto& g : games) if (!g.coverPath.empty()) ++withCover;
        std::printf("covers      %d of %zu have art\n", withCover, games.size());
        if (!games.empty())
            std::printf("first       %s\n", games.front().name.c_str());
    }

    // What Home will actually show. The hero is the most recently played game
    // this console can play — not simply the most recent, because offering a
    // Resume that cannot run is worse than offering nothing.
    std::vector<romm::Game> recent;
    if (client.fetchRecent(16, &recent, &err)) {
        std::printf("\nrecent      %zu games with play history\n", recent.size());
        const romm::Game* hero = nullptr;
        for (const auto& g : recent)
            if (catalog::playable(g)) { hero = &g; break; }
        if (hero) {
            std::printf("hero        %s (%s)\n", hero->name.c_str(),
                        hero->platformName.c_str());
        } else {
            std::printf("hero        none — nothing recent is playable here\n");
        }
        for (const auto& g : recent) {
            const catalog::Coverage c = catalog::coverageFor(g);
            std::printf("            %-40s %-14s %s\n", g.name.substr(0, 39).c_str(),
                        g.platformName.c_str(),
                        c.support == catalog::Support::Playable ? c.core : "— not playable here");
        }
    } else {
        std::printf("\nrecent      failed: %s\n", err.c_str());
    }

    // Actually FETCH a cover, rather than counting the ones that claim to have
    // one. Those are different questions, and the difference hid a real bug:
    // RomM appends "?ts=<datetime with a space>" to cover paths, which curl
    // rejects outright, so every cover failed while the count looked healthy.
    // A library with no art and no error is the worst kind of broken.
    for (const auto& p : platforms) {
        if (p.romCount == 0) continue;
        std::vector<romm::Game> games;
        if (!client.fetchGames(p.id, &games, &err)) continue;
        const romm::Game* withArt = nullptr;
        for (const auto& g : games)
            if (!g.coverPath.empty()) { withArt = &g; break; }
        if (!withArt) continue;

        std::vector<uint8_t> bytes = client.fetchBytes(withArt->coverPath);
        std::printf("\ncover test  %s — %s\n", withArt->name.c_str(),
                    bytes.empty() ? "FAILED, fetched 0 bytes" : "ok");
        if (!bytes.empty()) {
            // Say what it actually is. A 200 carrying an HTML error page is
            // still zero use to a texture upload.
            const bool png = bytes.size() > 8 && bytes[0] == 0x89 && bytes[1] == 'P';
            const bool jpg = bytes.size() > 3 && bytes[0] == 0xFF && bytes[1] == 0xD8;
            std::printf("            %zu bytes, %s\n", bytes.size(),
                        png ? "PNG" : jpg ? "JPEG" : "NOT AN IMAGE");
        } else {
            return 1;
        }
        break;
    }
    return 0;
}

std::optional<int> earlyProbe(const EarlyProbeOptions& o) {
    const bool sameNamesMode = o.sameNamesMode;
    const char* coreDir = o.coreDir;
    const char* rommAddress = o.rommAddress;
    const bool accountsTestMode = o.accountsTestMode;
    const bool playersTestMode = o.playersTestMode;
    const bool batteryTestMode = o.batteryTestMode;
    const bool volumeTestMode = o.volumeTestMode;
    const bool playtimeTestMode = o.playtimeTestMode;
    const bool discTestMode = o.discTestMode;
    const bool filesTestMode = o.filesTestMode;
    const bool vectrexSheetsTestMode = o.vectrexSheetsTestMode;
    const int padsSeconds = o.padsSeconds;
    const bool firstRunRulesMode = o.firstRunRulesMode;
    const bool firstRunWriteMode = o.firstRunWriteMode;
    const char* qrText = o.qrText;
    const char* qrPbm = o.qrPbm;
    const bool networkProbeMode = o.networkProbeMode;
    const bool networkScan = o.networkScan;
    const bool gpuProbeMode = o.gpuProbeMode;

    // Neither of these needs a storage tree, a window, GL or a controller, so
    // they answer before anything is created on disk. --qr in particular should
    // not cost a console a directory it did not have.
    // The report's same-name list (#383): it reads the server and the account
    // only, so it answers before drives are mounted or a missing drive is
    // reported (and forgotten), which a report must not do to a console.
    if (sameNamesMode) {
        catalog::setCoreDirectory(coreDir);
        return samenames::probe(rommAddress);
    }
    if (accountsTestMode) return accountsTest();
    if (playersTestMode) return players::test();
    if (batteryTestMode) return battery::test();
    if (volumeTestMode) return volume::test();
    if (playtimeTestMode) return playtime::test();
    if (discTestMode) return disc::test();
    if (filesTestMode) return filesTest();
    if (vectrexSheetsTestMode) return vectrexsheets::test();
    if (padsSeconds > 0) return players::report(padsSeconds);
    if (firstRunRulesMode) return firstRunRules();
    if (firstRunWriteMode) return firstRunWriteTest();
    if (qrText) return qrProbe(qrText, qrPbm);
    if (networkProbeMode) return networkProbe(networkScan);
    if (gpuProbeMode) {
        cab::gpu::report();
        quality::machineClass();
        return 0;
    }
    return std::nullopt;
}

std::optional<int> storageProbe(const StorageProbeOptions& o) {
    const bool firstRunProbeMode = o.firstRunProbeMode;
    const char* firstRunStep = o.firstRunStep;
    const bool firstRunServerCheck = o.firstRunServerCheck;
    const char* rommAddress = o.rommAddress;
    const int romProbeId = o.romProbeId;
    const char* romProbeExts = o.romProbeExts;
    const bool rommProbeMode = o.rommProbeMode;
    const bool rommPair = o.rommPair;
    const bool coreOptionsAudit = o.coreOptionsAudit;
    const bool coreOptionsDetail = o.coreOptionsDetail;
    const char* corePath = o.corePath;
    const char* romPath = o.romPath;
    const int autoLaunchId = o.autoLaunchId;
    const char* coreDir = o.coreDir;
    const bool accountsProbeMode = o.accountsProbeMode;
    const char* serverCheckAddress = o.serverCheckAddress;
    const int keepersRomId = o.keepersRomId;
    const bool storageReport = o.storageReport;
    const int removeAccountId = o.removeAccountId;
    const bool removeAnyway = o.removeAnyway;

    // One probe runs per invocation. Each of these needs the storage tree:
    // first run's marker and the server address live in it.
    if (firstRunProbeMode) {
        romm::Client fr;
        accounts::loadActiveToken(fr);
        return firstRunProbe(fr, firstRunStep, firstRunServerCheck);
    }
    // REMOVE A PERSON WITHOUT THE SCREENS (#194): exactly what Settings'
    // Remove runs, for a test on a console whose PIN the tester does not
    // have. When something is still owed it says what Remove would ask and
    // deletes nothing, unless --remove-anyway (the "Remove anyway" answer).
    // HERE, after the drives are mounted, as they are when Settings' Remove
    // runs: a kept game on a drive that was not mounted lost its keep marker
    // (#488, FIRSTRUN-15).
    if (removeAccountId > 0) {
        const std::vector<accounts::Account> list = accounts::all();
        const accounts::Account* who = accounts::find(list, removeAccountId);
        std::string err;
        if (!who) {
            std::fprintf(stderr, "[remove] no account %d\n", removeAccountId);
            return 1;
        }
        if (!accounts::canRemove(who->id, &err)) {
            std::fprintf(stderr, "[remove] refused: %s\n", err.c_str());
            return 1;
        }
        const int left = sendOwedBeforeRemoving(*who, rommAddress ? rommAddress : "");
        if (left > 0 && !removeAnyway) {
            std::fprintf(stderr, "[remove] would ask \"Some saves haven't reached RomM\"; "
                                 "nothing deleted\n");
            return 2;
        }
        if (!removePersonNow(*who, &err)) {
            std::fprintf(stderr, "[remove] could not: %s\n", err.c_str());
            return 1;
        }
        return 0;
    }

    if (rommAddress && romProbeId > 0)
        return romProbe(rommAddress, romProbeId, romProbeExts);
    if (rommAddress && rommProbeMode) return rommProbe(rommAddress, rommPair);

    // Every option every built core declares, and what it is answered with.
    //
    // Runs before SDL, because loading a core and reading its table needs no
    // window, no GL and no controller — and because the answer has to be
    // checkable in CI, on a machine with no screen.
    //
    // WHAT TO LOOK FOR: a core with ZERO declared options is the suspicious
    // case, not the clean one. It means either the core genuinely has none, or
    // it declares them through an API generation this host does not read — and
    // the second is indistinguishable from the first without going and looking.
    // NOT when a specific core and ROM were named, or a launch was asked for.
    // A core's table with a game loaded, the only way to see the three that
    // declare nothing until they know what they are running (Dolphin, FBNeo
    // and MAME), is printed by a library launch: --launch <id> with
    // --core-options-detail (launch.cpp, reportLoadedOptions). With --core
    // and --rom nothing prints it (#488, FIRSTRUN-21). Sweeping the directory
    // instead would report zero for exactly the cores the question is about.
    if (coreOptionsAudit && !(corePath && romPath) && autoLaunchId == 0) {
        DIR* d = opendir(coreDir);
        if (!d) {
            std::fprintf(stderr, "[options] no core directory at %s\n", coreDir);
            return 1;
        }
        std::vector<std::string> sos;
        while (struct dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name.size() > 3 && name.compare(name.size() - 3, 3, ".so") == 0)
                sos.push_back(name);
        }
        closedir(d);
        std::sort(sos.begin(), sos.end());

        int totalOptions = 0, totalOverridden = 0, coresWithNone = 0;
        for (const std::string& so : sos) {
            cab::Core& core = cab::Core::shared();
            // Before load(), because a core may read its options inside
            // retro_init and several do.
            //
            // The directories go in for the same reason, and the audit used to
            // skip them: with no system directory PPSSPP looked for its own
            // assets at a relative path, found none, and printed "Core system
            // files missing, expect bugs" during an audit that is supposed to
            // report what a core does in the product. An instrument that sets
            // the core up differently from the way the product does is
            // measuring something else.
            core.setDirectories(storage::biosDir(), storage::scratchSavesDir(so));
            storage::makeDirs(storage::scratchSavesDir(so));
            core.setOptionOverrides(catalog::optionOverrides(so));
            if (!core.load(std::string(coreDir) + "/" + so)) {
                std::printf("%-24s  FAILED TO LOAD: %s\n", so.c_str(),
                            core.error().c_str());
                continue;
            }
            const std::vector<cab::Core::OptionReport> opts = core.options();
            std::printf("\n%s  —  %d option(s)\n", so.c_str(),
                        static_cast<int>(opts.size()));
            if (opts.empty()) ++coresWithNone;
            for (const auto& o : opts) {
                totalOptions++;
                if (o.overridden) totalOverridden++;
                if (!coreOptionsDetail) {
                    std::printf("  %-34s %-18s %s%s\n", o.key.c_str(), o.chosen.c_str(),
                                o.overridden ? "OURS, core says " : "core default",
                                o.overridden ? o.defaultValue.c_str() : "");
                    continue;
                }
                // EVERYTHING THE CORE SAID, which is the difference between an
                // audit and a list. A key and a value cannot answer "what
                // would this do if I changed it" — that needs the prose the
                // core wrote and every value it will accept, and both are
                // already captured and were simply never printed.
                std::printf("  %s\n", o.key.c_str());
                if (!o.desc.empty()) std::printf("      what   %s\n", o.desc.c_str());
                std::printf("      now    %s%s\n", o.chosen.c_str(),
                            o.overridden ? "   (ours)" : "");
                if (!o.defaultValue.empty() && o.overridden)
                    std::printf("      core   %s\n", o.defaultValue.c_str());
                if (!o.values.empty()) {
                    std::printf("      takes ");
                    for (size_t i = 0; i < o.values.size(); ++i)
                        std::printf("%s%s", i ? " | " : " ", o.values[i].c_str());
                    std::printf("\n");
                }
            }
            for (const std::string& k : core.undeclaredOptionAsks())
                std::printf("  !! asked but never declared: %s\n", k.c_str());
            core.unload();
        }
        std::printf("\n%d core(s), %d option(s) answered, %d of them ours.\n",
                    static_cast<int>(sos.size()), totalOptions, totalOverridden);
        if (coresWithNone > 0)
            std::printf("%d core(s) declared NO options — worth checking by hand.\n",
                        coresWithNone);
        return 0;
    }

    // What the disk actually holds, without opening a window.
    //
    // This is the Storage screen's data, and the Storage screen is the ONE
    // exception to the cache being invisible: nobody cares what is cached until
    // they go looking, and when they do they should find it. Until that screen
    // exists this is how anyone — or anything in CI — checks that keeping,
    // eviction and the floors agree with each other.
    // Read-only and needs no network: the list is on disk. It goes here rather
    // than with --first-run-rules because it reports the REAL console, so it
    // has to run after the storage root is settled.
    if (accountsProbeMode) return accountsProbe();
    if (serverCheckAddress) {
        std::string detail;
        const server::Check r = server::check(serverCheckAddress, &detail);
        std::printf("%s: %s%s%s\n", serverCheckAddress,
                    r == server::Check::Same        ? "same server"
                    : r == server::Check::Different ? "different server"
                    : r == server::Check::NoServer  ? "no server"
                                                    : "could not tell",
                    detail.empty() ? "" : " (", detail.empty() ? "" : (detail + ")").c_str());
        return r == server::Check::Same ? 0 : 1;
    }
    if (keepersRomId > 0) return keepersProbe(keepersRomId);

    if (storageReport) {
        romm::Client sclient;
        if (rommAddress && sclient.setAddress(rommAddress, nullptr))
            accounts::loadActiveToken(sclient);
        adoptUser(sclient);

        std::printf("root            %s\n", storage::root().c_str());
        std::printf("pending upload  %10.2f GB\n", cache::pendingBytes() / 1e9);
        std::printf("system reserve  %10.2f GB\n", cache::kSystemReserveBytes / 1e9);
        // WHERE A KEPT GAME WOULD LAND, by size: the main drive up to 80%,
        // then the extra drive with the most room (cache::keepLocation).
        for (const double g : {1.0, 5.0, 50.0})
            std::printf("keep a %2.0f GB game  -> %s\n", g,
                        cache::keepLocation(static_cast<int64_t>(g * 1e9)).c_str());

        // PER LOCATION, because the floors are a fact about a filesystem and
        // the whole reason `roms/` and `cache/` repeat is that there is more
        // than one of them.
        const std::vector<std::string> locs = storage::locations();
        for (size_t i = 0; i < locs.size(); ++i) {
            const std::string& loc = locs[i];
            const std::vector<cache::Entry> evictable = cache::candidates(loc);
            int64_t evictableBytes = 0;
            for (const auto& e : evictable) evictableBytes += e.bytes;
            std::printf("\n%-15s %s%s\n", i == 0 ? "internal" : "games drive",
                        loc.c_str(),
                        loc == cache::keepLocation(0) ? "   <- the next kept game goes here" : "");
            std::printf("  free          %10.2f GB\n", cache::freeBytes(loc) / 1e9);
            std::printf("  save floor    %10.2f GB\n", cache::saveFloorBytes(loc) / 1e9);
            std::printf("  evictable     %10.2f GB  in %zu game(s)\n",
                        evictableBytes / 1e9, evictable.size());
            // Oldest first is the whole eviction order, so printing it in order
            // is printing what would go, in the order it would go.
            for (const auto& e : evictable)
                std::printf("    evictable  %8.1f MB  rom %d  %s\n", e.bytes / 1e6,
                            e.romId, e.path.c_str());
        }

        // WHO KEPT WHAT, which is the question the old boolean could not
        // answer. A game with two keepers is a game one person releasing must
        // not take away from the other.
        const std::vector<int> kept = cache::allKeptRoms();
        std::printf("\nkept            %zu game(s)\n", kept.size());
        for (int romId : kept) {
            const cache::Placement p = cache::find(romId);
            std::string who;
            for (int id : cache::keepers(romId))
                who += (who.empty() ? "" : ", ") + std::to_string(id);
            std::printf("  rom %-6d kept by user(s) %-12s %s%s\n", romId, who.c_str(),
                        p.present ? p.entryPath.c_str() : "NOT ON THIS DISK",
                        p.present && p.isDirectory ? "/   (a set of files)" : "");
        }
        for (const storage::User& u : storage::knownUsers())
            std::printf("user            %s\n", u.dirName().c_str());
        return 0;
    }
    return std::nullopt;
}
