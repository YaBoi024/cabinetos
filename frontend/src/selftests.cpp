#include "selftests.h"

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "accounts.h"
#include "covercache.h"
#include "dirsave.h"
#include "fileio.h"
#include "firstrun.h"
#include "pending.h"
#include "prefs.h"
#include "romfile.h"
#include "romm.h"
#include "savemirror.h"
#include "savepick.h"
#include "saves.h"
#include "server.h"
#include "storage.h"
#include "unsettled.h"

// Does completing setup actually STICK?
//
// THIS TESTS THE ONLY CODE IN FIRST RUN THAT HAS NEVER RUN. Every walkthrough
// of the flow so far has used `--setup`, which forces it on an already
// configured machine and deliberately writes nothing — so `setServerAddress`,
// `markCompleted` and the token save have never once been executed by the
// product. Three separate faults found on 2026-09-20 were in code that looked
// correct and had simply never been exercised: SDL text input that was never
// enabled, a key press that never reported a commit, and a frame that never
// reached the window.
//
// AND THE FAILURE THIS GUARDS IS THE WORST ONE THE FEATURE HAS. If the marker
// does not persist, a console completes setup and boots straight back into
// setup, for ever, with no way past it — on a machine somebody has just
// installed. It would look exactly like a console that cannot be set up at all.
//
// It runs against a scratch root and touches nothing real, so it is safe on any
// machine and belongs in CI.
int firstRunWriteTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    // Somewhere that is nobody's console.
    char root[] = "/tmp/cabinetos-firstrun-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[first-run] could not make a scratch root\n");
        return 1;
    }
    // AND NO REAL DRIVE. Sign out (6, below) empties roms/ and cache/ on every
    // location, and a games drive plugged into this machine is one: the drive
    // search points into the scratch root, where nothing is mounted, before
    // anything asks where the games are (#398, FIRSTRUN-14).
    setenv("CABINETOS_DRIVES", (std::string(root) + "/drives").c_str(), 1);
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[first-run] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    std::printf("scratch root  %s\n\n", root);
    check(storage::locations() == std::vector<std::string>{root},
          "the scratch root is the only place for games");

    // 1. A machine with nothing on it has not been set up.
    //
    // NOTE WHAT THIS CANNOT ISOLATE: `serverAddress()` reads $CABINETOS_ROMM and
    // /etc/cabinetos/session.env before it reads our file, by design — root's
    // answer outranks the session's. On a console that is configured by hand,
    // those are set, so "is it configured" can legitimately be true here. That
    // is the adoption rule working, not a failure, and the test says which case
    // it is rather than pretending the environment is clean.
    const bool envConfigured = !firstrun::serverAddress().empty();
    const firstrun::Completion before = firstrun::completion();
    if (envConfigured) {
        std::printf("  note  this machine has a server address from %s, so the\n"
                    "        'not set up' case cannot be checked here\n",
                    firstrun::serverAddressSource().c_str());
    } else {
        check(!before.done, "a machine with nothing on it is not set up");
    }

    // 2. The address is written, and comes back.
    const std::string want = "cabinetos-write-test.invalid:6005";
    std::string werr;
    check(firstrun::setServerAddress(want, &werr),
          "setServerAddress reports success");
    if (!werr.empty()) std::printf("        (%s)\n", werr.c_str());

    // Read the FILE, not the resolver, because the resolver correctly prefers
    // root's answer and would hide whether ours landed at all.
    {
        const std::string path = storage::configDir() + "/server.json";
        FILE* f = std::fopen(path.c_str(), "rb");
        std::string body;
        if (f) {
            char buf[512];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
            std::fclose(f);
        }
        check(body.find(want) != std::string::npos,
              "config/server.json holds the address that was written");
    }

    // 3. The marker is written, and says the flow was WALKED.
    check(firstrun::markCompleted(/*adopted=*/false, &werr),
          "markCompleted reports success");
    const firstrun::Completion after = firstrun::completion();
    check(after.done, "the console now reports itself set up");
    check(after.why == firstrun::Why::Completed,
          "and records that the flow was walked, not adopted");
    check(!after.when.empty(), "with a timestamp");

    // 4. THE ONE THAT MATTERS ON A REBOOT: it survives being read fresh. The
    // marker is a file, so this is the same question as "does the next boot see
    // it" — and a `completion()` that answered from memory would pass every
    // check above while a rebooted console went round the loop again.
    {
        const std::string path = storage::configDir() + "/first-run.json";
        struct stat st;
        check(::stat(path.c_str(), &st) == 0 && st.st_size > 0,
              "config/first-run.json is on disk and not empty");
    }

    // 5. Adopted is a different answer and has to stay different, because it is
    // how a machine set up by hand is told from one that walked the flow.
    check(firstrun::markCompleted(/*adopted=*/true, &werr), "markCompleted(adopted)");
    check(firstrun::completion().why == firstrun::Why::AdoptedExisting,
          "an adopted machine records itself as adopted");

    // 6. SIGN OUT (issue #61): everything tied to the server goes, and
    // nothing else. A console's worth of files in the scratch root, and a
    // scratch HOME for the tokens and the PIN, so no real one is touched.
    {
        const std::string r = root, home = r + "/home";
        setenv("HOME", home.c_str(), 1);
        auto put = [](const std::string& path) {
            storage::makeDirs(path.substr(0, path.rfind('/')));
            if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fputs("x\n", f); std::fclose(f); }
        };
        const std::vector<std::string> gone = {
            r + "/roms/snes/1 - Kept.sfc",       r + "/cache/snes/2 - Cached.sfc",
            r + "/users/1 - A/saves/snes/1/x.srm", r + "/users/1 - A/pending/1-x.srm.pending",
            r + "/covers/srv_6005/a.jpg",        r + "/config/user.json",
            r + "/config/accounts.json",         home + "/.config/cabinetos/accounts/1.json",
            home + "/.config/cabinetos/pin",
            // What was learned per game, by rom id: the next server's game 1
            // is another game (FILES-12).
            r + "/games/1.json",                 r + "/games/1.facts.json",
            r + "/config/wii-codes.txt",         r + "/config/wiiu-codes.txt",
            r + "/config/picture-quality.json",  r + "/config/ps2-widescreen.json",
        };
        const std::vector<std::string> kept = {
            r + "/bios/scph5501.bin", r + "/config/settings.json",
        };
        for (const auto& p : gone) put(p);
        for (const auto& p : kept) put(p);
        check(server::unsentSaves() == 1, "an unsent save is counted before signing out");
        check(server::signOut(&werr), "signOut reports success");
        const firstrun::Completion out = firstrun::completion();
        check(!out.done && out.why == firstrun::Why::SignedOut && out.clearing,
              "signed out: first run is needed, and the clearing is still owed");
        server::finishSignOut();
        bool allGone = true;
        for (const auto& p : gone)
            if (storage::exists(p)) { allGone = false; std::printf("        left: %s\n", p.c_str()); }
        check(allGone, "games and what was learned about them, saves, covers, accounts, tokens "
                       "and the PIN are gone");
        bool allKept = true;
        for (const auto& p : kept) allKept = allKept && storage::exists(p);
        check(allKept, "BIOS and the console's settings stay");
        check(storage::exists(r + "/roms") && storage::exists(r + "/cache") &&
                  storage::exists(r + "/users"),
              "the empty folders are back");
        const firstrun::Completion cleared = firstrun::completion();
        check(!cleared.done && cleared.why == firstrun::Why::SignedOut && !cleared.clearing,
              "still signed out, and cleared");
        check(!accounts::pinIsSet(), "no PIN");

        // 7. CHANGE SERVER ADDRESS keeps the covers, filed under the address.
        if (server::addressIsOurs()) {
            firstrun::setServerAddress("old.invalid:6005", &werr);
            put(r + "/covers/" + storage::safeSegment("old.invalid:6005") + "/a.jpg");
            check(server::changeAddress("old.invalid:6005", "new.invalid:6005", &werr),
                  "changeAddress reports success");
            check(firstrun::serverAddress() == "new.invalid:6005", "the new address is read back");
            check(storage::exists(r + "/covers/" + storage::safeSegment("new.invalid:6005") +
                                  "/a.jpg"),
                  "the covers moved with it");
        } else {
            std::printf("  note  the address is set by %s, so changing it cannot be checked "
                        "here\n", firstrun::serverAddressSource().c_str());
        }
    }

    // Leave nothing behind. A test that litters is a test nobody runs twice.
    std::string rm = "rm -rf ";
    rm += root;
    if (std::system(rm.c_str()) != 0)
        std::printf("\n  (could not remove %s)\n", root);

    std::printf("\nfirst-run writes: %d failure%s\n", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

// The store's rules, asserted against a scratch root rather than reasoned
// about. Every case here is a REFUSAL, because the happy path is the part that
// already works — the same argument --first-run-rules is built on, and it found
// a real deadlock on its first run.
int accountsTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    char root[] = "/tmp/cabinetos-accounts-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[accounts] could not make a scratch root\n");
        return 1;
    }
    // All three have to move: the list follows the storage root, the tokens
    // follow HOME, and the drive search would make each person's empty
    // CabinetOS/roms and cache folders on every plugged-in drive (#488).
    // Missing any one writes into the real console.
    storage::setRoot(root);
    setenv("HOME", root, 1);
    setenv("CABINETOS_DRIVES", (std::string(root) + "/drives").c_str(), 1);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[accounts] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    std::printf("scratch root  %s\n\n", root);

    std::string err;
    accounts::Account alice; alice.id = 1; alice.name = "MMagTech";
    accounts::Account bob;   bob.id = 2;   bob.name = "Someone Else";

    check(accounts::all().empty(), "a fresh console knows nobody");
    check(accounts::activeId() == 0, "and is acting as nobody");

    check(!accounts::add(alice, "", &err), "an account with no token is refused");
    accounts::Account noId; noId.name = "nobody";
    check(!accounts::add(noId, "t", &err), "an account with no id is refused");

    check(accounts::add(alice, "alice-token", &err), "the first account is added");
    check(accounts::activeId() == 1, "and becomes active, having nobody to take over from");

    check(accounts::add(bob, "bob-token", &err), "a second account is added");
    check(accounts::activeId() == 1,
          "and does NOT take over from whoever is signed in");
    check(accounts::all().size() == 2, "both are listed");

    check(accounts::add(alice, "alice-again", &err), "re-pairing an existing id succeeds");
    check(accounts::all().size() == 2, "and replaces rather than making a second row");

    check(!accounts::remove(1, &err), "removing the ACTIVE account is refused");
    check(!accounts::remove(99, &err), "removing an unknown account is refused");
    check(accounts::remove(2, &err), "removing an inactive account works");
    check(accounts::all().size() == 1, "and it is gone from the list");

    check(accounts::add(bob, "bob-token", &err) && accounts::setActive(2, &err),
          "someone else can be added and signed in");
    check(accounts::ownerId() == 1, "the first account added owns the console");
    check(!accounts::remove(1, &err),
          "removing the OWNER is refused, even with someone else signed in");
    check(accounts::setActive(1, &err) && accounts::remove(2, &err),
          "and the other one can go once the owner is back");

    check(!accounts::setActive(99, &err), "switching to an unknown account is refused");

    // The list and the tokens can disagree if somebody has been in here by
    // hand. That has to refuse rather than sign the console out silently.
    accounts::add(bob, "bob-token", &err);
    ::unlink(accounts::tokenPath(2).c_str());
    check(!accounts::setActive(2, &err), "switching to an account whose token is gone is refused");


    check(!accounts::pinIsSet(), "no PIN by default");
    check(!accounts::checkPin("0000"), "and nothing matches when none is set");
    check(accounts::setPin("4821", &err), "a PIN can be set");
    check(accounts::pinIsSet() && accounts::checkPin("4821"), "and it matches");
    check(!accounts::checkPin("4822"), "and a wrong one does not");
    check(accounts::setPin("", &err) && !accounts::pinIsSet(), "an empty PIN clears it");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all good",
                failures, failures == 1 ? "" : "s");
    std::printf("scratch root left at %s\n", root);
    return failures ? 1 : 0;
}

// Every combination of facts the flow can be handed, walked to the end.
//
// WHY THIS EXISTS RATHER THAN A DEMONSTRATION ON ONE MACHINE. The reference
// console is online over a cable with a paired token, so walking it there
// proves the happy path and nothing else — and the rules that matter most are
// the REFUSALS. Open question 15b's whole argument is that first run is a
// linear path with no skip, because on the far side of a skip there is nothing
// to show. A rule like that is only worth anything if it cannot be got round,
// and the way to know is to try every way round it rather than to read the code
// again.
//
// It needs no network, no server, no pad and no screen, so it runs in CI and it
// runs on a laptop.
int firstRunRules() {
    int cases = 0, failures = 0;
    auto fail = [&](const char* what, const firstrun::Facts& f) {
        ++failures;
        std::printf("  FAIL  %s\n        zone=%d online=%d wired=%d wifiHw=%d wifiUp=%d "
                    "addr=%d checked=%d answered=%d token=%d pads=%d\n",
                    what, f.zoneChosen, f.online, f.wiredOnline, f.wifiPresent,
                    f.wifiConfigured, f.haveServerAddress, f.serverChecked,
                    f.serverAnswered, f.havePairedToken, f.gamepadCount);
    };

    // Each set of facts twice: from the network, as on a console that has a
    // time zone, and from the time zone, as on one that had none (setup.cpp
    // opens there; the bit is the run's start, not a fact).
    for (int bits = 0; bits < 2048; ++bits) {
        firstrun::Facts f;
        f.online          = bits & 1;
        f.wiredOnline     = bits & 2;
        f.wifiPresent     = bits & 4;
        f.wifiConfigured  = bits & 8;
        f.haveServerAddress = bits & 16;
        f.serverAnswered  = bits & 32;
        f.havePairedToken = bits & 64;
        f.gamepadCount    = (bits & 128) ? 1 : 0;
        f.serverChecked   = bits & 256;
        f.zoneChosen      = bits & 512;
        const bool askZone = bits & 1024;

        // Nonsense the machine can never be handed: a wired link that is up
        // while nothing is online, or a radio connected with no radio. Skipped
        // rather than asserted about, because a rule about an impossible state
        // is a rule nobody can act on.
        if (f.wiredOnline && !f.online) continue;
        if (f.wifiConfigured && !f.wifiPresent) continue;
        if (f.wifiConfigured && !f.online) continue;
        // net::status() only ever reports online because of a wired device or a
        // radio, so "online by neither" cannot be handed to the machine. It is
        // skipped rather than asserted about — but the Wi-Fi gate is written to
        // survive it anyway, because that is one net.cpp change away from being
        // reachable.
        if (f.online && !f.wiredOnline && !f.wifiConfigured) continue;
        // A server cannot have answered without having been asked.
        if (f.serverAnswered && !f.serverChecked) continue;
        ++cases;

        firstrun::Machine m;
        m.update(f);
        if (askZone) m.openAt("timezone");

        // BACK REACHES THE TIME ZONE ONLY IN A RUN THAT ASKED IT: a console
        // with a zone of its own (an install from the ISO) never sees it.
        {
            firstrun::Machine copy = m;
            if (copy.step() == firstrun::Step::TimeZone) copy.advance();
            if (copy.step() == firstrun::Step::Network &&
                copy.back() != askZone)
                fail(askZone ? "Back from the network did not reach the time zone it asked"
                             : "Back from the network reached a time zone this run never asked",
                     f);
        }

        int guard = 0;
        for (; guard < 16 && !m.finished(); ++guard) {
            const firstrun::Gate g = m.gate();

            // Every refusal explains itself. A setup screen that stops with no
            // sentence is worse than a black one.
            if (g == firstrun::Gate::Blocked && m.because().empty())
                fail("a Blocked step with nothing to say", f);

            // advance() and skip() are the only enforcement there is, so they
            // must refuse on anything but their own gate.
            if (g != firstrun::Gate::Ready) {
                firstrun::Machine copy = m;
                if (copy.advance()) fail("advance() got past a gate that was not ready", f);
            }
            if (g != firstrun::Gate::Skippable) {
                firstrun::Machine copy = m;
                if (copy.skip()) fail("skip() got past a step that may not be skipped", f);
            }

            if (g == firstrun::Gate::Blocked) break;
            if (g == firstrun::Gate::Skippable) m.skip();
            else m.advance();
        }
        if (guard >= 16) fail("the chain did not terminate", f);

        // THE THREE HARD GATES. Finishing setup without any one of them is a
        // console with nothing to show, which is the thing 15b forbids. And a
        // run that asked the time zone does not finish without one.
        if (m.finished()) {
            if (!f.online)          fail("finished setup while offline", f);
            if (!f.serverAnswered)  fail("finished setup with no server answering", f);
            if (!f.havePairedToken) fail("finished setup with no token", f);
            if (askZone && !f.zoneChosen) fail("finished setup with no time zone", f);
        }
        if (askZone && !f.zoneChosen && m.step() != firstrun::Step::TimeZone)
            fail("got past the time zone with none chosen", f);

        // A machine with a radio, no cable and no Wi-Fi must not get past the
        // network step at all.
        if (!f.online && static_cast<int>(m.step()) > static_cast<int>(firstrun::Step::Network))
            fail("got past the network step while offline", f);

        // The one soft gate, and it has to STAY soft: refusing to finish
        // without a controller would break the keyboard guarantee the whole
        // flow exists to make.
        if (f.online && f.serverAnswered && f.havePairedToken && (f.zoneChosen || !askZone) &&
            !m.finished())
            fail("a fully configured machine could not finish setup", f);
    }

    // WHICH SCHEME THE SERVER STEP TRIES FIRST (#479): http for an address
    // that is the person's own, Tailscale's 100.64.0.0/10 included, and https
    // for the rest. The wrong one first cost a try at every heartbeat.
    const struct { const char* host; bool local; } schemes[] = {
        {"100.101.1.2", true},   {"100.101.1.2:8080", true}, {"100.64.0.1", true},
        {"100.127.255.254", true}, {"100.128.0.1", false},   {"100.63.0.1", false},
        {"192.168.77.20:7070", true}, {"romm", true},        {"romm.example.com", false},
    };
    for (const auto& c : schemes) {
        ++cases;
        if (romm::looksLocal(c.host) != c.local) {
            ++failures;
            std::printf("  FAIL  looksLocal(\"%s\") is not %s\n", c.host,
                        c.local ? "true" : "false");
        }
    }

    std::printf("first-run rules: %d fact combinations, %d failure%s\n", cases,
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

// What a full disk or a cut write leaves behind (#482). Every file the
// console replaces goes through cab::writeBytes; this proves the rules it
// promises in fileio.h, and the callers with rules of their own on top:
// pending markers, prefs::set, the cover cache's sweeps and a game's archive
// unpacked (romfile::prepareFile), and not unpacked again while what came out
// of it is untouched (romfile::alreadyUnpacked, #530), and the list of what a
// drive may not have yet (unsettled.h, #559), across boots played by their
// ids. A full disk is a file size limit here (RLIMIT_FSIZE, with SIGXFSZ
// ignored so the write fails rather than the process). A power cut cannot be
// made headless; the fsync is read, not run.
int filesTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    auto exists = [](const std::string& p) {
        struct stat st{};
        return ::stat(p.c_str(), &st) == 0;
    };

    char root[] = "/tmp/cabinetos-files-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[files] could not make a scratch root\n");
        return 1;
    }
    setenv("CABINETOS_DRIVES", (std::string(root) + "/drives").c_str(), 1);
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[files] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    std::printf("scratch root  %s\n\n", root);

    // 1. A good file, then a write the disk cannot take: the good file stays.
    const std::string path = std::string(root) + "/a/b/good.json";
    check(cab::writeBytes(path, std::string("{\"good\":true}\n")),
          "a file is written, its folders made");
    const std::vector<uint8_t> big(256 * 1024, 'x');
    struct rlimit was{};
    ::getrlimit(RLIMIT_FSIZE, &was);
    void (*oldXfsz)(int) = std::signal(SIGXFSZ, SIG_IGN);
    struct rlimit tight = was;
    tight.rlim_cur = 64 * 1024;
    ::setrlimit(RLIMIT_FSIZE, &tight);
    const bool bigWrote = cab::writeBytes(path, big);
    ::setrlimit(RLIMIT_FSIZE, &was);
    std::signal(SIGXFSZ, oldXfsz);
    check(!bigWrote, "a write the disk cannot hold says so");
    const std::vector<uint8_t> after = cab::readBytes(path);
    check(std::string(after.begin(), after.end()) == "{\"good\":true}\n",
          "and the file it was replacing is untouched");
    check(!exists(path + ".placing"), "and no temporary is left");

    // 2. A credential is never readable by anybody else, not even briefly.
    const std::string secret = std::string(root) + "/secret.json";
    ::close(::open((secret + ".placing").c_str(), O_WRONLY | O_CREAT, 0644));
    check(cab::writeBytes(secret, std::string("token\n"), 0600), "a 0600 file is written");
    struct stat st{};
    check(::stat(secret.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600,
          "with mode 0600, over a temporary a dead run left at 0644");

    // 3. Pending markers: written whole, and a temporary is not one.
    storage::User u;
    u.id = 1;
    u.name = "files-test";
    cache::Owed o;
    o.romId = 7;
    o.emulator = "files-test";
    o.fileName = "Game.srm";
    o.localPath = std::string(root) + "/Game.srm";
    cache::markPending(u, o, 123);
    const std::string stray = storage::pendingDir(u) + "/8-Other.srm.pending.placing";
    ::close(::open(stray.c_str(), O_WRONLY | O_CREAT, 0644));
    const std::vector<cache::Owed> owed = cache::owed(u);
    check(owed.size() == 1 && owed[0].romId == 7 && owed[0].fileName == "Game.srm",
          "a marker reads back as what was owed");
    check(cache::pendingCount(u) == 1, "a cut write's temporary is not counted as owed");

    // 4. Two prefs::set at once keep both changes (Steam's thread sets one).
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) {
        ts.emplace_back([t] {
            for (int i = 0; i < 50; ++i)
                prefs::set("files_test_" + std::to_string(t), std::to_string(i));
        });
    }
    for (std::thread& t : ts) t.join();
    bool allKept = true;
    for (int t = 0; t < 8; ++t)
        allKept = allKept && prefs::get("files_test_" + std::to_string(t), "") == "49";
    check(allKept, "eight threads setting their own key keep every last value");

    // 5. The cover cache's sweeps (FILES-04, 05): the size sweep leaves the
    // tile map and a cover being written, and the platform sweep removes a
    // link without following it.
    covercache::setServer("files-test");
    const std::string covers = covercache::dir() + "/" + storage::safeSegment("files-test");
    const std::string pic(1000, 'c');
    const time_t now = std::time(nullptr);
    auto aged = [&](const std::string& p, const std::string& body, time_t age) {
        cab::writeBytes(p, body);
        const struct timeval tv[2] = {{now - age, 0}, {now - age, 0}};
        ::utimes(p.c_str(), tv);
    };
    aged(covers + "/tiles.tsv", "1\t2025-01-01\t3\tcover\n", 1000);
    aged(covers + "/1/old.png", pic, 100);
    aged(covers + "/1/new.png", pic, 50);
    aged(covers + "/1/coming.png.part", pic, 0);
    covercache::evict(1500);
    check(!exists(covers + "/1/old.png") && exists(covers + "/1/new.png"),
          "the size sweep removes the least recently used cover");
    check(exists(covers + "/tiles.tsv"), "and leaves the tile map, though it is the oldest");
    check(exists(covers + "/1/coming.png.part"), "and a cover still being written");
    const std::string outside = std::string(root) + "/outside";
    cab::writeBytes(outside + "/keep.txt", std::string("keep\n"));
    ::symlink(outside.c_str(), (covers + "/99").c_str());
    covercache::sweep({1});
    struct stat lst{};
    check(::lstat((covers + "/99").c_str(), &lst) != 0,
          "a platform the server lost goes, though its folder is a link");
    check(exists(outside + "/keep.txt") && exists(covers + "/1/new.png"),
          "and what the link pointed at, and a live platform, stay");

    // 6. A game's archive keeps its folders (FILES-07): two members with one
    // name in two folders both come out whole, a member named outside the
    // game's folder refuses the archive, and so does a name that comes twice.
    const std::string src = std::string(root) + "/zipsrc";
    cab::writeBytes(src + "/a/x.bin", std::string("first\n"));
    cab::writeBytes(src + "/b/x.bin", std::string("second\n"));
    cab::writeBytes(src + "/A/X.bin", std::string("third\n"));
    cab::writeBytes(src + "/x", std::string("outside\n"));
    storage::makeDirs(src + "/in");
    auto unpack = [&](const std::string& game, const std::string& from,
                      const std::vector<std::string>& members, std::string* primary,
                      std::string* why) {
        std::vector<uint8_t> zip;
        if (!cab::zipTree(from, members, &zip, why)) return false;
        const std::string dir = std::string(root) + "/" + game;
        const std::string file = dir + "/" + game + ".zip";
        cab::writeBytes(file, zip);
        romfile::Kind kind = romfile::Kind::Plain;
        const std::atomic<bool> cancel{false};
        return romfile::prepareFile(file, dir, "bin", false, primary, &kind, why, cancel);
    };
    auto text = [](const std::string& p) {
        const std::vector<uint8_t> b = cab::readBytes(p);
        return std::string(b.begin(), b.end());
    };
    std::string primary, why;
    const std::string twoDirs = std::string(root) + "/twodirs";
    const bool unpacked = unpack("twodirs", src, {"a/x.bin", "b/x.bin"}, &primary, &why);
    check(unpacked && text(twoDirs + "/a/x.bin") == "first\n" &&
              text(twoDirs + "/b/x.bin") == "second\n",
          "a zip holding a/x.bin and b/x.bin comes out whole in both places");
    check(primary == twoDirs + "/a/x.bin", "and the core is handed the first, in its folder");
    check(!unpack("escapes", src + "/in", {"../x"}, &primary, &why) &&
              !exists(std::string(root) + "/x") && !exists(std::string(root) + "/escapes/x"),
          "a member named ../x is refused, and nothing is written for it");
    check(!unpack("twice", src, {"a/x.bin", "a/x.bin"}, &primary, &why),
          "a name that comes twice fails the unpacking");
    const bool cased = unpack("cased", src, {"a/x.bin", "A/X.bin"}, &primary, &why);
    check(!cased && text(std::string(root) + "/cased/a/x.bin") == "first\n",
          "and so does one differing only in case (one file on exFAT), the first not "
          "written over");

    // 7. A game's archive is unpacked once (#530): a later start finds every
    // file at its size with the archive's own time and boots the same one;
    // anything else unpacks again.
    const std::string again = std::string(root) + "/again";
    const std::string zip = again + "/again.zip";
    std::string boot;
    const bool once = unpack("again", src, {"a/x.bin", "b/x.bin"}, &boot, &why);
    auto skips = [&] {
        std::string booted;
        return romfile::alreadyUnpacked(zip, again, "bin", false, &booted) && booted == boot;
    };
    check(once && skips(), "a second start finds the zip unpacked, and boots the same file");
    const std::atomic<bool> go{false}, stop{true};
    auto redo = [&](const std::atomic<bool>& cancel) {
        romfile::Kind kind = romfile::Kind::Plain;
        return romfile::prepareFile(zip, again, "bin", false, &boot, &kind, &why, cancel);
    };
    struct stat zst{};
    ::stat(zip.c_str(), &zst);
    auto stampAt = [&](const std::string& p, time_t later) {
        const struct timespec t[2] = {{0, UTIME_OMIT},
                                      {zst.st_mtim.tv_sec + later, zst.st_mtim.tv_nsec}};
        ::utimensat(AT_FDCWD, p.c_str(), t, 0);
    };
    // A filesystem's clock moves in ticks of a few milliseconds, and a write
    // in the same tick as the download is not one a person can make.
    ::usleep(20 * 1000);
    cab::writeBytes(again + "/a/x.bin", std::string("FIRST\n"));
    check(!skips(), "a file changed at its own size unpacks again");
    check(redo(go) && text(again + "/a/x.bin") == "first\n" && skips(),
          "and the unpacking puts it back, skipped after");
    stampAt(again + "/b/x.bin", 1);
    check(!skips(), "a file one second newer than the archive unpacks again");
    redo(go);
    ::truncate((again + "/b/x.bin").c_str(), 3);
    stampAt(again + "/b/x.bin", 0);
    check(!skips(), "a file cut short unpacks again, though it has the archive's time");
    check(!redo(stop) && !skips(), "and so does an unpacking cancelled before its end");
    check(redo(go) && skips(), "which the next unpacking finishes");
    ::usleep(20 * 1000);
    cab::writeBytes(zip, cab::readBytes(zip));
    check(!skips(), "a new download of the archive unpacks again, the same bytes or not");

    // 8. What a drive may not have yet (unsettled.h, #559). A list of this
    // boot is kept; one from an earlier boot deletes what it names, so the
    // game comes down again; a settle drops the list once the drive has it.
    const std::string un = std::string(root) + "/unsettled-game";
    const std::string unA = un + "/a.bin", unB = un + "/b.bin", unCut = un + "/b.b";
    const std::string unList = storage::configDir() + "/unsettled/77";
    cab::writeBytes(unA, std::string("a\n"));
    cab::writeBytes(unB, std::string("b\n"));
    check(!unsettled::note({unA}), "nothing is listed with no launch begun");
    unsettled::begin(77, "boot-1");
    check(unsettled::note({unA}) && unsettled::note({unA, unB}) && exists(unList),
          "a launch lists the files it marks, on the console's own drive");
    unsettled::begin(77, "boot-1");
    check(exists(unA) && exists(unB), "a second start in the same boot keeps them");
    unsettled::begin(77, "boot-2");
    check(!exists(unA) && !exists(unB) && !exists(unList),
          "a start in a later boot deletes them, and the list");
    cab::writeBytes(unA, std::string("a\n"));
    check(unsettled::note({unA}), "and lists what it writes again");
    unsettled::settle();
    bool dropped = false;
    for (int i = 0; i < 500 && !dropped; ++i) {
        dropped = !exists(unList);
        if (!dropped) ::usleep(10 * 1000);
    }
    check(dropped && exists(unA), "once the drive has them the list goes, and the files stay");
    unsettled::begin(77, "boot-3");
    check(exists(unA), "so a later boot keeps them");
    unsettled::settle();
    cab::writeBytes(unCut, std::string("x"));
    cab::writeBytes(unList, "boot-4\n" + unA + "\n" + unB.substr(0, unB.size() - 2));
    unsettled::begin(77, "boot-5");
    check(!exists(unA) && exists(unCut), "a last line a power cut left unfinished names nothing");
    unsettled::settle();

    storage::removeEntry(root);
    std::printf("\nfiles: %d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

namespace {

bool sameFile(const romm::SnapshotFile& a, const romm::SnapshotFile& b) {
    return a.id == b.id && a.fileName == b.fileName && a.sizeBytes == b.sizeBytes &&
           a.contentHash == b.contentHash && a.picturePath == b.picturePath &&
           a.shape == b.shape && a.format == b.format && a.emulator == b.emulator &&
           a.core == b.core;
}

bool sameSnapshot(const romm::Snapshot& a, const romm::Snapshot& b) {
    if (a.id != b.id || a.digest != b.digest || a.kind != b.kind || a.parentId != b.parentId ||
        a.channelId != b.channelId || a.hardcore != b.hardcore || a.createdAt != b.createdAt ||
        a.deviceOwn != b.deviceOwn || a.deviceId != b.deviceId ||
        a.deviceClient != b.deviceClient || a.hasSave != b.hasSave ||
        (a.hasSave && !sameFile(a.save, b.save)) || a.heldBy.size() != b.heldBy.size() ||
        a.states.size() != b.states.size())
        return false;
    for (size_t i = 0; i < a.heldBy.size(); ++i)
        if (a.heldBy[i].deviceId != b.heldBy[i].deviceId ||
            a.heldBy[i].client != b.heldBy[i].client ||
            a.heldBy[i].syncedAt != b.heldBy[i].syncedAt)
            return false;
    for (size_t i = 0; i < a.states.size(); ++i)
        if (a.states[i].core != b.states[i].core || a.states[i].slot != b.states[i].slot ||
            !sameFile(a.states[i].file, b.states[i].file))
            return false;
    return true;
}

bool sameChannel(const romm::Channel& a, const romm::Channel& b) {
    return a.id == b.id && a.label == b.label && a.hardcore == b.hardcore &&
           a.romId == b.romId && a.romFileId == b.romFileId &&
           a.snapshotCount == b.snapshotCount && a.updatedAt == b.updatedAt &&
           a.currentId == b.currentId && (a.currentId == 0 || sameSnapshot(a.current, b.current));
}

bool samePush(const cache::OwedPush& a, const cache::OwedPush& b) {
    if (a.romId != b.romId || a.madeAt != b.madeAt || a.romFileId != b.romFileId ||
        a.channelId != b.channelId || a.label != b.label ||
        a.expectedCurrentId != b.expectedCurrentId || a.parentId != b.parentId ||
        a.couldNotUseNewest != b.couldNotUseNewest || a.emulator != b.emulator ||
        a.core != b.core || a.hasSave != b.hasSave || a.saveName != b.saveName ||
        a.savePath != b.savePath || a.saveShape != b.saveShape ||
        a.states.size() != b.states.size())
        return false;
    for (size_t i = 0; i < a.states.size(); ++i) {
        const cache::OwedPush::State &x = a.states[i], &y = b.states[i];
        if (x.core != y.core || x.madeMs != y.madeMs || x.fileName != y.fileName ||
            x.path != y.path || x.pictureName != y.pictureName || x.picturePath != y.picturePath)
            return false;
    }
    return true;
}

romm::SnapshotFile noteFile(int id, const std::string& name, const std::string& hash) {
    romm::SnapshotFile f;
    f.id = id;
    f.fileName = name;
    f.sizeBytes = 1000 + id;
    f.contentHash = hash;
    f.picturePath = "picture-" + std::to_string(id) + ".png";
    f.shape = "SINGLE";
    f.format = "native";
    f.emulator = "gambatte-native";
    f.core = "gambatte";
    return f;
}

}  // namespace

// `--save-notes-test` (#346): what the console remembers about each game's
// save (savemirror.h, pending.h), in a scratch root. RomM's channels as last
// seen read back with every field, an empty one and a hardcore one among
// them; each current's save kept by its hash and dropped once no channel
// holds it, RomM's old rows and their bytes beside them each left by the
// other; forgetting RomM's copy leaves the snapshot this console holds, and
// a game gone takes it. A game's owed push read back whole; a newer one
// replaces it; a push that lands clears only the one it sent; a cut write's
// temporary is no marker. And the markers today's version writes, one per
// file, read back beside a game's push as the upload queue sends them
// (Uploader::resendOwedFor makes its job of these fields).
int saveNotesTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    auto exists = [](const std::string& p) {
        struct stat st{};
        return ::stat(p.c_str(), &st) == 0;
    };
    char root[] = "/tmp/cabinetos-save-notes-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[save notes] could not make a scratch root\n");
        return 1;
    }
    setenv("CABINETOS_DRIVES", (std::string(root) + "/drives").c_str(), 1);
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[save notes] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    std::printf("scratch root  %s\n\n", root);
    storage::User u;
    u.id = 1;
    u.name = "notes-test";
    const int game = 705;
    const std::string folder = storage::userDir(u) + "/server-saves/705";

    // 1. RomM's channels for a game, as last seen.
    romm::Channel run;
    run.id = "01937a3c-0000-7000-8000-00000000000a";
    run.label = "default";
    run.romId = game;
    run.romFileId = 4410;
    run.snapshotCount = 3;
    run.updatedAt = "2026-11-18T22:42:48.649000+00:00";
    run.currentId = 12;
    run.current.id = 12;
    run.current.digest = "sha256:5d1c0ffee";
    run.current.kind = "channel";
    run.current.parentId = 11;
    run.current.channelId = run.id;
    run.current.createdAt = "2026-11-18T22:42:48.649000+00:00";
    run.current.deviceOwn = true;
    run.current.deviceId = "01937a3c-0000-7000-8000-0000000000d1";
    run.current.deviceClient = "cabinetos";
    run.current.heldBy.push_back({run.current.deviceId, "cabinetos", "2026-11-18T22:42:49Z"});
    run.current.hasSave = true;
    run.current.save = noteFile(31, "Game [2026-11-18_22-42-48-649].srm",
                                "0123456789abcdef0123456789abcdef");
    run.current.states.push_back({"gambatte", "1", noteFile(41, "Game.state1", "41")});
    run.current.states.push_back({"gambatte", "2", noteFile(42, "Game.state2", "42")});
    run.current.states.push_back({"mgba", "auto", noteFile(43, "Game.auto", "43")});
    romm::Channel hard;
    hard.id = "01937a3c-0000-7000-8000-00000000000b";
    hard.label = "Phone";
    hard.hardcore = true;
    hard.romId = game;
    hard.romFileId = 4410;
    hard.snapshotCount = 1;
    hard.updatedAt = "2026-11-17T08:00:00+00:00";
    hard.currentId = 20;
    hard.current.id = 20;
    hard.current.digest = "sha256:aaaa";
    hard.current.kind = "channel";
    hard.current.channelId = hard.id;
    hard.current.hardcore = true;
    hard.current.createdAt = "2026-11-17T08:00:00+00:00";
    hard.current.hasSave = true;
    hard.current.save = noteFile(50, "Game.srm", "fedcba9876543210fedcba9876543210");
    romm::Channel empty;
    empty.id = "01937a3c-0000-7000-8000-00000000000c";
    empty.label = "softcore";
    empty.romId = game;
    empty.romFileId = 4410;
    std::vector<romm::Channel> got;
    check(!savemirror::getChannels(u, game, &got) && got.empty(),
          "a game whose channels were never seen has none");
    savemirror::putChannels(u, game, {run, hard, empty});
    check(savemirror::getChannels(u, game, &got) && got.size() == 3 &&
              sameChannel(got[0], run) && sameChannel(got[1], hard) && sameChannel(got[2], empty),
          "three channels read back with every field: one with its states, a hardcore one "
          "and an empty one");

    // 2. Each current's save, by its hash, beside RomM's old rows.
    const std::vector<uint8_t> runSave(2048, 0x5a), hardSave(512, 0xa5), oldSave(64, 0x33);
    savemirror::putSnapshotSave(u, game, run.current.save, runSave);
    savemirror::putSnapshotSave(u, game, hard.current.save, hardSave);
    check(savemirror::getSnapshotSave(u, game, run.current.save) == runSave &&
              savemirror::getSnapshotSave(u, game, hard.current.save) == hardSave,
          "each current's save reads back as it was kept");
    romm::SnapshotFile other = run.current.save;
    other.contentHash = "99999999999999999999999999999999";
    romm::SnapshotFile unhashed = run.current.save;
    unhashed.contentHash.clear();
    savemirror::putSnapshotSave(u, game, unhashed, runSave);
    check(savemirror::getSnapshotSave(u, game, other).empty() &&
              !savemirror::hasSnapshotSave(u, game, unhashed),
          "another hash under the same id is not those bytes, and a save with no hash is "
          "not kept");
    romm::Asset row;
    row.id = 5;
    row.romId = game;
    row.fileName = "Game (Cabinet).srm";
    row.sizeBytes = 64;
    row.emulator = "gambatte-native";
    row.updatedAt = "2026-11-01T10:00:00+00:00";
    savemirror::putList(u, game, {row});
    savemirror::putBytes(u, game, row, oldSave);
    savemirror::putChannels(u, game, {run, empty});
    check(savemirror::hasSnapshotSave(u, game, run.current.save) &&
              !savemirror::hasSnapshotSave(u, game, hard.current.save),
          "a save no listed channel holds any more is dropped, the one still held kept");
    savemirror::putList(u, game, {row});
    std::vector<romm::Asset> rows;
    check(savemirror::getList(u, game, &rows) && rows.size() == 1 && rows[0].id == 5 &&
              savemirror::getBytes(u, game, row) == oldSave &&
              savemirror::hasSnapshotSave(u, game, run.current.save),
          "RomM's old rows and their bytes stay beside the channels, each left by the other");
    cab::writeBytes(folder + "/channels.json", std::string("{\"detail\":\"cut\"}"));
    check(!savemirror::getChannels(u, game, &got) && got.empty(),
          "channels that are not a list read as none");
    savemirror::putChannels(u, game, {run, empty});

    // 3. The snapshot this console holds.
    savemirror::Held held;
    check(!savemirror::getHeld(u, game, &held), "a game this console never held has no note");
    savemirror::Held had;
    had.channelId = run.id;
    had.snapshotId = 12;
    had.digest = run.current.digest;
    had.saveHash = run.current.save.contentHash;
    had.at = 1763505768;
    had.madeAt = 1763502168;
    had.beside = true;
    had.hashesHad = {"fedcba9876543210fedcba9876543210", had.saveHash};
    savemirror::putHeld(u, game, had);
    savemirror::Held none;
    savemirror::putHeld(u, game, none);
    check(savemirror::getHeld(u, game, &held) && held.channelId == had.channelId &&
              held.snapshotId == 12 && held.digest == had.digest &&
              held.saveHash == had.saveHash && held.at == had.at &&
              held.madeAt == had.madeAt && held.beside && held.hashesHad == had.hashesHad,
          "the snapshot held reads back whole, with every save's hash this console has had, "
          "and a note of no snapshot writes nothing");
    const uint64_t before = savemirror::mark();
    savemirror::forget(u, game);
    check(!savemirror::getChannels(u, game, &got) && !savemirror::getList(u, game, &rows) &&
              !savemirror::hasSnapshotSave(u, game, run.current.save) &&
              !savemirror::hasBytes(u, game, row) && savemirror::forgottenSince(game, before),
          "forgetting RomM's copy takes the channels, the rows and every byte kept");
    check(savemirror::getHeld(u, game, &held) && held.snapshotId == 12,
          "and leaves the snapshot this console holds");
    savemirror::forgetHeld(u, game);
    check(!savemirror::getHeld(u, game, &held) && !exists(folder),
          "a game gone takes that too, and its folder");

    // 4. A game's owed push.
    check(!cache::isPushOwed(u, game) && cache::owedPushes(u).empty(),
          "a game with nothing owed has no push owed");
    cache::OwedPush push;
    push.romId = game;
    push.madeAt = 1763505768;
    push.romFileId = 4410;
    push.channelId = run.id;
    push.label = "default";
    push.expectedCurrentId = 12;
    push.parentId = 11;
    push.couldNotUseNewest = true;
    push.emulator = "gambatte-native";
    push.core = "gambatte";
    push.hasSave = true;
    push.saveName = "Game (Cabinet).srm";
    push.savePath = std::string(root) + "/saves/Game (Cabinet).srm";
    push.saveShape = "SINGLE";
    push.states.push_back({"gambatte", "Game (Cabinet).state1", root + std::string("/1.state"),
                           "Game (Cabinet).state1.png", root + std::string("/1.png"),
                           1763560512345});
    push.states.push_back({"gambatte", "Game (Cabinet).state2", root + std::string("/2.state"),
                           "", "", 1763560400000});
    const std::string first = cache::markPushOwed(u, push, 4096);
    cache::OwedPush back;
    check(!first.empty() && cache::owedPush(u, game, &back) && back.stamp == first &&
              samePush(back, push),
          "a game's owed push reads back with every field, a state with no picture among them");
    check(cache::isPushOwed(u, game) && cache::owedPushes(u).size() == 1 &&
              cache::pendingCount(u) == 1 && cache::pendingBytes() == 4096 &&
              cache::owed(u).empty(),
          "it is owed, counted once with its bytes, and is no file owed");
    cache::OwedPush newer = push;
    newer.hasSave = false;
    newer.saveName.clear();
    newer.savePath.clear();
    newer.saveShape.clear();
    newer.couldNotUseNewest = false;
    newer.channelId.clear();
    newer.label = "softcore";
    newer.expectedCurrentId = 0;
    newer.parentId = 0;
    newer.states.resize(1);
    const std::string second = cache::markPushOwed(u, newer, 1024);
    check(!second.empty() && second != first && cache::owedPush(u, game, &back) &&
              back.stamp == second && samePush(back, newer) && cache::pendingCount(u) == 1,
          "a newer one, states only, into a channel yet to be made, replaces it whole");
    cache::clearPushOwed(u, game, first);
    check(cache::isPushOwed(u, game), "a push of the older one landing leaves the newer owed");
    cache::clearPushOwed(u, game, second);
    check(!cache::isPushOwed(u, game) && cache::pendingCount(u) == 0,
          "and the push of the newer one landing clears it");
    cab::writeBytes(storage::pendingDir(u) + "/706.push.placing", std::string("12\n"));
    check(cache::pendingCount(u) == 0 && cache::owedPushes(u).empty(),
          "a cut write's temporary is no push owed");

    // 5. The markers today's version writes, one per file, beside a push:
    // the bytes it writes, not written through markPending, so a change to
    // either is seen here.
    const std::string pend = storage::pendingDir(u);
    cab::writeBytes(pend + "/8-Game__Cabinet_.srm.pending",
                    std::string("2048\nkind save\nrom 8\nemulator gambatte-native\n"
                                "name Game (Cabinet).srm\nlocal /saves/Game (Cabinet).srm\n"));
    cab::writeBytes(pend + "/8-Game__Cabinet_.state1.pending",
                    std::string("512\nkind state\nrom 8\nemulator gambatte\n"
                                "name Game (Cabinet).state1\nlocal /states/1.state\n"
                                "shotname Game (Cabinet).state1.png\nshot /states/1.png\n"));
    push.romId = 8;
    const std::string beside = cache::markPushOwed(u, push, 256);
    const std::vector<cache::Owed> owed = cache::owed(u);
    const cache::Owed* save = nullptr;
    const cache::Owed* state = nullptr;
    for (const cache::Owed& o : owed) (o.isState ? state : save) = &o;
    check(owed.size() == 2 && save && state, "today's two markers read back beside the push");
    check(save && save->romId == 8 && !save->isState && !save->isScreenshot &&
              save->emulator == "gambatte-native" && save->fileName == "Game (Cabinet).srm" &&
              save->localPath == "/saves/Game (Cabinet).srm" && save->shotName.empty(),
          "a save's as the upload queue sends it: its game, tag, name and file");
    check(state && state->romId == 8 && state->isState && state->emulator == "gambatte" &&
              state->fileName == "Game (Cabinet).state1" && state->localPath == "/states/1.state" &&
              state->shotName == "Game (Cabinet).state1.png" && state->shotPath == "/states/1.png",
          "a state's with its picture");
    check(cache::pendingCount(u) == 3 && cache::pendingBytes() == 2048 + 512 + 256,
          "each still owed, counted and its bytes held back with the push's");
    cache::clearPending(u, 8, "Game (Cabinet).srm");
    check(cache::isPushOwed(u, 8) && cache::owed(u).size() == 1 && cache::owed(u)[0].isState,
          "a file sent clears its own marker and leaves the push owed");
    cache::clearPushOwed(u, 8, beside);
    check(!cache::isPushOwed(u, 8) && cache::owed(u).size() == 1,
          "and the push landing leaves the file still owed");

    storage::removeEntry(root);
    std::printf("\nsave notes: %d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

// `--save-pick-test` (#346): which save a start continues from. The rule's
// own table first (savepick::test, no disk), then the restores that write the
// picked save, in a scratch root with no core: a core's save file and a save
// folder each taken from a channel's snapshot and from RomM's saves from
// before channels, the save they replace kept aside first and only then, one
// copy a game; a save that is what the console already has changing nothing;
// an empty one never replacing a real one, and a pick of the console's own
// taking nothing; and the notes a start leaves, in the session and of the
// snapshot held.
int savePickTest() {
    std::printf("which save a start continues from\n\n");
    int failures = savepick::test();
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    auto exists = [](const std::string& p) {
        struct stat st{};
        return ::stat(p.c_str(), &st) == 0;
    };
    char root[] = "/tmp/cabinetos-save-pick-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[save pick] could not make a scratch root\n");
        return 1;
    }
    setenv("CABINETOS_DRIVES", (std::string(root) + "/drives").c_str(), 1);
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[save pick] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    std::printf("\nthe restores, in %s\n", root);
    storage::User u;
    u.id = 1;
    u.name = "pick-test";
    storage::setCurrentUser(u);
    const char* tag = "fbneo-native";
    const int game = 705;
    const std::string saveDir = storage::savesDir(u, "FBNEO", game, "fbneo");
    const std::string aside = storage::replacedSaveDir(u, "FBNEO", game);
    auto bytesOf = [](const char* text, size_t size) {
        std::vector<uint8_t> b(size, 0x00);
        for (size_t i = 0; text[i] && i < size; ++i) b[i] = static_cast<uint8_t>(text[i]);
        return b;
    };
    const std::vector<uint8_t> own = bytesOf("this console's own save", 64);
    const std::vector<uint8_t> theirs = bytesOf("a save another device made", 64);
    const std::vector<uint8_t> blank(64, 0xFF);

    // A fetch whose pick is a channel's snapshot holding `save`.
    auto fromChannel = [&](int snapshotId, const std::vector<uint8_t>& save, bool elsewhere) {
        SaveFetch f;
        f.romId = game;
        f.asked = f.listed = true;
        f.channelFileId = 4410;
        f.mark = savemirror::mark();
        f.pick.from = savepick::Pick::From::Channel;
        f.pick.madeElsewhere = elsewhere;
        f.pick.channel.id = "01937a3c-0000-7000-8000-00000000000a";
        f.pick.channel.label = "default";
        f.pick.channel.currentId = snapshotId;
        f.pick.channel.current.id = snapshotId;
        f.pick.channel.current.digest = "sha256:" + std::to_string(snapshotId);
        f.pick.channel.current.hasSave = true;
        f.pick.channel.current.save.id = snapshotId * 10;
        f.pick.channel.current.save.fileName = "kof98.fs";
        f.pick.channel.current.save.contentHash = romm::contentHash(save);
        f.pick.channel.current.save.emulator = tag;
        f.picked = save;
        return f;
    };
    auto notesFor = [&]() {
        StartNotes n;
        n.asideDir = aside;
        return n;
    };

    // 1. A core's save file (FinalBurn Neo's, one file).
    catalog::SaveFile spec;
    spec.path = "fbneo/kof98.fs";
    spec.untouched = catalog::Untouched::Uniform;
    const std::string file = saveDir + "/fbneo/kof98.fs";
    {
        StartNotes n = notesFor();
        const SaveFetch f = fromChannel(21, theirs, true);
        const std::vector<cab::FileSaveState> st =
            restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
        GameSession sess;
        const bool pill = noteStart(f, n, u, &sess);
        savemirror::Held held;
        check(cab::readBytes(file) == theirs && st.size() == 1 && st[0].atLaunch == theirs &&
                  n.fromChannel && n.changedOwn && n.keptAside.empty() && !exists(aside),
              "a console with no save takes the snapshot's, and keeps nothing aside");
        check(pill && sess.startChannelId == f.pick.channel.id && sess.startSnapshotId == 21 &&
                  sess.channelFileId == 4410 && !sess.couldNotUseNewest &&
                  savemirror::getHeld(u, game, &held) && held.snapshotId == 21 &&
                  held.saveHash == romm::contentHash(theirs) && held.hashesHad.size() == 1,
              "the start says so, the session knows its channel and snapshot, and the "
              "console holds the snapshot");
    }
    {
        StartNotes n = notesFor();
        const SaveFetch f = fromChannel(21, theirs, true);
        restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
        GameSession sess;
        check(n.fromChannel && !n.changedOwn && n.keptAside.empty() && !exists(aside) &&
                  !noteStart(f, n, u, &sess) && sess.startSnapshotId == 21,
              "started again from the same save: nothing replaced, nothing aside, no pill");
    }
    cab::writeBytes(file, own);
    {
        StartNotes n = notesFor();
        const SaveFetch f = fromChannel(22, theirs, true);
        restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
        check(cab::readBytes(file) == theirs && n.changedOwn && n.keptAside.size() == 1 &&
                  cab::readBytes(aside + "/fbneo/kof98.fs") == own,
              "a newer save over this console's own: its own is kept aside first, under its "
              "name in the save folder");
    }
    {
        // The next start that replaces one: the one copy is the newest.
        cab::writeBytes(aside + "/left-by-an-earlier-start.bin", own);
        const std::vector<uint8_t> third = bytesOf("a third save", 64);
        StartNotes n = notesFor();
        const SaveFetch f = fromChannel(23, third, true);
        restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
        check(cab::readBytes(file) == third && cab::readBytes(aside + "/fbneo/kof98.fs") == theirs &&
                  !exists(aside + "/left-by-an-earlier-start.bin"),
              "the next replacement takes the place of the last copy kept");
    }
    cab::writeBytes(file, own);
    {
        StartNotes n = notesFor();
        SaveFetch f = fromChannel(24, blank, true);
        restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
        GameSession sess;
        noteStart(f, n, u, &sess);
        check(cab::readBytes(file) == own && !n.fromChannel && sess.startChannelId.empty() &&
                  sess.couldNotUseNewest,
              "a snapshot whose save holds nothing never replaces a real one, and the session "
              "knows the newest was not used");
        f = fromChannel(25, {}, true);
        restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
        check(cab::readBytes(file) == own && !n.fromChannel,
              "nor one whose bytes did not come");
    }
    {
        // RomM's saves from before channels, by the old rule, with the same
        // care for the save they replace.
        SaveFetch f;
        f.romId = game;
        f.asked = f.listed = true;
        f.mark = savemirror::mark();
        f.pick.from = savepick::Pick::From::OldRows;
        romm::Asset row;
        row.id = 9;
        row.romId = game;
        row.fileName = "kof98 (Cabinet fbneo).srm";
        row.emulator = tag;
        row.updatedAt = "2026-11-18T20:00:00+00:00";
        f.rows = {row};
        f.bytes[9] = theirs;
        StartNotes n = notesFor();
        restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
        GameSession sess;
        check(cab::readBytes(file) == theirs && !n.fromChannel && n.keptAside.size() == 1 &&
                  cab::readBytes(aside + "/fbneo/kof98.fs") == own &&
                  !noteStart(f, n, u, &sess) && sess.startChannelId.empty(),
              "a save from before channels over this console's own: kept aside too, no pill, "
              "no channel in the session");
        // A pick of this console's own, which is what a save still owed
        // gets (savepick.h), takes nothing from RomM whatever was fetched.
        cab::writeBytes(file, own);
        f.pick.from = savepick::Pick::From::Own;
        StartNotes kept = notesFor();
        restoreFileSaves({spec}, "kof98", saveDir, tag, f, &kept);
        check(cab::readBytes(file) == own && kept.keptAside.empty(),
              "a pick of this console's own save leaves it, whatever RomM listed");
    }
    {
        // A fetch that went stale: a save of this game landed after it.
        cab::writeBytes(file, own);
        const SaveFetch f = fromChannel(26, theirs, true);
        savemirror::forget(u, game);
        StartNotes n = notesFor();
        restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
        GameSession sess;
        check(cab::readBytes(file) == own && !n.fromChannel && !noteStart(f, n, u, &sess) &&
                  !sess.couldNotUseNewest,
              "a save of this console's that reached RomM after the fetch: its own stands");
    }
    {
        // Two files in one snapshot: a zip, each under its path.
        catalog::SaveFile internal, cart;
        internal.path = "Lunar.brm";
        internal.untouched = catalog::Untouched::Blank;
        cart.path = "4Mbit_cart.brm";
        cart.region = "cart";
        cart.untouched = catalog::Untouched::Blank;
        const std::string dir = storage::savesDir(u, "Sega CD", 801, "genesis_plus_gx");
        cab::writeBytes(dir + "/Lunar.brm", theirs);
        cab::writeBytes(dir + "/4Mbit_cart.brm", own);
        std::vector<uint8_t> zip;
        std::string zerr;
        cab::zipTree(dir, {"Lunar.brm", "4Mbit_cart.brm"}, &zip, &zerr);
        cab::removeFile(dir + "/Lunar.brm");
        cab::writeBytes(dir + "/4Mbit_cart.brm", theirs);
        SaveFetch f = fromChannel(31, zip, true);
        f.romId = 801;
        StartNotes n;
        n.asideDir = storage::replacedSaveDir(u, "Sega CD", 801);
        restoreFileSaves({internal, cart}, "Lunar", dir, "gpgx-native", f, &n);
        check(cab::readBytes(dir + "/Lunar.brm") == theirs &&
                  cab::readBytes(dir + "/4Mbit_cart.brm") == own && n.fromChannel &&
                  n.keptAside.size() == 1 &&
                  cab::readBytes(n.asideDir + "/4Mbit_cart.brm") == theirs,
              "a snapshot's zip of two save files: each to its own file, the one replaced "
              "kept aside");
        // One file as it is goes to the first of the core's files only.
        f = fromChannel(32, own, true);
        f.romId = 801;
        StartNotes single;
        restoreFileSaves({internal, cart}, "Lunar", dir, "gpgx-native", f, &single);
        check(cab::readBytes(dir + "/Lunar.brm") == own &&
                  cab::readBytes(dir + "/4Mbit_cart.brm") == own,
              "a snapshot's one file: the first of the core's files, the other left");
    }

    // 2. A save folder (PSP's memory stick).
    {
        const std::string psp = storage::savesDir(u, "PSP", 900, "ppsspp") + "/PSP/SAVEDATA";
        const std::string pspAside = storage::replacedSaveDir(u, "PSP", 900);
        cab::writeBytes(psp + "/ULUS10002/PARAM.SFO", theirs);
        cab::writeBytes(psp + "/ULUS10002/DATA.BIN", theirs);
        std::vector<uint8_t> zip;
        std::string zerr;
        cab::zipTree(psp, {"ULUS10002/PARAM.SFO", "ULUS10002/DATA.BIN"}, &zip, &zerr);
        SaveFetch f = fromChannel(41, zip, true);
        f.romId = 900;
        StartNotes same;
        same.asideDir = pspAside;
        restoreDirSave(f, "ppsspp-native", psp, "Lumines", std::string(), &same);
        check(same.fromChannel && !same.changedOwn && same.keptAside.empty() && !exists(pspAside),
              "a save folder that already holds exactly the snapshot's save: nothing changes");
        cab::writeBytes(psp + "/ULUS10002/DATA.BIN", own);
        cab::writeBytes(psp + "/ULUS99999/OTHER.BIN", own);
        StartNotes n;
        n.asideDir = pspAside;
        restoreDirSave(f, "ppsspp-native", psp, "Lumines", std::string(), &n);
        std::vector<cab::ZipFile> kept;
        const bool read = cab::readZip(cab::readBytes(pspAside + "/Lumines.zip"), &kept, &zerr);
        bool keptOwn = false, keptOther = false;
        for (const cab::ZipFile& z : kept) {
            if (z.relPath == "ULUS10002/DATA.BIN" && z.bytes == own) keptOwn = true;
            if (z.relPath.rfind("ULUS99999", 0) == 0) keptOther = true;
        }
        check(cab::readBytes(psp + "/ULUS10002/DATA.BIN") == theirs && n.fromChannel &&
                  n.changedOwn && read && kept.size() == 2 && keptOwn && !keptOther &&
                  cab::readBytes(psp + "/ULUS99999/OTHER.BIN") == own,
              "a save folder with other bytes: the files the snapshot replaces are kept aside "
              "as a zip, and a folder it does not name is left");
        f.picked = bytesOf("not a zip", 32);
        StartNotes bad;
        cab::writeBytes(psp + "/ULUS10002/DATA.BIN", own);
        restoreDirSave(f, "ppsspp-native", psp, "Lumines", std::string(), &bad);
        check(cab::readBytes(psp + "/ULUS10002/DATA.BIN") == own && !bad.fromChannel,
              "a snapshot's save that is no zip is never unpacked over a save folder");
    }
    std::printf("\n%s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
