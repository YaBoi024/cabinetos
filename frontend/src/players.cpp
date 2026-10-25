#include "players.h"

#include "battery.h"
#include "fightstick.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>

namespace players {

// ---- The rules --------------------------------------------------------------

int Seats::playerOf(uint32_t id) const {
    if (id == 0) return -1;
    for (size_t i = 0; i < seats_.size(); ++i)
        if (seats_[i].id == id) return static_cast<int>(i);
    return -1;
}

uint32_t Seats::idOf(int player) const {
    if (player < 0 || player >= static_cast<int>(seats_.size())) return 0;
    return seats_[player].id;
}

int Seats::add(uint32_t id, const std::string& key) {
    if (id == 0) return -1;
    if (const int p = playerOf(id); p >= 0) return p;
    // A pad coming back to the game it left takes its own number back. Known
    // by its key, never by position: two people's pads can both be away.
    if (!key.empty())
        for (size_t i = 0; i < seats_.size(); ++i)
            if (seats_[i].id == 0 && seats_[i].key == key) {
                seats_[i].id = id;
                return static_cast<int>(i);
            }
    if (seats_.size() < static_cast<size_t>(kMax)) {
        seats_.push_back({id, key});
        return static_cast<int>(seats_.size()) - 1;
    }
    // Four seats and one held for a pad that has not come back: a fifth pad
    // takes it rather than being left out of the game.
    for (size_t i = 0; i < seats_.size(); ++i)
        if (seats_[i].id == 0) {
            seats_[i] = {id, key};
            return static_cast<int>(i);
        }
    return -1;
}

void Seats::remove(uint32_t id) {
    const int p = playerOf(id);
    if (p < 0) return;
    if (!inGame_) {
        seats_.erase(seats_.begin() + p);
        return;
    }
    seats_[p].id = 0;
    const bool anyOn = std::any_of(seats_.begin(), seats_.end(),
                                   [](const Seat& s) { return s.id != 0; });
    if (!anyOn) seats_.clear();
}

void Seats::closeUp() {
    seats_.erase(std::remove_if(seats_.begin(), seats_.end(),
                                [](const Seat& s) { return s.id == 0; }),
                 seats_.end());
}

void Seats::setInGame(bool on) {
    inGame_ = on;
    if (!on) closeUp();
}

bool Seats::swap(int a, int b) {
    if (inGame_) return false;
    const int n = static_cast<int>(seats_.size());
    if (a < 0 || b < 0 || a >= n || b >= n) return false;
    if (seats_[a].id == 0 || seats_[b].id == 0) return false;
    std::swap(seats_[a], seats_[b]);
    return true;
}

// ---- The console's pads -----------------------------------------------------

namespace {

Seats gSeats;     // the console's own pads
Seats gRemote;    // pads that came through a stream (setStreaming)
std::atomic<bool> gStreaming{false};   // read by setAside, on that thread too
std::map<SDL_JoystickID, bool> gIsRemote;
// gIsRemote IS READ ON ANOTHER THREAD (#483, EMU-11): SDL runs the event
// filter, and so setAside, on the thread that pushes the event, which for
// a Wii Remote is its reader. Held only around the map, never an SDL call.
std::mutex gIsRemoteLock;
std::map<SDL_JoystickID, SDL_Gamepad*> gOpen;
std::map<SDL_JoystickID, std::string> gDir;   // its HID device, for its battery
std::map<SDL_JoystickID, const fightstick::Stick*> gStick;   // the open fight sticks

// The seats a game hears now.
Seats& active() { return gStreaming ? gRemote : gSeats; }
int gGeneration = 0;

// WHAT A PAD IS, ACROSS A RECONNECT. The joystick id is new every time a pad
// connects, so it cannot say "this is the pad that was player one". The
// serial can: over Bluetooth it is the pad's own address. The device path is
// the fallback for a pad with no serial, which holds for a wired pad put back
// in the same socket.
std::string addressOf(SDL_Gamepad* gp);

std::string keyOf(SDL_Gamepad* gp) {
    if (std::string a = addressOf(gp); !a.empty()) return "bt:" + a;
    if (const char* s = SDL_GetGamepadSerial(gp); s && *s) return std::string("serial:") + s;
    if (const char* p = SDL_GetGamepadPath(gp); p && *p) return std::string("path:") + p;
    return {};
}

// "AA:BB:CC:DD:EE:01" from "aa-bb-cc-dd-ee-01" or "aa:bb:cc:dd:ee:01"; empty
// for anything that is not an address (a wired pad's "000000000003").
std::string asAddress(const std::string& s) {
    if (s.size() != 17) return {};
    std::string out;
    for (int i = 0; i < 17; ++i) {
        const char c = s[static_cast<size_t>(i)];
        if (i % 3 == 2) {
            if (c != '-' && c != ':') return {};
            out += ':';
        } else {
            if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

std::string firstLine(const std::string& path, const char* prefix = nullptr) {
    std::ifstream f(path);
    for (std::string line; std::getline(f, line);) {
        if (!prefix) return line;
        if (line.rfind(prefix, 0) == 0) return line.substr(std::strlen(prefix));
    }
    return {};
}

// A PAD'S BLUETOOTH ADDRESS. SDL's serial has it when SDL drives the pad
// itself (a Switch pad through hidraw: "aa-bb-cc-dd-ee-01"), and not when the
// kernel does: an Xbox pad and an 8BitDo Lite 2 came through with no serial,
// so their rows had no Forget (MMagTech, 2026-09-26). The kernel always keeps
// it, as the device's "uniq", so that is asked second.
std::string addressOf(SDL_Gamepad* gp) {
    if (const char* s = SDL_GetGamepadSerial(gp); s)
        if (std::string a = asAddress(s); !a.empty()) return a;
    const char* p = SDL_GetGamepadPath(gp);
    if (!p) return {};
    const std::string path = p;
    std::string uniq;
    if (path.rfind("/dev/input/", 0) == 0)
        uniq = firstLine("/sys/class/input/" + path.substr(11) + "/device/uniq");
    else if (path.rfind("/dev/hidraw", 0) == 0)
        uniq = firstLine("/sys/class/hidraw/" + path.substr(5) + "/device/uevent", "HID_UNIQ=");
    return asAddress(uniq);
}

// The name the kernel gives the pad's device, which SDL does not pass on: an
// input device's name, or a HID device's HID_NAME.
std::string deviceName(SDL_Gamepad* gp) {
    const char* p = SDL_GetGamepadPath(gp);
    if (!p) return {};
    const std::string path = p;
    if (path.rfind("/dev/input/", 0) == 0)
        return firstLine("/sys/class/input/" + path.substr(11) + "/device/name");
    if (path.rfind("/dev/hidraw", 0) == 0)
        return firstLine("/sys/class/hidraw/" + path.substr(5) + "/device/uevent", "HID_NAME=");
    return {};
}

// A PAD THAT CAME THROUGH A STREAM: Sunshine's virtual controller, whose
// device is named "Sunshine (libvirtualhid) X-Box Series Controller" (read on
// the A9, 2026-10-07). SDL names it "Xbox Series X Controller", like a real
// one, so the device's own name is read.
bool isRemote(SDL_Gamepad* gp) {
    const std::string name = deviceName(gp);
    return name.find("Sunshine") != std::string::npos ||
           name.find("libvirtualhid") != std::string::npos;
}

// The name the pad gives. A pad in its Switch mode says it is a Switch Pro
// Controller, and that is what is shown: an attempt to name such a pad after
// its maker (from its Bluetooth address) was built and taken back out the
// same day. MMagTech, 2026-09-26: in Switch mode, that is what it should show.
std::string nameOf(SDL_Gamepad* gp) {
    const char* n = SDL_GetGamepadName(gp);
    return n ? n : "";
}

// The player lights on pads that have them (a Switch Pro Controller's four
// dots, a DualSense's bar) show the number the console gave it.
void light() {
    for (const auto& [id, gp] : gOpen) {
        const int p = active().playerOf(id);
        SDL_SetGamepadPlayerIndex(gp, p >= 0 ? p : -1);
    }
}

void changed(const char* what, SDL_JoystickID id) {
    ++gGeneration;
    light();
    std::string order = gStreaming ? "streaming: " : "";
    for (size_t i = 0; i < active().seats().size(); ++i) {
        const Seat& s = active().seats()[i];
        char buf[48];
        std::snprintf(buf, sizeof buf, "%s%zu=%s", i ? " " : "", i + 1,
                      s.id ? std::to_string(s.id).c_str() : "held");
        order += buf;
    }
    if (id)
        std::fprintf(stderr, "[players] %s pad %u; players: %s\n", what,
                     static_cast<unsigned>(id), order.empty() ? "none" : order.c_str());
    else
        std::fprintf(stderr, "[players] %s; players: %s\n", what,
                     order.empty() ? "none" : order.c_str());
}

}  // namespace

void loadMappings() {
    const char* env = std::getenv("CABINETOS_PAD_DB");
    const std::string path = env && *env ? env : "/usr/share/cabinetos/gamecontrollerdb.txt";
    const int n = SDL_AddGamepadMappingsFromFile(path.c_str());
    if (n < 0)
        std::fprintf(stderr, "[players] no controller list at %s: %s\n", path.c_str(),
                     SDL_GetError());
    else
        std::fprintf(stderr, "[players] %d controller mappings from %s\n", n, path.c_str());
}

void added(SDL_JoystickID id) {
    if (gOpen.count(id)) return;
    SDL_Gamepad* gp = SDL_OpenGamepad(id);
    if (!gp) {
        std::fprintf(stderr, "[players] could not open pad %u: %s\n",
                     static_cast<unsigned>(id), SDL_GetError());
        return;
    }
    gOpen[id] = gp;
    const bool remote = isRemote(gp);
    {
        std::lock_guard<std::mutex> lk(gIsRemoteLock);
        gIsRemote[id] = remote;
    }
    gDir[id] = battery::deviceDir(SDL_GetGamepadPath(gp));
    (remote ? gRemote : gSeats).add(id, keyOf(gp));
    std::fprintf(stderr, "[players] pad %u is %s%s%s%s\n", static_cast<unsigned>(id),
                 nameOf(gp).c_str(), addressOf(gp).empty() ? "" : " over Bluetooth ",
                 addressOf(gp).c_str(), remote ? ", through a stream" : "");
    if (const fightstick::Stick* s = fightstick::match(SDL_GetGamepadVendor(gp),
                                                       SDL_GetGamepadProduct(gp), deviceName(gp))) {
        gStick[id] = s;
        std::fprintf(stderr, "[players] pad %u is a fight stick on the table: %s\n",
                     static_cast<unsigned>(id), s->name);
    }
    changed("connected", id);
}

void removed(SDL_JoystickID id) {
    auto it = gOpen.find(id);
    if (it == gOpen.end()) return;
    SDL_CloseGamepad(it->second);
    gOpen.erase(it);
    gSeats.remove(id);
    gRemote.remove(id);
    {
        std::lock_guard<std::mutex> lk(gIsRemoteLock);
        gIsRemote.erase(id);
    }
    gDir.erase(id);
    gStick.erase(id);
    changed("disconnected", id);
}

void setInGame(bool on) {
    if (gSeats.inGame() == on) return;
    gSeats.setInGame(on);
    gRemote.setInGame(on);
    changed(on ? "game started" : "game ended", 0);
}

bool swap(int a, int b) {
    if (!active().swap(a, b)) return false;
    changed("swapped", 0);
    return true;
}

void setStreaming(bool on) {
    if (gStreaming == on) return;
    gStreaming = on;
    changed(on ? "stream started" : "stream ended", 0);
}

bool streaming() { return gStreaming; }

bool setAside(SDL_JoystickID id) {
    std::lock_guard<std::mutex> lk(gIsRemoteLock);
    auto it = gIsRemote.find(id);
    return it != gIsRemote.end() && it->second != gStreaming;
}

SDL_Gamepad* gamepad(int player) {
    const uint32_t id = active().idOf(player);
    if (!id) return nullptr;
    auto it = gOpen.find(id);
    return it == gOpen.end() ? nullptr : it->second;
}

const fightstick::Stick* stick(SDL_JoystickID id) {
    auto it = gStick.find(id);
    return it == gStick.end() ? nullptr : it->second;
}

int playerOf(SDL_JoystickID id) { return active().playerOf(id); }

int count() { return static_cast<int>(active().seats().size()); }

std::vector<Pad> connected() {
    std::vector<Pad> out;
    for (size_t i = 0; i < active().seats().size(); ++i) {
        const uint32_t id = active().seats()[i].id;
        auto it = id ? gOpen.find(id) : gOpen.end();
        if (it == gOpen.end()) continue;
        Pad p;
        p.player = static_cast<int>(i);
        p.id = id;
        p.name = nameOf(it->second);
        p.address = addressOf(it->second);
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<Held> held() {
    std::vector<Held> out;
    for (size_t i = 0; i < active().seats().size(); ++i) {
        const Seat& s = active().seats()[i];
        auto it = s.id ? gOpen.find(s.id) : gOpen.end();
        if (it == gOpen.end()) continue;
        Held h;
        h.player = static_cast<int>(i);
        h.key = s.key;
        h.dir = gDir[s.id];
        h.gp = it->second;
        {
            std::lock_guard<std::mutex> lk(gIsRemoteLock);
            h.stream = gIsRemote[s.id];
        }
        out.push_back(std::move(h));
    }
    return out;
}

int generation() { return gGeneration; }

// ---- --players-test ---------------------------------------------------------

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    {
        Seats s;
        check(s.add(10, "a") == 0, "the first pad is player 1");
        check(s.add(11, "b") == 1, "the second is player 2");
        check(s.add(10, "a") == 0, "a pad already seated keeps its seat");
        s.remove(10);
        check(s.playerOf(11) == 0, "outside a game, player 1 going off makes player 2 player 1");
        s.remove(11);
        check(s.add(12, "b") == 0,
              "both off, and the pad that was player 2 is picked up: it is player 1");
    }
    {
        Seats s;
        s.add(10, "a");
        s.add(11, "b");
        s.setInGame(true);
        s.remove(10);
        check(s.playerOf(11) == 1, "in a game, player 1 going off leaves player 2 as player 2");
        check(s.idOf(0) == 0, "and player 1's seat is held empty");
        check(s.add(20, "a") == 0, "player 1's pad comes back, under a new id, as player 1");
        s.remove(11);
        check(s.add(21, "c") == 2, "a different pad does not take a held seat");
        s.setInGame(false);
        check(s.seats().size() == 2 && s.playerOf(21) == 1,
              "leaving the game closes up the held seat");
    }
    {
        Seats s;
        s.add(10, "a");
        s.add(11, "b");
        s.setInGame(true);
        s.remove(10);
        s.remove(11);
        check(s.seats().empty(), "in a game, every pad off frees every seat");
        check(s.add(22, "b") == 0, "and the first pad back is player 1, whichever it was");
        check(s.add(23, "a") == 1, "and the other is player 2");
    }
    {
        Seats s;
        s.add(10, "");
        s.setInGame(true);
        s.add(11, "");
        s.remove(10);
        check(s.add(12, "") == 2, "a pad with no key never claims a held seat");
    }
    {
        Seats s;
        for (uint32_t i = 1; i <= 4; ++i) s.add(i, "k" + std::to_string(i));
        check(s.add(5, "k5") == -1, "a fifth pad has no player");
        s.setInGame(true);
        s.remove(2);
        check(s.add(5, "k5") == 1, "but takes a held seat when all four are taken");
    }
    {
        Seats s;
        s.add(10, "a");
        s.add(11, "b");
        check(s.swap(0, 1) && s.playerOf(11) == 0 && s.playerOf(10) == 1,
              "giving player 2's pad the number 1 swaps the two");
        s.add(12, "c");
        s.remove(12);
        check(s.playerOf(11) == 0, "and the swap holds while the numbers close up");
        check(!s.swap(0, 5), "a number with no pad is refused");
        s.setInGame(true);
        check(!s.swap(0, 1), "a swap during a game is refused");
    }

    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

// ---- --pads -----------------------------------------------------------------

int report(int seconds) {
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    loadMappings();
    auto describe = [](SDL_JoystickID id) {
        const bool known = SDL_IsGamepad(id);
        std::printf("pad %u  %s\n", static_cast<unsigned>(id),
                    known ? "known to SDL" : "NOT KNOWN TO SDL (joystick only)");
        std::printf("  name     %s\n", SDL_GetJoystickNameForID(id) ? SDL_GetJoystickNameForID(id) : "");
        std::printf("  path     %s\n", SDL_GetJoystickPathForID(id) ? SDL_GetJoystickPathForID(id) : "");
        char guid[64];
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof guid);
        std::printf("  guid     %s\n  vid:pid  %04x:%04x\n", guid,
                    SDL_GetJoystickVendorForID(id), SDL_GetJoystickProductForID(id));
        if (known) {
            if (SDL_Gamepad* gp = SDL_OpenGamepad(id)) {
                const char* serial = SDL_GetGamepadSerial(gp);
                const SDL_JoystickConnectionState cs = SDL_GetGamepadConnectionState(gp);
                std::printf("  serial   %s\n  link     %s\n  type     %d\n", serial ? serial : "(none)",
                            cs == SDL_JOYSTICK_CONNECTION_WIRELESS ? "wireless"
                            : cs == SDL_JOYSTICK_CONNECTION_WIRED  ? "wired"
                                                                   : "unknown",
                            static_cast<int>(SDL_GetGamepadType(gp)));
                // Whether SDL can drive its motors, which is all rumble.h
                // can use (#149): a pad that has motors but whose driver
                // SDL does not know how to talk to reads "no" here.
                std::printf("  rumble   %s\n",
                            SDL_GetBooleanProperty(SDL_GetGamepadProperties(gp),
                                                   SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)
                                ? "yes"
                                : "no");
                char* m = SDL_GetGamepadMapping(gp);
                std::printf("  mapping  %s\n", m ? m : "(none)");
                SDL_free(m);
            }
        } else {
            SDL_OpenJoystick(id);
        }
    };
    const Uint64 end = SDL_GetTicks() + static_cast<Uint64>(seconds) * 1000;
    while (SDL_GetTicks() < end) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_JOYSTICK_ADDED: describe(e.jdevice.which); break;
                case SDL_EVENT_JOYSTICK_REMOVED:
                    std::printf("pad %u gone\n", static_cast<unsigned>(e.jdevice.which));
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    std::printf("pad %u pressed %s\n", static_cast<unsigned>(e.gbutton.which),
                                SDL_GetGamepadStringForButton(
                                    static_cast<SDL_GamepadButton>(e.gbutton.button)));
                    break;
                case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
                    if (!SDL_IsGamepad(e.jbutton.which))
                        std::printf("pad %u raw button %d\n", static_cast<unsigned>(e.jbutton.which),
                                    e.jbutton.button);
                    break;
                default: break;
            }
            std::fflush(stdout);
        }
        SDL_Delay(10);
    }
    SDL_Quit();
    return 0;
}

}  // namespace players
