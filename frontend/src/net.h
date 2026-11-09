// The network, as far as a console needs to care about one.
//
// WHY THIS EXISTS AT ALL: first run cannot be completed without a network, and
// that is not a UI decision — docs/PROJECT.md, open question 15b. The entirety
// of this OS is a client of a RomM server, so a machine that cannot reach one
// has nothing to show. There is no "continue without a network", which means
// the network step is a GATE and this file is what the gate asks.
//
// WHAT IT IS NOT. It is not a network settings page and must not grow into one.
// A console needs four answers — am I online, what is on the air, join this
// one, forget that one — and every field below exists because one of those four
// needs it. No proxies, no static addressing, no VPNs, no 802.1X. Enterprise
// networks are REPORTED and REFUSED rather than half-supported, which is open
// question 17's decision and the honest treatment of a form nobody can fill in
// from a sofa.
//
// --- Why nmcli, and not libnm or D-Bus -------------------------------------
//
// This shells out to `nmcli`. That looks like the lazy answer and it is the
// considered one.
//
//   * libnm is GLib-shaped: it wants a GMainLoop, and this program already has
//     a frame loop that owns the process. Bolting a second event loop on for
//     four questions is a threading model, not a feature.
//   * Raw D-Bus (sd-bus) avoids the loop but not the work: adding a Wi-Fi
//     connection means hand-building a nested `a{sa{sv}}` of NetworkManager's
//     settings schema. That is the part most likely to be subtly wrong and the
//     part with no way to check it by hand.
//   * nmcli does exactly that construction, is already in the image, is the
//     same command a person would type, and therefore any fault here can be
//     reproduced at a shell in one line. On a project whose rule is "measure
//     rather than reason", that last property is worth more than elegance.
//
// ONE EXCEPTION, AND IT IS THE PASSWORD. nmcli takes a Wi-Fi password only
// on its command line or from a passwd-file, and neither will do (#629, `join`
// in net.cpp): any process on the console can read any other's argv through
// /proc/<pid>/cmdline, and a passwd-file's password is handed back on every
// retry, which turns a wrong one into a 45-second wait. So a new profile with a
// typed password is made over D-Bus, one small `a{sa{sv}}` of three groups,
// checked by joining a simulated radio's networks (WPA2, mixed, WPA3, hidden);
// nmcli still brings it up and says why when it fails.
//
// The price is parsing, and it is paid in one place: `--terse` output is
// colon-separated with `\` escapes, and an SSID may legally contain a colon.
// Everything that reads nmcli goes through `splitFields` for that reason.
//
// --- NOTHING HERE GOES THROUGH A SHELL -------------------------------------
//
// An SSID is a string an attacker chooses. Anyone within radio range can name a
// network `"; rm -rf ~"` and this console will list it. So every command is run
// with fork/execvp and an argv array, never popen and never a constructed
// command line. That is not defensiveness about a hypothetical — a scan puts
// unvetted bytes from strangers into this process, every time it runs.
//
// AND A PASSWORD NEVER GOES ON A COMMAND LINE: above, the one exception.
//
// --- IT BLOCKS, AND CALLERS MUST TREAT IT THAT WAY -------------------------
//
// A scan takes seconds and joining a network takes tens of them. Every call
// here is synchronous, exactly as romm::Client is, and for the same stated
// reason: this does not own a thread, so the cost is the caller's to see. Call
// it from a worker. A frame loop that calls `scan()` has stopped drawing.

#pragma once

#include <string>
#include <vector>

namespace net {

// --- Am I online ------------------------------------------------------------

enum class Link { None, Ethernet, WiFi };

// What the machine's connectivity actually is, read fresh every time. Nothing
// here is cached, for the same reason storage::locations() caches nothing: a
// remembered answer that has gone stale is worse than asking again.
struct Status {
    // THE ONE FIELD THE GATE READS. True when NetworkManager has a device in
    // the `connected` state — which is what "this machine can reach its LAN"
    // means, and is deliberately not "the internet is reachable". RomM is on
    // the LAN; a console behind a dead uplink can still be set up.
    bool online = false;

    Link link = Link::None;      // what is carrying it, when something is
    std::string device;          // "enp197s0"
    std::string connection;      // NetworkManager's name for it
    std::string ipv4;            // "192.168.0.20/24", or empty

    // A wired port exists on this machine. Distinct from `ethernetUp`, because
    // the two lead to different sentences: a console with no port must never
    // say "plug in a cable".
    //
    // A PHONE ON A USB CABLE LANDS HERE. Tethering presents as an ordinary
    // wired device with no configuration at all, which is open question 17's
    // rung 4 and the reason it needs no code — only for somebody to say so on
    // the screen, because nobody thinks of it.
    bool ethernetPresent = false;
    bool ethernetUp = false;

    // There is a radio at all. THE THREE-STATE RULE, from Phase 6 and repeated
    // for Bluetooth in open question 15b: searching, found, and *no adapter* are
    // three different screens, not two. A machine with no Wi-Fi hardware that is
    // offered a Wi-Fi step has been lied to.
    bool wifiPresent = false;
    bool wifiEnabled = false;    // the radio is on — rfkill and `nmcli radio`
    bool wifiUp = false;

    // NetworkManager is not running or could not be reached. Every other field
    // is meaningless when this is set, and the caller must say so rather than
    // reporting a machine that is merely offline.
    bool managerMissing = false;
};

Status status();

// --- What is on the air -----------------------------------------------------

struct Network {
    std::string ssid;
    int signal = 0;            // 0..100, as NetworkManager reports it
    bool secured = false;
    std::string security;      // "WPA2", "WPA3", "WPA2 802.1X", "" when open

    // NetworkManager already holds a saved connection for this SSID, so joining
    // it needs no passphrase. Free to know and it removes the commonest reason
    // somebody has to type at all.
    bool known = false;
    bool active = false;

    // 802.1X. Out of scope by decision rather than by omission — it needs a
    // certificate, an identity and an inner method, which is a different form
    // entirely and one nobody is filling in with a d-pad. Listed so it can be
    // refused with a reason instead of failing after the passphrase screen.
    bool enterprise = false;
};

// Asks the radio to look, then reports. `--rescan yes`, so this is the slow
// call and not a cached list: on the A9 it takes a handful of seconds.
//
// An empty list with no error is a real answer and the screen has to handle it
// — a console in a cupboard hears nothing.
bool scan(std::vector<Network>* out, std::string* err);

// The same list NetworkManager already has, without asking the radio to look
// again. For a screen that wants to draw something immediately while `scan()`
// runs on a worker.
bool cachedScan(std::vector<Network>* out, std::string* err);

// --- Joining ----------------------------------------------------------------

// Joins, and SAVES A SYSTEM CONNECTION so it comes back after a reboot. That
// last half is the whole point and it is the half that needs a privilege:
// `org.freedesktop.NetworkManager.settings.modify.system`. See
// system_files/usr/share/polkit-1/rules.d/60-cabinetos-network.rules, and
// `polkitVerdict()` below, which is how a console reports that it has lost it
// rather than presenting it as a network fault.
//
// `passphrase` is empty for an open network. `hidden` adds the SSID by name,
// for a network that does not broadcast — open question 17 names this as one of
// the three things that would otherwise be discovered late.
//
// Blocks until NetworkManager settles or `timeoutSeconds` passes. It returns
// false on a wrong passphrase, and `err` carries what nmcli said, because
// "Secrets were required, but not provided" is the one failure a person can act
// on and paraphrasing it loses that. A failed join with a passphrase leaves no
// profile behind: one holding the wrong password showed as "Saved", and
// joining it tried that password again (MMagTech on the TV, 2026-09-24).
bool join(const std::string& ssid, const std::string& passphrase, bool hidden,
          std::string* err, int timeoutSeconds = 45);

// Deletes every saved connection for an SSID. Plural deliberately:
// NetworkManager will happily hold two profiles for one network, and forgetting
// one of them leaves the console rejoining with the other. True when none is
// left; false, with why in `err`, when none was saved or one would not go.
bool forget(const std::string& ssid, std::string* err);

// The radio. A console has no reason to turn it off, but it may well find it
// off — rfkill survives a reboot — and a Wi-Fi step that shows an empty list on
// a machine whose radio is simply off is the silent failure this prevents.
bool setRadio(bool on, std::string* err);

// --- The instruments --------------------------------------------------------

// Is there a NetworkManager to talk to. False means every other call here will
// fail, and the console should say that rather than "no networks found".
bool available();

// What polkit says, right now, about saving a system connection — "yes", "no",
// "auth_admin_keep", or a reason it could not be asked.
//
// THIS IS AN INSTRUMENT AND IT EXISTS BECAUSE OF A SPECIFIC TRAP. The grant
// is 60-cabinetos-network.rules, and the session user is ALSO in `wheel`
// (the installer adds it), which Bazzite's own rule grants too. If either
// ever changes, the failure has no error message anywhere near its cause: Wi-Fi simply stops saving, on a machine whose network is fine. So the
// console can ask the question directly and answer it in one line, instead of
// somebody spending a day on it.
std::string polkitVerdict();

}  // namespace net
