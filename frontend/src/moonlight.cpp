// FORK (handheld): Moonlight's command lines and the hosts paired with
// (moonlight.h).

#include "moonlight.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <random>

#include "fileio.h"
#include "storage.h"
#include "vpad.h"

namespace moonlight {

namespace {

std::string hostsFile() { return storage::configDir() + "/moonlight-hosts"; }
std::string lastFile() { return storage::configDir() + "/moonlight-last"; }

std::string readText(const std::string& path) {
    const std::vector<uint8_t> b = cab::readBytes(path);
    return std::string(b.begin(), b.end());
}

std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : readText(path)) {
        if (ch == '\n') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

void writeLines(const std::string& path, const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& l : lines) text += l + "\n";
    storage::makeDirs(storage::configDir());
    cab::writeBytes(path, text);
}

// The parts of `flatpak run` both commands share. Moonlight needs the
// network (it is a stream), so it is not given --unshare=network as the
// emulators are; the address it reaches is the one the player typed.
std::vector<std::string> base(const char* qtPlatform) {
    return {
        "flatpak", "run",
        std::string("--env=QT_QPA_PLATFORM=") + qtPlatform,
    };
}

}  // namespace

bool installed() {
    return ::access((std::string("/var/lib/flatpak/app/") + kApp + "/current/active/files")
                        .c_str(),
                    F_OK) == 0;
}

std::string newPin() {
    std::random_device rd;
    std::uniform_int_distribution<int> d(0, 9999);
    std::string pin = std::to_string(d(rd));
    while (pin.size() < 4) pin.insert(pin.begin(), '0');
    return pin;
}

std::vector<std::string> pairArgs(const std::string& host, const std::string& pin) {
    std::vector<std::string> a = base("offscreen");
    a.insert(a.end(), {kApp, "pair", host, "--pin", pin});
    return a;
}

std::vector<std::string> streamArgs(const std::string& host, int players) {
    std::vector<std::string> a = base("xcb");
    // THE SAME RULE AS EVERY EMULATOR OF ITS OWN (vpad.h): Moonlight's SDL
    // sees only this console's virtual controllers, one per player, laid out
    // as the console says, and never the real pads' HID devices.
    std::string maps;
    for (int p = 0; p < std::min(std::max(players, 1), cab::vpad::kMaxPlayers); ++p)
        maps += cab::vpad::sdlMapping(p) + "\n";
    a.insert(a.end(), {
        "--env=SDL_JOYSTICK_HIDAPI=0",
        "--env=SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1209/0xCAB0",
        "--env=SDL_GAMECONTROLLERCONFIG=" + maps,
        kApp, "stream", host, "CabinetOS",
        "--display-mode", "fullscreen",
    });
    return a;
}

std::vector<std::string> pairedHosts() { return readLines(hostsFile()); }

bool isPaired(const std::string& host) {
    const std::vector<std::string> h = pairedHosts();
    return std::find(h.begin(), h.end(), host) != h.end();
}

void rememberPaired(const std::string& host) {
    std::vector<std::string> h = pairedHosts();
    h.erase(std::remove(h.begin(), h.end(), host), h.end());
    h.insert(h.begin(), host);
    if (h.size() > 8) h.resize(8);
    writeLines(hostsFile(), h);
}

void forget(const std::string& host) {
    std::vector<std::string> h = pairedHosts();
    h.erase(std::remove(h.begin(), h.end(), host), h.end());
    writeLines(hostsFile(), h);
}

std::string lastHost() {
    const std::vector<std::string> l = readLines(lastFile());
    return l.empty() ? std::string() : l.front();
}

void setLastHost(const std::string& host) { writeLines(lastFile(), {host}); }

std::string cleanHost(const std::string& typed) {
    std::string s;
    for (const char ch : typed)
        if (!std::isspace(static_cast<unsigned char>(ch))) s += ch;
    for (const char* scheme : {"http://", "https://"})
        if (s.rfind(scheme, 0) == 0) s.erase(0, std::char_traits<char>::length(scheme));
    while (!s.empty() && s.back() == '/') s.pop_back();
    // An address, a name or an IPv6 literal: nothing else, so nothing typed
    // can become a second argument or an option to Moonlight.
    for (const char ch : s)
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '.' && ch != '-' &&
            ch != ':' && ch != '[' && ch != ']')
            return {};
    if (!s.empty() && s.front() == '-') return {};
    return s;
}

}  // namespace moonlight
