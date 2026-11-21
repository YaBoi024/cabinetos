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
#include "timewords.h"
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
                  n.tookPick && n.changedOwn && n.keptAside.empty() && !exists(aside),
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
        check(n.tookPick && !n.changedOwn && n.keptAside.empty() && !exists(aside) &&
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
        check(cab::readBytes(file) == own && !n.tookPick && sess.startChannelId.empty() &&
                  sess.couldNotUseNewest,
              "a snapshot whose save holds nothing never replaces a real one, and the session "
              "knows the newest was not used");
        f = fromChannel(25, {}, true);
        restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
        check(cab::readBytes(file) == own && !n.tookPick,
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
        check(cab::readBytes(file) == theirs && !n.tookPick && n.keptAside.size() == 1 &&
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
        check(cab::readBytes(file) == own && !n.tookPick && !noteStart(f, n, u, &sess) &&
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
                  cab::readBytes(dir + "/4Mbit_cart.brm") == own && n.tookPick &&
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
    {
        // SATURN'S CARTRIDGE BESIDE ITS BATTERY (#346, step 11): a zip's
        // cartridge to the cartridge, found by the end of its name; a
        // snapshot's one file is the battery, never the cartridge.
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
        const std::vector<catalog::SaveFile> specs =
            catalog::saveFiles("saturn", "beetle_saturn", "Rally (USA).chd", false);
        const std::string dir = storage::savesDir(u, "Saturn", 802, "beetle_saturn");
        cab::writeBytes(dir + "/Rally (USA).bcr", saturnCart('A'));
        std::vector<uint8_t> zip;
        std::string zerr;
        cab::zipFiles({{"Rally (Japan).srm", theirs}, {"Rally (Japan).bcr", saturnCart('B')}},
                      &zip, &zerr);
        SaveFetch f = fromChannel(33, zip, true);
        f.romId = 802;
        StartNotes n;
        n.asideDir = storage::replacedSaveDir(u, "Saturn", 802);
        restoreFileSaves(specs, "Rally (USA)", dir, "saturn-native", f, &n);
        check(specs.size() == 1 && specs[0].region == "cart" && specs[0].path == "Rally (USA).bcr" &&
                  cab::readBytes(dir + "/Rally (USA).bcr") == saturnCart('B') && n.tookPick &&
                  cab::readBytes(n.asideDir + "/Rally (USA).bcr") == saturnCart('A'),
              "Saturn: a zip's cartridge, named after another file, to the cartridge by its "
              "end; this console's kept aside");
        // The internal memory, with a save's mark where a cartridge's would be,
        // so only the rule keeps it off the cartridge.
        std::vector<uint8_t> internal(32768, 0x00);
        for (size_t i = 0; i < 64; ++i)
            internal[i] = static_cast<uint8_t>("BackUpRam Format"[i % 16]);
        internal[1024] = 0x80;
        f = fromChannel(34, internal, true);
        f.romId = 802;
        StartNotes single;
        restoreFileSaves(specs, "Rally (USA)", dir, "saturn-native", f, &single);
        check(cab::readBytes(dir + "/Rally (USA).bcr") == saturnCart('B') && !single.tookPick,
              "Saturn: a snapshot's one file is its battery, never written as the cartridge");
        cab::zipFiles({{"Rally (USA).srm", theirs}, {"Rally (USA).bcr", saturnCart(0)}}, &zip,
                      &zerr);
        f = fromChannel(35, zip, true);
        f.romId = 802;
        StartNotes blank;
        restoreFileSaves(specs, "Rally (USA)", dir, "saturn-native", f, &blank);
        check(cab::readBytes(dir + "/Rally (USA).bcr") == saturnCart('B'),
              "Saturn: a cartridge with no save never replaces one with a save");
        check(!cab::holdsASave(saturnCart(0), catalog::Untouched::SaturnCartridge) &&
                  cab::holdsASave(saturnCart('A'), catalog::Untouched::SaturnCartridge),
              "Saturn: a formatted cartridge holds no save; one with a save's mark does");
        std::vector<uint8_t> deleted = saturnCart('A');
        deleted[1024] = 0x00;
        std::vector<uint8_t> reserved = saturnCart(0);
        reserved[512] = 0x80;
        check(!cab::holdsASave(deleted, catalog::Untouched::SaturnCartridge) &&
                  !cab::holdsASave(reserved, catalog::Untouched::SaturnCartridge) &&
                  !cab::holdsASave(std::vector<uint8_t>(1024, 0x80),
                                   catalog::Untouched::SaturnCartridge),
              "Saturn: a save deleted on the Saturn, a mark in the two blocks the format keeps, "
              "and a file shorter than them hold none");
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
        check(same.tookPick && !same.changedOwn && same.keptAside.empty() && !exists(pspAside),
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
        check(cab::readBytes(psp + "/ULUS10002/DATA.BIN") == theirs && n.tookPick &&
                  n.changedOwn && read && kept.size() == 2 && keptOwn && !keptOther &&
                  cab::readBytes(psp + "/ULUS99999/OTHER.BIN") == own,
              "a save folder with other bytes: the files the snapshot replaces are kept aside "
              "as a zip, and a folder it does not name is left");
        f.picked = bytesOf("not a zip", 32);
        StartNotes bad;
        cab::writeBytes(psp + "/ULUS10002/DATA.BIN", own);
        restoreDirSave(f, "ppsspp-native", psp, "Lumines", std::string(), &bad);
        check(cab::readBytes(psp + "/ULUS10002/DATA.BIN") == own && !bad.tookPick,
              "a snapshot's save that is no zip is never unpacked over a save folder");
    }

    // 3. A start that took RomM's saves from before channels brings them into
    //    a channel (#346, step 7): the game's push, owed, aimed and queued.
    std::printf("\nsaves from before channels, brought in by the start\n");
    {
        auto oldRow = [&](int id, const char* name, const char* at) {
            romm::Asset a;
            a.id = id;
            a.romId = game;
            a.fileName = name;
            a.emulator = tag;
            a.updatedAt = at;
            return a;
        };
        auto oldFetch = [&](int romId, std::vector<romm::Asset> rows) {
            SaveFetch f;
            f.romId = romId;
            f.asked = f.listed = true;
            f.channelFileId = 4410;
            f.mark = savemirror::mark();
            f.pick.from = savepick::Pick::From::OldRows;
            f.rows = std::move(rows);
            return f;
        };
        auto sessionOf = [&](int romId, const char* sessTag, const std::string& dir,
                             const char* stem) {
            GameSession s;
            s.romId = romId;
            s.saveTag = sessTag;
            s.saveDir = dir;
            s.fsStem = stem;
            return s;
        };
        const char* at = "2026-11-10T09:30:00+00:00";
        const int64_t atSec = isoMicros(at) / 1000000;
        savemirror::forgetHeld(u, game);
        cab::writeBytes(file, own);
        cache::OwedPush p;
        {
            SaveFetch f = oldFetch(game, {oldRow(9, "kof98 (Cabinet fbneo).srm", at)});
            f.bytes[9] = theirs;
            StartNotes n = notesFor();
            restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
            GameSession sess = sessionOf(game, tag, saveDir, "kof98");
            bool brings = false;
            noteStart(f, n, u, &sess, &brings);
            check(brings && n.oldRowsTaken.size() == 1 && cache::owedPush(u, game, &p) &&
                      p.hasSave && p.fromOldRows() && p.oldRows.size() == 1 &&
                      p.oldRows[0].id == 9 && p.oldRows[0].at == atSec && p.madeAt == atSec &&
                      p.savePath.empty() && p.saveShape == "SINGLE" && p.emulator == tag &&
                      p.label == "default" && p.channelId.empty() && p.romFileId == 4410,
                  "a core's one save file from before channels: the game's push names its row "
                  "for RomM to copy, no bytes, into the person's \"default\", made when RomM "
                  "stored it");
            check(!cache::saveOwed(u, game),
                  "and it is no save this console owes: the next start still takes RomM's newest");
            cache::clearPushOwed(u, game, p.stamp);
        }
        {
            // The console holds a snapshot: onto it, as the session's own save
            // would go; and the states an earlier marker owes go with it.
            savemirror::Held held;
            held.channelId = "01937a3c-0000-7000-8000-00000000000a";
            held.snapshotId = 61;
            held.saveHash = "aaaa";
            held.at = 1;
            savemirror::putHeld(u, game, held);
            cache::OwedPush states;
            states.romId = game;
            states.emulator = tag;
            states.label = "default";
            states.states.push_back({tag, "kof98.state", saveDir + "/kof98.state", "", "",
                                     1763560512345});
            cache::markPushOwed(u, states, 64);
            SaveFetch f = oldFetch(game, {oldRow(9, "kof98 (Cabinet fbneo).srm", at)});
            f.bytes[9] = theirs;
            StartNotes n = notesFor();
            restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
            GameSession sess = sessionOf(game, tag, saveDir, "kof98");
            bool brings = false;
            noteStart(f, n, u, &sess, &brings);
            check(brings && cache::owedPush(u, game, &p) && p.fromOldRows() &&
                      p.channelId == held.channelId && p.expectedCurrentId == 61 &&
                      p.states.size() == 1 && p.states[0].fileName == "kof98.state",
                  "onto the snapshot this console holds, carrying the states owed before it");
            cache::clearPushOwed(u, game, p.stamp);
            savemirror::forgetHeld(u, game);
        }
        {
            // A save of this console's owed: it stands, and nothing is brought
            // in over it (a start that took RomM's has none; this one is made
            // up to see the guard).
            cache::OwedPush mine;
            mine.romId = game;
            mine.emulator = tag;
            mine.hasSave = true;
            mine.saveName = "kof98.fs";
            mine.savePath = file;
            mine.label = "default";
            const std::string stamp = cache::markPushOwed(u, mine, 64);
            SaveFetch f = oldFetch(game, {oldRow(9, "kof98 (Cabinet fbneo).srm", at)});
            f.bytes[9] = theirs;
            StartNotes n = notesFor();
            restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
            GameSession sess = sessionOf(game, tag, saveDir, "kof98");
            bool brings = true;
            noteStart(f, n, u, &sess, &brings);
            check(!brings && cache::owedPush(u, game, &p) && p.stamp == stamp &&
                      !p.fromOldRows() && cache::saveOwed(u, game),
                  "a save of this console's still owed is never replaced by one from RomM");
            cache::clearPushOwed(u, game, stamp);
        }
        {
            // RomM's from before channels holds no save: the console's stands
            // and nothing is brought in.
            cab::writeBytes(file, own);
            SaveFetch f = oldFetch(game, {oldRow(9, "kof98 (Cabinet fbneo).srm", at)});
            f.bytes[9] = blank;
            StartNotes n = notesFor();
            restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
            GameSession sess = sessionOf(game, tag, saveDir, "kof98");
            bool brings = true;
            noteStart(f, n, u, &sess, &brings);
            check(cab::readBytes(file) == own && n.oldRowsTaken.empty() && !brings &&
                      !cache::isPushOwed(u, game),
                  "a save from before channels that holds nothing is not put in place, nor "
                  "brought in");
            // The same blank card on this console: it is what is in place,
            // so it is brought in, and a state made later has a snapshot to
            // go onto.
            cab::writeBytes(file, blank);
            StartNotes same = notesFor();
            restoreFileSaves({spec}, "kof98", saveDir, tag, f, &same);
            GameSession again = sessionOf(game, tag, saveDir, "kof98");
            brings = false;
            noteStart(f, same, u, &again, &brings);
            check(cab::readBytes(file) == blank && same.oldRowsTaken.size() == 1 &&
                      same.keptAside.empty() && brings && cache::owedPush(u, game, &p) &&
                      p.fromOldRows(),
                  "one that is byte for byte this console's own blank card is what is in "
                  "place, and is brought in");
            cache::clearPushOwed(u, game, p.stamp);
            cab::writeBytes(file, own);
        }
        {
            // The newest on RomM is another emulator's: the console's own
            // from before channels is put in place, not brought in beside it.
            SaveFetch f = oldFetch(game, {oldRow(9, "kof98 (Cabinet fbneo).srm", at)});
            f.bytes[9] = theirs;
            f.pick.couldNotUseNewest = true;
            StartNotes n = notesFor();
            restoreFileSaves({spec}, "kof98", saveDir, tag, f, &n);
            GameSession sess = sessionOf(game, tag, saveDir, "kof98");
            bool brings = true;
            noteStart(f, n, u, &sess, &brings);
            check(cab::readBytes(file) == theirs && !brings && !cache::isPushOwed(u, game) &&
                      sess.couldNotUseNewest,
                  "another emulator's save the newest: the old save is put in place and not "
                  "brought in");
        }
        {
            // Sega CD's two files, each a row of its own: one zip of both.
            catalog::SaveFile internal, cart;
            internal.path = "Lunar.brm";
            internal.untouched = catalog::Untouched::Blank;
            cart.path = "4Mbit_cart.brm";
            cart.region = "cart";
            cart.untouched = catalog::Untouched::Blank;
            const int lunar = 802;
            const std::string dir = storage::savesDir(u, "Sega CD", lunar, "genesis_plus_gx");
            romm::Asset internalRow = oldRow(11, "Lunar (Cabinet).srm", at);
            romm::Asset cartRow = oldRow(12, "Lunar (Cabinet).cart", "2026-11-10T09:31:00+00:00");
            internalRow.romId = cartRow.romId = lunar;
            internalRow.emulator = cartRow.emulator = "gpgx-native";
            SaveFetch f = oldFetch(lunar, {internalRow, cartRow});
            f.bytes[11] = theirs;
            f.bytes[12] = own;
            StartNotes n;
            n.asideDir = storage::replacedSaveDir(u, "Sega CD", lunar);
            restoreFileSaves({internal, cart}, "Lunar", dir, "gpgx-native", f, &n);
            GameSession sess = sessionOf(lunar, "gpgx-native", dir, "Lunar");
            bool brings = false;
            noteStart(f, n, u, &sess, &brings);
            std::vector<cab::ZipFile> zipped;
            std::string zerr;
            const bool zipOk = cache::owedPush(u, lunar, &p) &&
                               cab::readZip(cab::readBytes(p.savePath), &zipped, &zerr);
            bool both = zipped.size() == 2;
            for (const cab::ZipFile& z : zipped)
                both = both && ((z.relPath == "Lunar.brm" && z.bytes == theirs) ||
                                (z.relPath == "4Mbit_cart.brm" && z.bytes == own));
            check(brings && zipOk && both && p.fromOldRows() && p.oldRows.size() == 2 &&
                      p.saveShape == "MULTI" && p.saveName == "Lunar.zip" &&
                      p.savePath == dir + "/" + saveRowName("Lunar", "zip") &&
                      p.madeAt == isoMicros("2026-11-10T09:31:00+00:00") / 1000000,
                  "Sega CD's two files from before channels: one zip of both, each under the "
                  "path its core reads, made when the newer row was stored");
            cache::clearPushOwed(u, lunar, p.stamp);
        }
        {
            // A save folder's zip from before channels: copied in as a folder.
            const std::string psp = storage::savesDir(u, "PSP", 901, "ppsspp") + "/PSP/SAVEDATA";
            cab::writeBytes(psp + "/ULUS10002/DATA.BIN", own);
            std::vector<uint8_t> zip;
            std::string zerr;
            cab::zipTree(psp, {"ULUS10002/DATA.BIN"}, &zip, &zerr);
            cab::writeBytes(psp + "/ULUS10002/DATA.BIN", theirs);
            romm::Asset row = oldRow(13, "Lumines.zip", at);
            row.romId = 901;
            row.emulator = "ppsspp-native";
            SaveFetch f = oldFetch(901, {row});
            f.bytes[13] = zip;
            StartNotes n;
            restoreDirSave(f, "ppsspp-native", psp, "Lumines", std::string(), &n);
            GameSession sess = sessionOf(901, "ppsspp-native", "", "");
            bool brings = false;
            noteStart(f, n, u, &sess, &brings);
            check(cab::readBytes(psp + "/ULUS10002/DATA.BIN") == own && brings &&
                      cache::owedPush(u, 901, &p) && p.oldRows.size() == 1 &&
                      p.oldRows[0].id == 13 && p.saveShape == "FOLDER" && p.savePath.empty(),
                  "a save folder's zip from before channels: unpacked, and its row copied in "
                  "as a folder");
            cache::clearPushOwed(u, 901, p.stamp);
            // GameCube's card from before #344 is no zip: Dolphin reads it in,
            // and nothing is brought in at the start.
            romm::Asset card = oldRow(14, "card.USA.raw", at);
            card.romId = 902;
            card.emulator = "dolphin-native";
            const std::string gc = storage::savesDir(u, "GameCube", 902, "dolphin");
            SaveFetch g = oldFetch(902, {card});
            g.bytes[14] = bytesOf("not a zip, a raw card", 64);
            StartNotes cn;
            restoreDirSave(g, "dolphin-native", gc + "/GC/USA/Card A", "Ikaruga",
                           gc + "/card.raw", &cn);
            GameSession gsess = sessionOf(902, "dolphin-native", "", "");
            bool gBrings = true;
            noteStart(g, cn, u, &gsess, &gBrings);
            check(cn.oldRowsTaken.empty() && !gBrings && !cache::isPushOwed(u, 902),
                  "a card from before #344, which is no zip: not brought in at the start");
        }
    }

    // ANOTHER EMULATOR'S SAVE IN THIS CONSOLE'S FORM, AND ITS CHECK, PER
    // SYSTEM (#346, step 8; saves.h, toOwnForm): RomM's answer put the way a
    // start reads a snapshot's save, each system's check refusing a wrong
    // size, a wrong mark and an empty file. The table's own entries
    // (catalog::saveForm, saveFiles, batteryCheck) are what is checked.
    std::printf("\nanother emulator's save in this console's form, checked per system\n");
    {
        auto answer = [](const std::string& path, std::vector<uint8_t> b) {
            romm::SaveAnswer a;
            a.status = 200;
            a.bytes = std::move(b);
            a.savePath = path;
            return a;
        };
        auto zipped = [](cab::ZipFile one, cab::ZipFile two = cab::ZipFile{}) {
            romm::SaveAnswer a;
            a.status = 200;
            std::vector<cab::ZipFile> files = {std::move(one)};
            if (!two.relPath.empty()) files.push_back(std::move(two));
            std::string err;
            cab::zipFiles(files, &a.bytes, &err);
            return a;
        };
        auto form = [](const char* core, const char* platform, const char* rom, bool folder) {
            OwnForm o;
            o.form = catalog::saveForm(core, platform);
            o.tag = catalog::saveTag(core) ? catalog::saveTag(core) : "";
            o.platform = platform;
            o.folder = folder;
            if (!folder) o.files = catalog::saveFiles(platform, core, rom, false);
            return o;
        };
        auto takes = [](romm::SaveAnswer a, bool plain, const OwnForm& o,
                        std::vector<uint8_t>* out = nullptr, const char* madeBy = "",
                        const char* madeByCore = "") {
            std::vector<uint8_t> b;
            std::string why;
            const bool ok = toOwnForm(a, plain, o, madeBy, madeByCore, &b, &why);
            if (!ok) std::printf("        (%s)\n", why.c_str());
            if (out) *out = b;
            return ok;
        };
        auto filled = [](size_t n, const std::string& head, size_t at = 0) {
            std::vector<uint8_t> b(n, 0x00);
            for (size_t i = 0; i < head.size() && at + i < n; ++i) b[at + i] = head[i];
            if (n > 300) b[n / 2] = 0x5a;   // something a game wrote
            return b;
        };
        auto marked = [&](size_t n, const std::string& head) { return filled(n, head); };
        auto unmarked = [&](size_t n) { return filled(n, std::string("\x5a\x5a", 2)); };

        // The names a push sends and a request asks with, in Sigil's spelling.
        check(catalog::saveForm("mupen64plus", "n64").sigil == "mupen64plus_next" &&
                  catalog::saveForm("beetle_saturn", "saturn").sigil == "mednafen_saturn" &&
                  catalog::saveForm("beetle_ngp", "neo-geo-pocket-color").sigil == "mednafen_ngp" &&
                  catalog::saveForm("fbneo_libretro", "arcade").sigil == "fbneo" &&
                  catalog::saveForm("gambatte", "gb").sigil == "gambatte" &&
                  catalog::saveForm("xenia", "xbox360").sigil == "xenia_edge",
              "the core's name in Sigil's spelling, the four that differ from the console's");
        check(catalog::saveForm("melonds", "nds").sameCoreOnly &&
                  catalog::saveForm("mgba", "gba").sameCoreOnly &&
                  catalog::saveForm("picodrive", "sega32").sameCoreOnly &&
                  catalog::saveForm("genesis_plus_gx", "genesis").sameCoreOnly &&
                  !catalog::saveForm("genesis_plus_gx", "segacd").sameCoreOnly &&
                  !catalog::saveForm("snes9x", "snes").sameCoreOnly,
              "\"Sigil names a difference\": DS, GBA and the Mega Drive family, not Sega CD");

        // Batteries.
        const OwnForm gb = form("gambatte", "gb", "Pokemon Red.gb", false);
        std::vector<uint8_t> out;
        check(takes(answer("Pokemon Red.srm", unmarked(32768)), false, gb, &out) &&
                  out == unmarked(32768),
              "Game Boy: the converted battery as it is");
        {
            std::vector<cab::ZipFile> in;
            std::string zerr;
            check(takes(zipped({"Pokemon Gold.rtc", unmarked(8)},
                               {"Pokemon Gold.srm", unmarked(32768)}),
                        false, gb, &out) &&
                      cab::readZip(out, &in, &zerr) && in.size() == 2 &&
                      in[0].relPath == "Pokemon Gold.srm" && in[0].bytes == unmarked(32768) &&
                      in[1].relPath == "Pokemon Gold.rtc" && in[1].bytes == unmarked(8),
                  "Game Boy: a cartridge with a clock, RomM's battery and clock both kept, a "
                  "zip of the two under RomM's names (#346, step 11)");
        }
        check(takes(zipped({"M.rtc", unmarked(16)}, {"M.srm", unmarked(8192)}), false,
                    form("snes9x", "snes", "M.sfc", false), &out) &&
                  out == unmarked(8192),
              "Super Nintendo: a clock in RomM's zip left out; Snes9x's stays on the console");
        check(!takes(answer("Pokemon Red.srm", {}), false, gb), "Game Boy: an empty file refused");
        check(!takes(answer("Pokemon Red.srm", std::vector<uint8_t>(32768, 0xFF)), false, gb),
              "Game Boy: one that holds no save refused");
        const OwnForm psx = form("pcsx_rearmed", "psx", "Crash.chd", false);
        check(takes(answer("Crash.srm", marked(131072, "MC")), false, psx),
              "PlayStation: a 128 KB card marked MC");
        check(!takes(answer("Crash.srm", marked(65536, "MC")), false, psx) &&
                  !takes(answer("Crash.srm", unmarked(131072)), false, psx) &&
                  !takes(answer("Crash.srm", {}), false, psx),
              "PlayStation: wrong size, no mark, empty: refused");
        const OwnForm sat = form("beetle_saturn", "saturn", "Rally.chd", false);
        check(takes(answer("Rally.srm", marked(32768, "BackUpRam Format")), false, sat) &&
                  !takes(answer("Rally.srm", marked(65536, "BackUpRam Format")), false, sat) &&
                  !takes(answer("Rally.srm", unmarked(32768)), false, sat) &&
                  !takes(answer("Rally.srm", {}), false, sat),
              "Saturn: 32 KB marked BackUpRam Format; wrong size, no mark, empty refused");
        const OwnForm n64 = form("mupen64plus", "n64", "Wave Race.v64", false);
        check(takes(answer("Wave Race.srm", unmarked(296960)), false, n64) &&
                  !takes(answer("Wave Race.srm", unmarked(32768)), false, n64) &&
                  !takes(answer("Wave Race.srm", {}), false, n64),
              "N64: 296960 bytes; wrong size, empty refused");
        check(!takes(zipped({"a.srm", unmarked(512)}), true, form("snes9x", "snes", "M.sfc", false)),
              "a battery handed over as stored that is a zip: refused, the console converts "
              "nothing");

        // The files a core writes.
        std::vector<uint8_t> vmu(131072, 0x00);
        for (int i = 0; i < 16; ++i) vmu[255 * 512 + i] = 0x55;
        vmu[253 * 512] = 0x33;
        const OwnForm dc = form("flycast", "dc", "Ikaruga (Japan).chd", false);
        check(dc.form.options.size() == 1 && dc.form.options[0].second == "VMU A1",
              "Dreamcast is asked for one card per game");
        check(takes(answer("T38706M.A1.bin", vmu), false, dc, &out) && out == vmu,
              "Dreamcast: RomM's card under the disc's number is the game's card");
        std::vector<uint8_t> noRoot = vmu;
        noRoot[255 * 512] = 0;
        std::vector<uint8_t> noFile = vmu;
        noFile[253 * 512] = 0;
        check(!takes(answer("T38706M.A1.bin", std::vector<uint8_t>(vmu.begin(), vmu.begin() + 65536)),
                     false, dc) &&
                  !takes(answer("T38706M.A1.bin", noRoot), false, dc) &&
                  !takes(answer("T38706M.A1.bin", noFile), false, dc) &&
                  !takes(answer("T38706M.A1.bin", {}), false, dc),
              "Dreamcast: wrong size, no root mark, no file on it, empty: refused");
        const std::string format = std::string("SEGA_CD_ROM\0\1\0\0\0RAM_CARTRIDGE___", 32);
        auto brm = [&](size_t n) {
            std::vector<uint8_t> b = unmarked(n);
            std::copy(format.begin(), format.end(), b.end() - 32);
            return b;
        };
        const OwnForm scd = form("genesis_plus_gx", "segacd", "Lunar.chd", false);
        check(takes(zipped({"Lunar.brm", brm(8192)}, {"Lunar_4Mbit_cart.brm", brm(524288)}),
                    false, scd, &out),
              "Sega CD: the backup memory and the cartridge out of RomM's zip");
        std::vector<cab::ZipFile> files;
        std::string zerr;
        check(cab::readZip(out, &files, &zerr) && files.size() == 2 &&
                  files[0].relPath == "Lunar.brm" && files[1].relPath == "4Mbit_cart.brm",
              "each under the name this console's core reads it at");
        check(takes(answer("Lunar.brm", brm(8192)), false, scd, &out) && out == brm(8192),
              "Sega CD: the backup memory alone, as it is");
        check(takes(answer("Lunar_4Mbit_cart.brm", brm(524288)), false, scd, &out) &&
                  cab::readZip(out, &files, &zerr) && files.size() == 1 &&
                  files[0].relPath == "4Mbit_cart.brm",
              "Sega CD: the cartridge alone, a zip of one under its own name, never read as the "
              "backup memory");
        check(scd.form.options.size() == 2 &&
                  form("pcsx2", "ps2", "B.chd", false).form.options.size() == 1 &&
                  form("ppsspp", "psp", "", true).form.options.empty(),
              "the options a request names: Sega CD's memory and cartridge per game, PS2's card "
              "per game, none for the rest");
        check(!takes(answer("Lunar.brm", brm(16384)), false, scd) &&
                  !takes(answer("Lunar.brm", unmarked(8192)), false, scd) &&
                  !takes(answer("Lunar.brm", {}), false, scd),
              "Sega CD: wrong size, no format block, empty: refused");
        // Saturn's cartridge beside its battery (#346, step 11).
        auto cartOf = [&](size_t n, bool save) {
            std::vector<uint8_t> b(n, 0x00);
            const char* format = "BackUpRam Format";
            for (size_t i = 0; i < 512 && i < n; ++i) b[i] = static_cast<uint8_t>(format[i % 16]);
            if (save && n > 1024) b[1024] = 0x80;
            return b;
        };
        const std::vector<uint8_t> internal = marked(32768, "BackUpRam Format");
        {
            std::vector<cab::ZipFile> in;
            std::string zerr;
            check(sat.form.batteryFirst && sat.files.size() == 1 &&
                      takes(zipped({"Rally (Japan).srm", internal},
                                   {"Rally (Japan).bcr", cartOf(524288, true)}),
                            false, sat, &out) &&
                      cab::readZip(out, &in, &zerr) && in.size() == 2 &&
                      in[0].relPath == "Rally (Japan).srm" && in[0].bytes == internal &&
                      in[1].relPath == "Rally.bcr" && in[1].bytes == cartOf(524288, true),
                  "Saturn: RomM's internal memory and a cartridge holding a save, a zip of both, "
                  "the cartridge under the name this core reads, found by its end");
            check(takes(answer("Rally (Japan).bcr", cartOf(524288, true)), false, sat, &out) &&
                      cab::readZip(out, &in, &zerr) && in.size() == 1 &&
                      in[0].relPath == "Rally.bcr",
                  "Saturn: a cartridge alone, a zip of it alone, never read as the battery");
        }
        check(takes(zipped({"Rally.srm", internal}, {"Rally.bcr", cartOf(524288, false)}), false,
                    sat, &out) &&
                  out == internal,
              "Saturn: a formatted, empty cartridge left out; the battery alone, as it is");
        check(!takes(zipped({"Rally.srm", internal}, {"Rally.bcr", cartOf(262144, true)}), false,
                     sat) &&
                  !takes(zipped({"Rally.srm", internal}, {"Rally.bcr", unmarked(524288)}), false,
                         sat) &&
                  !takes(zipped({"Rally.srm", unmarked(32768)}, {"Rally.bcr", cartOf(524288, true)}),
                         false, sat),
              "Saturn: a cartridge of another size or with no format's name, or a battery this "
              "core does not read: refused");
        check(takes(answer("", internal), true, sat, &out) && out == internal,
              "Saturn handed over as stored: the one file is the battery");
        const OwnForm ps2 = form("pcsx2", "ps2", "Burnout 3.chd", false);
        const std::string ps2Mark = "Sony PS2 Memory Card Format ";
        check(takes(answer("Burnout 3.ps2", marked(8650752, ps2Mark)), false, ps2) &&
                  !takes(answer("Burnout 3.ps2", marked(8388608, ps2Mark)), false, ps2) &&
                  !takes(answer("Burnout 3.ps2", unmarked(8650752)), false, ps2) &&
                  !takes(answer("Burnout 3.ps2", {}), false, ps2),
              "PlayStation 2: an 8 MB card with its ECC, formatted; wrong size, no mark, "
              "empty refused");
        const OwnForm ngp = form("beetle_ngp", "neo-geo-pocket-color", "Metal Slug.ngc", false);
        check(takes(answer("", marked(272, std::string("\x53\x00", 2))), true, ngp, &out) &&
                  !takes(answer("", unmarked(272)), true, ngp) &&
                  !takes(answer("", {}), true, ngp),
              "Neo Geo Pocket, handed over as stored: the flash file's id; no id, empty refused");
        const OwnForm nds = form("melonds", "nds", "Contra 4.nds", false);
        check(takes(answer("", unmarked(512)), true, nds) &&
                  !takes(answer("", unmarked(512 + 122)), true, nds) &&
                  !takes(answer("", {}), true, nds),
              "DS: a cartridge's size; DeSmuME's 122 more bytes, empty refused");
        // Handed over as stored (no path in the emulator's folder, or asked
        // plainly): where Sigil names a difference, and for a folder, only
        // what this core made, by RomM's `may_load`.
        check(!takes(answer("", unmarked(512)), false, nds, nullptr, "desmume") &&
                  takes(answer("", unmarked(512)), false, nds, nullptr, "melonDS") &&
                  takes(answer("", unmarked(512)), false, nds, nullptr, "melonds-native") &&
                  !takes(answer("", unmarked(512)), false, nds, nullptr, "melonds", "desmume"),
              "DS handed over as stored: used only when melonDS made it, the core first");
        check(takes(answer("", unmarked(8192)), false, form("fceumm", "nes", "K.nes", false),
                    nullptr, "nestopia"),
              "NES handed over as stored: another emulator's used, Sigil names no difference");
        check(takes(answer("Contra 4.sav", unmarked(512)), false, nds, nullptr, "desmume"),
              "DS converted by RomM (a path in the emulator's folder): used, whoever made it");
        // Arcade: no other app's save, whichever emulator it names; the same
        // answer is taken by a game that is not one, so the refusal is the rule's.
        OwnForm board = form("fbneo_libretro", "arcade", "btoads.zip", false);
        board.files = catalog::saveFiles("arcade", "fbneo_libretro", "btoads.zip", true);
        const std::vector<uint8_t> nvram = unmarked(8192);
        check(takes(answer("", nvram), true, board, nullptr, "fbneo"),
              "an arcade board's file taken when the game is not marked arcade");
        board.arcade = true;
        check(!takes(answer("", nvram), true, board, nullptr, "mame2003_plus") &&
                  !takes(answer("", nvram), true, board, nullptr, "fbneo") &&
                  !takes(answer("", nvram), true, board, nullptr, "fbneo", "fbneo") &&
                  !takes(answer("", nvram), true, board),
              "arcade: another app's save refused, even one naming FinalBurn Neo");
        const OwnForm opera = form("opera", "3do", "Gex.chd", false);
        const std::string label("\x01\x5a\x5a\x5a\x5a\x5a", 6);
        check(takes(answer("", marked(32768, label)), true, opera) &&
                  !takes(answer("", marked(16384, label)), true, opera) &&
                  !takes(answer("", unmarked(32768)), true, opera) &&
                  !takes(answer("", {}), true, opera),
              "3DO: 32 KB with Opera's volume label; wrong size, no label, empty refused");

        // Folders: RomM's paths under the emulator's own save folder.
        auto sfo = [&](size_t n) { return marked(n, std::string("\0PSF", 4)); };
        const OwnForm psp = form("ppsspp", "psp", "", true);
        check(takes(zipped({"PSP/SAVEDATA/ULUS10002LUMINES/PARAM.SFO", sfo(4912)},
                            {"PSP/SAVEDATA/ULUS10002LUMINES/DATA.BIN", unmarked(64)}),
                    false, psp, &out) &&
                  cab::readZip(out, &files, &zerr) && files.size() == 2 &&
                  files[0].relPath == "ULUS10002LUMINES/PARAM.SFO",
              "PSP: the save folders under PSP/SAVEDATA, as this console's zip has them");
        check(!takes(zipped({"PSP/SAVEDATA/ULUS10002LUMINES/PARAM.SFO", unmarked(4912)}),
                     false, psp) &&
                  !takes(zipped({"elsewhere/PARAM.SFO", sfo(4912)}), false, psp) &&
                  !takes(answer("", {}), false, psp),
              "PSP: a PARAM.SFO without its mark, a file outside PSP/SAVEDATA, empty: refused");
        const OwnForm gc = form("dolphin", "ngc", "", true);
        std::vector<uint8_t> gci = unmarked(64 + 8192 * 4);
        gci[0] = 'G'; gci[1] = 'I'; gci[2] = 'K'; gci[3] = 'E';
        check(takes(answer("User/GC/USA/Card A/70-GIKE-ikaruga_save_data.gci", gci), false, gc,
                    &out) &&
                  cab::readZip(out, &files, &zerr) && files.size() == 1 &&
                  files[0].relPath == "70-GIKE-ikaruga_save_data.gci",
              "GameCube: RomM's one .gci, as a zip of the folder's files");
        std::vector<uint8_t> noCode = gci;
        noCode[0] = 0xFF;
        check(!takes(answer("User/GC/USA/Card A/x.gci", std::vector<uint8_t>(gci.begin(), gci.end() - 100)),
                     false, gc) &&
                  !takes(answer("User/GC/USA/Card A/x.gci", noCode), false, gc) &&
                  !takes(answer("User/GC/USA/Card A/x.gci", {}), false, gc),
              "GameCube: no whole blocks, no game's code, empty: refused");
        const OwnForm wiiForm = form("dolphin", "wii", "", true);
        check(takes(zipped({"User/Wii/title/00010001/57545245/data/savefile.dat", unmarked(512)}),
                    false, wiiForm) &&
                  !takes(zipped({"User/Wii/title/00010001/57545245/content/x.app", unmarked(512)}),
                         false, wiiForm) &&
                  !takes(answer("", {}), false, wiiForm),
              "Wii: a title's data folder; anything else, empty: refused");
        const std::string sd(32, '0');
        const OwnForm n3ds = form("azahar", "3ds", "", true);
        check(takes(zipped({"sdmc/Nintendo 3DS/" + sd + "/" + sd +
                                 "/title/00040000/00049000/data/00000001/n640.sav",
                             unmarked(428)}),
                    false, n3ds) &&
                  !takes(zipped({"sdmc/Nintendo 3DS/" + sd + "/n640.sav", unmarked(428)}), false,
                         n3ds) &&
                  !takes(answer("", {}), false, n3ds),
              "3DS: a title's save on the SD card; anything else, empty: refused");
        const OwnForm sw = form("eden", "switch", "", true);
        check(sw.form.profile.size() == 32 &&
                  takes(zipped({"nand/user/save/0000000000000000/" + sw.form.profile +
                                     "/0100AC10085CE000/save.dat",
                                 unmarked(64)}),
                        false, sw, &out) &&
                  cab::readZip(out, &files, &zerr) &&
                  files[0].relPath == "0100AC10085CE000/save.dat" &&
                  !takes(zipped({"nand/user/save/0000000000000000/" + sw.form.profile +
                                      "/save.dat",
                                  unmarked(64)}),
                         false, sw) &&
                  !takes(answer("", {}), false, sw),
              "Switch: the title's folder under the console's one profile; anything else, "
              "empty: refused");
        check(!takes(zipped({"0100AC10085CE000/save.dat", unmarked(64)}), true, sw, nullptr,
                     "Ryujinx") &&
                  takes(zipped({"0100AC10085CE000/save.dat", unmarked(64)}), true, sw, nullptr,
                        "Eden"),
              "a folder handed over as stored: only one this emulator made, as it is");
        const OwnForm wiiu = form("cemu", "wiiu", "", true);
        check(takes(zipped({"mlc01/usr/save/00050000/1016e100/user/80000001/saveData.bin",
                             unmarked(64)},
                            {"mlc01/usr/save/00050000/1016e100/meta/meta.xml", unmarked(64)}),
                    false, wiiu) &&
                  !takes(zipped({"mlc01/usr/save/00050000/1016e100/x.bin", unmarked(64)}),
                         false, wiiu) &&
                  !takes(answer("", {}), false, wiiu),
              "Wii U: a title's user and meta folders; anything else, empty: refused");
        const OwnForm ps3 = form("rpcs3", "ps3", "", true);
        check(takes(zipped({"dev_hdd0/home/00000001/savedata/NPUB30046-SAVE/PARAM.SFO", sfo(64)}),
                    false, ps3) &&
                  !takes(zipped({"dev_hdd0/home/00000001/savedata/NPUB30046-SAVE/PARAM.SFO",
                                  unmarked(64)}),
                         false, ps3) &&
                  !takes(answer("", {}), false, ps3),
              "PS3: save folders with PARAM.SFO; one without its mark, empty: refused");
        const OwnForm vita = form("vita3k", "psvita", "", true);
        check(takes(zipped({"ux0/user/00/savedata/PCSE00640/sdslot.dat", unmarked(64)}), false,
                    vita) &&
                  !takes(zipped({"ux0/user/00/sdslot.dat", unmarked(64)}), false, vita) &&
                  !takes(answer("", {}), false, vita),
              "PS Vita: the game's save folders; anything else, empty: refused");
    }

    // ANOTHER APP'S SAVE IN NO CHANNEL, PUT IN PLACE (#346, step 8): the
    // pill, no snapshot held, its row left as it is; and a start whose
    // newest could not be used saying so.
    std::printf("\nanother app's save at a start\n");
    {
        SaveFetch f;
        f.romId = 990;
        f.asked = f.listed = true;
        f.mark = savemirror::mark();
        f.pick.from = savepick::Pick::From::OtherRow;
        f.pick.other = true;
        f.pick.madeElsewhere = true;
        f.pick.row.id = 77;
        f.pick.row.fileName = "Game.srm";
        f.pick.row.emulator = "sameboy";
        f.picked = theirs;
        f.converted = true;
        catalog::SaveFile spec;
        spec.path = "kof98.fs";
        const std::string dir = storage::savesDir(u, "FBNEO", 990, "fbneo");
        StartNotes n;
        restoreFileSaves({spec}, "kof98", dir, "fbneo-native", f, &n);
        GameSession sess;
        sess.romId = 990;
        sess.saveTag = "fbneo-native";
        const bool pill = noteStart(f, n, u, &sess);
        savemirror::Held held;
        check(cab::readBytes(dir + "/kof98.fs") == theirs && n.tookPick && pill &&
                  !sess.couldNotUseNewest && sess.startSnapshotId == 0 &&
                  !savemirror::getHeld(u, 990, &held) && !cache::isPushOwed(u, 990),
              "another app's save in no channel: in place, with the pill; nothing held, its "
              "row left as it is");
        SaveFetch g;
        g.romId = 990;
        g.asked = g.listed = true;
        g.mark = savemirror::mark();
        g.pick.couldNotUseNewest = true;
        GameSession again;
        again.romId = 990;
        check(!noteStart(g, StartNotes{}, u, &again) && again.couldNotUseNewest,
              "one that could not be used: the console's own stands, and the session says so "
              "(\"Couldn't use the newest save\")");
    }

    // A BATTERY'S SAVE AND ITS CLOCK AS A START READS THEM (#346, step 11;
    // saves.h, batteryParts and clockToSet), with no core.
    std::printf("\na battery's save, its clock and its cartridge\n");
    {
        BatteryParts parts;
        std::string why;
        const std::vector<uint8_t> clock = bytesOf("clock 8", 8);
        check(batteryParts(own, &parts, &why) && !parts.zipped && parts.battery == own &&
                  parts.clock.empty(),
              "a battery alone, as it is, with no clock");
        std::vector<uint8_t> zip;
        cab::zipFiles({{"Game.srm", own}, {"Game.rtc", clock}, {"Game.bcr", theirs}}, &zip, &why);
        check(batteryParts(zip, &parts, &why) && parts.zipped && parts.battery == own &&
                  parts.clock == clock,
              "a zip: the battery by `.srm`, the clock by `.rtc`, the cartridge left to the "
              "file restores");
        cab::zipFiles({{"Game.bcr", theirs}}, &zip, &why);
        check(batteryParts(zip, &parts, &why) && parts.zipped && parts.battery.empty(),
              "a zip with no `.srm` holds no battery");
        const std::vector<uint8_t> mine = bytesOf("this console's clock", 8);
        const std::vector<uint8_t> none;
        check(clockToSet(clock, mine, 8, true) == &clock,
              "RomM's battery put in place, not what this console had: its clock with it");
        check(clockToSet(clock, mine, 8, false) == &mine,
              "RomM's battery is what this console had: this console's own clock, which a "
              "change to the clock alone left");
        check(clockToSet(clock, none, 8, false) == &clock,
              "this console keeps no clock: RomM's");
        check(clockToSet(none, mine, 8, true) == &mine,
              "a save that carries no clock leaves this console's as it is");
        check(clockToSet(bytesOf("x", 16), mine, 8, true) == &mine &&
                  clockToSet(bytesOf("x", 16), bytesOf("y", 4), 8, true) == nullptr &&
                  clockToSet(clock, mine, 0, true) == nullptr,
              "a clock of another size is none this core keeps; a core with no clock sets none");
        const catalog::SaveForm gbForm = catalog::saveForm("gambatte", "gbc");
        const catalog::SaveForm satForm = catalog::saveForm("beetle_saturn", "saturn");
        check(gbForm.clock && !gbForm.batteryFirst && satForm.batteryFirst && !satForm.clock &&
                  !catalog::saveForm("snes9x", "snes").clock &&
                  !catalog::saveForm("mgba", "gba").clock &&
                  catalog::saveFiles("gb", "gambatte", "Pokemon Gold.gbc", false).empty(),
              "the clock travels for Gambatte only, Saturn's battery comes first; no other "
              "system's changes");
    }

    // RomM'S SHAPE FOR WII AND 3DS ZIPS (dirsave.h, ZipShape): Sigil's, and
    // the folder's own read back from either.
    std::printf("\nSigil's shape for Wii and 3DS saves\n");
    {
        using cab::ZipShape;
        const std::vector<cab::ZipFile> wiiTree = {{"00010001/57545245/data/banner.bin", {1}},
                                                    {"00010001/57545245/data/savefile.dat", {2}}};
        const std::vector<cab::ZipFile> wiiUnit = cab::toZipShape(ZipShape::Wii, wiiTree);
        check(wiiUnit.size() == 2 && wiiUnit[0].relPath == "57545245/data/banner.bin",
              "Wii: a push holds <title>/data/..., without the kind folder above it");
        check(cab::fromZipShape(ZipShape::Wii, wiiKindOf("0001000157545245"), wiiUnit)[1].relPath ==
                      "00010001/57545245/data/savefile.dat" &&
                  wiiKindOf("52474C45") == "00010000",
              "and a start puts it back under the title's kind: WiiWare's, a disc's");
        check(cab::fromZipShape(ZipShape::Wii, "00010000", wiiTree)[0].relPath ==
                  wiiTree[0].relPath,
              "a zip in the folder's own shape, from before, is read as it is");
        const std::string sd(32, '0');
        const std::vector<cab::ZipFile> tree = {
            {sd + "/" + sd + "/title/00040000/00049000/data/00000001.metadata", {1}},
            {sd + "/" + sd + "/title/00040000/00049000/data/00000001/n640.sav", {2}},
            {sd + "/" + sd + "/extdata/00000000/00000490/user/x.dat", {3}}};
        const std::vector<cab::ZipFile> unit = cab::toZipShape(ZipShape::ThreeDS, tree);
        check(unit.size() == 3 && unit[0].relPath == "00040000/00049000/data/00000001.metadata" &&
                  unit[2].relPath == "00040000/00049000/extdata/user/x.dat",
              "3DS: a push holds <high>/<low>/data/... and the title's extra data");
        const std::vector<cab::ZipFile> back = cab::fromZipShape(ZipShape::ThreeDS, "", unit);
        check(back.size() == 3 && back[1].relPath == tree[1].relPath &&
                  back[2].relPath == tree[2].relPath,
              "and a start puts both back on the SD card");
        std::vector<cab::ZipFile> stray = tree;
        stray.push_back({sd + "/" + sd + "/extdata/00048000/f000000b/x", {4}});
        check(cab::toZipShape(ZipShape::ThreeDS, stray)[0].relPath == stray[0].relPath,
              "a 3DS save with a file Sigil's shape has no place for travels as the folder is, "
              "nothing left out");
        check(cab::fromZipShape(ZipShape::ThreeDS, "", tree)[0].relPath == tree[0].relPath,
              "a 3DS zip from before is read as it is");
        check(zipShapeOf("dolphin", "wii", "52474C45") == ZipShape::Wii &&
                  zipShapeOf("azahar", "3ds", "") == ZipShape::ThreeDS &&
                  zipShapeOf("ppsspp", "psp", "") == ZipShape::Folder,
              "which systems' zips take Sigil's shape: Wii and 3DS");
    }
    std::printf("\n%s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
