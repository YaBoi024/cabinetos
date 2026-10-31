#include "firstrun.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <json-c/json.h>

#include "net.h"
#include "romm.h"
#include "accounts.h"
#include "fileio.h"
#include "storage.h"
#include "firstrun_internal.h"

namespace firstrun {
namespace {

std::string markerPath() { return storage::configDir() + "/first-run.json"; }

// WHAT "PAIRED" MEANS, AND IT CHANGED ON 2026-09-21. It used to be the single
// `~/.config/cabinetos/romm.json`. Pairing now writes an ACCOUNT, so the fact
// to test is whether this console has one — `accounts::activeId()` is true
// only when the list holds somebody and one of them is active.
//
// There is deliberately no fallback to the old file. Open question 26: nobody
// else is running this, the two machines here were written into accounts.json
// by hand, and a permanent branch serving users who do not exist is the
// migration tool the folder layout already refused to add.
bool havePairedAccount() { return accounts::activeId() > 0; }

bool fileExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

}  // namespace

// Shared with serveraddress.cpp (firstrun_internal.h).
std::string readAll(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string body;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
    std::fclose(f);
    return body;
}

bool writeAll(const std::string& path, const std::string& body, std::string* err) {
    // Through cab::writeBytes (a temporary, synced, then renamed), because the
    // alternative is a console that loses its server address to a power cut
    // halfway through a 40-byte write and nobody able to say why.
    if (cab::writeBytes(path, body)) return true;
    if (err) *err = "cannot write " + path + ": " + std::strerror(errno);
    return false;
}

std::string isoNow() {
    const time_t t = ::time(nullptr);
    struct tm g;
    ::gmtime_r(&t, &g);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &g);
    return buf;
}

std::string jsonString(const std::string& body, const char* key) {
    json_object* o = json_tokener_parse(body.c_str());
    if (!o) return {};
    json_object* v = nullptr;
    std::string out;
    if (json_object_object_get_ex(o, key, &v) && v) {
        if (const char* s = json_object_get_string(v)) out = s;
    }
    json_object_put(o);
    return out;
}

namespace {

// Everything first run would produce, already present. See the header: this is
// the rule that stops a machine set up by hand booting into a wizard.
bool alreadyConfigured() {
    const bool haveAddress = !serverAddress().empty();
    const bool haveToken = havePairedAccount();
    // A cached user means a token that was actually USED — somebody reached
    // /api/users/me with it — which is a stronger fact than a file existing,
    // and it is what the save tree is namespaced on.
    const bool haveUser = fileExists(storage::configDir() + "/user.json");
    return haveAddress && haveToken && haveUser;
}

}  // namespace

const char* name(Step s) {
    switch (s) {
        case Step::Network:    return "network";
        case Step::Server:     return "server";
        case Step::Pair:       return "pair";
        case Step::Controller: return "controller";
        case Step::Done:       return "done";
    }
    return "?";
}

// --- Is this the first run --------------------------------------------------

Completion completion() {
    Completion c;
    const std::string body = readAll(markerPath());
    if (const std::string out = jsonString(body, "signed_out"); !out.empty()) {
        // Not done, whatever else is on the disk: alreadyConfigured() is not
        // asked, because a console signed out on purpose must not be adopted
        // back by the address and token it has not cleared yet.
        c.why = Why::SignedOut;
        c.when = out;
        c.clearing = jsonString(body, "cleared") != "true";
        return c;
    }
    if (!body.empty()) {
        c.when = jsonString(body, "completed");
        // DONE NEEDS A SERVER TOO (#485, FIRSTRUN-06). With the marker there
        // and no address anywhere (session.env gone, server.json not
        // written at the server step), the console started on the stand-in
        // library of six made-up games, which must never appear on a
        // console (the header), with nothing saying what was missing.
        if (serverAddress().empty()) {
            c.why = Why::NoServer;
            return c;
        }
        c.done = true;
        c.why = jsonString(body, "how") == "adopted" ? Why::AdoptedExisting
                                                     : Why::Completed;
        return c;
    }
    if (alreadyConfigured()) {
        c.done = true;
        c.why = Why::AdoptedExisting;
        return c;
    }
    return c;
}

bool markCompleted(bool adopted, std::string* err) {
    if (!storage::makeDirs(storage::configDir())) {
        if (err) *err = "cannot create " + storage::configDir();
        return false;
    }
    json_object* o = json_object_new_object();
    json_object_object_add(o, "completed", json_object_new_string(isoNow().c_str()));
    json_object_object_add(o, "how",
                           json_object_new_string(adopted ? "adopted" : "walked"));
    // The version of the flow that wrote it. A console upgraded into a first
    // run that asks something new has to be able to tell that it never answered
    // the new question — and a marker with no version in it cannot say.
    json_object_object_add(o, "flow", json_object_new_int(1));
    const std::string body = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
    json_object_put(o);
    return writeAll(markerPath(), body + "\n", err);
}

bool markSignedOut(bool cleared, std::string* err) {
    if (!storage::makeDirs(storage::configDir())) {
        if (err) *err = "cannot create " + storage::configDir();
        return false;
    }
    json_object* o = json_object_new_object();
    json_object_object_add(o, "signed_out", json_object_new_string(isoNow().c_str()));
    json_object_object_add(o, "cleared", json_object_new_string(cleared ? "true" : "false"));
    json_object_object_add(o, "flow", json_object_new_int(1));
    const std::string body = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
    json_object_put(o);
    return writeAll(markerPath(), body + "\n", err);
}

// --- The rules --------------------------------------------------------------

Gate Machine::gate() const {
    switch (step_) {
        case Step::Network:
            // THE ONE HARD GATE. No skip, and none may ever be added: on the
            // far side of it there is no server, so there is nothing to show.
            //
            // AND IT IS THE WHOLE OF THE WI-FI RULE TOO, now that the two steps
            // are one. "Offered even when Ethernet is up, skippable then and
            // required otherwise" is exactly `online ? Ready : Blocked` — the
            // list is drawn either way, and being online is what decides
            // whether anybody has to touch it. The old second step needed its
            // own Skip button to say the same thing, and then said it on a
            // screen headed "Set up Wi-Fi" to somebody who had just set up
            // Wi-Fi.
            return facts_.online ? Gate::Ready : Gate::Blocked;

        case Step::Server:
            return facts_.serverAnswered ? Gate::Ready : Gate::Blocked;

        case Step::Pair:
            return facts_.havePairedToken ? Gate::Ready : Gate::Blocked;

        case Step::Controller:
            // Insistent, not blocking. See the header: refusing to finish
            // without a pad would break the keyboard guarantee this whole flow
            // exists to make.
            return facts_.gamepadCount > 0 ? Gate::Ready : Gate::Skippable;

        case Step::Done:
            return Gate::Ready;
    }
    return Gate::Blocked;
}

// One line, and it assumes a competent adult.
//
// MMagTech, 2026-09-20, on the first draft of these: *"if you have a RomM
// server and can install an OS I shouldn't need to tell you in depth how to
// pair a controller."* That is the right test, and it is a stronger one than
// "is this clear" — the person in front of this screen has already stood up a
// self-hosted web application and written an operating system to a USB stick.
// Explaining what a pairing button is insults them and buries the one thing
// they actually need, which is what this step will and will not let them do.
//
// So each of these says the CONSTRAINT and stops. Required or optional, and
// why if the why is not obvious. No instructions for things that have one
// obvious way to do them.
std::string Machine::because() const {
    switch (step_) {
        case Step::Network:
            // SIX ANSWERS, AND THE WORD "REQUIRED" OR "OPTIONAL" IS IN ALL THE
            // ONES WHERE IT IS IN QUESTION.
            //
            // MMagTech, 2026-09-20: *"the wording needs to say something along
            // the lines of wifi is optional but not required and probably
            // something different if nothing is plugged in."* The old line
            // described what Wi-Fi would buy and never said you could walk past
            // it, and the offline line said a connection was required without
            // saying what to do about it. Neither is a sentence somebody can
            // act on.
            if (!facts_.online) {
                // Nothing is carrying the connection, so this is the hard gate.
                // It said what to do ("Pick a network to join, or plug in a
                // cable") until #110, 2026-10-06: the panel beside it already
                // shows the networks, or "plug in a cable" when there is no
                // Wi-Fi, so the sentence states the constraint and stops.
                // NO PHONE TETHERING ON THIS SCREEN, and that reverses what
                // open question 17 decided.
                //
                // It used to end "or share a phone's connection over USB",
                // because tethering presents as an ordinary wired device and
                // nobody thinks of it. MMagTech, 2026-09-20: *"i dont know if i
                // like the idea of the phone option, leaves a lot of potential
                // on me when this doesn't work for people."*
                //
                // He is right, and checking the image settles it. Android
                // tethering is pure kernel — rndis_host and cdc_ncm are in the
                // image and it simply appears as a wired device. **iPhone
                // tethering needs usbmuxd**, which is installed but inactive
                // and `static`, and it needs the phone to TRUST the computer:
                // a prompt, an unlock and a pairing step, none of which anybody
                // here has ever run on this console.
                //
                // So it is a promise that holds for one phone ecosystem and is
                // untested for the other, offered on the one screen where
                // somebody is already stuck and out of options. A console
                // should not suggest a fix it has never seen work. It stays in
                // the documentation as a trick; it does not go on a television.
                return "A network connection is required.";
            }
            // Online. What is carrying it, and whether anything below is worth
            // touching.
            if (facts_.wiredOnline && facts_.wifiConfigured)
                return "Connected over Ethernet, with Wi-Fi set up as a "
                       "fallback.";
            if (facts_.wifiConfigured) return "Connected over Wi-Fi.";
            if (!facts_.wifiPresent) return "Connected over Ethernet.";
            return "Connected over Ethernet. Wi-Fi is optional.";

        case Step::Server:
            if (facts_.serverAnswered) return {};
            if (!facts_.haveServerAddress)
                return "Enter the RomM server's address.";
            if (!facts_.serverChecked) return "Checking…";
            return "Nothing answered at that address.";

        case Step::Pair:
            if (facts_.havePairedToken) return {};
            return "Approve this console in a browser signed in to RomM.";

        case Step::Controller:
            if (facts_.gamepadCount > 0) return {};
            // SAY WHAT SKIPPING COSTS, in the same breath as saying it is
            // allowed. Without the second half this reads as "this does not
            // matter", and the person finds out it did on the last screen.
            return "Optional, but without one you will still need the keyboard.";

        case Step::Done:
            return {};
    }
    return {};
}

bool Machine::advance() {
    if (step_ == Step::Done) return false;
    if (gate() != Gate::Ready) return false;
    step_ = static_cast<Step>(static_cast<int>(step_) + 1);
    return true;
}

bool Machine::skip() {
    if (gate() != Gate::Skippable) return false;
    step_ = static_cast<Step>(static_cast<int>(step_) + 1);
    return true;
}

bool Machine::back() {
    if (step_ == Step::Network) return false;
    if (step_ == Step::Done) return false;
    step_ = static_cast<Step>(static_cast<int>(step_) - 1);
    return true;
}

bool Machine::openAt(const std::string& stepName) {
    for (int i = 0; i <= static_cast<int>(Step::Done); ++i) {
        const Step s = static_cast<Step>(i);
        if (stepName == name(s)) { step_ = s; return true; }
    }
    return false;
}

// --- Going and looking ------------------------------------------------------

Facts observeLocal(const romm::Client& client, int gamepadCount) {
    Facts f;
    f.haveServerAddress = !serverAddress().empty();
    // Only something that has tried can set these two; see the header.
    f.serverChecked = false;
    f.havePairedToken = client.haveToken() || havePairedAccount();
    // Left to the caller: only something that has tried can say. See the
    // header.
    f.serverAnswered = false;

    f.gamepadCount = gamepadCount;
    return f;
}

Facts observe(const romm::Client& client, int gamepadCount) {
    Facts f = observeLocal(client, gamepadCount);
    const net::Status s = net::status();
    f.online = s.online;
    f.wiredOnline = s.ethernetUp;
    f.wifiPresent = s.wifiPresent;
    // A radio that is up is by definition configured; one that is merely
    // enabled is not. Asking NetworkManager for saved profiles would be a
    // second shell-out for a fact the status already implies, and a saved
    // profile that has never connected is not a working fallback anyway.
    f.wifiConfigured = s.wifiUp;
    return f;
}

}  // namespace firstrun
