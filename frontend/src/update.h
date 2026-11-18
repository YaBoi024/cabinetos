// System update, the session's half. docs/SETTINGS.md, System; issue #70.
//
// The work is root's: /usr/libexec/cabinetos-update, run by two oneshot units
// the session may start and nothing else (61-cabinetos-update.rules). This
// side starts them, reads the one status file they write, and knows which
// version it is running. What any of that MEANS on screen, and when a check
// may run, is the app's (updateflow.cpp), the same division every module here
// keeps.

#pragma once

#include <cstdint>
#include <string>

namespace update {

enum class State {
    None,         // nothing has run this boot
    Checking,
    UpToDate,
    Available,
    Downloading,
    Installing,
    Ready,        // staged; the next restart or power off applies it
    Failed,
};

struct Status {
    State state = State::None;
    bool download = false;     // written by the download, not the check
    std::string version;       // 2026.09.28
    int64_t size = 0;          // Available: bytes a download will fetch
    std::string romm;          // Available: the RomM it needs, "5.5"; empty, none named
    int64_t done = 0;          // Downloading
    int64_t total = 0;
    int64_t since = 0;         // Installing: when it began, epoch seconds
    int64_t written = 0;
    std::string reason;        // Failed
    int64_t at = 0;            // when root wrote this, epoch seconds

    bool busy() const {
        return state == State::Checking || state == State::Downloading ||
               state == State::Installing;
    }
};

// For the TV loop: read the status from here instead of /run, so each state
// can be put on the screen by writing a file. `--update-dir`.
void setDir(const std::string& dir);

Status read();

// Starts the check or the download, over D-Bus with no password prompt.
// Returns at once; the status file says how it goes. False, with why, if
// systemd refused it (no unit, or polkit said no).
bool start(bool download, std::string* why);

// The version this boot is running, from /usr/share/cabinetos/version. Empty
// on an image built before there was one.
std::string bootedVersion();

// This boot's id, which tells "started again" from "restarted the machine".
std::string bootId();

// The tag the booted image follows, "latest" or "testing", read from the
// booted deployment's origin file. Empty if it cannot be read.
std::string channel();

// Bazzite's version underneath, 44.20260916. Empty if it cannot be read.
std::string baseVersion();

// WHETHER THE SERVER'S RomM IS NEW ENOUGH for an image that needs `needed`
// (its cabinetos.romm-needed label, "5.5"; empty, it names none). `server` is
// what RomM's heartbeat says ("5.5.0-alpha.2"). The numbers are compared one
// by one, a missing one counting as 0, and anything after a "-" or "+" is not
// read, so RomM's own alpha of 5.5 counts as 5.5: the save channels build
// goes on testing for people running it (#346). Unknown when `server` is no
// version: empty, never heard, or "development", which a RomM built from its
// source says.
enum class Romm { NewEnough, TooOld, Unknown };
Romm rommFor(const std::string& server, const std::string& needed);

// --update-romm-test: rommFor on RomM's versions, and the status file's
// romm= line.
int test();

}  // namespace update
