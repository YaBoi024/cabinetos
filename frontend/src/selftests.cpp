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
#include "server.h"
#include "storage.h"

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
        std::printf("  FAIL  %s\n        online=%d wired=%d wifiHw=%d wifiUp=%d "
                    "addr=%d checked=%d answered=%d token=%d pads=%d\n",
                    what, f.online, f.wiredOnline, f.wifiPresent, f.wifiConfigured,
                    f.haveServerAddress, f.serverChecked, f.serverAnswered,
                    f.havePairedToken, f.gamepadCount);
    };

    for (int bits = 0; bits < 512; ++bits) {
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
        // console with nothing to show, which is the thing 15b forbids.
        if (m.finished()) {
            if (!f.online)          fail("finished setup while offline", f);
            if (!f.serverAnswered)  fail("finished setup with no server answering", f);
            if (!f.havePairedToken) fail("finished setup with no token", f);
        }

        // A machine with a radio, no cable and no Wi-Fi must not get past the
        // network step at all.
        if (!f.online && m.step() != firstrun::Step::Network)
            fail("got past the network step while offline", f);

        // The one soft gate, and it has to STAY soft: refusing to finish
        // without a controller would break the keyboard guarantee the whole
        // flow exists to make.
        if (f.online && f.serverAnswered && f.havePairedToken && !m.finished())
            fail("a fully configured machine could not finish setup", f);
    }

    // WHICH SCHEME THE SERVER STEP TRIES FIRST (#479): http for an address
    // that is the person's own, Tailscale's 100.64.0.0/10 included, and https
    // for the rest. The wrong one first cost a try at every heartbeat.
    const struct { const char* host; bool local; } schemes[] = {
        {"100.101.1.2", true},   {"100.101.1.2:8080", true}, {"100.64.0.1", true},
        {"100.127.255.254", true}, {"100.128.0.1", false},   {"100.63.0.1", false},
        {"192.168.1.10:6005", true}, {"romm", true},         {"romm.example.com", false},
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
// of it is untouched (romfile::alreadyUnpacked, #530). A full disk is a
// file size limit here (RLIMIT_FSIZE, with SIGXFSZ ignored so the write
// fails rather than the process). A power cut cannot be made headless; the fsync is read, not run.
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

    storage::removeEntry(root);
    std::printf("\nfiles: %d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
