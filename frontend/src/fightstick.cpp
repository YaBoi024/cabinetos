#include "fightstick.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "players.h"

namespace fightstick {

namespace {

// ---- The table ----------------------------------------------------------------

// THE KNOWN STICKS, one row per stick measured on a console.
constexpr Stick kSticks[] = {
    // 8BitDo Arcade Stick, over Bluetooth in X mode: the Xbox One S pad's own
    // id, so the kernel's device name tells it apart (an Xbox One S pad's is
    // "Xbox Wireless Controller"). SDL names it "Xbox One S Controller".
    // Measured on the A9, 2026-10-24. Wired in X mode it is a plain 045e:028e
    // "Microsoft X-Box 360 pad", in S mode a Switch Pro Controller, and its
    // 2.4G receiver was never measured: none of those is on the table.
    {0x045e, 0x02e0, "8BitDo Arcade Stick", "8bitdo-arcade-stick"},
};

// WHERE EACH OF THE EIGHT REACHES THE READER, as the RetroPad bit play.cpp's
// readGameInput makes of that button (SDL's position, the same for every
// pad). The 8BitDo Arcade Stick, measured on the A9 2026-10-24 by SDL and by
// the kernel, both agreeing: the top row is West, North, the right shoulder
// and the left; the bottom row South, East, the right trigger and the left
// (an axis at full travel, which the reader's half-travel line makes a
// button). One panel for every row; a stick wired another way would carry
// its own order in its row.
constexpr cab::Button kSlot[kSlots] = {cab::Y, cab::X, cab::R,  cab::L,
                                       cab::B, cab::A, cab::R2, cab::L2};

// MAME 2003-PLUS'S CLASSIC TABLE: which of the game's buttons each joypad id
// is, for a port given a plain joypad (its PAD_CLASSIC, which the console
// plugs in). EMIT_RETROPAD_CLASSIC in src/mame2003/mame2003.c at the pinned
// commit 21256d24120b04916c5197d95b757635ca880fd9 (cores/build-core.sh): B is
// button 1, A 2, Y 3, X 4, L 5, R 6, L2 7, R2 8. Its L3 and R3, buttons 9 and
// 10, are not counted. The core's build stops when a pin's table reads
// otherwise (cores/build-core.sh, the mame2003_plus block), so this is put
// right in the same change. The eight are also the ids a game's button may
// be on, whichever core.
constexpr cab::Button kClassic[] = {cab::B, cab::A, cab::Y,  cab::X,
                                    cab::L, cab::R, cab::L2, cab::R2};

enum class Order { Declared, Classic };

// The order a core's buttons are counted in, or none for a core that is not
// an arcade core (which plays the stick as a pad).
std::optional<Order> orderFor(const std::string& core) {
    if (core == "fbneo_libretro") return Order::Declared;
    if (core == "mame2003_plus") return Order::Classic;
    return std::nullopt;
}

// The button number a MAME 2003-Plus label carries, or 0: "Button 3", or
// "B3: Fierce Punch" (its BTN1 to BTN8 in src/controls.h).
int labelNumber(const std::string& s) {
    auto digit = [&](size_t i) {
        return i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]));
    };
    size_t from = 0;
    if (s.rfind("Button ", 0) == 0) from = 7;
    else if (s.size() > 1 && s[0] == 'B' && digit(1)) from = 1;
    else return 0;
    size_t to = from;
    while (digit(to)) ++to;
    if (to == from || to - from > 2) return 0;
    if (from == 7 ? to != s.size() : (to >= s.size() || s[to] != ':')) return 0;
    return std::atoi(s.substr(from, to - from).c_str());
}

// FBNEO'S OWN COMBINATION BUTTONS, which press two or three of the game's
// buttons at once: "Buttons AB", "Buttons 3x Punch" and the rest, named
// "P1 Buttons ..." in its src/burner/libretro/retro_input.cpp, where no
// game's own button is called that (its drivers, at the pin; the core's build
// stops when either changes, cores/build-core.sh). No cabinet had them,
// and on a stick two buttons are pressed together as on the cabinet, so
// they get no button (MMagTech at the TV, 2026-10-24).
bool fbneoCombination(const std::string& label) { return label.rfind("Buttons ", 0) == 0; }

// ---- The layout the game in play gives each port --------------------------------

struct Kept {
    bool made = false;
    SDL_JoystickID pad = 0;
    unsigned version = 0;
    std::string core;
    std::optional<Layout> layout;
};
Kept gKept[players::kMax];

// The layout line, each time one is made: what a person checking the stick
// needs, the game's own word for each button against where it went.
void say(int port, SDL_Gamepad* gp, const std::optional<Layout>& l) {
    const std::string who = kind(gp);
    if (!l) {
        std::fprintf(stderr,
                     "[fightstick] player %d (%s): the game names no buttons, so it plays "
                     "as a pad\n",
                     port + 1, who.c_str());
        return;
    }
    std::string rows;
    for (int s = 0; s < kSlots; ++s) {
        rows += s == 0 ? "top " : s == 4 ? "; bottom " : ", ";
        rows += std::to_string(s + 1) + " ";
        rows += l->id[s] == kNone ? std::string("-") : "\"" + l->label[s] + "\"";
    }
    std::fprintf(stderr, "[fightstick] player %d (%s), %d of the game's buttons: %s\n",
                 port + 1, who.c_str(), l->count, rows.c_str());
    for (const std::string& f : l->left)
        std::fprintf(stderr, "[fightstick] player %d: no button left for \"%s\"\n", port + 1,
                     f.c_str());
    for (const std::string& w : l->warnings)
        std::fprintf(stderr, "[fightstick] player %d: %s\n", port + 1, w.c_str());
}

}  // namespace

int classicNumber(unsigned id) {
    for (int i = 0; i < 8; ++i)
        if (id == kClassic[i]) return i + 1;
    return 0;
}

bool isArcadeCore(const std::string& core) { return orderFor(core).has_value(); }

bool readsClassic(const std::string& core) { return orderFor(core) == Order::Classic; }

const Stick* match(uint16_t vendor, uint16_t product, const std::string& device) {
    for (const Stick& s : kSticks)
        if (s.vendor == vendor && s.product == product && (!*s.device || device == s.device))
            return &s;
    return nullptr;
}

std::string kind(SDL_Gamepad* gp) {
    if (!gp) return {};
    if (const Stick* s = players::stick(SDL_GetGamepadID(gp))) return s->name;
    char guid[64] = {};
    SDL_GUIDToString(SDL_GetJoystickGUID(SDL_GetGamepadJoystick(gp)), guid, sizeof guid);
    return guid;
}

std::optional<Layout> make(const Stick* stick, const std::string& core,
                           const std::vector<cab::InputDescriptor>& port,
                           const std::vector<cab::InputDescriptor>& port0) {
    if (!stick) return std::nullopt;
    const std::optional<Order> order = orderFor(core);
    if (!order) return std::nullopt;
    std::vector<const cab::InputDescriptor*> f;
    for (const cab::InputDescriptor& d : port.empty() ? port0 : port)
        if (classicNumber(d.id) && !(*order == Order::Declared && fbneoCombination(d.label)))
            f.push_back(&d);
    if (f.empty()) return std::nullopt;
    if (*order == Order::Classic)
        std::stable_sort(f.begin(), f.end(), [](const auto* a, const auto* b) {
            return classicNumber(a->id) < classicNumber(b->id);
        });

    // Slots counted from 0: top row 0 to 3, bottom row 4 to 7.
    static constexpr int kMany[] = {0, 1, 2, 4, 5, 6, 3, 7};
    static constexpr int kFour[] = {4, 5, 6, 7};
    static constexpr int kFew[] = {4, 5, 7};
    Layout l;
    l.count = static_cast<int>(f.size());
    const int* to = l.count >= 5 ? kMany : l.count == 4 ? kFour : kFew;
    const int places = l.count >= 5 ? 8 : l.count;
    for (int k = 0; k < l.count; ++k) {
        if (k >= places) {
            l.left.push_back(f[k]->label);
            continue;
        }
        l.id[to[k]] = static_cast<int>(f[k]->id);
        l.label[to[k]] = f[k]->label;
    }
    // THE CLASSIC TABLE, CHECKED AGAINST THE CORE'S OWN WORDS. Nothing is
    // done with a label but this: a number that disagrees says the table in
    // the core moved since the pin it was read at.
    if (*order == Order::Classic)
        for (const cab::InputDescriptor* d : f)
            if (const int n = labelNumber(d->label); n && n != classicNumber(d->id))
                l.warnings.push_back("MAME 2003-Plus calls joypad id " + std::to_string(d->id) +
                                     " \"" + d->label + "\", button " + std::to_string(n) +
                                     ", where its Classic table read button " +
                                     std::to_string(classicNumber(d->id)));
    return l;
}

const Layout* forPort(int port, SDL_Gamepad* gp, const std::string& core) {
    if (port < 0 || port >= players::kMax || !gp) return nullptr;
    const SDL_JoystickID pad = SDL_GetGamepadID(gp);
    const Stick* stick = players::stick(pad);
    if (!stick || !orderFor(core)) return nullptr;
    const cab::Core& c = cab::Core::shared();
    Kept& k = gKept[port];
    if (!k.made || k.pad != pad || k.version != c.inputDescriptorsVersion() || k.core != core) {
        k = {true, pad, c.inputDescriptorsVersion(), core,
             make(stick, core, c.inputDescriptors(port), c.inputDescriptors(0))};
        // Nothing to say before the core has declared anything: FBNeo does
        // at its first frame, not at the load.
        if (k.layout || !c.inputDescriptors(port).empty() || !c.inputDescriptors(0).empty())
            say(port, gp, k.layout);
    }
    return k.layout ? &*k.layout : nullptr;
}

void apply(const Layout& layout, cab::PadState& pad) {
    uint32_t out = pad.buttons;
    for (const cab::Button b : kSlot) out &= ~cab::bit(b);
    for (int s = 0; s < kSlots; ++s)
        if ((pad.buttons & cab::bit(kSlot[s])) && layout.id[s] != kNone)
            out |= 1u << layout.id[s];
    pad.buttons = out;
    // The triggers' pull follows the ids too, for a core that reads L2 and
    // R2 as how far: the stick's buttons are all the way or not at all.
    pad.leftTrigger = (out & cab::bit(cab::L2)) ? 1.0f : 0.0f;
    pad.rightTrigger = (out & cab::bit(cab::R2)) ? 1.0f : 0.0f;
}

// ---- --fightstick-test --------------------------------------------------------------

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    using D = std::vector<cab::InputDescriptor>;
    const D none;
    const std::string fbneo = "fbneo_libretro", mame = "mame2003_plus";
    // The ids each slot sends, top row then bottom, kNone as -1.
    auto slots = [](const std::optional<Layout>& l) {
        return l ? std::vector<int>(l->id.begin(), l->id.end()) : std::vector<int>{};
    };
    auto ids = [](std::initializer_list<int> v) { return std::vector<int>(v); };
    constexpr int no = kNone;

    const Stick* stick = match(0x045e, 0x02e0, "8BitDo Arcade Stick");
    check(stick && std::strcmp(stick->name, "8bitdo-arcade-stick") == 0,
          "the 8BitDo Arcade Stick over Bluetooth is on the table");
    check(!match(0x045e, 0x02e0, "Xbox Wireless Controller"),
          "an Xbox One S pad, the same id under its own name, is not");
    check(!match(0x045e, 0x028e, "Microsoft X-Box 360 pad"), "the stick on a cable is not");
    check(!match(0x045e, 0x02e0, ""), "nor the stick's id with no name read");

    // Street Fighter II on FBNeo: the game's order, punches then kicks,
    // with the lever, Coin and Start among them as a core sends them.
    const D sf2 = {{cab::Select, "Coin"},          {cab::Start, "Start"},
                   {cab::Up, "Up"},                {cab::Down, "Down"},
                   {cab::Left, "Left"},            {cab::Right, "Right"},
                   {cab::Y, "Weak Punch"},         {cab::X, "Medium Punch"},
                   {cab::L, "Strong Punch"},       {cab::B, "Weak Kick"},
                   {cab::A, "Medium Kick"},        {cab::R, "Strong Kick"}};
    check(!make(nullptr, fbneo, sf2, sf2), "a pad not on the table has no layout");
    check(!make(stick, "snes9x_libretro", sf2, sf2), "nor has the stick in a game on another core");
    const std::optional<Layout> six = make(stick, fbneo, sf2, sf2);
    check(slots(six) == ids({cab::Y, cab::X, cab::L, no, cab::B, cab::A, cab::R, no}),
          "FBNeo, six buttons: punches on top, kicks below, in the game's order");
    check(six && six->label[0] == "Weak Punch" && six->label[6] == "Strong Kick" &&
              six->count == 6 && six->left.empty(),
          "and each keeps the core's own label");

    // MAME 2003-Plus sends them in RetroPad id order; its Classic table
    // puts them back in the game's.
    const D mame6 = {{cab::B, "B1: Jab Punch"},       {cab::Y, "B3: Fierce Punch"},
                     {cab::Select, "Coin"},           {cab::Start, "Start"},
                     {cab::A, "B2: Strong Punch"},    {cab::X, "B4: Short Kick"},
                     {cab::L, "B5: Forward Kick"},    {cab::R, "B6: Roundhouse Kick"}};
    const std::optional<Layout> m6 = make(stick, mame, mame6, mame6);
    check(slots(m6) == ids({cab::B, cab::A, cab::Y, no, cab::X, cab::L, cab::R, no}),
          "MAME 2003-Plus, six buttons: B A Y on top, X L R below (the Classic table)");
    check(m6 && m6->label[1] == "B2: Strong Punch" && m6->warnings.empty(),
          "its labels agree with the table, and nothing is said");

    check(slots(make(stick, fbneo, {{cab::B, "Button 1"}}, none)) ==
              ids({no, no, no, no, cab::B, no, no, no}),
          "one button: bottom row, first");
    check(slots(make(stick, mame, {{cab::B, "Button 1"}, {cab::A, "Button 2"}}, none)) ==
              ids({no, no, no, no, cab::B, cab::A, no, no}),
          "two buttons: bottom row, first and second");
    const D fb3 = {{cab::B, "Button 1"}, {cab::A, "Button 2"}, {cab::Y, "Button 3"}};
    check(slots(make(stick, fbneo, fb3, none)) == ids({no, no, no, no, cab::B, cab::A, no, cab::Y}),
          "three buttons on FBNeo: bottom row, the third after the gap");
    const D mame3 = {{cab::B, "Button 1"}, {cab::Y, "Button 3"}, {cab::A, "Button 2"}};
    check(slots(make(stick, mame, mame3, none)) == ids({no, no, no, no, cab::B, cab::A, no, cab::Y}),
          "three buttons on MAME 2003-Plus, sent B Y A: the same three places");
    const D neo = {{cab::B, "Button A"}, {cab::A, "Button B"}, {cab::Y, "Button C"},
                   {cab::X, "Button D"}};
    check(slots(make(stick, fbneo, neo, none)) == ids({no, no, no, no, cab::B, cab::A, cab::Y, cab::X}),
          "four buttons (Neo Geo): the bottom row, as on the cabinet's one row");
    const D mame4 = {{cab::B, "Button 1"}, {cab::Y, "Button 3"}, {cab::A, "Button 2"},
                     {cab::X, "Button 4"}};
    check(slots(make(stick, mame, mame4, none)) ==
              ids({no, no, no, no, cab::B, cab::A, cab::Y, cab::X}),
          "four on MAME 2003-Plus: the bottom row, in the table's order");
    // Metal Slug on FBNeo, as the A9 logged it: A to D and FBNeo's four
    // combination buttons on the shoulders and triggers.
    const D mslug = {{cab::B, "Button A"},     {cab::A, "Button B"},   {cab::Y, "Button C"},
                     {cab::X, "Button D"},     {cab::R, "Buttons AB"}, {cab::L, "Buttons CD"},
                     {cab::R2, "Buttons ABC"}, {cab::L2, "Buttons BCD"}};
    const std::optional<Layout> ms = make(stick, fbneo, mslug, none);
    check(ms && ms->count == 4 && ms->left.empty() &&
              slots(ms) == ids({no, no, no, no, cab::B, cab::A, cab::Y, cab::X}),
          "FBNeo's combination buttons are not the game's: Metal Slug is four, on the bottom row");
    D sf2x = sf2;
    sf2x.push_back({cab::L2, "Buttons 3x Punch"});
    sf2x.push_back({cab::R2, "Buttons 3x Kick"});
    check(slots(make(stick, fbneo, sf2x, none)) == slots(six),
          "nor are 3x Punch and 3x Kick: six buttons stay six");
    check(slots(make(stick, mame, {{cab::B, "Buttons AB"}}, none)) ==
              ids({no, no, no, no, cab::B, no, no, no}),
          "a MAME 2003-Plus label is never read that way");
    const D five = {{cab::Y, "1"}, {cab::X, "2"}, {cab::L, "3"}, {cab::B, "4"}, {cab::A, "5"}};
    check(slots(make(stick, fbneo, five, none)) ==
              ids({cab::Y, cab::X, cab::L, no, cab::B, cab::A, no, no}),
          "five buttons: three on top, two below");
    const D eight = {{cab::Y, "1"}, {cab::X, "2"}, {cab::L, "3"}, {cab::B, "4"},
                     {cab::A, "5"}, {cab::R, "6"}, {cab::L2, "7"}, {cab::R2, "8"}};
    check(slots(make(stick, fbneo, eight, none)) ==
              ids({cab::Y, cab::X, cab::L, cab::L2, cab::B, cab::A, cab::R, cab::R2}),
          "eight buttons: the seventh top right, the eighth bottom right");
    const D mame8 = {{cab::B, "B1: a"},  {cab::Y, "B3: c"},  {cab::A, "B2: b"},  {cab::X, "B4: d"},
                     {cab::L, "B5: e"},  {cab::R, "B6: f"},  {cab::L2, "B7: g"}, {cab::R2, "B8: h"}};
    check(slots(make(stick, mame, mame8, none)) ==
              ids({cab::B, cab::A, cab::Y, cab::L2, cab::X, cab::L, cab::R, cab::R2}),
          "eight on MAME 2003-Plus, in the table's order");
    D clicks = eight;
    clicks.push_back({cab::L3, "Button 9"});
    clicks.push_back({cab::R3, "Button 10"});
    const std::optional<Layout> c = make(stick, fbneo, clicks, none);
    check(c && c->count == 8 && c->left.empty(),
          "buttons on L3 and R3 are not counted: the stick has no clicks");
    D nine = eight;
    nine.push_back({cab::B, "9"});
    const std::optional<Layout> n9 = make(stick, fbneo, nine, none);
    check(n9 && n9->count == 9 && slots(n9) == slots(make(stick, fbneo, eight, none)) &&
              n9->left == std::vector<std::string>{"9"},
          "a ninth gets no button, and is named for the log");

    check(slots(make(stick, fbneo, none, sf2)) == slots(six),
          "a port the core said nothing about takes port 0's");
    check(!make(stick, fbneo, none, none), "no buttons named anywhere: no layout");
    check(!make(stick, fbneo, {{cab::Up, "Up"}, {cab::Start, "Start"}}, none),
          "only the lever and Start named: no layout");

    const std::optional<Layout> wrong =
        make(stick, mame, {{cab::B, "B2: Strong Punch"}, {cab::A, "Button 1"}, {cab::Y, "Coin"}},
             none);
    check(wrong && wrong->warnings.size() == 2,
          "a MAME label whose number is not the table's is logged");
    check(labelNumber("B12: x") == 12 && labelNumber("Button 3") == 3 &&
              labelNumber("Button 3 (Fire)") == 0 && labelNumber("Bomb") == 0 &&
              labelNumber("B3") == 0,
          "and only a label that reads Button N, or starts BN:, carries a number");

    if (m6) {
        cab::PadState p;
        p.buttons = cab::bit(cab::Y) | cab::bit(cab::Up) | cab::bit(cab::Start);
        apply(*m6, p);
        check(p.buttons == (cab::bit(cab::B) | cab::bit(cab::Up) | cab::bit(cab::Start)),
              "the top left button sends button 1; the lever and Start pass as they are");
        p.buttons = cab::bit(cab::R2) | cab::bit(cab::L);
        apply(*m6, p);
        check(p.buttons == cab::bit(cab::R) && p.leftTrigger == 0.0f && p.rightTrigger == 0.0f,
              "the bottom row's third sends button 6, and the top right sends nothing");
    }
    if (const std::optional<Layout> e = make(stick, fbneo, eight, none)) {
        cab::PadState p;
        p.buttons = cab::bit(cab::L2);
        apply(*e, p);
        check(p.buttons == cab::bit(cab::R2) && p.rightTrigger == 1.0f && p.leftTrigger == 0.0f,
              "a button that sends a trigger's id pulls that trigger all the way");
    }

    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace fightstick
