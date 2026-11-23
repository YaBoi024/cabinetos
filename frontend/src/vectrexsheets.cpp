// The Vectrex games' screen sheets (vectrexsheets.h): the table's rule, the
// Overlays choice, and the sheet's texture and its draw (#224).

#include "vectrexsheets.h"

#include <GLES3/gl3.h>
#include <SDL3/SDL.h>
#include <json-c/json.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "fileio.h"
#include "imagecodec.h"
#include "prefs.h"
#include "ui.h"

namespace vectrexsheets {
namespace {

// The game in play's sheet and its choice, set by prepare. Frame thread only.
struct State {
    GLuint texture = 0;
    std::string slug;
    bool on = true;
};
State& state() {
    static State s;
    return s;
}

std::string prefKey(const std::string& slug) { return "overlays." + slug; }
bool chosenOn(const std::string& slug) { return prefs::get(prefKey(slug), "on") != "off"; }

// MEASURED ON THE A9, 2026-10-20 (#224), eight games headless, 25 captures
// each: with the core's own 1 the lines reach x 0 to 868 of the 869-point
// picture (Web Wars' web, Scramble's ground, Mine Storm's mines as they
// wrap) and the sheets' windows run from about x 30 to 839, so 850 to 8,700
// lit pixels a game went under a frame. At 0.92 the lines span x 34 to 835
// and y 144 to 957, and what is left under a sheet is the system ROM's
// start-up frame and a few pixels (0 to 74) of the games themselves. The
// same on both axes keeps the picture's shape.
constexpr const char* kFit = "0.92";

std::string normalised(const std::string& s) {
    std::string out;
    bool space = false;
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            if (space && !out.empty()) out += ' ';
            out += static_cast<char>(std::tolower(c));
            space = false;
        } else {
            space = true;
        }
    }
    return out;
}

std::string lowered(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::vector<std::string> strings(json_object* row, const char* key) {
    std::vector<std::string> out;
    json_object* a = nullptr;
    if (!json_object_object_get_ex(row, key, &a) || json_object_get_type(a) != json_type_array)
        return out;
    for (size_t i = 0; i < json_object_array_length(a); ++i)
        if (const char* v = json_object_get_string(json_object_array_get_idx(a, i)))
            out.emplace_back(v);
    return out;
}

std::string textOf(const std::string& path) {
    const std::vector<uint8_t> b = cab::readBytes(path);
    return std::string(b.begin(), b.end());
}

}  // namespace

std::string sheetFor(const std::string& table, const std::string& md5,
                     const std::string& name, const std::string& fsName) {
    json_object* root = json_tokener_parse(table.c_str());
    json_object* rows = nullptr;
    if (!root || !json_object_object_get_ex(root, "overlays", &rows) ||
        json_object_get_type(rows) != json_type_array) {
        if (root) json_object_put(root);
        return "";
    }
    const std::string hash = lowered(md5);
    const std::string haystacks[] = {normalised(name), normalised(fsName)};
    json_object* winner = nullptr;
    const size_t n = json_object_array_length(rows);
    for (size_t i = 0; i < n && !hash.empty() && !winner; ++i) {
        json_object* row = json_object_array_get_idx(rows, i);
        for (const std::string& h : strings(row, "md5"))
            if (lowered(h) == hash) winner = row;
    }
    // Longer only, so a tie stays with the earlier row, as in Cabinet.
    json_object* named = nullptr;
    size_t best = 0;
    for (size_t i = 0; i < n && !winner; ++i) {
        json_object* row = json_object_array_get_idx(rows, i);
        for (const std::string& token : strings(row, "match"))
            for (const std::string& hay : haystacks)
                if (token.size() > best && hay.find(token) != std::string::npos) {
                    named = row;
                    best = token.size();
                }
    }
    if (!winner) winner = named;
    std::string out;
    json_object* v = nullptr;
    if (winner && json_object_object_get_ex(winner, "historical", &v) &&
        json_object_get_boolean(v) && json_object_object_get_ex(winner, "file", &v))
        out = json_object_get_string(v);
    json_object_put(root);
    return out;
}

std::string folder() {
    const char* env = std::getenv("CABINETOS_VECTREX");
    return env && *env ? env : "/usr/share/cabinetos/vectrex";
}

void release() {
    State& s = state();
    if (s.texture) glDeleteTextures(1, &s.texture);
    s.texture = 0;
}

void prepare(const std::string& platformSlug, const std::string& md5,
             const std::string& name, const std::string& fsName) {
    release();
    State& s = state();
    s.slug = platformSlug;
    if (platformSlug != "vectrex") return;
    s.on = chosenOn(platformSlug);
    const Uint64 since = SDL_GetTicksNS();
    const std::string file =
        sheetFor(textOf(folder() + "/vectrex-overlays.json"), md5, name, fsName);
    if (file.empty()) {
        std::fprintf(stderr, "[vectrex] no screen sheet for this game\n");
        return;
    }
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    const std::string path = folder() + "/" + file + ".png";
    if (!ui::decodeImage(cab::readBytes(path), rgba, w, h) || w <= 0 || h <= 0) {
        std::fprintf(stderr, "[vectrex] the sheet %s could not be read\n", path.c_str());
        return;
    }
    // PREMULTIPLIED, as Cabinet drew it: linear sampling then mixes colour by
    // coverage, so the frame's edge against the clear border takes no dark
    // fringe from the transparent texels' colour.
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            rgba[i + c] = static_cast<uint8_t>((rgba[i + c] * rgba[i + 3] + 127) / 255);
    glGenTextures(1, &s.texture);
    glBindTexture(GL_TEXTURE_2D, s.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::fprintf(stderr, "[vectrex] screen sheet %s, %dx%d, %.1f ms, overlays %s\n",
                 file.c_str(), w, h, (SDL_GetTicksNS() - since) / 1e6, s.on ? "on" : "off");
}

std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug, bool fitted) {
    if (platformSlug != "vectrex" || core != "vecx") return {};
    const char* v = fitted ? kFit : "1";
    return {{"vecx_scale_x", v}, {"vecx_scale_y", v}};
}

std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug,
                                               const std::string& md5, const std::string& name,
                                               const std::string& fsName) {
    if (platformSlug != "vectrex" || core != "vecx") return {};
    const bool fitted =
        chosenOn(platformSlug) &&
        !sheetFor(textOf(folder() + "/vectrex-overlays.json"), md5, name, fsName).empty();
    return coreOptions(core, platformSlug, fitted);
}

bool ready() { return state().texture != 0; }

bool on() { return state().on; }

void setOn(bool on) {
    State& s = state();
    s.on = on;
    if (!s.slug.empty()) prefs::set(prefKey(s.slug), on ? "on" : "off");
}

void draw(ui::Renderer& r, float x, float y, float w, float h) {
    const State& s = state();
    if (!s.texture || !s.on) return;
    // The renderer blends straight alpha (Renderer::beginFrame); a
    // premultiplied texture takes its colour as it is, and the blend goes
    // back after.
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    r.drawTextured(x, y, w, h, s.texture, 0, 0, 1, 1, ui::Color{1, 1, 1, 1}, false);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

int test() {
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++failures;
    };
    const std::string table = textOf("data/vectrex-overlays.json");
    check(!table.empty(), "data/vectrex-overlays.json is read (run from frontend/)");
    auto is = [&](const char* md5, const char* name, const char* fsName, const char* want) {
        const std::string got = sheetFor(table, md5, name, fsName);
        check(got == want, std::string("'") + name + "' / '" + fsName + "' md5 '" + md5 +
                               "' -> '" + got + "', want '" + want + "'");
    };
    // By md5, whatever the name says, and in either case.
    is("9657037a461eee919462eb6a01879685", "", "", "ArmorAttack");
    is("9657037A461EEE919462EB6A01879685", "Not Its Name", "x.vec", "ArmorAttack");
    // Mine Storm II by md5 is the console's own Mine Storm sheet; by name it is
    // its invented one, which draws nothing.
    is("126fecbf4fb09fdb5a3c9af92abaed7c", "Mine Storm II", "", "Mine_Storm");
    is("", "Mine Storm II", "Mine Storm II (USA) (Rev 2).7z", "");
    // By name: punctuation and case do not matter, and the file name counts.
    is("", "Armor..Attack (World)", "", "ArmorAttack");
    is("", "", "Spin ball (USA).7z", "Spinball");
    is("", "Star Ship", "", "Star_Ship");
    is("", "Mr. Boston Clean Sweep", "", "Mr_Boston_Clean_Sweep");
    // The longest token wins, across the name and the file name: RomM calls it
    // "3-D Mine Storm", and the file's "3D" is what keeps Mine Storm's away.
    is("", "3-D Mine Storm", "3D Mine Storm (USA).7z", "");
    is("", "Mine Storm", "Mine Storm (World).7z", "Mine_Storm");
    // A prototype's sheet is off; a game with none has none.
    is("db9929b0ad53c97afda065596806bf86", "Dark Tower", "", "");
    is("", "Engine Analyzer", "Engine Analyzer (USA) (Proto).7z", "");
    is("", "", "", "");
    // The table itself: 38 sheets, 28 of them drawn (25 retail, 3 variants).
    int rows = 0, drawn = 0;
    if (json_object* root = json_tokener_parse(table.c_str())) {
        json_object* a = nullptr;
        if (json_object_object_get_ex(root, "overlays", &a)) {
            for (size_t i = 0; i < json_object_array_length(a); ++i) {
                json_object* v = nullptr;
                ++rows;
                if (json_object_object_get_ex(json_object_array_get_idx(a, i), "historical", &v) &&
                    json_object_get_boolean(v))
                    ++drawn;
            }
        }
        json_object_put(root);
    }
    check(rows == 38 && drawn == 28, "38 rows, 28 drawn (got " + std::to_string(rows) + ", " +
                                         std::to_string(drawn) + ")");
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace vectrexsheets
