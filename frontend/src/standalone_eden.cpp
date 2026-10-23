// Switch, through Eden from Flathub: its keys and firmware out of `bios/`, its
// profile, each player's controls and the settings the console lays over
// Eden's own before every start (prepareEden), and where its saves are
// (edenSaveRoot). What every emulator shares is standalone.cpp. Moved out of
// standalone.cpp in the restructure (#398).

#include "standalone_internal.h"

#include "storage.h"
#include "vpad.h"

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace cab::standalone::detail {
namespace {

// The file in `bios/` that RomM serves under `prefix` + anything + `.keys`,
// with the highest version in its name: `prod-v19.keys` over `prod-v17.keys`,
// and over a bare `prod.keys`, which says nothing about its version.
std::string newestKeys(const std::string& biosDir, const std::string& prefix) {
    auto version = [](const std::string& name) {
        long best = -1;
        for (size_t i = 0; i < name.size();) {
            if (!std::isdigit(static_cast<unsigned char>(name[i]))) { ++i; continue; }
            size_t j = i;
            while (j < name.size() && std::isdigit(static_cast<unsigned char>(name[j]))) ++j;
            best = std::max(best, std::strtol(name.substr(i, j - i).c_str(), nullptr, 10));
            i = j;
        }
        return best;
    };
    std::string pick;
    long pickVersion = -2;
    DIR* d = opendir(biosDir.c_str());
    if (!d) return pick;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.size() < prefix.size() + 5) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        if (name.compare(name.size() - 5, 5, ".keys") != 0) continue;
        struct stat st;
        const std::string path = biosDir + "/" + name;
        if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) continue;
        const long v = version(name);
        if (v > pickVersion || (v == pickVersion && path > pick)) {
            pick = path;
            pickVersion = v;
        }
    }
    closedir(d);
    return pick;
}

// The Switch firmware zip RomM serves, if any: a `.zip` in `bios/` with
// "firmware" in its name, the highest version when there are several. Named
// rather than any zip because `bios/` is shared by every system, and arcade
// BIOS sets are zips too.
std::string newestFirmwareZip(const std::string& biosDir) {
    std::string pick;
    long pickVersion = -2;
    DIR* d = opendir(biosDir.c_str());
    if (!d) return pick;
    while (dirent* e = readdir(d)) {
        std::string lower = e->d_name;
        for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".zip") != 0) continue;
        if (lower.find("firmware") == std::string::npos) continue;
        long v = -1;
        for (size_t i = 0; i < lower.size();) {
            if (!std::isdigit(static_cast<unsigned char>(lower[i]))) { ++i; continue; }
            size_t j = i;
            while (j < lower.size() && std::isdigit(static_cast<unsigned char>(lower[j]))) ++j;
            v = std::max(v, std::strtol(lower.substr(i, j - i).c_str(), nullptr, 10));
            break;   // the first number is the major version
        }
        const std::string path = biosDir + "/" + e->d_name;
        if (v > pickVersion || (v == pickVersion && path > pick)) {
            pick = path;
            pickVersion = v;
        }
    }
    closedir(d);
    return pick;
}

// FIRMWARE IS USED WHEN RomM HAS IT (decision A), installed the way Eden's
// own "Install Firmware" does it (qt_common/util/content.cpp): every `.nca` in
// the source, at any depth, copied by name into nand/system/Contents/
// registered after that folder is emptied. From the zip directly, so it does
// not matter whether the files are at its top or inside a folder.
//
// Once per zip: a note of which zip and its size is kept beside it, and the
// install runs again only when the zip in RomM changes. Mario Kart 8 Deluxe
// is why: without firmware its Mii screen crashed the game (2026-09-28).
bool installFirmware(const std::string& homeDir, const std::string& zip) {
    struct stat st;
    if (::stat(zip.c_str(), &st) != 0) return false;
    const std::string contents = homeDir + "/user/nand/system/Contents";
    const std::string registered = contents + "/registered";
    const std::string note = contents + "/cabinetos-firmware.txt";
    const std::string stamp =
        zip.substr(zip.find_last_of('/') + 1) + " " + std::to_string(st.st_size) + "\n";
    if (readFile(note) == stamp) return true;

    storage::makeDirs(registered);
    if (DIR* d = opendir(registered.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            const std::string p = registered + "/" + n;
            struct stat es;
            if (::lstat(p.c_str(), &es) == 0 && S_ISREG(es.st_mode)) ::unlink(p.c_str());
        }
        closedir(d);
    }

    archive* a = archive_read_new();
    archive_read_support_format_zip(a);
    int placed = 0;
    bool ok = archive_read_open_filename(a, zip.c_str(), 1 << 16) == ARCHIVE_OK;
    archive_entry* entry = nullptr;
    while (ok && archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) != AE_IFREG) continue;
        std::string name = archive_entry_pathname(entry);
        name = name.substr(name.find_last_of('/') + 1);
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".nca") != 0) continue;
        const std::string dest = registered + "/" + name;
        FILE* out = std::fopen(dest.c_str(), "wb");
        if (!out) { ok = false; break; }
        const void* buf;
        size_t size;
        la_int64_t offset;
        int r;
        while ((r = archive_read_data_block(a, &buf, &size, &offset)) == ARCHIVE_OK)
            if (std::fwrite(buf, 1, size, out) != size) { r = ARCHIVE_FATAL; break; }
        std::fclose(out);
        if (r != ARCHIVE_EOF) { ok = false; break; }
        ++placed;
    }
    archive_read_free(a);
    if (!ok || placed == 0) {
        std::fprintf(stderr, "[standalone] firmware from %s: %s\n", zip.c_str(),
                     placed == 0 ? "no .nca files in it" : "could not unpack it");
        return false;
    }
    writeFile(note, stamp);
    std::fprintf(stderr, "[standalone] firmware installed from %s (%d files)\n", zip.c_str(),
                 placed);
    return true;
}

// EDEN'S PROFILE, which names the folder every save sits in:
// `user/save/0000000000000000/<profile>/<title ID>/`. Eden makes one with a
// RANDOM ID the first time it starts, so on a new console the folder a save
// must be restored into would not exist until the game was already running.
// So the console makes it first, with a fixed ID, and on a console where Eden
// already made one it is read and kept.
//
// The file is Eden's ProfileDataRaw (acc/profile_manager.cpp): 0x10 bytes of
// padding, then eight 0xC8-byte users, each a 16-byte ID, the same ID again,
// an 8-byte timestamp, a 32-byte name and 0x80 bytes of extra data. The
// folder is the ID's bytes REVERSED, in capitals: checked against two
// profiles Eden made on the A9, 2026-09-28.
//
// The name is set to whoever is playing each time, because some games show
// it. The ID never changes, so neither does the folder.
constexpr const char* kEdenProfiles =
    "/user/nand/system/save/8000000000000010/su/avators/profiles.dat";
constexpr size_t kProfilesSize = 0x650;
constexpr size_t kUserSize = 0xC8;
constexpr size_t kUserNameAt = 0x28;   // after two IDs and the timestamp
constexpr size_t kUserNameSize = 0x20;
// "CabinetOS player", which reads back from the folder name in a hex dump.
constexpr uint8_t kProfileId[16] = {'C', 'a', 'b', 'i', 'n', 'e', 't', 'O',
                                    'S', ' ', 'p', 'l', 'a', 'y', 'e', 'r'};

std::string profileFolder(const uint8_t* id) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (int i = 15; i >= 0; --i) {
        out += hex[id[i] >> 4];
        out += hex[id[i] & 0xF];
    }
    return out;
}

// The first profile's folder, making the profile if there is none. Empty
// only when the file cannot be written.
std::string edenProfile(const std::string& homeDir, const std::string& player) {
    const std::string path = homeDir + kEdenProfiles;
    std::string data = readFile(path);
    if (data.size() != kProfilesSize) data.assign(kProfilesSize, '\0');

    size_t user = 0x10;
    bool found = false;
    for (size_t at = 0x10; at + kUserSize <= kProfilesSize; at += kUserSize) {
        bool zero = true;
        for (size_t i = 0; i < 16; ++i)
            if (data[at + i] != '\0') { zero = false; break; }
        if (!zero) { user = at; found = true; break; }
    }
    if (!found) {
        std::memcpy(&data[user], kProfileId, 16);
        std::memcpy(&data[user + 16], kProfileId, 16);
    }

    // The name, cut at a whole character: 32 bytes, zero-padded.
    std::string name = player.empty() ? std::string("Player") : player;
    if (name.size() > kUserNameSize) {
        size_t cut = kUserNameSize;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
        name.resize(cut);
    }
    std::string field(kUserNameSize, '\0');
    std::memcpy(&field[0], name.data(), name.size());
    const bool rename = data.compare(user + kUserNameAt, kUserNameSize, field) != 0;
    if (rename) std::memcpy(&data[user + kUserNameAt], field.data(), kUserNameSize);

    if (!found || rename) {
        storage::makeDirs(path.substr(0, path.find_last_of('/')));
        if (!writeFile(path, data)) return {};
    }
    return profileFolder(reinterpret_cast<const uint8_t*>(&data[user]));
}

// EACH PLAYER'S CONTROLS, on their virtual controller (vpad.h), laid out as a
// Pro Controller the way Eden lays out any SDL pad itself: by POSITION, so
// the button on the right of the diamond is A on every pad, as it is on a
// Switch (sdl_driver.cpp, GetDefaultButtonBinding). Players past `players` are
// unplugged, or a game would count four controllers and offer four seats.
std::vector<std::string> edenControls(int players) {
    const std::string guid = cab::vpad::edenGuid();
    std::vector<std::string> lines;   // "key=value" for [Controls]
    auto btn = [&](int p, SDL_GamepadButton b) {
        return "\"engine:sdl,port:" + std::to_string(p) + ",guid:" + guid +
               ",button:" + std::to_string(cab::vpad::buttonIndex(b)) + "\"";
    };
    auto hat = [&](int p, const char* dir) {
        return "\"engine:sdl,port:" + std::to_string(p) + ",guid:" + guid +
               ",hat:0,direction:" + dir + "\"";
    };
    auto trig = [&](int p, SDL_GamepadAxis a) {
        return "\"engine:sdl,port:" + std::to_string(p) + ",guid:" + guid +
               ",axis:" + std::to_string(cab::vpad::axisIndex(a)) +
               ",threshold:0.5,invert:+\"";
    };
    auto stick = [&](int p, SDL_GamepadAxis x, SDL_GamepadAxis y) {
        return "\"engine:sdl,port:" + std::to_string(p) + ",guid:" + guid +
               ",axis_x:" + std::to_string(cab::vpad::axisIndex(x)) +
               ",axis_y:" + std::to_string(cab::vpad::axisIndex(y)) +
               ",offset_x:0,offset_y:0,invert_x:+,invert_y:+\"";
    };
    for (int p = 0; p < 8; ++p) {
        const std::string k = "player_" + std::to_string(p) + "_";
        lines.push_back(k + "connected=" + (p < players ? "true" : "false"));
        if (p >= cab::vpad::kMaxPlayers) continue;
        lines.push_back(k + "type=0");   // Pro Controller
        lines.push_back(k + "button_a=" + btn(p, SDL_GAMEPAD_BUTTON_EAST));
        lines.push_back(k + "button_b=" + btn(p, SDL_GAMEPAD_BUTTON_SOUTH));
        lines.push_back(k + "button_x=" + btn(p, SDL_GAMEPAD_BUTTON_NORTH));
        lines.push_back(k + "button_y=" + btn(p, SDL_GAMEPAD_BUTTON_WEST));
        lines.push_back(k + "button_lstick=" + btn(p, SDL_GAMEPAD_BUTTON_LEFT_STICK));
        lines.push_back(k + "button_rstick=" + btn(p, SDL_GAMEPAD_BUTTON_RIGHT_STICK));
        lines.push_back(k + "button_l=" + btn(p, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        lines.push_back(k + "button_r=" + btn(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        lines.push_back(k + "button_zl=" + trig(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        lines.push_back(k + "button_zr=" + trig(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        lines.push_back(k + "button_plus=" + btn(p, SDL_GAMEPAD_BUTTON_START));
        lines.push_back(k + "button_minus=" + btn(p, SDL_GAMEPAD_BUTTON_BACK));
        lines.push_back(k + "button_dleft=" + hat(p, "left"));
        lines.push_back(k + "button_dup=" + hat(p, "up"));
        lines.push_back(k + "button_dright=" + hat(p, "right"));
        lines.push_back(k + "button_ddown=" + hat(p, "down"));
        lines.push_back(k + "button_slleft=" + btn(p, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        lines.push_back(k + "button_srleft=" + btn(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        lines.push_back(k + "button_slright=" + btn(p, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        lines.push_back(k + "button_srright=" + btn(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        lines.push_back(k + "button_home=" + btn(p, SDL_GAMEPAD_BUTTON_GUIDE));
        lines.push_back(k + "button_screenshot=\"\"");
        lines.push_back(k + "lstick=" + stick(p, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY));
        lines.push_back(k + "rstick=" + stick(p, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY));
    }
    return lines;
}

}  // namespace

bool prepareEden(const Emulator& e, const std::string& saveDir, const std::string& player,
                 int players, quality::Level level, bool* missingKeys, std::string* err) {
    const std::string user = home(e) + "/user";
    storage::makeDirs(user + "/keys");
    storage::makeDirs(user + "/config");

    // KEYS ARE REQUIRED, FIRMWARE IS NOT (decision A). Without prod.keys Eden
    // opens its own "Install them now?" box over the game, so the console
    // refuses first and says why. title.keys is optional: most games carry
    // their own ticket.
    const std::string prod = newestKeys(storage::biosDir(), "prod");
    if (prod.empty()) {
        *missingKeys = true;
        return false;
    }
    if (!placeFile(prod, user + "/keys/prod.keys")) {
        *err = "could not place " + prod;
        return false;
    }
    std::fprintf(stderr, "[standalone] keys: %s\n", prod.c_str());
    const std::string title = newestKeys(storage::biosDir(), "title");
    if (!title.empty() && !placeFile(title, user + "/keys/title.keys"))
        std::fprintf(stderr, "[standalone] could not place %s\n", title.c_str());
    // Not fatal either way: most games need none (decision A).
    if (const std::string fw = newestFirmwareZip(storage::biosDir()); !fw.empty())
        installFirmware(home(e), fw);

    // THE CONSOLE'S SETTINGS, laid over Eden's own file before every start.
    // Every one of these is something that would otherwise put a box or a
    // choice on the television. Batocera's Switch generators (yuzu, suyu,
    // Citron; Batocera dropped the Switch on 2026-07-13) and the community
    // Eden add-on (foclabroc/New-batocera-switch) write some of the same,
    // between them: fullscreen, firstStart, confirmStop, update checks and
    // docked mode.
    const std::string ini = user + "/config/qt-config.ini";
    std::vector<IniSet> sets = {
        {"UI", "fullscreen", "true"},
        // No welcome on the first start.
        {"UI", "firstStart", "false"},
        // 2 is Ask_Never: no "Are you sure?" when the console closes it.
        // Without this, SIGTERM opens a question instead of closing.
        {"UI", "confirmStop", "2"},
        // Compiled out of the Flathub build, and off in any other.
        {"UI", "check_for_updates", "false"},
        // "Wayland Detected!" at start.
        {"UI", "gui_hide_backend_warning", "true"},
        // A game asking for a controller layout opens Eden's own dialog.
        // Ours alone: no Batocera generator ever set it, and the add-on's
        // first-start template sets it false, so the dialog shows there.
        {"UI", "disableControllerApplet", "true"},
        // Docked, always: the television mode (decision B). 1 is Docked.
        {"System", "use_docked_mode", "1"},
        // THIS PERSON'S SAVES FOR THIS GAME, a different folder every launch,
        // so nothing of anybody else's is in reach of the game and what
        // changed while it ran is exactly what it saved.
        {"Data%20Storage", "save_directory", saveDir.c_str()},
        // Eden's own drivers for Nintendo pads open the real ones directly,
        // which the virtual controllers exist to prevent (vpad.h).
        {"Controls", "enable_joycon_driver", "false"},
        {"Controls", "enable_procon_driver", "false"},
        // NO INTERNET, as far as a game can tell. Nintendo's online services do
        // not answer an emulator, so nothing is lost. It did NOT save
        // Shredder's Revenge, whose Epic online layer quits on a call Eden
        // only stubs (bsd EventFd); nor did cutting the sandbox off the
        // network, which made it hang instead and was taken out
        // (2026-09-28).
        {"Network", "airplane_mode", "true"},
        // VSYNC ON (#209, MMagTech 2026-10-04): 2 is Fifo, Eden's own
        // default. #209 first set 0 (Immediate) on the reasoning that
        // gamescope already lines frames up with the screen; then PS2 showed
        // that without vsync gamescope keeps one picture waiting and replaces
        // it, so frames are lost (4 in 60, #226). Fifo queues them.
        {"Renderer", "use_vsync", "2"},
    };
    const std::vector<std::string> controls = edenControls(players);
    std::vector<std::string> keys, values;
    keys.reserve(controls.size());
    values.reserve(controls.size());
    for (const std::string& l : controls) {
        const size_t eq = l.find('=');
        keys.push_back(l.substr(0, eq));
        values.push_back(l.substr(eq + 1));
    }
    for (size_t i = 0; i < controls.size(); ++i)
        sets.push_back({"Controls", keys[i].c_str(), values[i].c_str()});
    // PICTURE QUALITY (#63): resolution, filtering and shader building.
    const std::vector<quality::Setting> picture = quality::eden(level);
    for (const quality::Setting& q : picture)
        sets.push_back({q.section.c_str(), q.key.c_str(), q.value.c_str()});
    if (!writeFile(ini, applyIni(readFile(ini), sets))) {
        *err = "could not write " + ini;
        return false;
    }
    if (edenProfile(home(e), player).empty()) {
        *err = "could not write Eden's profile";
        return false;
    }
    return true;
}

// Where its saves are (saveRoot): under the first profile's folder in Eden's
// profiles.dat (edenProfile), or empty when Eden has none.
std::string edenSaveRoot(const Emulator& e, const std::string& saveDir) {
    const std::string data = readFile(home(e) + kEdenProfiles);
    for (size_t at = 0x10; data.size() == kProfilesSize && at + kUserSize <= kProfilesSize;
         at += kUserSize) {
        bool zero = true;
        for (size_t i = 0; i < 16; ++i)
            if (data[at + i] != '\0') { zero = false; break; }
        if (!zero)
            return saveDir + "/user/save/0000000000000000/" +
                   profileFolder(reinterpret_cast<const uint8_t*>(&data[at]));
    }
    return {};
}

}  // namespace cab::standalone::detail
