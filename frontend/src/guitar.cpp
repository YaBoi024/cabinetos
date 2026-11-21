#include "guitar.h"

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "fileio.h"
#include "players.h"

namespace guitar {

namespace {

// ---- The layouts --------------------------------------------------------------

// Microsoft's Xbox 360 guitar (guitar.h, "The layouts", 1).
constexpr Layout kXbox360 = {"Xbox 360 guitar", Whammy::RightXFromMin, Tilt::RightYToMin};
// CRKD's Les Paul on its dial 1: whammy on the left trigger, tilt as Back held
// (by cable and receiver; nothing over Bluetooth). Measured on the A9,
// 2026-11-19 and 11-20.
constexpr Layout kCrkdDial1 = {"CRKD Les Paul, dial 1", Whammy::LeftTrigger, Tilt::Back};

// THE MEASURED ROWS: a guitar somebody connected and read, by its id and what
// tells it from another device with that id. "" and -1 match anything.
struct Row {
    uint16_t vendor;
    uint16_t product;
    const char* usbProduct;   // its USB product string
    const char* device;       // the kernel's name for its device
    int subtype;              // its XInput subtype
    const Layout* layout;
};
constexpr Row kRows[] = {
    // Cable and CRKD's receiver: Microsoft's own Xbox 360 pad id, xpad's
    // "Microsoft X-Box 360 pad"; only the USB product string says guitar, and
    // the descriptors say gamepad.
    {0x045e, 0x028e, "CRKD Guitar", "", 0x01, &kCrkdDial1},
    // Bluetooth, the firmware since 2026-11-20 (v3.3.0): the Xbox One S pad's
    // id, read through hidraw.
    {0x045e, 0x02e0, "", "CRKD Guitar XInput", -1, &kCrkdDial1},
    // Bluetooth, the firmware before: SDL's database calls the id an "Ipega
    // PG 9069"; the whammy is its brake axis, SDL's left trigger.
    {0x1949, 0x0402, "", "Guitar-Dongle", -1, &kCrkdDial1},
};

// Microsoft's XInput subtypes for a guitar: guitar, guitar alternate, bass.
constexpr int kGuitarSubtypes[] = {0x06, 0x07, 0x0B};

bool hasWordGuitar(const std::string& s) {
    std::string lower = s;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("guitar") != std::string::npos;
}

// ---- sysfs -----------------------------------------------------------------------

// A sysfs file whole. Not cab::readBytes: a sysfs file says it is bigger than
// what it holds (4096 bytes, or 65553 for `descriptors`), so it is read to
// its end instead.
std::vector<uint8_t> readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string readLine(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    return line;
}

bool exists(const std::string& path) { return ::access(path.c_str(), F_OK) == 0; }

std::string realDir(const std::string& path) {
    char buf[PATH_MAX];
    return ::realpath(path.c_str(), buf) ? std::string(buf) : std::string();
}

// From the pad's device in sysfs, up to the USB device it is part of: its
// interface's number on the way, then, at the USB device, its product string
// and its subtype. Only when that device's id is the pad's: a Bluetooth pad's
// parents end at the Bluetooth adapter, another device.
void readUsb(const std::string& from, Identity& id) {
    int iface = -1;
    std::string dir = from;
    for (int up = 0; up < 8 && !dir.empty() && dir != "/"; ++up) {
        if (iface < 0 && exists(dir + "/bInterfaceNumber"))
            iface = static_cast<int>(std::strtol(readLine(dir + "/bInterfaceNumber").c_str(), nullptr, 16));
        if (exists(dir + "/idVendor") && exists(dir + "/descriptors")) {
            const long vendor = std::strtol(readLine(dir + "/idVendor").c_str(), nullptr, 16);
            const long product = std::strtol(readLine(dir + "/idProduct").c_str(), nullptr, 16);
            if (vendor == id.vendor && product == id.product) {
                id.usbProduct = readLine(dir + "/product");
                id.subtype = xinputSubtype(readAll(dir + "/descriptors"), iface);
            }
            return;
        }
        dir = dir.substr(0, dir.find_last_of('/'));
    }
}

// The seats' tilts held for PCSX2, one per port.
TiltHold gPs2Hold[players::kMax];

}  // namespace

// ---- The rule ------------------------------------------------------------------

const Layout* match(const Identity& id) {
    for (const Row& r : kRows) {
        if (r.vendor != id.vendor || r.product != id.product) continue;
        if (*r.usbProduct && id.usbProduct != r.usbProduct) continue;
        if (*r.device && id.device != r.device) continue;
        if (r.subtype >= 0 && id.subtype != r.subtype) continue;
        return r.layout;
    }
    if (std::find(std::begin(kGuitarSubtypes), std::end(kGuitarSubtypes), id.subtype) !=
        std::end(kGuitarSubtypes))
        return &kXbox360;
    if (hasWordGuitar(id.usbProduct) || hasWordGuitar(id.device)) return &kXbox360;
    return nullptr;
}

int xinputSubtype(const std::vector<uint8_t>& d, int iface) {
    bool afterXboxInterface = false;
    for (size_t i = 0; i + 2 <= d.size();) {
        const size_t len = d[i];
        const uint8_t type = d[i + 1];
        if (len < 2 || i + len > d.size()) break;
        if (afterXboxInterface && type == 0x21 && len > 4) return d[i + 4];
        // An interface descriptor: its number at 2, class, subclass and
        // protocol at 5, 6 and 7. Xbox 360's is ff/5d/01.
        afterXboxInterface = type == 0x04 && len >= 9 && d[i + 5] == 0xFF && d[i + 6] == 0x5D &&
                             d[i + 7] == 0x01 && (iface < 0 || d[i + 2] == iface);
        i += len;
    }
    return -1;
}

Identity identify(uint16_t vendor, uint16_t product, const std::string& device, const char* path) {
    Identity id;
    id.vendor = vendor;
    id.product = product;
    id.device = device;
    const std::string p = path ? path : "";
    if (p.rfind("/dev/input/", 0) == 0)
        readUsb(realDir("/sys/class/input/" + p.substr(11) + "/device"), id);
    else if (p.rfind("/dev/hidraw", 0) == 0)
        readUsb(realDir("/sys/class/hidraw/" + p.substr(5) + "/device"), id);
    return id;
}

const Layout* seated(int player) {
    SDL_Gamepad* gp = players::gamepad(player);
    return gp ? players::guitar(SDL_GetGamepadID(gp)) : nullptr;
}

// ---- PlayStation 2 ----------------------------------------------------------------

float TiltHold::step(float level, uint64_t nowMs) {
    if (level >= kTilted) {
        lastOnMs = nowMs;
        heldLevel = level;
        return level;
    }
    if (heldLevel > 0.0f && nowMs >= lastOnMs && nowMs - lastOnMs < kTiltHoldMs)
        return std::max(level, heldLevel);
    heldLevel = 0.0f;
    return level;
}

cab::PadState ps2State(const Layout& layout, const cab::PadState& pad, TiltHold& hold,
                       uint64_t nowMs) {
    using cab::bit;
    constexpr uint32_t kKept = bit(cab::B) | bit(cab::A) | bit(cab::X) | bit(cab::Y) |
                               bit(cab::L) | bit(cab::Up) | bit(cab::Down) |
                               bit(cab::Select) | bit(cab::Start);
    cab::PadState out;
    out.buttons = pad.buttons & kKept;
    out.leftTrigger = std::clamp(layout.whammy == Whammy::RightXFromMin ? (pad.rightX + 1.0f) / 2.0f
                                                                        : pad.leftTrigger,
                                 0.0f, 1.0f);
    const float tilt =
        layout.tilt == Tilt::RightYToMin ? std::clamp(-pad.rightY, 0.0f, 1.0f) : 0.0f;
    if (hold.step(tilt, nowMs) >= kTilted) out.buttons |= bit(cab::L2);
    return out;
}

cab::PadState ps2(int port, const cab::PadState& pad) {
    if (port < 0 || port >= players::kMax) return pad;
    const Layout* layout = seated(port);
    return ps2State(layout ? *layout : kXbox360, pad, gPs2Hold[port], SDL_GetTicks());
}

// ---- PlayStation 3 ------------------------------------------------------------------

void Ps3Shape::reset(const Layout* layout) {
    layout_ = layout;
    release();
}

void Ps3Shape::release() {
    hold_ = {};
    tilt_ = 0.0f;
    r1_ = false;
}

void Ps3Shape::tiltTo(uint64_t nowMs, std::vector<Change>& out) {
    const bool on = hold_.step(tilt_, nowMs) >= kTilted;
    if (on == r1_) return;
    r1_ = on;
    out.push_back({false, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, on ? 1 : 0});
}

void Ps3Shape::axis(SDL_GamepadAxis a, int16_t value, uint64_t nowMs, std::vector<Change>& out) {
    if (!layout_) {
        out.push_back({true, a, value});
        return;
    }
    const bool fromRightX = layout_->whammy == Whammy::RightXFromMin;
    if (a == (fromRightX ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) {
        // From the centre, 0, to the right, 32767: from its minimum at rest,
        // or a trigger's 0.
        const int w = fromRightX ? (static_cast<int>(value) + 32768) / 2 : std::max(0, static_cast<int>(value));
        out.push_back({true, SDL_GAMEPAD_AXIS_RIGHTX, std::min(w, 32767)});
        return;
    }
    if (layout_->tilt == Tilt::RightYToMin && a == SDL_GAMEPAD_AXIS_RIGHTY) {
        tilt_ = std::clamp(-static_cast<float>(value) / 32768.0f, 0.0f, 1.0f);
        tiltTo(nowMs, out);
        return;
    }
    if (a == SDL_GAMEPAD_AXIS_RIGHTX || a == SDL_GAMEPAD_AXIS_RIGHTY) return;   // the shape's
    out.push_back({true, a, value});
}

void Ps3Shape::tick(uint64_t nowMs, std::vector<Change>& out) {
    if (layout_) tiltTo(nowMs, out);
}

float Ps3Shape::motionTilt(uint64_t nowMs) {
    return layout_ ? hold_.step(tilt_, nowMs) : 0.0f;
}

Rpcs3Guitar rpcs3For(const std::string& gameName) {
    std::string lower = gameName;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // The virtual controller sends yellow as BTN_NORTH and blue as BTN_WEST,
    // which RPCS3's evdev handler names "X" and "Y" (standalone_rpcs3.cpp,
    // rpcs3Controls). Guitar Hero's PS3 guitar has yellow on Square and blue
    // on Triangle, Rock Band's the other way round (PlasticBand, Guitar Hero
    // "PS3.md" and Rock Band "PS3 and Wii.md", a7b5ca30).
    if (lower.find("rock band") != std::string::npos) return {0x0200, "Y", "X"};
    return {0x0100, "X", "Y"};
}

std::string rpcs3Lines(const Rpcs3Guitar& g) {
    // Class 1, CELL_PAD_PCLASS_TYPE_GUITAR; the vendor is Sony Computer
    // Entertainment America's, 0x12BA, for both.
    return "    Device Class Type: 1\n    Vendor ID: " + std::to_string(0x12BA) +
           "\n    Product ID: " + std::to_string(g.product) + "\n";
}

// ---- --guitar-test ----------------------------------------------------------------

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    using cab::bit;
    auto named = [](const std::string& device) {
        Identity id;
        id.vendor = 0x1234;
        id.product = 0x5678;
        id.device = device;
        return id;
    };

    // ---- The rule, by name
    {
        bool all = true;
        for (const char* n : {"CRKD Guitar", "CRKD Guitar XP", "CRKD Guitar XInput", "Guitar-Dongle",
                              "RedOctane Guitar Hero X-plorer"})
            all = all && match(named(n)) == &kXbox360;
        check(all, "a device named CRKD Guitar, CRKD Guitar XP, CRKD Guitar XInput, Guitar-Dongle or "
                   "RedOctane Guitar Hero X-plorer, with no row of its own, is a guitar with the Xbox "
                   "360 layout");
        bool none = true;
        for (const char* n : {"Xbox 360 Controller", "Microsoft X-Box 360 pad", "Ipega PG 9069",
                              "Xbox One S Controller", "Xbox Wireless Controller", ""})
            none = none && match(named(n)) == nullptr;
        check(none, "Xbox 360 Controller, Microsoft X-Box 360 pad, Ipega PG 9069, Xbox One S "
                    "Controller and a nameless pad are not guitars");
        Identity id = named("");
        id.usbProduct = "Guitar Hero X-plorer";
        check(match(id) == &kXbox360, "the word in the USB product string is enough");
    }

    // ---- The measured rows
    {
        Identity cable;
        cable.vendor = 0x045e;
        cable.product = 0x028e;
        cable.device = "Microsoft X-Box 360 pad";
        cable.usbProduct = "CRKD Guitar";
        cable.subtype = 0x01;
        check(match(cable) == &kCrkdDial1,
              "CRKD's Les Paul on dial 1 by cable or receiver (045e:028e, \"CRKD Guitar\", subtype 1) "
              "plays with its whammy on the left trigger");
        Identity pad = cable;
        pad.usbProduct = "Controller";
        check(match(pad) == nullptr,
              "NOT A GUITAR: a Microsoft Xbox 360 pad, the same id and subtype, product \"Controller\"");
        Identity bt;
        bt.vendor = 0x045e;
        bt.product = 0x02e0;
        bt.device = "CRKD Guitar XInput";
        check(match(bt) == &kCrkdDial1, "dial 1 over Bluetooth, \"CRKD Guitar XInput\", likewise");
        bt.device = "CRKD Guitar";
        check(match(bt) == &kXbox360,
              "dial 8 over Bluetooth, \"CRKD Guitar\" on the same id, plays as Microsoft's guitar");
        bt.device = "Xbox Wireless Controller";
        check(match(bt) == nullptr, "NOT A GUITAR: an Xbox One S pad over Bluetooth");
        Identity dongle;
        dongle.vendor = 0x1949;
        dongle.product = 0x0402;
        dongle.device = "Guitar-Dongle";
        check(match(dongle) == &kCrkdDial1, "\"Guitar-Dongle\", 1949:0402, plays with dial 1's layout");
        Identity xplorer;
        xplorer.vendor = 0x1430;
        xplorer.product = 0x4748;
        xplorer.subtype = 0x07;
        check(match(xplorer) == &kXbox360,
              "an X-plorer's id with subtype 7, whatever its name (RedOctane's, untested on real "
              "hardware, and CRKD's on dial 8), plays as Microsoft's guitar");
    }

    // ---- The descriptors
    {
        // A device descriptor, a configuration, then Xbox 360 interfaces each
        // with its vendor descriptor. The guitar's is a Guitar Hero Live Xbox
        // 360 dongle's, from PlasticBand's dump (a7b5ca30); the gamepad's
        // begins as CRKD's receiver's did on dial 1 (measured 2026-11-19,
        // `10 21 10 01 01 24`), the rest Microsoft's pad's.
        const std::vector<uint8_t> device = {0x12, 0x01, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x08, 0x30,
                                             0x14, 0x0B, 0x07, 0x16, 0x01, 0x01, 0x02, 0x03, 0x01};
        const std::vector<uint8_t> config = {0x09, 0x02, 0x99, 0x00, 0x04, 0x01, 0x00, 0x80, 0x32};
        auto iface = [](uint8_t n, uint8_t cls, uint8_t sub, uint8_t proto) {
            return std::vector<uint8_t>{0x09, 0x04, n, 0x00, 0x02, cls, sub, proto, 0x00};
        };
        const std::vector<uint8_t> guitarVendor = {0x11, 0x21, 0x10, 0x01, 0x07, 0x25, 0x81, 0x14, 0x03,
                                                   0x03, 0x03, 0x04, 0x13, 0x01, 0x08, 0x03, 0x03};
        const std::vector<uint8_t> padVendor = {0x10, 0x21, 0x10, 0x01, 0x01, 0x24, 0x81, 0x14,
                                                0x03, 0x00, 0x03, 0x13, 0x01, 0x00, 0x03, 0x00};
        std::vector<uint8_t> drumVendor = guitarVendor;
        drumVendor[4] = 0x08;
        const std::vector<uint8_t> endpoint = {0x07, 0x05, 0x81, 0x03, 0x20, 0x00, 0x04};
        auto join = [](std::initializer_list<std::vector<uint8_t>> parts) {
            std::vector<uint8_t> out;
            for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
            return out;
        };
        const auto guitarDump = join({device, config, iface(0, 0xFF, 0x5D, 0x01), guitarVendor, endpoint});
        check(xinputSubtype(guitarDump, -1) == 0x07, "a guitar's descriptors give subtype 7");
        check(xinputSubtype(join({device, config, iface(0, 0xFF, 0x5D, 0x01), padVendor, endpoint}), -1) ==
                  0x01,
              "a gamepad's give 1");
        check(xinputSubtype(join({device, config, iface(0, 0xFF, 0x5D, 0x01), drumVendor, endpoint}), -1) ==
                  0x08,
              "a drum kit's give 8");
        Identity drums;
        drums.subtype = 0x08;
        check(match(drums) == nullptr, "NOT A GUITAR: a drum kit");
        const auto two = join({device, config, iface(0, 0xFF, 0x5D, 0x01), padVendor, endpoint,
                               iface(1, 0xFF, 0x5D, 0x01), guitarVendor, endpoint});
        check(xinputSubtype(two, 1) == 0x07 && xinputSubtype(two, 0) == 0x01 && xinputSubtype(two, -1) == 0x01,
              "with two Xbox 360 interfaces, each pad's own is read, or the first");
        check(xinputSubtype(join({device, config, iface(0, 0xFF, 0x5D, 0x81), guitarVendor}), -1) == -1 &&
                  xinputSubtype(join({device, config, iface(0, 0x03, 0x00, 0x00), guitarVendor}), -1) == -1,
              "no subtype from an interface that is not Xbox 360's wired one");
        check(xinputSubtype(join({device, config, iface(0, 0xFF, 0x5D, 0x01), endpoint, guitarVendor}), -1) ==
                  -1,
              "nor from a vendor descriptor that is not right after it");
        std::vector<uint8_t> cut = guitarDump;
        cut.resize(cut.size() - 20);
        check(xinputSubtype(cut, -1) == -1 && xinputSubtype({}, -1) == -1 &&
                  xinputSubtype({0x00, 0x21, 0x07}, -1) == -1,
              "cut short, empty or malformed descriptors give none");

        // The same read from sysfs, as a cable's pad is laid out there: its
        // input device under its USB interface, under its USB device.
        char tmpl[] = "/tmp/cabinetos-guitar-XXXXXX";
        const char* root = ::mkdtemp(tmpl);
        check(root != nullptr, "a scratch folder");
        if (root) {
            const std::string usb = std::string(root) + "/usb1/1-2";
            const std::string input = usb + "/1-2:1.0/input/input5";
            std::filesystem::create_directories(input);
            cab::writeBytes(usb + "/idVendor", std::string("1430\n"));
            cab::writeBytes(usb + "/idProduct", std::string("4748\n"));
            cab::writeBytes(usb + "/product", std::string("CRKD Guitar\n"));
            cab::writeBytes(usb + "/descriptors", guitarDump);
            cab::writeBytes(usb + "/1-2:1.0/bInterfaceNumber", std::string("00\n"));
            Identity id;
            id.vendor = 0x1430;
            id.product = 0x4748;
            readUsb(input, id);
            check(id.usbProduct == "CRKD Guitar" && id.subtype == 0x07,
                  "from a pad's input device in sysfs: its USB device's product string and subtype");
            Identity other;
            other.vendor = 0x045e;
            other.product = 0x02e0;
            readUsb(input, other);
            check(other.usbProduct.empty() && other.subtype == -1,
                  "and nothing from a USB device whose id is another's (a Bluetooth pad's adapter)");
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    }

    // ---- PS2: what PCSX2's guitar is handed
    {
        cab::PadState in;
        in.buttons = bit(cab::B) | bit(cab::X) | bit(cab::L) | bit(cab::Down) | bit(cab::Select) |
                     bit(cab::R) | bit(cab::L3) | bit(cab::L2);
        in.rightX = -1.0f;
        in.leftTrigger = 0.8f;
        TiltHold hold;
        cab::PadState out = ps2State(kXbox360, in, hold, 1000);
        check(out.buttons == (bit(cab::B) | bit(cab::X) | bit(cab::L) | bit(cab::Down) | bit(cab::Select)),
              "Microsoft's layout: green, yellow, orange, strum down and star power pass; R, L3 and a "
              "left trigger's L2 do not");
        check(out.leftTrigger == 0.0f, "the whammy at rest, the right stick at its left end, is 0");
        in.rightX = 1.0f;
        check(ps2State(kXbox360, in, hold, 1000).leftTrigger == 1.0f, "pushed all the way, 1");
        in.rightX = 0.0f;
        check(std::fabs(ps2State(kXbox360, in, hold, 1000).leftTrigger - 0.5f) < 0.001f, "halfway, a half");
        in.rightY = -1.0f;
        check(ps2State(kXbox360, in, hold, 1000).buttons & bit(cab::L2), "tilted, the L2 bit is on");

        cab::PadState d1;
        d1.buttons = bit(cab::A) | bit(cab::Y) | bit(cab::L2) | bit(cab::Select);
        d1.leftTrigger = 0.6f;
        d1.rightX = -1.0f;
        d1.rightY = -1.0f;
        TiltHold h1;
        const cab::PadState o1 = ps2State(kCrkdDial1, d1, h1, 1000);
        check(o1.leftTrigger == 0.6f && !(o1.buttons & bit(cab::L2)),
              "dial 1: the whammy is the left trigger, and its L2 bit is not tilt");
        check((o1.buttons & bit(cab::Select)) && (o1.buttons & bit(cab::A)) && (o1.buttons & bit(cab::Y)),
              "and its tilt, Back held, reaches the game as Select, star power");
    }

    // ---- The tilt's hold
    {
        TiltHold h;
        check(h.step(1.0f, 1000) == 1.0f, "a tilted reading is tilt");
        check(h.step(0.0f, 1050) == 1.0f && h.step(0.0f, 1299) == 1.0f,
              "the flicker after it is still tilt, for the hold");
        check(h.step(0.0f, 1300) == 0.0f, "and then it is let go");
        check(h.step(0.3f, 1400) == 0.3f, "a reading short of tilted is held by nothing");
        h.step(1.0f, 2000);
        h.step(0.0f, 2100);
        check(h.step(1.0f, 2200) == 1.0f && h.step(0.0f, 2450) == 1.0f && h.step(0.0f, 2500) == 0.0f,
              "the hold runs from the last tilted reading");
    }

    // ---- PS3: the shape on the virtual controller
    {
        auto one = [](const std::vector<Change>& c, bool isAxis, int code, int value) {
            return c.size() == 1 && c[0].isAxis == isAxis && c[0].code == code && c[0].value == value;
        };
        Ps3Shape s;
        s.reset(&kXbox360);
        std::vector<Change> c;
        s.axis(SDL_GAMEPAD_AXIS_RIGHTX, -32768, 0, c);
        check(one(c, true, SDL_GAMEPAD_AXIS_RIGHTX, 0), "the whammy at rest is the right stick's centre");
        c.clear();
        s.axis(SDL_GAMEPAD_AXIS_RIGHTX, 32767, 0, c);
        check(one(c, true, SDL_GAMEPAD_AXIS_RIGHTX, 32767), "pushed all the way, its right end");
        c.clear();
        s.axis(SDL_GAMEPAD_AXIS_RIGHTY, -32768, 1000, c);
        check(one(c, false, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 1) && std::fabs(s.motionTilt(1000) - 1.0f) < 0.001f,
              "tilted: R1 down, and the motion device's neck straight up");
        c.clear();
        s.axis(SDL_GAMEPAD_AXIS_RIGHTY, 0, 1050, c);
        s.tick(1200, c);
        check(c.empty() && s.motionTilt(1200) > 0.99f, "a flicker back to level is held");
        s.tick(1300, c);
        check(one(c, false, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0) && s.motionTilt(1300) == 0.0f,
              "and once the hold is over, R1 up and the neck level");
        c.clear();
        s.axis(SDL_GAMEPAD_AXIS_LEFTX, 12000, 1400, c);
        s.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 20000, 1400, c);
        check(c.size() == 2 && c[0].isAxis && c[0].code == SDL_GAMEPAD_AXIS_LEFTX && c[0].value == 12000 &&
                  c[1].code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER && c[1].value == 20000,
              "every other axis passes as it is");
        s.axis(SDL_GAMEPAD_AXIS_RIGHTY, -32768, 1500, c);
        s.release();
        check(s.motionTilt(1501) == 0.0f, "the pause menu lets the tilt go");

        Ps3Shape d1;
        d1.reset(&kCrkdDial1);
        c.clear();
        d1.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767, 0, c);
        check(one(c, true, SDL_GAMEPAD_AXIS_RIGHTX, 32767), "dial 1: the left trigger is the whammy, on the right stick");
        c.clear();
        d1.axis(SDL_GAMEPAD_AXIS_RIGHTX, -20000, 0, c);
        d1.axis(SDL_GAMEPAD_AXIS_RIGHTY, -32768, 0, c);
        check(c.empty() && d1.motionTilt(0) == 0.0f, "and its own right stick moves nothing");

        Ps3Shape pad;
        pad.reset(nullptr);
        c.clear();
        pad.axis(SDL_GAMEPAD_AXIS_RIGHTY, -32768, 0, c);
        pad.tick(5000, c);
        check(one(c, true, SDL_GAMEPAD_AXIS_RIGHTY, -32768) && pad.motionTilt(0) == 0.0f,
              "NOT A GUITAR: a pad's seat passes its axes as they are, and tilts nothing");
    }

    // ---- RPCS3's lines
    {
        const Rpcs3Guitar gh = rpcs3For("Guitar Hero: Warriors of Rock");
        const Rpcs3Guitar rb = rpcs3For("Rock Band 3");
        check(rpcs3Lines(gh) == "    Device Class Type: 1\n    Vendor ID: 4794\n    Product ID: 256\n",
              "a Guitar Hero game: class 1, RedOctane's guitar 0x12BA:0x0100");
        check(std::string(gh.square) == "X" && std::string(gh.triangle) == "Y",
              "with yellow on Square and blue on Triangle");
        check(rpcs3Lines(rb) == "    Device Class Type: 1\n    Vendor ID: 4794\n    Product ID: 512\n",
              "a Rock Band game: Harmonix's guitar 0x12BA:0x0200");
        check(std::string(rb.square) == "Y" && std::string(rb.triangle) == "X",
              "with yellow on Triangle and blue on Square, as a pad has them");
        check(rpcs3For("The Beatles: Rock Band").product == 0x0200 &&
                  rpcs3For("LEGO ROCK BAND").product == 0x0200 && rpcs3For("Band Hero").product == 0x0100,
              "\"Rock Band\" anywhere in the name, in any case, and nowhere else");
    }

    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace guitar
