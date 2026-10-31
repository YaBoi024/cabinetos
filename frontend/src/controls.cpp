#include "controls.h"

#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <set>

#include <json-c/json.h>

#include "fileio.h"
#include "players.h"
#include "storage.h"

namespace controls {

namespace {

// A word in the file that is no control at all ("Start", "Up", "7").
constexpr int kNotAWord = -2;

bool isControl(int c) {
    for (const cab::Button b : kControl)
        if (c == static_cast<int>(b)) return true;
    return false;
}

// Every RetroPad id as the file and the log write it: the ten controls, and
// the six that are never rows, for the log's hidden list.
const char* idWord(unsigned id) {
    switch (id) {
        case cab::B: return "B";
        case cab::Y: return "Y";
        case cab::Select: return "Select";
        case cab::Start: return "Start";
        case cab::Up: return "Up";
        case cab::Down: return "Down";
        case cab::Left: return "Left";
        case cab::Right: return "Right";
        case cab::A: return "A";
        case cab::X: return "X";
        case cab::L: return "L";
        case cab::R: return "R";
        case cab::L2: return "L2";
        case cab::R2: return "R2";
        case cab::L3: return "L3";
        case cab::R3: return "R3";
        default: return "?";
    }
}

// Where `def` puts each row: the control that sends its id, or kNone.
std::vector<int> defaults(const std::vector<Row>& rows, const Map& def) {
    std::vector<int> a(rows.size(), kNone);
    for (size_t i = 0; i < rows.size(); ++i)
        for (const cab::Button c : kControl)
            if (def[c] == static_cast<int>(rows[i].id)) {
                a[i] = c;
                break;
            }
    return a;
}

int rowOf(const std::vector<Row>& rows, unsigned id) {
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].id == id) return static_cast<int>(i);
    return -1;
}

bool injective(const std::vector<int>& a) {
    std::array<bool, 16> used{};
    for (const int c : a) {
        if (c == kNone) continue;
        if (!isControl(c) || used[c]) return false;
        used[c] = true;
    }
    return true;
}

// ONE PRESS: row `i` takes control `c`, and the row that had it, if any,
// takes row i's old one, which may be none.
void step(std::vector<int>& a, int i, int c) {
    for (size_t j = 0; j < a.size(); ++j)
        if (static_cast<int>(j) != i && a[j] == c) a[j] = a[i];
    a[i] = c;
}

// The order a person reads MAME 2003-Plus's buttons in: its Classic table,
// then its buttons 9 and 10 on the stick clicks.
int classicOrder(unsigned id) {
    if (const int n = fightstick::classicNumber(id)) return n;
    return id == cab::L3 ? 9 : 10;
}

}  // namespace

bool isTarget(int control) { return isControl(control) && control != cab::L3 && control != cab::R3; }

const char* word(int control) { return isControl(control) ? idWord(control) : "None"; }

int fromWord(const std::string& w) {
    if (w == "None") return kNone;
    for (const cab::Button c : kControl)
        if (w == idWord(c)) return c;
    return kNotAWord;
}

std::string trimLabel(const std::string& s) {
    if (s.size() < 2 || s[0] != 'B') return s;
    size_t i = 1;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    const size_t digits = i - 1;
    // One or two digits, a colon, a space, and words after them.
    if (digits < 1 || digits > 2 || i + 2 >= s.size() || s[i] != ':' || s[i + 1] != ' ') return s;
    return s.substr(i + 2);
}

Rows rowsFrom(const std::string& core, const std::vector<cab::InputDescriptor>& port0) {
    Rows out;
    std::array<bool, 16> seen{};
    for (const cab::InputDescriptor& d : port0) {
        // A second button on the same id is the same button to the core:
        // one row, and the other said in the log. FLYCAST'S L3 AND R3 ARE A
        // BOARD'S TEST AND SERVICE, which never reach a game but by holding
        // Start (arcadesettings.h): no rows.
        const bool board = core == "flycast" && (d.id == cab::L3 || d.id == cab::R3);
        if (d.id >= 16 || !isControl(static_cast<int>(d.id)) || seen[d.id] || board) {
            out.hidden.push_back(d);
            continue;
        }
        seen[d.id] = true;
        out.rows.push_back({d.id, trimLabel(d.label)});
    }
    if (fightstick::readsClassic(core))
        std::stable_sort(out.rows.begin(), out.rows.end(), [](const Row& a, const Row& b) {
            return classicOrder(a.id) < classicOrder(b.id);
        });
    return out;
}

Map plain() {
    Map m;
    m.fill(kNone);
    for (const cab::Button c : kControl) m[c] = c;
    return m;
}

Map fromLayout(const fightstick::Layout& layout) {
    Map m;
    m.fill(kNone);
    for (const cab::Button c : kControl) {
        cab::PadState p;
        p.buttons = cab::bit(c);
        fightstick::apply(layout, p);
        for (const cab::Button id : kControl)
            if (p.buttons & cab::bit(id)) m[c] = id;
    }
    return m;
}

std::vector<int> assignment(const std::vector<Row>& rows, const Map& def, const Changes& ch) {
    std::vector<int> a = defaults(rows, def);
    std::vector<int> direct = a;
    for (const auto& [id, c] : ch) {
        const int i = rowOf(rows, id);
        if (i >= 0 && (c == kNone || isControl(c))) direct[i] = c;
    }
    if (injective(direct)) return direct;
    for (const auto& [id, c] : ch) {
        const int i = rowOf(rows, id);
        if (i < 0) continue;
        if (c == kNone) {
            a[i] = kNone;
        } else if (isControl(c)) {
            step(a, i, c);
        }
    }
    return a;
}

Changes changesFor(const std::vector<Row>& rows, const Map& def, const std::vector<int>& a) {
    const std::vector<int> d = defaults(rows, def);
    Changes ch;
    for (size_t i = 0; i < rows.size() && i < a.size(); ++i)
        if (a[i] != d[i]) ch.push_back({rows[i].id, a[i]});
    return ch;
}

bool assign(const std::vector<Row>& rows, const Map& def, Changes& ch, int row, int target) {
    if (row < 0 || row >= static_cast<int>(rows.size()) || !isTarget(target)) return false;
    std::vector<int> a = assignment(rows, def, ch);
    if (a[row] == target) return false;
    step(a, row, target);
    ch = changesFor(rows, def, a);
    return true;
}

Map sends(const std::vector<Row>& rows, const Map& def, const std::vector<int>& a) {
    Map m;
    m.fill(kNone);
    std::array<bool, 16> isRow{}, placed{}, holds{};
    for (const Row& r : rows) isRow[r.id] = true;
    for (size_t i = 0; i < rows.size() && i < a.size(); ++i)
        if (isControl(a[i])) {
            m[a[i]] = static_cast<int>(rows[i].id);
            placed[rows[i].id] = true;
            holds[a[i]] = true;
        }
    // A control no row has keeps what it sent, when that was no row's.
    for (const cab::Button c : kControl) {
        const int d = def[c];
        if (!holds[c] && d != kNone && !isRow[d] && !placed[d]) {
            m[c] = d;
            placed[d] = true;
        }
    }
    // What is still unsent was on a control a row took: it goes to the
    // controls the rows left, in the pad's order.
    std::vector<int> ids, free;
    for (const cab::Button c : kControl) {
        const int d = def[c];
        if (d != kNone && !isRow[d] && !placed[d]) ids.push_back(d);
        if (!holds[c] && m[c] == kNone) free.push_back(c);
    }
    for (size_t k = 0; k < ids.size() && k < free.size(); ++k) m[free[k]] = ids[k];
    return m;
}

void apply(const Map& map, cab::PadState& pad) {
    uint32_t out = pad.buttons;
    for (const cab::Button c : kControl) out &= ~cab::bit(c);
    float left = 0.0f, right = 0.0f;
    for (const cab::Button c : kControl) {
        const int to = map[c];
        if (to == kNone) continue;
        const bool down = pad.buttons & cab::bit(c);
        if (down) out |= 1u << to;
        const float pull = c == cab::L2 ? pad.leftTrigger
                           : c == cab::R2 ? pad.rightTrigger
                                          : (down ? 1.0f : 0.0f);
        if (to == cab::L2) left = pull;
        if (to == cab::R2) right = pull;
    }
    pad.buttons = out;
    pad.leftTrigger = left;
    pad.rightTrigger = right;
}

// ---- The file ------------------------------------------------------------------

std::string toJson(const File& f) {
    json_object* root = json_object_new_object();
    for (const auto& [kind, k] : f) {
        json_object* o = json_object_new_object();
        json_object_object_add(o, "controller", json_object_new_string(k.name.c_str()));
        json_object* b = json_object_new_object();
        for (const auto& [label, w] : k.buttons)
            json_object_object_add(b, label.c_str(), json_object_new_string(w.c_str()));
        json_object_object_add(o, "buttons", b);
        json_object_object_add(root, kind.c_str(), o);
    }
    std::string out =
        json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_NOSLASHESCAPE);
    out += '\n';
    json_object_put(root);
    return out;
}

File fromJson(const std::string& text, std::vector<std::string>& notes) {
    File f;
    json_object* root = text.empty() ? nullptr : json_tokener_parse(text.c_str());
    if (!root || json_object_get_type(root) != json_type_object) {
        notes.push_back("the file is not a JSON object; nothing in it is used");
        if (root) json_object_put(root);
        return f;
    }
    json_object_object_foreach(root, kind, entry) {
        if (json_object_get_type(entry) != json_type_object) {
            notes.push_back("\"" + std::string(kind) + "\" is not a controller's entry; left out");
            continue;
        }
        KindLines k;
        json_object_object_foreach(entry, key, val) {
            const std::string what = key;
            if (what == "controller" && json_object_get_type(val) == json_type_string) {
                k.name = json_object_get_string(val);
            } else if (what == "buttons" && json_object_get_type(val) == json_type_object) {
                json_object_object_foreach(val, label, w) {
                    if (json_object_get_type(w) != json_type_string ||
                        fromWord(json_object_get_string(w)) == kNotAWord) {
                        notes.push_back("\"" + std::string(label) + "\" for " + kind + ": \"" +
                                        json_object_get_string(w) +
                                        "\" is not a control (B A Y X L R L2 R2 L3 R3 None); "
                                        "left out");
                        continue;
                    }
                    k.buttons.push_back({label, json_object_get_string(w)});
                }
            } else {
                notes.push_back("\"" + what + "\" in " + kind + " is not understood; left out");
            }
        }
        f[kind] = std::move(k);
    }
    json_object_put(root);
    return f;
}

Changes resolve(const KindLines& k, const std::vector<Row>& rows, std::vector<std::string>& notes) {
    Changes ch;
    for (const auto& [label, w] : k.buttons) {
        int i = -1;
        for (size_t r = 0; r < rows.size() && i < 0; ++r)
            if (rows[r].label == label) i = static_cast<int>(r);
        if (i < 0) {
            notes.push_back("no button called \"" + label + "\" in this game; that line is left alone");
            continue;
        }
        ch.push_back({rows[i].id, fromWord(w)});
    }
    return ch;
}

KindLines linesFor(const std::string& name, const std::vector<Row>& rows, const Changes& ch,
                   const std::vector<std::pair<std::string, std::string>>& keep) {
    KindLines k;
    k.name = name;
    for (const auto& [id, c] : ch) {
        const int i = rowOf(rows, id);
        if (i >= 0) k.buttons.push_back({rows[i].label, word(c)});
    }
    for (const auto& line : keep) {
        bool isRow = false;
        for (const Row& r : rows) isRow = isRow || r.label == line.first;
        if (!isRow) k.buttons.push_back(line);
    }
    return k;
}

// ---- The game in play ---------------------------------------------------------------

namespace {

struct Game {
    bool arcade = false;
    std::string core, title;
    int romId = 0;
    storage::User user;
    bool seenAny = false;
    unsigned seen = 0;   // the core's descriptors version the rows were made from
    Rows rows;
    File file;
    std::map<std::string, Changes> changes;   // by kind, against the rows
    unsigned generation = 0;
};
Game gGame;

// The map each port's pad is read through, made again when the pad, the
// rows or the changes move.
struct Kept {
    bool made = false;
    SDL_JoystickID pad = 0;
    unsigned generation = 0;
    unsigned version = 0;
    bool has = false;
    Map map{};
};
Kept gKept[players::kMax];

std::string pathOf() {
    return storage::controlsDir(gGame.user) + "/" + storage::entryName(gGame.romId, gGame.title) +
           ".json";
}

// THE NUMBER IDENTIFIES, THE WORDS ARE FOR A PERSON (storage.h): every file
// of this game in the folder, whatever title it was written under.
std::vector<std::string> filesOf() {
    std::vector<std::string> out;
    const std::string dir = storage::controlsDir(gGame.user);
    if (DIR* d = ::opendir(dir.c_str())) {
        while (const dirent* e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0 &&
                storage::romIdFromEntry(n) == gGame.romId)
                out.push_back(dir + "/" + n);
        }
        ::closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void say(const std::vector<std::string>& notes) {
    for (const std::string& n : notes)
        std::fprintf(stderr, "[controls] %s: %s\n", gGame.title.c_str(), n.c_str());
}

void resolveAll(bool sayNotes) {
    std::vector<std::string> notes;
    gGame.changes.clear();
    for (const auto& [kind, k] : gGame.file) gGame.changes[kind] = resolve(k, gGame.rows.rows, notes);
    if (sayNotes) say(notes);
    ++gGame.generation;
}

void read() {
    gGame.file.clear();
    if (!gGame.arcade || gGame.romId <= 0 || !gGame.user.valid()) return;
    const std::vector<std::string> files = filesOf();
    std::string path = pathOf();
    if (std::find(files.begin(), files.end(), path) == files.end()) {
        if (files.empty()) {
            resolveAll(false);
            return;
        }
        path = files.front();
    }
    const std::vector<uint8_t> bytes = cab::readBytes(path);
    std::vector<std::string> notes;
    gGame.file = fromJson(std::string(bytes.begin(), bytes.end()), notes);
    std::fprintf(stderr, "[controls] %s: read %s, %zu controller(s)\n", gGame.title.c_str(),
                 path.c_str(), gGame.file.size());
    say(notes);
    resolveAll(!gGame.rows.rows.empty());
}

// Written whole, or gone when nothing in it differs from the defaults; any
// file of this game under an older title goes too.
void write() {
    for (auto it = gGame.file.begin(); it != gGame.file.end();)
        it = it->second.buttons.empty() ? gGame.file.erase(it) : std::next(it);
    const std::string path = pathOf();
    bool ok = true;
    if (!gGame.file.empty()) ok = cab::writeBytes(path, toJson(gGame.file));
    for (const std::string& p : filesOf())
        if (gGame.file.empty() || p != path) cab::removeFile(p);
    if (!ok) std::fprintf(stderr, "[controls] %s: could not write %s\n", gGame.title.c_str(), path.c_str());
}

// THE ROWS, EVERY TIME THE CORE DECLARES ITS BUTTONS ANEW: player 1's control
// for each, and the buttons that are not rows with their RetroPad ids, so a
// button wrongly left out is seen. A wrong rule is put right in rowsFrom,
// never for one game.
void sayRows() {
    std::string rows, hidden;
    SDL_Gamepad* gp = players::gamepad(0);
    const Map def = [&] {
        if (!gp) return plain();
        const fightstick::Layout* l = fightstick::forPort(0, gp, gGame.core);
        return l ? fromLayout(*l) : plain();
    }();
    const std::vector<int> a = current(gp ? fightstick::kind(gp) : std::string(), def);
    for (size_t i = 0; i < gGame.rows.rows.size(); ++i)
        rows += (i ? ", " : "") + gGame.rows.rows[i].label + "=" + word(a[i]);
    for (size_t i = 0; i < gGame.rows.hidden.size(); ++i)
        hidden += (i ? ", " : "") + gGame.rows.hidden[i].label + "=" +
                  idWord(gGame.rows.hidden[i].id);
    std::fprintf(stderr, "[controls] %s: rows: %s; hidden: %s\n", gGame.title.c_str(),
                 rows.empty() ? "none" : rows.c_str(), hidden.empty() ? "none" : hidden.c_str());
}

}  // namespace

void beginGame(const std::string& core, int romId, const std::string& title) {
    gGame = Game{};
    for (Kept& k : gKept) k = Kept{};
    gGame.arcade = fightstick::isArcadeCore(core);
    if (!gGame.arcade) return;
    gGame.core = core;
    gGame.romId = romId;
    gGame.title = title;
    gGame.user = storage::currentUser();
    read();
}

void follow() {
    if (!gGame.arcade) return;
    const cab::Core& c = cab::Core::shared();
    if (gGame.seenAny && c.inputDescriptorsVersion() == gGame.seen) return;
    gGame.seenAny = true;
    gGame.seen = c.inputDescriptorsVersion();
    gGame.rows = rowsFrom(gGame.core, c.inputDescriptors(0));
    resolveAll(!gGame.rows.rows.empty());
    // Nothing to say before the core has declared anything: FBNeo does at
    // its first frame, not at the load.
    if (!gGame.rows.rows.empty() || !gGame.rows.hidden.empty()) sayRows();
}

bool offered() { return gGame.arcade && !gGame.rows.rows.empty(); }
const std::vector<Row>& rows() { return gGame.rows.rows; }
unsigned generation() { return gGame.generation; }

const Map* forPort(int port, SDL_Gamepad* gp, const fightstick::Layout* layout) {
    if (!offered() || port < 0 || port >= players::kMax || !gp) return nullptr;
    const SDL_JoystickID pad = SDL_GetGamepadID(gp);
    const unsigned version = cab::Core::shared().inputDescriptorsVersion();
    Kept& k = gKept[port];
    if (!k.made || k.pad != pad || k.generation != gGame.generation || k.version != version) {
        k = Kept{true, pad, gGame.generation, version, false, {}};
        const auto it = gGame.changes.find(fightstick::kind(gp));
        if (it != gGame.changes.end() && !it->second.empty()) {
            const Map def = layout ? fromLayout(*layout) : plain();
            k.has = true;
            k.map = sends(gGame.rows.rows, def, assignment(gGame.rows.rows, def, it->second));
        }
    }
    return k.has ? &k.map : nullptr;
}

std::vector<int> current(const std::string& kind, const Map& def) {
    const auto it = gGame.changes.find(kind);
    return assignment(gGame.rows.rows, def, it == gGame.changes.end() ? Changes{} : it->second);
}

bool give(const std::string& kind, const std::string& name, const Map& def, int row, int target) {
    if (!offered()) return false;
    Changes& ch = gGame.changes[kind];
    const std::vector<int> before = assignment(gGame.rows.rows, def, ch);
    if (!assign(gGame.rows.rows, def, ch, row, target)) return false;
    const std::vector<int> after = assignment(gGame.rows.rows, def, ch);
    std::string swapped;
    for (size_t j = 0; j < after.size(); ++j)
        if (static_cast<int>(j) != row && after[j] != before[j])
            swapped = ", " + gGame.rows.rows[j].label + " to " + word(after[j]);
    std::fprintf(stderr, "[controls] %s: %s (%s): %s to %s%s\n", gGame.title.c_str(),
                 name.c_str(), kind.c_str(), gGame.rows.rows[row].label.c_str(), word(target),
                 swapped.c_str());
    const auto old = gGame.file.find(kind);
    gGame.file[kind] = linesFor(name, gGame.rows.rows, ch,
                                old == gGame.file.end() ? decltype(KindLines::buttons){}
                                                        : old->second.buttons);
    write();
    ++gGame.generation;
    return true;
}

void reload() {
    if (gGame.arcade) read();
}

void reset() {
    if (!gGame.arcade) return;
    gGame.file.clear();
    write();
    resolveAll(false);
    std::fprintf(stderr, "[controls] %s: reset to default\n", gGame.title.c_str());
}

std::string name(SDL_Gamepad* gp, int control) {
    auto face = [gp](SDL_GamepadButton b, const char* plainName) -> std::string {
        switch (gp ? SDL_GetGamepadButtonLabel(gp, b) : SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN) {
            case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
            case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
            case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
            case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
            case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
            case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
            case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
            case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
            default: return plainName;
        }
    };
    switch (control) {
        case cab::B: return face(SDL_GAMEPAD_BUTTON_SOUTH, "A");
        case cab::A: return face(SDL_GAMEPAD_BUTTON_EAST, "B");
        case cab::Y: return face(SDL_GAMEPAD_BUTTON_WEST, "X");
        case cab::X: return face(SDL_GAMEPAD_BUTTON_NORTH, "Y");
        case cab::L: return "LB";
        case cab::R: return "RB";
        case cab::L2: return "LT";
        case cab::R2: return "RT";
        case cab::L3: return "L3";
        case cab::R3: return "R3";
        default: return "None";
    }
}

int controlOf(SDL_GamepadButton b) {
    switch (b) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return cab::B;
        case SDL_GAMEPAD_BUTTON_EAST: return cab::A;
        case SDL_GAMEPAD_BUTTON_WEST: return cab::Y;
        case SDL_GAMEPAD_BUTTON_NORTH: return cab::X;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return cab::L;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return cab::R;
        default: return kNone;
    }
}

// ---- --controls-test --------------------------------------------------------------

int test() {
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++failures;
    };
    using D = std::vector<cab::InputDescriptor>;
    const std::string fbneo = "fbneo_libretro", mame = "mame2003_plus";

    // ---- Labels and rows
    check(trimLabel("B1: Jab Punch") == "Jab Punch" && trimLabel("B12: x") == "x",
          "a leading B<n>: is taken off a label");
    check(trimLabel("Button 1") == "Button 1" && trimLabel("Bomb") == "Bomb" &&
              trimLabel("B1:Jab") == "B1:Jab" && trimLabel("B123: x") == "B123: x" &&
              trimLabel("B: x") == "B: x" && trimLabel("B1: ") == "B1: ",
          "and nothing else is");

    const D sf2 = {{cab::Select, "Coin"},    {cab::Start, "Start"},       {cab::Up, "Up"},
                   {cab::Down, "Down"},      {cab::Left, "Left"},         {cab::Right, "Right"},
                   {cab::Y, "Weak Punch"},   {cab::X, "Medium Punch"},    {cab::L, "Strong Punch"},
                   {cab::B, "Weak Kick"},    {cab::A, "Medium Kick"},     {cab::R, "Strong Kick"}};
    const Rows sf2Rows = rowsFrom(fbneo, sf2);
    check(sf2Rows.rows.size() == 6 && sf2Rows.rows[0].label == "Weak Punch" &&
              sf2Rows.rows[5].label == "Strong Kick",
          "FBNeo's rows: its six buttons, in its order");
    check(sf2Rows.hidden.size() == 6 && sf2Rows.hidden[0].label == "Coin",
          "the lever, Coin and Start are not rows, and are kept for the log");
    const D mameMk = {{cab::B, "B1: Low Punch"}, {cab::Y, "B3: High Punch"},
                      {cab::Select, "Coin"},     {cab::A, "B2: Low Kick"},
                      {cab::X, "B4: High Kick"}, {cab::L3, "Button 9"},
                      {cab::L, "B5: Block"},     {cab::R, "B6: Block 2"}};
    const Rows mk = rowsFrom(mame, mameMk);
    check(mk.rows.size() == 7 && mk.rows[0].label == "Low Punch" && mk.rows[1].label == "Low Kick" &&
              mk.rows[2].label == "High Punch" && mk.rows[5].label == "Block 2" &&
              mk.rows[6].label == "Button 9",
          "MAME 2003-Plus's rows: in its Classic table's order, button 9 last, prefixes off");
    const Rows twice = rowsFrom(fbneo, {{cab::B, "Fire"}, {cab::B, "Fire again"}});
    check(twice.rows.size() == 1 && twice.hidden.size() == 1,
          "a second button on the same id is one row, the other logged");
    check(rowsFrom(fbneo, {{cab::Start, "Start"}}).rows.empty(), "only Start: no rows");

    // ---- Words
    bool words = true;
    for (const cab::Button c : kControl) words = words && fromWord(word(c)) == static_cast<int>(c);
    check(words && fromWord("None") == kNone && std::string(word(kNone)) == "None",
          "every control's word reads back, and None is no control");
    check(fromWord("Start") == kNotAWord && fromWord("Up") == kNotAWord && fromWord("b") == kNotAWord &&
              fromWord("") == kNotAWord,
          "Start, Up, a lower-case b and nothing are not controls");
    int targets = 0;
    for (int c = -2; c < 20; ++c) targets += isTarget(c);
    check(targets == 8 && !isTarget(cab::L3) && !isTarget(cab::R3) && !isTarget(cab::Start) &&
              !isTarget(cab::Select) && !isTarget(cab::Up) && !isTarget(kNone),
          "the targets are the eight: not L3, R3, Start, Select, the d-pad or none");

    // ---- What a pad and a stick send by default
    check(plain()[cab::B] == cab::B && plain()[cab::R3] == cab::R3 && plain()[cab::Up] == kNone,
          "a pad: every control sends its own id, and the rest are not in the map");
    const fightstick::Stick* stick = fightstick::match(0x045e, 0x02e0, "8BitDo Arcade Stick");
    const D gunsmoke = {{cab::B, "Fire Left"}, {cab::A, "Fire Center"}, {cab::Y, "Fire Right"},
                        {cab::Select, "Coin"}, {cab::Start, "Start"}};
    const std::optional<fightstick::Layout> gl = fightstick::make(stick, mame, gunsmoke, gunsmoke);
    const Map gdef = gl ? fromLayout(*gl) : plain();
    check(gl && gdef[cab::B] == cab::B && gdef[cab::A] == cab::A && gdef[cab::L2] == cab::Y &&
              gdef[cab::Y] == kNone && gdef[cab::R] == kNone && gdef[cab::L3] == cab::L3,
          "Gunsmoke on the stick: its three on the bottom row, the rest of the eight send nothing");

    // ---- The invariant, at one state
    const std::string kind = "test-kind";
    auto holds = [&](const std::vector<Row>& rows, const Map& def, const Changes& ch, bool everyRow,
                     bool bijective, std::string& why) {
        const std::vector<int> a = assignment(rows, def, ch);
        if (!injective(a)) return why = "two rows on one control", false;
        for (size_t i = 0; i < a.size(); ++i)
            if (everyRow && a[i] == kNone) return why = rows[i].label + " has no control", false;
        const Map m = sends(rows, def, a);
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != kNone && m[a[i]] != static_cast<int>(rows[i].id))
                return why = rows[i].label + "'s control does not send it", false;
        if (bijective) {
            std::array<int, 16> got{};
            for (const cab::Button c : kControl)
                if (m[c] != kNone) ++got[m[c]];
            for (const cab::Button c : kControl)
                if (got[c] != 1) return why = std::string("id ") + idWord(c) + " is sent " +
                                              std::to_string(got[c]) + " times", false;
        }
        for (const auto& [id, c] : ch)
            if (defaults(rows, def)[rowOf(rows, id)] == c) return why = "a default was kept", false;
        // RELOAD: written as the file is, read back, the same controls.
        File f;
        f[kind] = linesFor("Test Pad", rows, ch, {});
        std::vector<std::string> notes;
        const File back = fromJson(toJson(f), notes);
        const auto it = back.find(kind);
        if (!notes.empty() || it == back.end()) return why = "the file did not read back", false;
        if (assignment(rows, def, resolve(it->second, rows, notes)) != a || !notes.empty())
            return why = "the file read back to other controls", false;
        return true;
    };

    // EVERY STATE A PERSON CAN REACH, from the default, by every press of
    // every row: each target taken (a swap or a move), each other press
    // refused with nothing changed, the invariant after each.
    auto walk = [&](const std::vector<Row>& rows, const Map& def, bool everyRow, bool bijective,
                    size_t expect, const std::string& what) {
        std::set<std::vector<int>> seen;
        std::deque<Changes> todo{Changes{}};
        seen.insert(assignment(rows, def, {}));
        std::string why;
        bool ok = true, refusals = true;
        size_t presses = 0;
        while (!todo.empty() && ok) {
            const Changes ch = todo.front();
            todo.pop_front();
            if (!holds(rows, def, ch, everyRow, bijective, why)) {
                ok = false;
                break;
            }
            const std::vector<int> a = assignment(rows, def, ch);
            for (int i = 0; i < static_cast<int>(rows.size()); ++i)
                for (int c = -1; c < 16; ++c) {
                    Changes next = ch;
                    const bool took = assign(rows, def, next, i, c);
                    ++presses;
                    if (!isTarget(c) || a[i] == c) {
                        refusals = refusals && !took && next == ch;
                        continue;
                    }
                    const std::vector<int> b = assignment(rows, def, next);
                    if (!took || b[i] != c) {
                        ok = false;
                        why = "a press of " + std::string(word(c)) + " did not give it to " +
                              rows[i].label;
                        break;
                    }
                    // Swap, never overwrite: only the row pressed and the
                    // one that had the control moved, and that one went to
                    // the pressed row's old control.
                    for (size_t j = 0; j < a.size(); ++j) {
                        if (static_cast<int>(j) == i) continue;
                        const bool wasThere = a[j] == c;
                        if ((wasThere && b[j] != a[i]) || (!wasThere && b[j] != a[j])) {
                            ok = false;
                            why = "pressing " + std::string(word(c)) + " for " + rows[i].label +
                                  " moved " + rows[j].label;
                        }
                    }
                    if (seen.insert(b).second) todo.push_back(next);
                }
        }
        check(ok, what + ": every state keeps every rule" + (ok ? "" : " (" + why + ")"));
        check(refusals, what + ": L3, R3, the d-pad, Start, Select, none and a row's own control "
                               "change nothing");
        check(seen.size() == expect, what + ": " + std::to_string(seen.size()) + " states reached of " +
                                         std::to_string(expect) + ", " + std::to_string(presses) +
                                         " presses");
    };

    walk(sf2Rows.rows, plain(), true, true, 20160, "Street Fighter II on a pad (8P6)");
    // Three buttons and a ninth on L3: a row can be on L3 only by a swap with
    // the one there, and R3 never has one. 4 x 8P3 with a row on L3, 8P4
    // without.
    const Rows nine = rowsFrom(mame, {{cab::B, "B1: Shot"}, {cab::Y, "B3: Special"},
                                      {cab::A, "B2: Bomb"}, {cab::L3, "Button 9"}});
    walk(nine.rows, plain(), true, true, 4 * 336 + 1680, "A MAME 2003-Plus game on a pad, button 9 on L3");
    const Rows gs = rowsFrom(mame, gunsmoke);
    walk(gs.rows, gdef, true, false, 336, "Gunsmoke on the stick (8P3)");

    // ---- Single cases, said out loud
    {
        const std::vector<Row>& r = sf2Rows.rows;   // WP Y, MP X, SP L, WK B, MK A, SK R
        Changes ch;
        check(assign(r, plain(), ch, 0, cab::L2) && assignment(r, plain(), ch)[0] == cab::L2,
              "a move: Weak Punch to the free left trigger");
        const Map m = sends(r, plain(), assignment(r, plain(), ch));
        check(m[cab::L2] == cab::Y && m[cab::Y] == cab::L2,
              "and the left button sends what the trigger did, so nothing is lost");
        check(assign(r, plain(), ch, 0, cab::B) && assignment(r, plain(), ch)[0] == cab::B &&
                  assignment(r, plain(), ch)[3] == cab::L2,
              "a swap: Weak Punch to Weak Kick's button, Weak Kick to Weak Punch's");
        check(!assign(r, plain(), ch, 0, cab::B), "pressing the row's own control changes nothing");
        check(!assign(r, plain(), ch, 0, cab::L3) && !assign(r, plain(), ch, 0, cab::Start) &&
                  !assign(r, plain(), ch, 9, cab::R) && !assign(r, plain(), ch, -1, cab::R),
              "L3, Start, and rows that do not exist are refused");
        check(assignment(r, plain(), {}) == defaults(r, plain()),
              "reset: with no changes every row is back on its own control");
    }
    {
        // Button 9 on L3 can leave it, and the row it swaps with goes to L3.
        const std::vector<Row>& r = mk.rows;
        Changes ch;
        check(assign(r, plain(), ch, 6, cab::L) && assignment(r, plain(), ch)[6] == cab::L &&
                  assignment(r, plain(), ch)[4] == cab::L3,
              "button 9 to LB: Block goes to L3, the place button 9 left");
        check(!assign(r, plain(), ch, 4, cab::R3), "and nothing can be given L3 or R3 by a press");
    }
    {
        // A stick leaves FBNeo's combination buttons with no button.
        const D mslug = {{cab::B, "Button A"},     {cab::A, "Button B"},   {cab::Y, "Button C"},
                         {cab::X, "Button D"},     {cab::R, "Buttons AB"}, {cab::L, "Buttons CD"},
                         {cab::R2, "Buttons ABC"}, {cab::L2, "Buttons BCD"}};
        const std::optional<fightstick::Layout> ml = fightstick::make(stick, fbneo, mslug, mslug);
        const Map def = ml ? fromLayout(*ml) : plain();
        const Rows ms = rowsFrom(fbneo, mslug);
        const std::vector<int> a0 = assignment(ms.rows, def, {});
        check(ml && a0[4] == kNone && a0[0] == cab::B, "Metal Slug on the stick: Buttons AB has no button");
        Changes ch;
        check(assign(ms.rows, def, ch, 4, cab::Y) && assignment(ms.rows, def, ch)[4] == cab::Y &&
                  assignment(ms.rows, def, ch)[0] == cab::B,
              "given a free top-row button it takes it, and nobody loses one");
        check(assign(ms.rows, def, ch, 4, cab::B) && assignment(ms.rows, def, ch)[0] == cab::Y,
              "given Button A's, Button A takes the top-row button it left");
        Changes none;
        check(assign(ms.rows, def, none, 5, cab::B) && assignment(ms.rows, def, none)[0] == kNone,
              "a row with no button that takes one gives its none to the row it displaced");
        std::string why;
        check(holds(ms.rows, def, none, false, false, why), "and that reads back as None" +
                                                               (why.empty() ? "" : " (" + why + ")"));
        // Three buttons and one combination: the three on the bottom row,
        // the combination on none until it takes a button. 4 x 8P3 with one
        // row on none, 8P4 once every row has one.
        const D three = {{cab::B, "Button 1"}, {cab::A, "Button 2"}, {cab::Y, "Button 3"},
                         {cab::R, "Buttons AB"}};
        const std::optional<fightstick::Layout> tl = fightstick::make(stick, fbneo, three, three);
        walk(rowsFrom(fbneo, three).rows, tl ? fromLayout(*tl) : plain(), false, false, 4 * 336 + 1680,
             "Three buttons and a combination on the stick");
    }

    // ---- apply
    {
        Map m = plain();
        std::swap(m[cab::L2], m[cab::R2]);
        std::swap(m[cab::B], m[cab::L]);
        cab::PadState p;
        p.buttons = cab::bit(cab::B) | cab::bit(cab::Up) | cab::bit(cab::Start);
        p.leftTrigger = 0.4f;
        controls::apply(m, p);
        check(p.buttons == (cab::bit(cab::L) | cab::bit(cab::Up) | cab::bit(cab::Start)),
              "the bottom button sends L; the lever and Start pass as they are");
        check(p.rightTrigger == 0.4f && p.leftTrigger == 0.0f,
              "the left trigger's pull, short of a press, goes to R2 with its id");
        m = plain();
        m[cab::L] = cab::L2;
        m[cab::L2] = cab::L;
        p = {};
        p.buttons = cab::bit(cab::L);
        controls::apply(m, p);
        check(p.buttons == cab::bit(cab::L2) && p.leftTrigger == 1.0f,
              "a shoulder sending L2 pulls it all the way");
        m[cab::R2] = kNone;
        p = {};
        p.buttons = cab::bit(cab::R2);
        p.rightTrigger = 1.0f;
        controls::apply(m, p);
        check(p.buttons == 0 && p.rightTrigger == 0.0f, "a control sending nothing sends nothing");
    }

    // ---- The file, as a person may leave it
    {
        std::vector<std::string> notes;
        check(fromJson("", notes).empty() && notes.size() == 1, "an empty file: nothing, said once");
        notes.clear();
        check(fromJson("{not json", notes).empty() && !notes.empty(), "a broken one: nothing, said");
        notes.clear();
        check(fromJson("[1, 2]", notes).empty() && !notes.empty(), "a list, not an object: nothing");
        notes.clear();
        const File f = fromJson(R"({
            "stick": 7,
            "pad": {"controller": "Xbox One S Controller", "colour": "red",
                    "buttons": {"Weak Punch": "A", "Weak Kick": "Start", "Strong Kick": 3,
                                "Fierce": "L2", "Medium Kick": "None"}}
        })", notes);
        check(f.size() == 1 && f.count("pad") && f.at("pad").name == "Xbox One S Controller" &&
                  f.at("pad").buttons.size() == 3 && notes.size() == 4,
              "a hand edit: the lines that make sense are read, the four that do not are said");
        notes.clear();
        const Changes ch = resolve(f.at("pad"), sf2Rows.rows, notes);
        check(ch.size() == 2 && notes.size() == 1, "a label that is no row's is left out, and said");
        const KindLines kept = linesFor("Xbox One S Controller", sf2Rows.rows, ch, f.at("pad").buttons);
        check(kept.buttons.size() == 3 && kept.buttons.back().first == "Fierce",
              "and kept as it was when the file is written again");
        const std::vector<int> a = assignment(sf2Rows.rows, plain(), ch);
        check(a[0] == cab::A && a[4] == kNone && injective(a),
              "Weak Punch on A, Medium Kick on none, as the file says");
        const Changes clash = {{cab::Y, cab::B}, {cab::X, cab::B}};
        const std::vector<int> c = assignment(sf2Rows.rows, plain(), clash);
        check(injective(c) && c[1] == cab::B,
              "two rows put on one control by hand are made one press at a time");
    }

    // ---- On the disk: written, read back, reset
    {
        char tmpl[] = "/tmp/cabinetos-controls-XXXXXX";
        const char* dir = ::mkdtemp(tmpl);
        check(dir != nullptr, "a scratch folder");
        if (dir) {
            const std::string path = std::string(dir) + "/7 - Gunsmoke.json";
            File f;
            Changes ch;
            assign(gs.rows, gdef, ch, 0, cab::A);
            f["8bitdo-arcade-stick"] = linesFor("8BitDo Arcade Stick", gs.rows, ch, {});
            const std::string text = toJson(f);
            check(cab::writeBytes(path, text), "the file is written");
            const std::vector<uint8_t> back = cab::readBytes(path);
            std::vector<std::string> notes;
            const File g = fromJson(std::string(back.begin(), back.end()), notes);
            check(notes.empty() && g.count("8bitdo-arcade-stick") &&
                      assignment(gs.rows, gdef, resolve(g.at("8bitdo-arcade-stick"), gs.rows, notes)) ==
                          assignment(gs.rows, gdef, ch),
                  "read back from the disk: the same controls");
            check(text.find("\"Fire Left\": \"A\"") != std::string::npos &&
                      text.find("\"controller\": \"8BitDo Arcade Stick\"") != std::string::npos &&
                      text.find("Fire Center") != std::string::npos &&
                      text.find("Fire Right") == std::string::npos,
                  "in words: the controller's name, and only the rows that moved");
            check(cab::removeFile(path) && !storage::exists(path), "reset: the file is gone");
            ::rmdir(dir);
        }
    }

    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace controls
