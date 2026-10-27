#include "powerprofile.h"

#include <systemd/sd-bus.h>

#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

namespace powerprofile {
namespace {

// FORK (handheld): Home and light games on the battery-biased profile, heavy
// games on the full-speed one. Each tier has a fallback for a base without the
// first name, so the worst case is the upstream behaviour.
constexpr const char* kHeavy[] = {"throughput-performance-bazzite"};
constexpr const char* kLight[] = {"balanced-battery-bazzite", "balanced"};

// NEVER DESTROYED, on purpose. The worker is detached and waits on gWake for
// ever; destroying a condition variable somebody is waiting on blocks in
// glibc, so exit() hung for good after the first game had started the worker.
// Ordinary exits hid it (systemd kills a stopping session, Sign out execs);
// handing over to Steam (#223) is the first exit that has to finish on its
// own, and it sat on "Starting Steam" (A9, 2026-10-03: the stack was
// pthread_cond_destroy under __run_exit_handlers).
std::mutex& gLock = *new std::mutex;
std::condition_variable& gWake = *new std::condition_variable;
int gWanted = -1;     // a Tier as an int, -1 nothing asked yet
bool gStarted = false;
bool gComplained = false;

// One D-Bus call to tuned. Blocks for as long as tuned takes to apply it.
void apply(Tier tier) {
    const char* const* names = tier == Tier::Heavy ? kHeavy : kLight;
    const size_t count = tier == Tier::Heavy ? sizeof kHeavy / sizeof kHeavy[0]
                                             : sizeof kLight / sizeof kLight[0];
    const char* what = tier == Tier::Heavy ? "for a heavy game"
                       : tier == Tier::Light ? "for a light game" : "for Home";
    sd_bus* bus = nullptr;
    int r = sd_bus_open_system(&bus);
    std::string lastWhy = r < 0 ? "no bus" : "";
    bool done = false;
    const char* tried = names[0];
    for (size_t i = 0; r >= 0 && i < count && !done; ++i) {
        tried = names[i];
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        int ok = 0;
        const char* msg = nullptr;
        int c = sd_bus_call_method(bus, "com.redhat.tuned", "/Tuned", "com.redhat.tuned.control",
                                   "switch_profile", &err, &reply, "s", names[i]);
        if (c >= 0) c = sd_bus_message_read(reply, "(bs)", &ok, &msg);
        if (c >= 0 && ok) {
            std::fprintf(stderr, "[profile] %s, %s\n", names[i], what);
            done = true;
        } else {
            lastWhy = c < 0 ? (err.message ? err.message : "no answer") : (msg ? msg : "refused");
        }
        sd_bus_message_unref(reply);
        sd_bus_error_free(&err);
    }
    if (!done && !gComplained) {
        // Once: a machine without tuned, or without permission, would say it
        // at every game otherwise.
        gComplained = true;
        std::fprintf(stderr, "[profile] tuned did not switch to %s: %s\n", tried,
                     lastWhy.c_str());
    }
    sd_bus_flush_close_unref(bus);
}

// The latest wish wins: a game started and left inside one switch costs one
// more switch, never a queue of them.
void worker() {
    int applied = -1;
    for (;;) {
        int want;
        {
            std::unique_lock<std::mutex> lk(gLock);
            gWake.wait(lk, [&] { return gWanted != applied; });
            want = gWanted;
        }
        apply(static_cast<Tier>(want));
        applied = want;
    }
}

}  // namespace

void applyNow(bool game) { apply(game ? Tier::Heavy : Tier::Home); }

Tier tierForCore(const std::string& libraryName) {
    // The cores that need the processor: the ones that emulate a 3D system or a
    // disc console with a fast CPU. Matched on the lower-cased library_name, which
    // is what the core reports about itself. Everything else is a 2D or 8/16-bit
    // system that a battery-biased profile runs at full speed.
    std::string n;
    for (char c : libraryName) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const char* const kHeavyCores[] = {"dolphin", "mupen64", "parallel", "flycast",
                                              "ppsspp", "azahar", "citra", "pcsx2", "saturn",
                                              "yabause", "kronos", "opera"};
    for (const char* h : kHeavyCores)
        if (n.find(h) != std::string::npos) return Tier::Heavy;
    return Tier::Light;
}

void update(Tier tier) {
    const int want = static_cast<int>(tier);
    std::lock_guard<std::mutex> lk(gLock);
    if (want == gWanted) return;
    gWanted = want;
    if (!gStarted) {
        gStarted = true;
        std::thread(worker).detach();
    }
    gWake.notify_one();
}

}  // namespace powerprofile
