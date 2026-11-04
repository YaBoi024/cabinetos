// First run: the order setup happens in, and the rules about what may be
// skipped. No screens, no drawing, no network calls.
//
// THE REQUIREMENT THIS EXISTS TO MEET, from docs/PROJECT.md open question 15b:
//
//     A keyboard is needed exactly once, ever. After first run the console must
//     never require one again, for anything.
//
// It is testable, which is why it is the requirement rather than a principle.
// A keyboard is the one input an installed machine is guaranteed to have had —
// the firmware boot menu needs one, so a machine cannot reach an installed
// state without one having been present. A controller is NOT guaranteed: owning
// a Bluetooth pad and having its cable to hand at setup time are different
// things. So setup runs on a keyboard and PAIRS a controller as its last step.
//
// --- The chain, and why it is a line and not a tree -------------------------
//
//     keyboard -> network -> RomM server -> pair pad 1 -> unplug the keyboard
//
// with the time zone in front of it on a console that has none (`TimeZone`,
// below): a portable install, which no installer set one on.
//
// MMagTech, 2026-09-19: *you cannot have kept games until a server has been
// paired and you have kept one.* So first run ASSUMES A SERVER and is a linear
// path to pairing one. There is no "set this up later" and no "use it without
// a server", because on the other side of a skip there is nothing to show — no
// library, no covers, no saves, no user and no kept games. The only thing a
// skip could offer is the stand-in demo library, which PROJECT.md says must
// never appear on a console because it looks like a working machine showing
// somebody else's games.
//
// That is a simplification rather than a restriction: every step may assume the
// one before it succeeded, and the last one may assume a real library exists to
// drop the person into.
//
// AND IT IS WHY THE OFFLINE CONSOLE IS NOT A BRANCH OF THIS. Open question 22
// is a machine that HAS been paired and now cannot reach its server, so it may
// assume it knows the user, the library it last saw and which games are kept.
// "No server yet" and "no server right now" are different problems and only the
// second has anything to work with. Nothing in this file is the offline case.
//
// --- Why the facts are handed in rather than fetched ------------------------
//
// `Machine` is given a `Facts` and judges it. It never calls the network, the
// disk or a server itself. That is the same split `screens::` makes — a screen
// returns an Action and the app decides whether it can — and it buys the same
// thing: the whole flow can be walked in a test, at any point in it, on a
// machine with no network, no server and no pad attached. `observe()` below is
// the one place that goes and looks, and it is deliberately separate.

#pragma once

#include <string>

#include "serveraddress.h"

namespace romm { class Client; }

namespace firstrun {

// --- Is this the first run at all -------------------------------------------
//
// THE HARD PART OF THIS QUESTION IS THE MACHINE THAT WAS SET UP BY HAND. The
// reference console was: somebody SSHed in and wrote /etc/cabinetos/session.env
// and a token, which is exactly what this flow will one day produce. It has
// never seen a setup screen and must never be shown one — a console that boots
// into a wizard after a year of use is a far worse failure than one that skips
// a wizard it did not need.
//
// So the answer is in two parts, and the second matters more than the first:
//
//   1. A marker file says setup finished. Written by `markCompleted`.
//   2. With no marker, a machine that ALREADY HAS everything setup produces —
//      a server address, a token, and a user behind that token — is taken as
//      set up, and the marker is back-filled saying so.
//
// Rule 2 is not a convenience. It is the difference between "has this flow been
// run" and "is this machine configured", and only the second is the question
// anybody cares about.
enum class Why {
    NotRun,          // no marker, and the machine is not configured either
    Completed,       // the marker says somebody walked the flow
    AdoptedExisting, // no marker, but the machine was already set up by hand
    SignedOut,       // signed out in Settings; first run opens at the server
    NoServer,        // the marker says done, but no server address is set
                     // anywhere; first run opens at the server (#485)
};

struct Completion {
    bool done = false;
    Why why = Why::NotRun;
    std::string when;    // ISO-8601, or empty
    // SignedOut only: the games and accounts of the server that was left are
    // still on the disk, and the start that reads this clears them first.
    bool clearing = false;
};

// Reads the marker and, when there is none, judges the machine. Cheap: two
// file tests and a look at the environment. Safe to call at startup.
Completion completion();

// Writes the marker. `adopted` records WHICH of the two answers above put it
// there, because "somebody walked the flow" and "we found a configured machine"
// are different facts and the difference will matter one day.
bool markCompleted(bool adopted, std::string* err);

// SIGNED OUT, docs/SETTINGS.md, Network, issue #61. Replaces the marker with
// one saying so, which puts the console back into first run at the server
// step. Written twice: `cleared` false by Settings, as the last thing before
// the app restarts, and true by the next start once `server::finishSignOut`
// has removed that server's games and accounts. A power cut between the two
// leaves `cleared` false, so the clearing is done again rather than half done.
bool markSignedOut(bool cleared, std::string* err);

// --- The steps --------------------------------------------------------------

enum class Step {
    // THE TIME ZONE, ASKED ONLY WHEN THE CONSOLE HAS NONE, as the first
    // screen (MMagTech, 2026-11-03). That is a portable install: a disk image
    // written to a USB drive, which no installer ran on. An install from the
    // ISO has the zone its Time & Date screen set, and never sees this step;
    // nor does a console that chose one and started again before finishing.
    // Blocked until a zone is set: the zone is what makes Appearance's dark
    // hours switch at the right local hour, and what the PC's clock is read
    // in (timezone.h). Region, then city, on one step.
    TimeZone,

    // A link, and it is the ONE HARD GATE in the whole flow that nothing on
    // the screen can open (the time zone's is a choice). MMagTech,
    // 2026-09-19: the entirety of this OS relies on a RomM server, so a console
    // that cannot reach a network cannot be set up and must not pretend
    // otherwise.
    //
    // WI-FI IS PART OF THIS STEP AND NOT A SECOND ONE. It used to be its own
    // step, offered after the network was up, and that produced a flow which
    // made you join a network and then showed you a screen headed "Set up
    // Wi-Fi" — MMagTech, 2026-09-20: *"if the first screen had me setup wifi
    // then why did the second screen say setup wifi."*
    //
    // One screen states the rule exactly, where two only approximated it. The
    // list of networks is ALWAYS offered; whether you have to use it is decided
    // by whether anything else is carrying the connection, which is precisely
    // what this gate already asks. On a cable, Continue is live and the list
    // sits there for anybody who wants a fallback — so Continue IS the skip,
    // and there is no second button to explain.
    //
    // The fallback still matters and is still the reason the list appears at
    // all: a console under a television is exactly where a cable gets tripped
    // over, and setup is the one moment Wi-Fi is cheap to configure because it
    // is the one moment a real keyboard is near-certain.
    Network,

    // The address of the RomM server. The ONLY thing anybody types in the whole
    // of first run, which is what the QR code at the next step buys.
    Server,

    // Device authorisation: the server hands back a URL and a short code, and
    // somebody approves it in a browser they are already signed in to. A
    // console has no browser, so the URL goes on the screen as a QR code and a
    // phone reads it off the television. NO PASSWORD EVER REACHES THIS PROGRAM.
    //
    // A phone is therefore a real dependency of setup and should be stated as
    // one rather than discovered.
    Pair,

    // Pad one. INSISTENT BUT NOT A HARD BLOCK: the input model says a keyboard
    // must keep working forever, so refusing to finish without a controller
    // would break the very guarantee this flow exists to make. But somebody who
    // skips it owns a games console they cannot play from a sofa, and Settings
    // has to offer it again.
    Controller,

    Done,
};

const char* name(Step s);

// What the flow may do at the step it is on.
enum class Gate {
    Blocked,    // the condition is not met and there is no way past it
    Ready,      // met; `advance()` will move on
    Skippable,  // not met, but this one may be passed over
};

// Everything the rules are judged against. Nothing here is fetched by
// `Machine`; see the note at the top of this file.
struct Facts {
    // A time zone is set on this machine (/etc/localtime; timezone.h).
    bool zoneChosen = false;

    // Any device NetworkManager calls connected. Deliberately not "the internet
    // is reachable" — RomM is on the LAN, and a console behind a dead uplink
    // can still be set up.
    bool online = false;
    // Online by something that is NOT the radio. The only thing that makes the
    // Wi-Fi step skippable.
    bool wiredOnline = false;
    bool wifiPresent = false;      // there is a radio at all
    bool wifiConfigured = false;   // the radio is carrying the connection

    bool haveServerAddress = false;
    bool serverAnswered = false;   // something spoke RomM at that address
    // WHETHER ANYBODY HAS ACTUALLY ASKED. Without this, "we have an address and
    // it has not answered" and "we have an address and have not tried it yet"
    // are the same state, and the console tells somebody their perfectly good
    // server did not answer before it has sent a single packet at it. Arriving
    // at this step with an address already in /etc is the COMMON case, not an
    // edge one.
    bool serverChecked = false;
    bool havePairedToken = false;

    int gamepadCount = 0;
};

class Machine {
public:
    void update(const Facts& f) { facts_ = f; }
    const Facts& facts() const { return facts_; }

    Step step() const { return step_; }
    Gate gate() const;

    // One line saying why the gate is what it is, written for a person and not
    // for a log. Empty when a step is simply ready.
    //
    // A SETUP SCREEN NOBODY CAN OPERATE, WITH NO EXPLANATION, IS WORSE THAN A
    // BLACK ONE — open question 15b says so about the no-input case and it is
    // the same rule here. Every Blocked has a sentence.
    std::string because() const;

    // Moves on when the gate allows it. Returns false when it does not, which
    // is the whole of the enforcement: a caller cannot skip a hard gate by
    // asking twice.
    bool advance();

    // Moves on past a Skippable step. Returns false on anything else —
    // including a Ready one, because "skip" and "continue" are different
    // intentions and collapsing them hides which one a screen meant.
    bool skip();

    // Back, for a person who mistyped the address two screens ago. It stops at
    // the first step rather than falling off the front, and it never re-opens
    // `Done`. The first step is the network's, or the time zone's when this
    // run asked it (`zoneAsked`): a console that had a zone never sees it.
    bool back();

    bool finished() const { return step_ == Step::Done; }

    // Starts the flow at a named step, so any one of them can be reached
    // without walking the others — the same reasoning as `--screen`, which
    // opens a screen by walking the route a person walks so a capture cannot
    // show a state the product cannot reach. Returns false for a name that is
    // not a step. Opening at `timezone` is how a run asks the time zone:
    // setup.cpp does it when the console has none.
    bool openAt(const std::string& stepName);

    // This run began at the time zone, so Back from the network reaches it.
    bool zoneAsked() const { return zoneAsked_; }

private:
    Step step_ = Step::Network;
    Facts facts_;
    bool zoneAsked_ = false;
};

// Goes and looks, which is the one thing `Machine` will not do.
//
// `serverAnswered` is left false: whether an address answers is a question only
// something that has tried can answer, and trying belongs to the caller.
Facts observe(const romm::Client& client, int gamepadCount);

// The half that costs nothing — a couple of file tests and a flag. The LINK is
// deliberately not asked about, because `net::status()` is three or four nmcli
// round trips and several hundred milliseconds, which is ten frames.
//
// WHY THE SPLIT EXISTS. Anything drawing a screen refreshes far more often than
// a probe does, and it must never pay for a subprocess to do it. A frontend
// calls this and lets a worker own the link; `observe()` above is for the
// one-shot reports, where blocking is the whole point.
Facts observeLocal(const romm::Client& client, int gamepadCount);

}  // namespace firstrun
