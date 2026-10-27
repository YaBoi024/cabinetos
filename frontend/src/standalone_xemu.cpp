// The original Xbox, through xemu from Flathub: its BIOS, EEPROM and drive and
// the settings the console writes before every start (prepareXemu), a full
// disc dump cut to its game (installXbox), this person's saves carried onto the
// game's drive and off it around a game (xemuBeforeStart, xemuAfterEnd), its
// sandbox (Run::startXemu) and the watch on what the Xbox runs, over qemu's
// control socket (Run::watchDashboard). Moved out of standalone.cpp in the
// restructure (#398).

#include "standalone_internal.h"

#include "gpu.h"
#include "proc.h"
#include "storage.h"
#include "vpad.h"
#include "xboxhdd.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace cab::standalone::detail {
namespace {

// --- Xbox (xemu) --------------------------------------------------------------
//
// Decided with MMagTech 2026-09-29, issue #172: xemu from Flathub as it ships;
// the BIOS and MCPX boot ROM from RomM; xemu's own blank drive and one EEPROM
// for every console, both in the image; the drive in the game's folder; saves
// carried on and off it by the console (xboxhdd.h).

constexpr const char* kXboxDrive = "xbox_hdd.qcow2";

// THE DASHBOARD'S TITLE ID. xemu-dashboard's (FFFF0002, read from
// C:\xboxdash.xbe on the drive install-xemu-drive.sh pins), and Microsoft's
// (FFFE0000) should a drive ever carry that instead.
bool isXboxDashboard(uint32_t title) { return title == 0xFFFF0002 || title == 0xFFFE0000; }

// The title ID in an XBE's header, as it sits in the Xbox's memory at
// 0x10000: `XBEH`, the base address at 0x104 and the certificate's at 0x118,
// the title ID eight bytes into the certificate (xemu-xbe.h). Zero when it is
// not there yet.
uint32_t xbeTitle(const std::string& h) {
    auto le = [&](size_t at) {
        return static_cast<uint32_t>(static_cast<uint8_t>(h[at])) |
               static_cast<uint32_t>(static_cast<uint8_t>(h[at + 1])) << 8 |
               static_cast<uint32_t>(static_cast<uint8_t>(h[at + 2])) << 16 |
               static_cast<uint32_t>(static_cast<uint8_t>(h[at + 3])) << 24;
    };
    if (h.size() < 0x120 || h.compare(0, 4, "XBEH") != 0) return 0;
    const uint32_t base = le(0x104), cert = le(0x118);
    if (cert < base || cert - base + 12 > h.size()) return 0;
    return le(cert - base + 8);
}

// One QMP command and its answer: lines until one says "return" or "error"
// (events can come first). False on no answer or an error.
bool qmpCommand(int fd, const std::string& cmd) {
    const std::string line = cmd + "\n";
    if (::send(fd, line.data(), line.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(line.size()))
        return false;
    std::string got;
    char buf[4096];
    for (int reads = 0; reads < 16; ++reads) {
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) return false;
        got.append(buf, static_cast<size_t>(n));
        size_t nl;
        while ((nl = got.find('\n')) != std::string::npos) {
            const std::string one = got.substr(0, nl);
            got.erase(0, nl + 1);
            if (one.find("\"return\"") != std::string::npos) return true;
            if (one.find("\"error\"") != std::string::npos) return false;
        }
    }
    return false;
}

// What the image carries for xemu: `xbox_hdd.qcow2`, xemu's own blank drive
// (xemu-project/xemu-dashboard, pinned by build_files/install-xemu-assets.sh),
// and `eeprom.bin`, the one EEPROM every CabinetOS console uses. For
// development, `CABINETOS_XEMU_ASSETS` points somewhere else.
std::string xemuAssets() {
    if (const char* v = std::getenv("CABINETOS_XEMU_ASSETS"); v && *v) return v;
    return "/usr/share/cabinetos/xemu";
}

// THE XBOX'S RESET CODE, which begins every MCPX boot ROM (`33 C0`, xor ax,ax)
// and ends it (`02 EE`), and which a flash image carries in its top 512 bytes
// too. Read from both files on RomM, 2026-09-29, and from xemu's docs
// (required-files.md) for the ROM. It is how both are recognised: by what is
// in them, never by a name (RomM's is `bios_retail_4627.bin`, xemu's docs say
// Complex 4627, and nothing else in `bios/` looks like this).
bool xboxResetCode(const std::string& d, size_t at) {
    return d.size() >= at + 512 && static_cast<uint8_t>(d[at]) == 0x33 &&
           static_cast<uint8_t>(d[at + 1]) == 0xC0 && static_cast<uint8_t>(d[at + 510]) == 0x02 &&
           static_cast<uint8_t>(d[at + 511]) == 0xEE;
}

std::string findXboxFile(bool bootRom) {
    const std::string bios = storage::biosDir();
    for (const std::string& name : listDir(bios)) {
        const std::string p = bios + "/" + name;
        struct stat st;
        if (::stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        const int64_t n = st.st_size;
        if (bootRom ? n != 512 : (n != 256 * 1024 && n != 512 * 1024 && n != 1024 * 1024)) continue;
        const std::string d = readFile(p);
        if (xboxResetCode(d, d.size() - 512)) return p;
    }
    return {};
}

// A TOML basic string: a game's folder can hold an apostrophe.
std::string tomlQuote(const std::string& v) {
    std::string out = "\"";
    for (const char c : v) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

}  // namespace

bool prepareXemu(const Emulator& e, const std::string& entryPath, int players,
                 quality::Level level, bool* missing, std::string* err) {
    const std::string mcpx = findXboxFile(true);
    const std::string flash = findXboxFile(false);
    if (mcpx.empty() || flash.empty()) {
        std::fprintf(stderr, "[xbox] boot ROM %s, BIOS %s\n",
                     mcpx.empty() ? "missing" : mcpx.c_str(),
                     flash.empty() ? "missing" : flash.c_str());
        *missing = true;
        return false;
    }
    const std::string dir = home(e);
    storage::makeDirs(dir);
    // Copied into its home, where the sandbox is given a path it can read.
    // THE EEPROM IS PUT BACK EVERY TIME: some games lock their saves to its
    // key, so it must be the same on every console and never drift.
    const std::string assets = xemuAssets();
    if (!placeFile(mcpx, dir + "/mcpx.bin") || !placeFile(flash, dir + "/flash.bin") ||
        !placeFile(assets + "/eeprom.bin", dir + "/eeprom.bin")) {
        *err = "could not put xemu's BIOS and EEPROM in " + dir;
        return false;
    }
    // THE GAME'S DRIVE, from the blank one the first time.
    const std::string drive = entryPath + "/" + kXboxDrive;
    if (!isFile(drive) && !placeFile(assets + "/" + kXboxDrive, drive)) {
        *err = "could not make a drive from " + assets + "/" + kXboxDrive;
        return false;
    }
    // EACH PLAYER'S CONTROLLER, BY NAME, in the console's order, and nothing
    // else: auto-binding would hand xemu the real pads as well.
    std::string db;
    for (int p = 0; p < cab::vpad::kMaxPlayers; ++p) db += cab::vpad::sdlMapping(p) + "\n";
    if (!writeFile(dir + "/gamecontrollerdb.txt", db)) {
        *err = "could not write xemu's controller list";
        return false;
    }
    // THE CONSOLE'S SETTINGS, written whole before every start; xemu writes
    // back only what differs from its defaults on exit (xemu-settings.cc), so
    // nothing it wrote last time survives this. Names from config_spec.yml.
    std::string t;
    t += "[general]\nshow_welcome = false\nskip_boot_anim = true\n";
    t += "[general.updates]\ncheck = false\n";
    t += "[sys.files]\n";
    t += "bootrom_path = " + tomlQuote(dir + "/mcpx.bin") + "\n";
    t += "flashrom_path = " + tomlQuote(dir + "/flash.bin") + "\n";
    t += "eeprom_path = " + tomlQuote(dir + "/eeprom.bin") + "\n";
    t += "hdd_path = " + tomlQuote(drive) + "\n";
    // VULKAN, not xemu's OpenGL default: the console is built for it, and PS3
    // and Switch already use it (MMagTech, 2026-09-29). On the GPU the console
    // itself chose, never llvmpipe, which also offers Vulkan (gpu.cpp).
    t += "[display]\nrenderer = \"VULKAN\"\n";
    const std::string gpu = cab::gpu::vulkan().deviceName;
    if (!gpu.empty())
        t += "[display.vulkan]\npreferred_physical_device = " + tomlQuote(gpu) + "\n";
    // PICTURE QUALITY (#63): xemu's internal resolution.
    t += "[display.quality]\n" + quality::xemu(level);
    // VSYNC ON, xemu's default, as for every emulator since 2026-10-04
    // (#209). Also measured on the A9 2026-10-02 (FlatOut, frames.py): with
    // it off xemu redrew the screen 631 times a second, all wasted on the
    // graphics chip; its own limiter paces the game, not the picture.
    t += "[display.window]\nfullscreen_on_startup = true\n";
    t += "[display.ui]\nshow_menubar = false\nshow_notifications = false\nhide_cursor = true\n";
    t += "[input]\nauto_bind = false\n";
    t += "gamecontrollerdb_path = " + tomlQuote(dir + "/gamecontrollerdb.txt") + "\n";
    t += "[input.bindings]\n";
    for (int p = 0; p < 4; ++p)
        t += "port" + std::to_string(p + 1) + " = " +
             tomlQuote(p < players ? cab::vpad::sdlGuid(p) : std::string()) + "\n";
    if (!writeFile(dir + "/xemu.toml", t)) {
        *err = "could not write xemu's settings";
        return false;
    }
    return true;
}

namespace {

// A FULL DISC DUMP (redump) has the game partition 387 MiB in, where xemu
// cannot boot it; an XISO has it at the start. Both carry the Xbox's volume
// descriptor, `MICROSOFT*XBOX*MEDIA`, 64 KiB into the game partition
// (extract-xiso.c). Checked on the four games on RomM, 2026-09-29: all XISO.
constexpr uint64_t kRedumpGameOffset = 0x18300000;
bool xboxMediaAt(const std::string& path, uint64_t at) {
    std::ifstream in(path, std::ios::binary);
    char m[20] = {};
    in.seekg(static_cast<std::streamoff>(at + 0x10000));
    return in.read(m, sizeof m) && std::memcmp(m, "MICROSOFT*XBOX*MEDIA", 20) == 0;
}

// Cuts a full dump down to its game partition, in place, and notes the file
// as installed at its original size so it is not fetched again. A cancel
// (Escape, an Eject, the console leaving) stops the copy between two blocks
// and leaves the dump as it came (#472).
bool trimRedump(const std::string& entryPath, const std::string& name,
                const std::atomic<bool>& cancel, std::string* message) {
    const std::string path = entryPath + "/" + name;
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
    const std::string cut = path + ".cut";
    std::FILE* in = std::fopen(path.c_str(), "rb");
    std::FILE* out = std::fopen(cut.c_str(), "wb");
    bool ok = in && out && fseeko(in, static_cast<off_t>(kRedumpGameOffset), SEEK_SET) == 0;
    std::vector<char> buf(4 << 20);
    while (ok) {
        if (cancel.load()) { ok = false; break; }
        const size_t n = std::fread(buf.data(), 1, buf.size(), in);
        if (n == 0) break;
        ok = std::fwrite(buf.data(), 1, n, out) == n;
    }
    if (in) std::fclose(in);
    if (out && std::fclose(out) != 0) ok = false;
    if (!ok || std::rename(cut.c_str(), path.c_str()) != 0) {
        ::unlink(cut.c_str());
        *message = "Couldn't open this game's file";
        return false;
    }
    std::string note = readFile(entryPath + "/" + kInstalledNote);
    note += std::to_string(static_cast<long long>(st.st_size)) + " " + name + "\n";
    writeFile(entryPath + "/" + kInstalledNote, note);
    std::fprintf(stderr, "[xbox] %s was a full disc dump; kept its game partition\n",
                 name.c_str());
    return true;
}

}  // namespace

bool installXbox(const std::string& entryPath, const std::atomic<bool>& cancel,
                 std::string* romPath, std::string* message) {
    for (const std::string& name : listDir(entryPath)) {
        if (name[0] == '.' || name == kXboxDrive) continue;
        const std::string p = entryPath + "/" + name;
        if (!isFile(p) || xboxMediaAt(p, 0)) continue;
        if (xboxMediaAt(p, kRedumpGameOffset) && !trimRedump(entryPath, name, cancel, message))
            return false;
    }
    std::string pick;
    int64_t pickSize = -1;
    for (const std::string& name : listDir(entryPath)) {
        struct stat st;
        const std::string p = entryPath + "/" + name;
        if (name[0] == '.' || name == kXboxDrive || ::stat(p.c_str(), &st) != 0 ||
            !S_ISREG(st.st_mode) || !xboxMediaAt(p, 0))
            continue;
        if (st.st_size > pickSize) {
            pick = p;
            pickSize = st.st_size;
        }
    }
    if (pick.empty()) {
        std::fprintf(stderr, "[xbox] no Xbox disc image in %s\n", entryPath.c_str());
        *message = "Couldn't open this game's file";
        return false;
    }
    *romPath = pick;
    return true;
}

// What beforeStart does for xemu: this person's saves onto the game's drive,
// then the note in the game's folder.
bool xemuBeforeStart(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                     const std::string& note, std::string* err) {
    const std::string drive = entryPath + "/" + kXboxDrive;
    std::string why;
    if (!xboxhdd::writeSaves(drive, saveRoot(e, saveDir), &why)) {
        // A drive this cannot write is thrown away for a blank one: it holds
        // nothing the save zip does not.
        std::fprintf(stderr, "[xbox] %s: %s; starting from a blank drive\n", drive.c_str(),
                     why.c_str());
        ::unlink(drive.c_str());
        if (!placeFile(xemuAssets() + "/" + kXboxDrive, drive) ||
            !xboxhdd::writeSaves(drive, saveRoot(e, saveDir), &why)) {
            *err = "could not put the saves on " + drive + ": " + why;
            return false;
        }
    }
    // The note only once the saves are on: from here until afterEnd, this
    // folder holds somebody's saves that are nowhere else once the game
    // writes to the drive. Until the game runs the drive holds nothing their
    // folder lacks, and a note beside a drive a stop left half written had
    // the next start copy it back over their saves (#398, EMU-15).
    if (!writePlayingNote(entryPath, note)) {
        *err = "could not mark " + entryPath + " as playing";
        return false;
    }
    return true;
}

// What afterEnd does for xemu: the saves off the drive, and only then the note.
bool xemuAfterEnd(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                  std::string* err) {
    const std::string drive = entryPath + "/" + kXboxDrive;
    if (!xboxhdd::readSaves(drive, saveRoot(e, saveDir), err)) {
        // The note stays: the saves are still only on the drive.
        return false;
    }
    removePlayingNote(entryPath);
    return true;
}

}  // namespace cab::standalone::detail

namespace cab::standalone {

using namespace detail;

uint32_t Run::pollXboxTitle() {
    const std::string dir = home(*emu_);
    if (qmpFd_ < 0) {
        const std::string path = dir + "/qmp.sock";
        const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (fd < 0 || path.size() >= sizeof addr.sun_path) {
            if (fd >= 0) ::close(fd);
            return 0;
        }
        std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
            ::close(fd);
            return 0;   // not listening yet
        }
        timeval tv{0, 300 * 1000};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        qmpFd_ = fd;
        if (!qmpCommand(qmpFd_, "{\"execute\":\"qmp_capabilities\"}")) {
            ::close(qmpFd_);
            qmpFd_ = -1;
            return 0;
        }
    }
    const std::string dump = dir + "/xbe-header.bin";
    ::unlink(dump.c_str());
    if (!qmpCommand(qmpFd_, "{\"execute\":\"memsave\",\"arguments\":{\"val\":65536,"
                            "\"size\":4096,\"filename\":" + yamlQuote(dump) + "}}"))
        return 0;
    return xbeTitle(readFile(dump));
}

// XBOX. Its home is its XDG data folder, where xemu keeps anything it
// writes by itself; the game's folder is writable because the drive is
// in it (and a disc on a read-only mount cannot take qemu's lock: xemu
// #2958). X11 only: the Flatpak asks SDL for Wayland first.
bool Run::startXemu(const Emulator& e, const std::string& romPath, const std::string& dir,
                    const std::string& gameDir, const std::string& out, std::string* err) {
    const char* xdgCache = std::getenv("XDG_CACHE_HOME");
    const char* userHome = std::getenv("HOME");
    const std::string mesaCache = (xdgCache && *xdgCache
                                       ? std::string(xdgCache)
                                       : std::string(userHome ? userHome : "") + "/.cache");
    storage::makeDirs(mesaCache + "/mesa_shader_cache");
    const std::vector<std::string> args = {
        "flatpak", "run",
        // No network (#557), as for every emulator (Run::start).
        "--unshare=network",
        "--cwd=" + dir,
        "--filesystem=" + dir,
        "--filesystem=" + gameDir,
        // THE DRIVER'S SHADER MEMORY IS SHARED with the console's, as
        // RPCS3's is (Run::start), so its first-run wait is paid once.
        "--filesystem=" + mesaCache + "/mesa_shader_cache",
        "--env=MESA_SHADER_CACHE_DIR=" + mesaCache,
        "--env=XDG_DATA_HOME=" + dir,
        "--env=SDL_VIDEODRIVER=x11",
        "--nosocket=wayland",
        "--env=SDL_JOYSTICK_HIDAPI=0",
        // ONLY THE VIRTUAL CONTROLLERS, not the real pads too. xemu's own
        // menu reads every gamepad SDL shows it, bound to a port or not
        // (ui/xui/input-manager.cc), so a real pad's Guide, or Back and
        // Start together, opened it over the game (FlatOut, 2026-09-29).
        // The ID is vpad.cpp's; SDL's own hint, no patch.
        "--env=SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1209/0xCAB0",
        "--env=LC_NUMERIC=C",
        std::string("--command=") + e.program,
        e.flatpak,
        "-config_path", dir + "/xemu.toml",
        "-dvd_path", romPath,
        // qemu's control socket, for the dashboard watch.
        "-qmp", "unix:" + dir + "/qmp.sock,server=on,wait=off",
    };
    ::unlink((dir + "/qmp.sock").c_str());
    root_ = proc::spawn(args, out);
    if (root_ <= 0) {
        *err = "could not start flatpak";
        root_ = -1;
        return false;
    }
    startMs_ = nowMs();
    std::fprintf(stderr, "[standalone] started %s (pid %d) on %s\n", e.flatpak, root_,
                 romPath.c_str());
    return true;
}

// Run::poll's look, once a second, at what the Xbox is running.
void Run::watchDashboard(int64_t now) {
    if (emu_->watchesDashboard && !stopping() && !frozen_ && now - qmpCheckMs_ >= 1000) {
        qmpCheckMs_ = now;
        const uint32_t title = pollXboxTitle();
        if (title != 0 && !isXboxDashboard(title) && title != xboxTitle_) {
            xboxTitle_ = title;
            std::fprintf(stderr, "[xbox] running title %08X\n", title);
        } else if (title != 0 && isXboxDashboard(title)) {
            if (xboxTitle_ != 0) {
                std::fprintf(stderr, "[xbox] the game went to the dashboard; closing xemu\n");
                stop();
            } else {
                std::fprintf(stderr, "[xbox] the dashboard came up, not the game\n");
                failedLoad_ = true;
                stopAtMs_ = now;
                kill9();
            }
        }
    }
}

}  // namespace cab::standalone
