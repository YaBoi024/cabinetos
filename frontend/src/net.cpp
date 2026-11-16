#include "net.h"

#include <systemd/sd-bus.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "proc.h"

namespace net {
namespace {

// Every command here goes through proc::run, which takes an argv array and
// never builds a command line. See proc.h: a scan puts strings chosen by
// strangers into this process, every time it runs.
using proc::lines;
using proc::trimmed;

// THE SPLIT THAT HAS TO BE RIGHT. nmcli --terse separates fields with ':' and
// escapes any ':' or '\' inside a value with a backslash. An SSID may contain
// both, and anybody within radio range picks their own SSID — so a naive
// `split(':')` is not a tidiness bug, it is a stranger deciding how many fields
// this console thinks it received.
std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\\' && i + 1 < line.size()) {
            cur.push_back(line[++i]);   // an escaped ':' or '\' — taken literally
        } else if (c == ':') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

const std::string& fieldAt(const std::vector<std::string>& f, size_t i) {
    static const std::string kEmpty;
    return i < f.size() ? f[i] : kEmpty;
}

bool haveNmcli() {
    // `nmcli --version` rather than looking for the file: a binary that is
    // present and cannot run answers the same question the wrong way.
    return proc::run({"nmcli", "--version"}, 5).ok();
}

}  // namespace

bool available() {
    if (!haveNmcli()) return false;
    const proc::Result r = proc::run({"nmcli", "-t", "-f", "RUNNING", "general"}, 5);
    return r.ok() && trimmed(r.out) == "running";
}

Status status() {
    Status s;
    if (!available()) {
        s.managerMissing = true;
        return s;
    }

    // DEVICE:TYPE:STATE:CONNECTION, one line per device.
    const proc::Result dev = proc::run({"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION",
                         "device", "status"}, 10);
    if (!dev.ok()) {
        s.managerMissing = true;
        return s;
    }

    // WHICH DEVICE IS "THE" CONNECTION, when more than one is up. Wired wins.
    // It is the faster link and the one a console under a television is
    // normally on, and — more to the point — the Wi-Fi step's whole question is
    // "is there already another way out of here", which is exactly this.
    for (const std::string& line : lines(dev.out)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = splitFields(line);
        const std::string& device = fieldAt(f, 0);
        const std::string& type = fieldAt(f, 1);
        const std::string& state = fieldAt(f, 2);
        const std::string& conn = fieldAt(f, 3);
        // `connected` exactly. NetworkManager also says "connected (externally)"
        // for loopback and for devices somebody else brought up, and
        // "connecting" for one that has not arrived yet; neither is online.
        const bool up = state == "connected";

        if (type == "ethernet") {
            s.ethernetPresent = true;
            if (up && !s.ethernetUp) {
                s.ethernetUp = true;
                s.online = true;
                s.link = Link::Ethernet;
                s.device = device;
                s.connection = conn;
            }
        } else if (type == "wifi") {
            s.wifiPresent = true;
            if (up && !s.wifiUp) {
                s.wifiUp = true;
                if (!s.online) {
                    s.online = true;
                    s.link = Link::WiFi;
                    s.device = device;
                    s.connection = conn;
                }
            }
        }
        // Everything else — wifi-p2p, loopback, bridges, tun — is deliberately
        // ignored. A console is online over a cable or a radio; a machine whose
        // only route is a tunnel is not a case this product has.
    }

    if (s.wifiPresent) {
        const proc::Result radio = proc::run({"nmcli", "-t", "-f", "WIFI", "radio"}, 5);
        s.wifiEnabled = radio.ok() && trimmed(radio.out) == "enabled";
    }

    if (!s.device.empty()) {
        const proc::Result ip = proc::run({"nmcli", "-t", "-f", "IP4.ADDRESS", "device", "show",
                            s.device}, 10);
        if (ip.ok()) {
            for (const std::string& line : lines(ip.out)) {
                const std::vector<std::string> f = splitFields(line);
                if (f.size() >= 2 && !f[1].empty()) { s.ipv4 = f[1]; break; }
            }
        }
    }
    return s;
}

namespace {

// A saved Wi-Fi profile: the UUID that names it and the SSID it joins.
struct SavedWifi {
    std::string uuid;
    std::string ssid;
};

// Every saved Wi-Fi profile NetworkManager holds. False when they could not
// be listed.
//
// EACH ONE BY ITS UUID, NEVER BY ITS NAME. A name need not be unique, and
// NetworkManager holds two profiles of one name for one network without
// complaint (#642): `connection show <NAME>` then prints both, one after the
// other, and its output is no longer one SSID. A UUID is one profile.
//
// The SSID is the profile's own `802-11-wireless.ssid`, not its NAME. nmcli
// names a profile after the SSID by default, which makes the two look
// interchangeable right up until somebody renames one, and then the console
// offers to type a password for a network it already knows.
bool savedWifi(std::vector<SavedWifi>* out) {
    out->clear();
    const proc::Result r = proc::run({"nmcli", "-t", "-f", "UUID,TYPE", "connection", "show"}, 10);
    if (!r.ok()) return false;
    for (const std::string& line : lines(r.out)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = splitFields(line);
        if (fieldAt(f, 1) != "802-11-wireless") continue;
        const proc::Result got = proc::run({"nmcli", "-t", "-f", "802-11-wireless.ssid",
                                            "connection", "show", "uuid", fieldAt(f, 0)}, 10);
        if (!got.ok()) continue;
        const std::vector<std::string> sf = splitFields(trimmed(got.out));
        if (sf.size() >= 2 && !sf[1].empty()) out->push_back({fieldAt(f, 0), sf[1]});
    }
    return true;
}

bool listWifi(bool rescan, std::vector<Network>* out, std::string* err) {
    if (!out) return false;
    out->clear();
    if (!available()) {
        if (err) *err = "NetworkManager is not running";
        return false;
    }

    // A scan is the slow call in this file, which is why it is separated from
    // cachedScan: a screen can draw the cached list at once and replace it when
    // this returns.
    proc::Result r = proc::run({"nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
                                "device", "wifi", "list",
                                "--rescan", rescan ? "yes" : "no"},
                               rescan ? 30 : 10);
    // A REFUSED RESCAN IS NOT AN EMPTY SKY. NetworkManager declines
    // `--rescan yes` while a scan it started itself is already running, and it
    // scans on its own schedule — so the commonest reason this fails is that
    // results are on their way. Falling back to the cached list turns a
    // transient conflict into the list NetworkManager already has, instead of a
    // screen saying there is nothing on the air in a house full of routers.
    if (!r.ok() && rescan) {
        r = proc::run({"nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
                       "device", "wifi", "list", "--rescan", "no"}, 10);
    }
    if (!r.ok()) {
        if (err) {
            *err = r.timedOut ? "the scan did not finish"
                              : trimmed(r.err).empty() ? "nmcli could not list networks"
                                                       : trimmed(r.err);
        }
        return false;
    }

    std::vector<SavedWifi> saved;
    savedWifi(&saved);

    for (const std::string& line : lines(r.out)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = splitFields(line);
        Network n;
        n.active = fieldAt(f, 0) == "*";
        n.ssid = fieldAt(f, 1);
        // A network that does not broadcast its name comes back with an empty
        // SSID. There is nothing to show and nothing to tap, and the way to
        // join one is the hidden-network path where the name is typed — so it
        // is dropped here rather than drawn as a blank row.
        if (n.ssid.empty()) continue;
        n.signal = std::atoi(fieldAt(f, 2).c_str());
        n.security = fieldAt(f, 3);
        n.secured = !n.security.empty();
        n.enterprise = n.security.find("802.1X") != std::string::npos ||
                       n.security.find("EAP") != std::string::npos;
        n.known = std::any_of(saved.begin(), saved.end(),
                              [&](const SavedWifi& w) { return w.ssid == n.ssid; });

        // One row per network, not one per access point. A house with three
        // mesh nodes broadcasts the same SSID three times and nmcli lists all
        // of them; three identical rows is a list nobody can use. The strongest
        // wins, which is also the one NetworkManager would pick.
        auto same = std::find_if(out->begin(), out->end(),
                                 [&](const Network& x) { return x.ssid == n.ssid; });
        if (same == out->end()) {
            out->push_back(n);
        } else if (n.signal > same->signal) {
            const bool wasActive = same->active;
            *same = n;
            same->active = same->active || wasActive;
        } else if (n.active) {
            same->active = true;
        }
    }

    std::sort(out->begin(), out->end(), [](const Network& a, const Network& b) {
        // A ROW NOBODY CAN PICK SORTS LAST, whatever its signal. 802.1X is
        // refused by decision rather than by omission — it needs a certificate,
        // an identity and an inner method, which is a different form and not
        // one anybody fills in from a sofa — so its row is drawn greyed and
        // cannot be activated. Leaving it ranked by signal put a dead row above
        // live ones in the middle of the list somebody is choosing from.
        if (a.enterprise != b.enterprise) return b.enterprise;
        if (a.active != b.active) return a.active;      // the one we are on, first
        if (a.known != b.known) return a.known;         // then ones needing no typing
        return a.signal > b.signal;
    });
    return true;
}

}  // namespace

bool scan(std::vector<Network>* out, std::string* err) { return listWifi(true, out, err); }
bool cachedScan(std::vector<Network>* out, std::string* err) { return listWifi(false, out, err); }

namespace {

// What the screen shows for a failed join, from what the failure said.
void sayWhy(const std::string& why, std::string* err) {
    if (!err) return;
    *err = why.empty() ? "could not join that network" : why;
    // SAY WHEN THE PRIVILEGE IS THE PROBLEM, because it is the one failure
    // whose message points nowhere near its cause. Joining succeeds and
    // saving the profile does not, so the console comes up on a network it
    // has no memory of, every boot, forever. NetworkManager's own refusal
    // over D-Bus is "Insufficient privileges" (NM_UTILS_ERROR_MSG_INSUFF_PRIV).
    if (why.find("not authorized") != std::string::npos ||
        why.find("Not authorized") != std::string::npos ||
        why.find("insufficient privileges") != std::string::npos ||
        why.find("Insufficient privileges") != std::string::npos) {
        *err += ". This console is not permitted to save a network "
                "(polkit says " + polkitVerdict() + "); see "
                "60-cabinetos-network.rules";
    }
}

// The same, from a failed nmcli: its own words, less the "Hint:" line
// `connection up` adds after them, as a journalctl command means nothing on
// a television.
void sayWhy(const proc::Result& r, std::string* err) {
    if (r.timedOut) {
        if (err) *err = "the network did not answer in time";
        return;
    }
    std::string why;
    for (const std::string& line : lines(trimmed(r.err).empty() ? r.out : r.err)) {
        if (trimmed(line).empty() || line.compare(0, 5, "Hint:") == 0) continue;
        if (!why.empty()) why += ' ';
        why += trimmed(line);
    }
    sayWhy(why, err);
}

// The security a new profile is made with, from the list's SECURITY field
// ("WPA2", "WPA2 WPA3", "WPA3", "WEP"): the answers NetworkManager itself
// gives when it fills one in from the access point (nm-wifi-utils.c, 1.56),
// which is what `device wifi connect` relied on. "wpa-psk" whenever WPA1 or
// WPA2 is offered, a mixed WPA2/WPA3 network included, where NetworkManager
// still lets the radio use WPA3. "sae" only when WPA3 is all there is,
// because a "wpa-psk" profile does not match a WPA3-only network at all.
// "none" is WEP. Empty: an open network.
std::string keyMgmtFor(const std::string& security) {
    if (security.find("WPA1") != std::string::npos ||
        security.find("WPA2") != std::string::npos) return "wpa-psk";
    if (security.find("WPA3") != std::string::npos) return "sae";
    if (security.find("WEP") != std::string::npos) return "none";
    return "";
}

// `ssid` as it is on the air: its key management, and the Wi-Fi device that
// hears it, which `device wifi connect` tied its profile to. False when no
// access point by that name is in the list. A mesh lists one name several
// times: any that offers WPA2 decides, since "wpa-psk" joins them all.
//
// A HIDDEN NETWORK IS ASKED FOR BY NAME FIRST, as `device wifi connect ...
// hidden yes` did, and the list read until it has it: an access point that
// does not broadcast its name answers only a scan that says it.
bool onAir(const std::string& ssid, bool hidden, std::string* keyMgmt, std::string* device) {
    if (hidden) proc::run({"nmcli", "device", "wifi", "rescan", "ssid", ssid}, 15);
    for (int tries = hidden ? 10 : 1; tries > 0; --tries) {
        const proc::Result r = proc::run({"nmcli", "-t", "-f", "SSID,SECURITY,DEVICE",
                                          "device", "wifi", "list", "--rescan", "no"}, 10);
        bool seen = false;
        for (const std::string& line : lines(r.out)) {
            const std::vector<std::string> f = splitFields(line);
            if (fieldAt(f, 0) != ssid) continue;
            const std::string km = keyMgmtFor(fieldAt(f, 1));
            if (!seen || km == "wpa-psk") {
                *keyMgmt = km;
                *device = fieldAt(f, 2);
            }
            seen = true;
        }
        if (seen) return true;
        if (tries > 1) sleep(1);
    }
    return false;
}

// A new Wi-Fi profile, password and all, handed to NetworkManager on the
// system bus (Settings.AddConnection2), which saves it. Its settings are
// NetworkManager's own names (nm-settings-dbus(5)), and the same as the
// profile `device wifi connect` made, setting for setting (compared on a
// simulated radio, 2026-11-01): the device, "infrastructure" and, with
// "wpa-psk", the auth-alg "open" were NetworkManager's from the access point.
// Everything else is NetworkManager's default, as it was then. False, with
// NetworkManager's words in `why`, when it refuses.
bool addProfile(const std::string& ssid, const std::string& uuid, const std::string& device,
                const std::string& keyMgmt, const std::string& passphrase, bool hidden,
                std::string* why) {
    sd_bus* bus = nullptr;
    sd_bus_message* m = nullptr;
    sd_bus_message* reply = nullptr;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_open_system(&bus);
    if (r >= 0)
        r = sd_bus_message_new_method_call(bus, &m, "org.freedesktop.NetworkManager",
                                           "/org/freedesktop/NetworkManager/Settings",
                                           "org.freedesktop.NetworkManager.Settings",
                                           "AddConnection2");
    // a{sa{sv}}, three groups. The SSID is bytes (ay), so its group is built
    // a container at a time; the other two go in one call each.
    if (r >= 0) r = sd_bus_message_open_container(m, 'a', "{sa{sv}}");
    if (r >= 0)
        r = device.empty()
                ? sd_bus_message_append(m, "{sa{sv}}", "connection", 3,
                                        "id", "s", ssid.c_str(),
                                        "uuid", "s", uuid.c_str(),
                                        "type", "s", "802-11-wireless")
                : sd_bus_message_append(m, "{sa{sv}}", "connection", 4,
                                        "id", "s", ssid.c_str(),
                                        "uuid", "s", uuid.c_str(),
                                        "type", "s", "802-11-wireless",
                                        "interface-name", "s", device.c_str());
    if (r >= 0) r = sd_bus_message_open_container(m, 'e', "sa{sv}");
    if (r >= 0) r = sd_bus_message_append(m, "s", "802-11-wireless");
    if (r >= 0) r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) r = sd_bus_message_open_container(m, 'e', "sv");
    if (r >= 0) r = sd_bus_message_append(m, "s", "ssid");
    if (r >= 0) r = sd_bus_message_open_container(m, 'v', "ay");
    if (r >= 0) r = sd_bus_message_append_array(m, 'y', ssid.data(), ssid.size());
    if (r >= 0) r = sd_bus_message_close_container(m);   // v
    if (r >= 0) r = sd_bus_message_close_container(m);   // {sv}
    if (r >= 0) r = sd_bus_message_append(m, "{sv}", "mode", "s", "infrastructure");
    if (r >= 0 && hidden) r = sd_bus_message_append(m, "{sv}", "hidden", "b", 1);
    if (r >= 0) r = sd_bus_message_close_container(m);   // a{sv}
    if (r >= 0) r = sd_bus_message_close_container(m);   // {sa{sv}}
    // What `device wifi connect` made of a typed WEP key: the key itself
    // (wep-key-type 1), not a passphrase to hash.
    if (r >= 0)
        r = keyMgmt == "none"
                ? sd_bus_message_append(m, "{sa{sv}}", "802-11-wireless-security", 3,
                                        "key-mgmt", "s", "none",
                                        "wep-key-type", "u", 1u,
                                        "wep-key0", "s", passphrase.c_str())
            : keyMgmt == "wpa-psk"
                ? sd_bus_message_append(m, "{sa{sv}}", "802-11-wireless-security", 3,
                                        "key-mgmt", "s", "wpa-psk",
                                        "auth-alg", "s", "open",
                                        "psk", "s", passphrase.c_str())
                : sd_bus_message_append(m, "{sa{sv}}", "802-11-wireless-security", 2,
                                        "key-mgmt", "s", keyMgmt.c_str(),
                                        "psk", "s", passphrase.c_str());
    if (r >= 0) r = sd_bus_message_close_container(m);   // the settings
    // TO_DISK (0x1), so it outlives a reboot. BLOCK_AUTOCONNECT (0x20):
    // autoconnect is on, as `device wifi connect` left it, but NetworkManager
    // starts nothing with it by itself until it has been brought up once.
    if (r >= 0) r = sd_bus_message_append(m, "u", 0x1u | 0x20u);
    if (r >= 0) r = sd_bus_message_append(m, "a{sv}", 0);
    if (r >= 0) r = sd_bus_call(bus, m, 30 * 1000000ULL, &err, &reply);
    const bool ok = r >= 0;
    if (!ok && why) *why = err.message ? err.message : std::strerror(-r);
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_message_unref(m);
    sd_bus_unref(bus);
    return ok;
}

// JOINING WITH A PASSWORD WITHOUT PUTTING IT ON A COMMAND LINE (#629).
// `device wifi connect <ssid> password <password>` carried it in nmcli's argv
// for the seconds a join takes, and every process on the console can read
// every other's argv through /proc/<pid>/cmdline. So:
//
//   1. The profile, password and all, goes to NetworkManager over the system
//      bus (addProfile), which only NetworkManager reads.
//   2. nmcli brings it up, with its own deadline and its own words for a
//      failure. A wrong password fails within seconds with "Secrets were
//      required, but not provided", as before: nmcli has no password to
//      give when NetworkManager asks again.
//
// NOT nmcli's passwd-file, which would have kept all of it in nmcli: its
// agent hands the same password back every time NetworkManager asks again,
// so a wrong one is retried until the deadline, 45 seconds, and ends as
// "Timeout expired" (measured on a simulated radio, 2026-11-01).
//
// Any failure deletes the profile, so a failed join leaves nothing saved.
bool joinWithPassword(const std::string& ssid, const std::string& passphrase,
                      const std::string& keyMgmt, const std::string& device, bool hidden,
                      std::string* err, int timeoutSeconds) {
    // Named by this program, so the steps after can say exactly which
    // profile they mean: names need not be unique. (Not cab::readBytes,
    // which sizes a file first, and a /proc file's size is 0.)
    std::string uuid;
    std::ifstream id("/proc/sys/kernel/random/uuid");
    std::getline(id, uuid);
    uuid = trimmed(uuid);
    if (uuid.empty()) {
        if (err) *err = "could not make a new network profile";
        return false;
    }
    std::fprintf(stderr, "[wifi] joining %s as %s%s, the password over D-Bus\n",
                 ssid.c_str(), keyMgmt.c_str(), hidden ? ", hidden" : "");

    std::string why;
    if (!addProfile(ssid, uuid, device, keyMgmt, passphrase, hidden, &why)) {
        sayWhy(why, err);
        return false;
    }

    // `--wait` is nmcli's own deadline and `run`'s is the backstop for nmcli
    // itself wedging. The backstop is longer on purpose: a process killed at
    // exactly its own timeout never gets to print why it failed, and that
    // message is the only actionable thing a wrong passphrase produces.
    const proc::Result r = proc::run({"nmcli", "--wait", std::to_string(timeoutSeconds),
                                      "connection", "up", "uuid", uuid},
                                     timeoutSeconds + 10);
    if (r.ok()) return true;
    sayWhy(r, err);
    proc::run({"nmcli", "connection", "delete", "uuid", uuid}, 15);
    return false;
}

}  // namespace

bool join(const std::string& ssid, const std::string& passphrase, bool hidden,
          std::string* err, int timeoutSeconds) {
    if (ssid.empty()) {
        if (err) *err = "no network name";
        return false;
    }
    if (!available()) {
        if (err) *err = "NetworkManager is not running";
        return false;
    }

    if (!passphrase.empty()) {
        std::string keyMgmt, device;
        if (onAir(ssid, hidden, &keyMgmt, &device)) {
            // Open on the air after all: the password has nothing to do,
            // as `device wifi connect` ignored it.
            if (!keyMgmt.empty())
                return joinWithPassword(ssid, passphrase, keyMgmt, device, hidden, err,
                                        timeoutSeconds);
        } else if (hidden) {
            // Never answered by name: WPA2 and mixed networks, nearly all of
            // them, join as "wpa-psk", on whichever Wi-Fi device finds it.
            return joinWithPassword(ssid, passphrase, "wpa-psk", "", true, err, timeoutSeconds);
        } else {
            // nmcli's own sentence for this, which `device wifi connect` gave.
            if (err) *err = "Error: No network with SSID '" + ssid + "' found.";
            return false;
        }
    }

    // NOTHING SECRET TO HAND OVER: an open network, or one with a saved
    // profile, which this activates. `--wait` and the backstop are as above.
    std::vector<std::string> args = {
        "nmcli", "--wait", std::to_string(timeoutSeconds),
        "device", "wifi", "connect", ssid,
    };
    if (hidden) {
        args.push_back("hidden");
        args.push_back("yes");
    }
    const proc::Result r = proc::run(args, timeoutSeconds + 10);
    if (r.ok()) return true;
    sayWhy(r, err);
    return false;
}

bool forget(const std::string& ssid, std::string* err) {
    if (!available()) {
        if (err) *err = "NetworkManager is not running";
        return false;
    }
    // EVERY profile for the SSID, not the first. NetworkManager will hold two
    // for one network without complaint, and deleting one of them leaves the
    // console rejoining with the other, which looks exactly like "forget did
    // nothing". So it is done only when none is left.
    std::vector<SavedWifi> saved;
    if (!savedWifi(&saved)) {
        if (err) *err = "could not list saved networks";
        return false;
    }
    bool found = false;
    bool allGone = true;
    for (const SavedWifi& w : saved) {
        if (w.ssid != ssid) continue;
        found = true;
        const proc::Result r = proc::run({"nmcli", "connection", "delete", "uuid", w.uuid}, 15);
        if (r.ok()) continue;
        allGone = false;
        sayWhy(r, err);
    }
    if (!found && err) *err = "no saved network by that name";
    return found && allGone;
}

bool setRadio(bool on, std::string* err) {
    if (!available()) {
        if (err) *err = "NetworkManager is not running";
        return false;
    }
    const proc::Result r = proc::run({"nmcli", "radio", "wifi", on ? "on" : "off"}, 15);
    if (r.ok()) return true;
    if (err) *err = trimmed(r.err).empty() ? "could not change the radio" : trimmed(r.err);
    return false;
}

std::string polkitVerdict() {
    // Asked about THIS process, which is the subject that will actually make
    // the request. Asking about anything else answers a different question —
    // the grant depends on the session the caller is in, which is why the same
    // check from an SSH shell and from the console can legitimately disagree.
    const std::string pid = std::to_string(static_cast<long>(getpid()));
    const proc::Result r = proc::run({"pkcheck", "--action-id",
                       "org.freedesktop.NetworkManager.settings.modify.system",
                       "--process", pid}, 10);
    if (r.status == -1) return "unknown (pkcheck did not run)";

    // pkcheck prints SEVERAL `key=value` lines, not one, and the keys are
    // written with the dot escaped:
    //
    //     polkit\56result=auth_admin_keep
    //     polkit\56retains_authorization_after_challenge=1
    //
    // So the answer is the line whose key ends in `result`, and taking the last
    // '=' in the output reports `1` — which is not even one of the values this
    // action can have. Measured on both machines before it was written this way.
    for (const std::string& line : lines(r.out)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        if (key.size() >= 6 && key.compare(key.size() - 6, 6, "result") == 0)
            return trimmed(line.substr(eq + 1));
    }
    if (r.ok()) return "yes";
    const std::string e = trimmed(r.err);
    return e.empty() ? "no" : e;
}

}  // namespace net
