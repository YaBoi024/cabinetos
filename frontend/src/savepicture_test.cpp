// `--save-picture-test` (#346): the save's own picture, with no core, no
// screen and no server. In a scratch root, a pretend game whose battery,
// files and save folder the test changes as a game would, and whose frame is
// a plain colour that says when it was taken: when the picture is taken, of
// which moment, how small, where it is kept, which the snapshot names, and
// when it goes. Its own file, as `--save-push-test` has its own.

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "cache.h"
#include "fileio.h"
#include "imagecodec.h"
#include "savepicture.h"
#include "savepush.h"
#include "saves.h"
#include "standalone.h"
#include "storage.h"

namespace savepicture {
namespace {

// A frame of one colour, `shade` in red, so a picture says which moment it is.
std::vector<uint8_t> plain(unsigned w, unsigned h, uint8_t shade) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 0);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = shade;
        px[i + 3] = 0xFF;
    }
    return px;
}

// The game's area lit in a frame of black: columns [x0, x1) of rows
// [y0, y1), `shade` in red.
void fill(std::vector<uint8_t>& px, unsigned w, unsigned x0, unsigned y0, unsigned x1, unsigned y1,
          uint8_t shade) {
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = x0; x < x1; ++x) px[(static_cast<size_t>(y) * w + x) * 4] = shade;
}

// The red of the picture's leftmost pixel half way down, which bars leave black.
int leftRed(const std::string& path) {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!ui::decodeImage(cab::readBytes(path), rgba, w, h) || w <= 0 || h <= 0) return -1;
    return rgba[static_cast<size_t>(h / 2) * w * 4];
}

// The picture's size and the red of its middle pixel; false when it is no
// picture.
bool readPicture(const std::string& path, int* w, int* h, int* red) {
    std::vector<uint8_t> rgba;
    if (path.empty() || !ui::decodeImage(cab::readBytes(path), rgba, *w, *h) || *w <= 0)
        return false;
    const size_t mid = (static_cast<size_t>(*h / 2) * *w + *w / 2) * 4;
    *red = rgba[mid];
    return true;
}

// The red of the picture's last row, which a file read before it was whole
// leaves black.
int bottomRed(const std::string& path) {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!ui::decodeImage(cab::readBytes(path), rgba, w, h) || w <= 0 || h <= 0) return -1;
    return rgba[(static_cast<size_t>(h - 1) * w + w / 2) * 4];
}

bool exists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

// How many of the game's pictures are in the person's pending folder.
int picturesOf(const storage::User& u, int romId) {
    int n = 0;
    const std::string prefix = prefixFor(romId);
    if (DIR* d = ::opendir(storage::pendingDir(u).c_str())) {
        while (struct dirent* e = ::readdir(d))
            if (std::string(e->d_name).compare(0, prefix.size(), prefix) == 0) ++n;
        ::closedir(d);
    }
    return n;
}

}  // namespace

int test() {
    std::printf("the save's own picture\n\n");
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    char root[] = "/tmp/cabinetos-save-picture-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[save picture] could not make a scratch root\n");
        return 1;
    }
    setenv("CABINETOS_DRIVES", (std::string(root) + "/drives").c_str(), 1);
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[save picture] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    storage::User u;
    u.id = 1;
    u.name = "picture-test";
    storage::setCurrentUser(u);

    // ---- The pretend game ------------------------------------------------------
    std::vector<uint8_t> ram(32768, 0x11);
    uint8_t shade = 10;
    unsigned frameW = 800, frameH = 600;
    bool hasFrame = true;
    Game game;
    game.battery = [&](std::vector<uint8_t>* out) {
        *out = ram;
        return true;
    };
    game.frame = [&](std::vector<uint8_t>* rgba, unsigned* w, unsigned* h) {
        if (!hasFrame) return false;
        *rgba = plain(frameW, frameH, shade);
        *w = frameW;
        *h = frameH;
        return true;
    };
    // The picture being made on the worker, waited for: ticks at one moment,
    // so no new look is taken meanwhile.
    auto settle = [&](Watch& w, GameSession& s, uint64_t at) {
        const std::string before = s.savePicture;
        for (int i = 0; i < 60 && s.savePicture == before; ++i) {
            w.tick(s, at);
            usleep(5000);
        }
    };
    // A look a little later in real time too, so a picture asked of gamescope
    // on the worker has been asked by then.
    auto later = [&](Watch& w, GameSession& s, uint64_t at) {
        usleep(20000);
        w.tick(s, at);
    };
    auto battery = [&](int romId) {
        GameSession s;
        s.romId = romId;
        s.title = "Pokemon Red";
        s.fsStem = "Pokemon - Red Version (USA, Europe)";
        s.saveTag = "gambatte-native";
        s.saveDir = storage::savesDir(u, "gb", romId, "gambatte");
        s.saveAtLaunch = ram;
        return s;
    };

    std::printf("a battery, looked at once a second\n");
    int game1 = 500;
    {
        Watch w;
        GameSession s = battery(game1);
        w.begin(s, u, game, 0);
        for (uint64_t t = 1000; t <= 3000; t += 1000) w.tick(s, t);
        check(s.savePicture.empty() && picturesOf(u, game1) == 0,
              "a game whose save does not change: no picture");
        // The game writes its save between two looks.
        ram[100] = 0x22;
        shade = 40;
        w.tick(s, 3500);
        w.tick(s, 4000);
        check(s.savePicture.empty(), "the look that first sees it changed takes no picture");
        shade = 60;
        w.tick(s, 4500);
        w.tick(s, 5000);
        settle(w, s, 5000);
        int pw = 0, ph = 0, red = 0;
        check(readPicture(s.savePicture, &pw, &ph, &red) && red == 60,
              "the next look, the save as it was, takes the picture of that moment");
        check(pw == 640 && ph == 480, "made no wider than 640, its shape kept (800x600 to 640x480)");
        const std::string first = s.savePicture;
        // A save the game writes at every look takes no picture until it stops.
        for (uint64_t t = 6000; t <= 8000; t += 1000) {
            ram[200] = static_cast<uint8_t>(t / 1000);
            shade = static_cast<uint8_t>(70 + t / 1000);
            w.tick(s, t);
            settle(w, s, t);
        }
        check(s.savePicture == first, "a save still changing at every look: no new picture");
        shade = 90;
        w.tick(s, 9000);
        settle(w, s, 9000);
        check(readPicture(s.savePicture, &pw, &ph, &red) && red == 90 && s.savePicture != first,
              "once it stops, a new picture of that moment");
        check(!exists(first) && picturesOf(u, game1) == 1,
              "the sitting's earlier picture deleted: one picture a game, the newest");
        // Saved, then left within the second.
        ram[300] = 0x33;
        shade = 120;
        const std::string before = s.savePicture;
        w.leaving(s, 9300);
        w.finish(s, 9400);
        check(readPicture(s.savePicture, &pw, &ph, &red) && red == 120 && !exists(before),
              "a save left before it settled: its picture taken as the game is left");
        // And marked with the save (savepush::mark).
        cab::writeBytes(s.saveDir + "/" + saveRowName(s.fsStem, "srm"), ram);
        s.saveChanged = true;
        cache::OwedPush p;
        check(savepush::mark(s, u) && cache::owedPush(u, game1, &p) &&
                  p.savePicturePath == s.savePicture &&
                  p.savePictureName == "Pokemon - Red Version (USA, Europe).png",
              "the snapshot owed names the picture, after the save's name");
        const std::string marked = p.savePicturePath;
        savepush::StateMade made{"a.state", s.saveDir + "/a.state", "", "", 1};
        cab::writeBytes(made.path, std::string("state"));
        s.stateTag = "gambatte-native";
        check(savepush::markState(s, u, made) && cache::owedPush(u, game1, &p) &&
                  p.savePicturePath == marked && exists(marked),
              "a state made after keeps the save's picture in the snapshot owed");
        // The next sitting, nothing saved: the owed picture is kept.
        Watch w2;
        GameSession s2 = battery(game1);
        w2.begin(s2, u, game, 0);
        w2.tick(s2, 1000);
        w2.tick(s2, 2000);
        w2.leaving(s2, 2100);
        w2.finish(s2, 2200);
        check(s2.savePicture.empty() && exists(marked),
              "a sitting with no save takes no picture and keeps the one still owed");
        // A sitting that saves again: its picture replaces the owed one.
        Watch w3;
        GameSession s3 = battery(game1);
        w3.begin(s3, u, game, 0);
        ram[400] = 0x44;
        shade = 150;
        w3.tick(s3, 1000);
        w3.tick(s3, 2000);
        settle(w3, s3, 2000);
        w3.leaving(s3, 2100);
        w3.finish(s3, 2200);
        cab::writeBytes(s3.saveDir + "/" + saveRowName(s3.fsStem, "srm"), ram);
        s3.saveChanged = true;
        check(savepush::mark(s3, u) && cache::owedPush(u, game1, &p) &&
                  p.savePicturePath == s3.savePicture && !exists(marked) &&
                  picturesOf(u, game1) == 1,
              "a newer save owed with its own picture: the older save's picture deleted");
        const std::string owedPic = p.savePicturePath;
        cache::clearPushOwed(u, game1, p.stamp);
        check(!exists(owedPic) && picturesOf(u, game1) == 0,
              "the snapshot landed: its marker and its picture gone");
    }

    {
        // A core that stops reporting its battery (Genesis Plus GX, while
        // the battery is blank) has not saved.
        Watch w;
        GameSession s = battery(510);
        w.begin(s, u, game, 0);
        const std::vector<uint8_t> kept = ram;
        ram.clear();
        w.tick(s, 1000);
        w.tick(s, 2000);
        settle(w, s, 2000);
        check(s.savePicture.empty(), "a battery the core reports none of: no picture");
        ram = kept;
        w.finish(s, 2100);
    }

    std::printf("\na sitting whose save never went\n");
    {
        const int game2 = 501;
        const std::string stale = pathFor(u, game2, 1000);
        cab::writeBytes(stale, std::string("not a picture"));
        Watch w;
        GameSession s = battery(game2);
        w.begin(s, u, game, 0);
        check(!exists(stale), "a picture no snapshot owed names is deleted at the game's next start");
        // A save with no picture of its own does not take an older one.
        ram[500] = 0x55;
        hasFrame = false;
        w.tick(s, 1000);
        w.tick(s, 2000);
        settle(w, s, 2000);
        w.leaving(s, 2100);
        w.finish(s, 2200);
        hasFrame = true;
        cab::writeBytes(s.saveDir + "/" + saveRowName(s.fsStem, "srm"), ram);
        s.saveChanged = true;
        cache::OwedPush p;
        check(s.savePicture.empty() && savepush::mark(s, u) && cache::owedPush(u, game2, &p) &&
                  p.savePicturePath.empty(),
              "no frame to take: the save goes with no picture");
        cache::clearPushOwed(u, game2, p.stamp);
    }

    std::printf("\nthe files a core writes\n");
    {
        const int game3 = 502;
        GameSession s;
        s.romId = game3;
        s.title = "Contra 4";
        s.fsStem = "Contra 4 (USA)";
        s.saveTag = "melonds-native";
        s.saveDir = storage::savesDir(u, "nds", game3, "melonds");
        cab::FileSaveState f;
        f.spec.path = "Contra 4 (USA).sav";
        f.spec.captureSuffix = ".sav";
        f.spec.untouched = catalog::Untouched::Uniform;
        f.path = s.saveDir + "/Contra 4 (USA).sav";
        f.atLaunch = std::vector<uint8_t>(512, 0x00);
        f.hadOne = true;
        cab::writeBytes(f.path, f.atLaunch);
        s.fileSaves = {f};
        Watch w;
        w.begin(s, u, Game{nullptr, game.frame, nullptr}, 0);
        usleep(20000);
        cab::writeBytes(f.path, f.atLaunch);
        w.tick(s, 1000);
        w.tick(s, 2000);
        settle(w, s, 2000);
        check(s.savePicture.empty(), "the file written again with the same bytes: no picture");
        std::vector<uint8_t> saved = f.atLaunch;
        saved[10] = 0x7A;
        shade = 170;
        cab::writeBytes(f.path, saved);
        w.tick(s, 3000);
        w.tick(s, 4000);
        settle(w, s, 4000);
        int pw = 0, ph = 0, red = 0;
        check(readPicture(s.savePicture, &pw, &ph, &red) && red == 170,
              "the file written with new bytes: the picture once it stops changing");
        w.finish(s, 4100);
        // A core's first file, which holds nothing yet.
        GameSession n = s;
        n.romId = 503;
        n.fileSaves[0].atLaunch.clear();
        n.fileSaves[0].hadOne = false;
        cab::removeFile(f.path);
        Watch wn;
        wn.begin(n, u, Game{nullptr, game.frame, nullptr}, 0);
        cab::writeBytes(f.path, std::vector<uint8_t>(512, 0xFF));
        wn.tick(n, 1000);
        wn.tick(n, 2000);
        settle(wn, n, 2000);
        check(n.savePicture.empty(),
              "the file a core writes by starting up, holding nothing: no picture");
        wn.finish(n, 2100);
        // A file the core writes only as the game closes (Flycast's board
        // memory, written also whenever a state is made): not looked at.
        GameSession b = s;
        b.romId = 506;
        b.fileSaves[0].spec.writtenAtClose = true;
        b.fileSaves[0].atLaunch = cab::readBytes(f.path);
        Watch wb;
        wb.begin(b, u, Game{nullptr, game.frame, nullptr}, 0);
        std::vector<uint8_t> board = b.fileSaves[0].atLaunch;
        board[20] ^= 0x33;
        cab::writeBytes(f.path, board);
        wb.tick(b, 1000);
        wb.tick(b, 2000);
        settle(wb, b, 2000);
        wb.leaving(b, 2100);
        wb.finish(b, 2200);
        check(b.savePicture.empty(),
              "a file the core writes only as the game closes, written in play: no picture");
        savepicture::prune(u, game3);
    }

    std::printf("\na save folder\n");
    {
        const int game4 = 504;
        GameSession s;
        s.romId = game4;
        s.title = "Lumines";
        s.fsStem = "Lumines (USA)";
        s.saveTag = "ppsspp-native";
        s.saveDir = storage::savesDir(u, "psp", game4, "ppsspp");
        s.dirSaveRoot = s.saveDir + "/PSP/SAVEDATA";
        s.dirSaveCore = "ppsspp";
        s.dirSavePlatform = "psp";
        cab::writeBytes(s.dirSaveRoot + "/ULUS10002/PARAM.SFO", std::string("param"));
        s.dirAtLaunch = listDirSave(s);
        Watch w;
        w.begin(s, u, game, 0);
        ram[600] = 0x66;   // a battery is not this game's save
        w.tick(s, 1000);
        w.tick(s, 2000);
        settle(w, s, 2000);
        check(s.savePicture.empty(), "a game with a save folder reads no battery");
        shade = 190;
        cab::writeBytes(s.dirSaveRoot + "/ULUS10002/DATA.BIN", std::string("data"));
        w.tick(s, 3000);
        w.tick(s, 4000);
        settle(w, s, 4000);
        int pw = 0, ph = 0, red = 0;
        check(readPicture(s.savePicture, &pw, &ph, &red) && red == 190,
              "any file of the save written: the picture once it stops changing");
        w.finish(s, 4100);
        savepicture::prune(u, game4);
    }

    std::printf("\nan emulator run as a program\n");
    {
        const int game5 = 505;
        GameSession s;
        s.romId = game5;
        s.title = "Super Mario Odyssey";
        s.fsStem = "Super Mario Odyssey";
        s.saveTag = "eden";
        s.standaloneCore = "eden";
        s.saveDir = storage::savesDir(u, "switch", game5, "eden");
        s.dirSaveRoot = s.saveDir + "/nand/user/save";
        cab::writeBytes(s.dirSaveRoot + "/0000/progress.bin", std::string("one"));
        s.dirAtLaunch = listDirSave(s);
        cab::standalone::writePlayingNote(s.saveDir, playingNoteFor(s, s.saveDir));
        std::atomic<int> asked{0};
        std::atomic<bool> offThread{true};
        const std::thread::id caller = std::this_thread::get_id();
        std::atomic<bool> writes{true};
        // gamescope's pictures, made beforehand: asking returns at once, as
        // `gamescopectl` does, and the file is there a moment later.
        std::vector<uint8_t> shot210, shot220, shot230;
        ui::encodePNG(plain(1920, 1080, 210), 1920, 1080, shot210);
        ui::encodePNG(plain(1920, 1080, 220), 1920, 1080, shot220);
        ui::encodePNG(plain(1920, 1080, 230), 1920, 1080, shot230);
        Game prog;
        prog.askProgram = [&](const std::string& path) {
            ++asked;
            if (std::this_thread::get_id() == caller) offThread = false;
            if (writes) cab::writeBytes(path, shot210);
            return true;
        };
        Watch w;
        w.begin(s, u, prog, 0);
        cab::writeBytes(s.dirSaveRoot + "/0000/progress.bin", std::string("two"));
        w.tick(s, 1000);
        w.tick(s, 2000);
        for (uint64_t t = 2500; t <= 4000; t += 500) later(w, s, t);
        settle(w, s, 4000);
        int pw = 0, ph = 0, red = 0;
        check(asked == 1 && offThread && readPicture(s.savePicture, &pw, &ph, &red) &&
                  red == 210 &&
                  pw == 640 && ph == 360,
              "gamescope asked on the worker, never the caller's thread, and its picture "
              "read back once whole and made 640 wide");
        check(picturesOf(u, game5) == 1, "gamescope's own file deleted once read");
        std::string dir;
        const GameSession back =
            sessionOfPlayingNote(cab::standalone::playingNote(s.saveDir), &dir);
        check(back.savePicture == s.savePicture && back.romId == game5,
              "the playing note names the picture, for a power cut's recovery");
        // The program went by itself after another save: no picture of it.
        cab::writeBytes(s.dirSaveRoot + "/0000/progress.bin", std::string("three"));
        const std::string pic = s.savePicture;
        w.finish(s, 4500);
        check(s.savePicture.empty() && !exists(pic),
              "a program that went by itself with its save changed after its picture: none goes");
        // Left on purpose: the picture asked as it closes, and waited for.
        Watch w2;
        GameSession s2 = s;
        w2.begin(s2, u, prog, 0);
        cab::writeBytes(s2.dirSaveRoot + "/0000/progress.bin", std::string("four"));
        w2.leaving(s2, 500);
        // The program writes the last of its save as it closes.
        cab::writeBytes(s2.dirSaveRoot + "/0000/progress.bin", std::string("four, closed"));
        w2.finish(s2, 600);
        check(asked == 2 && readPicture(s2.savePicture, &pw, &ph, &red) && red == 210,
              "left on purpose, saved within the second: the picture asked as it closes, kept "
              "though the program wrote the last of its save as it closed");
        // gamescope still writing when first looked at: read once whole.
        bool half = true;
        const std::vector<uint8_t>& full = shot220;
        prog.askProgram = [&](const std::string& path) {
            ++asked;
            cab::writeBytes(path, std::vector<uint8_t>(full.begin(), full.begin() + full.size() / 2));
            return true;
        };
        Watch w4;
        GameSession s4 = s;
        w4.begin(s4, u, prog, 0);
        cab::writeBytes(s4.dirSaveRoot + "/0000/progress.bin", std::string("half"));
        w4.tick(s4, 1000);
        w4.tick(s4, 2000);
        later(w4, s4, 2500);
        if (half) {
            half = false;
            std::string raw;
            if (DIR* d = ::opendir(storage::pendingDir(u).c_str())) {
                while (struct dirent* e = ::readdir(d)) {
                    const std::string name = e->d_name;
                    if (name.find(".gamescope.png") != std::string::npos)
                        raw = storage::pendingDir(u) + "/" + name;
                }
                ::closedir(d);
            }
            cab::writeBytes(raw, full);
        }
        for (uint64_t t = 3000; t <= 4500; t += 500) later(w4, s4, t);
        settle(w4, s4, 4500);
        check(readPicture(s4.savePicture, &pw, &ph, &red) && red == 220 &&
                  bottomRed(s4.savePicture) == 220,
              "gamescope still writing its file: read only once its size stops changing, the "
              "whole picture");
        w4.finish(s4, 4600);
        // gamescope never writes one.
        prog.askProgram = [&](const std::string&) {
            ++asked;
            return true;
        };
        writes = false;
        Watch w3;
        GameSession s3 = s;
        w3.begin(s3, u, prog, 0);
        cab::writeBytes(s3.dirSaveRoot + "/0000/progress.bin", std::string("five"));
        w3.tick(s3, 1000);
        w3.tick(s3, 2000);
        for (uint64_t t = 2500; t <= 2000 + kProgramWaitMs + 1000; t += 500) later(w3, s3, t);
        check(s3.savePicture.empty() && picturesOf(u, game5) == 0,
              "gamescope writing nothing: given up after ten seconds, nothing left");
        w3.finish(s3, 20000);
        // And gamescope back: the next save still gets its picture.
        Watch w5;
        GameSession s5 = s;
        bool fails = true;
        prog.askProgram = [&](const std::string& path) {
            ++asked;
            if (fails) return true;
            return cab::writeBytes(path, shot230);
        };
        w5.begin(s5, u, prog, 0);
        cab::writeBytes(s5.dirSaveRoot + "/0000/progress.bin", std::string("six"));
        w5.tick(s5, 1000);
        w5.tick(s5, 2000);
        for (uint64_t t = 2500; t <= 2000 + kProgramWaitMs + 1000; t += 500) later(w5, s5, t);
        fails = false;
        cab::writeBytes(s5.dirSaveRoot + "/0000/progress.bin", std::string("seven"));
        for (uint64_t t = 14000; t <= 17000; t += 500) later(w5, s5, t);
        settle(w5, s5, 17000);
        check(readPicture(s5.savePicture, &pw, &ph, &red) && red == 230,
              "after gamescope gave nothing, the next save still gets its picture");
        w5.finish(s5, 17100);
    }

    std::printf("\nPlayStation 2 in its own window\n");
    {
        // The core's frame and gamescope's both: gamescope's while it plays,
        // the frame as the game is left.
        GameSession s;
        s.romId = 507;
        s.title = "Burnout 3 Takedown";
        s.fsStem = "Burnout 3 Takedown";
        s.saveTag = "pcsx2";
        s.saveDir = storage::savesDir(u, "ps2", 507, "pcsx2");
        cab::FileSaveState f;
        f.spec.path = "Burnout 3 Takedown.ps2";
        f.spec.captureSuffix = ".ps2";
        f.path = s.saveDir + "/Burnout 3 Takedown.ps2";
        f.atLaunch = std::vector<uint8_t>(4096, 0x11);
        f.hadOne = true;
        cab::writeBytes(f.path, f.atLaunch);
        s.fileSaves = {f};
        std::atomic<int> shots{0}, frames{0};
        std::atomic<bool> offThread{true};
        const std::thread::id caller = std::this_thread::get_id();
        Game ps2;
        ps2.frame = [&](std::vector<uint8_t>* rgba, unsigned* w, unsigned* h) {
            ++frames;
            *rgba = plain(640, 448, 240);
            *w = 640;
            *h = 448;
            return true;
        };
        std::vector<uint8_t> shot250;
        ui::encodePNG(plain(3840, 2160, 250), 3840, 2160, shot250);
        ps2.askProgram = [&](const std::string& path) {
            ++shots;
            if (std::this_thread::get_id() == caller) offThread = false;
            return cab::writeBytes(path, shot250);
        };
        Watch w;
        w.begin(s, u, ps2, 0);
        std::vector<uint8_t> card = f.atLaunch;
        card[100] = 0x22;
        cab::writeBytes(f.path, card);
        w.tick(s, 1000);
        w.tick(s, 2000);
        for (uint64_t t = 2500; t <= 4000; t += 500) later(w, s, t);
        settle(w, s, 4000);
        int pw = 0, ph = 0, red = 0;
        check(shots == 1 && frames == 0 && offThread &&
                  readPicture(s.savePicture, &pw, &ph, &red) && red == 250 && pw == 640,
              "while it plays: gamescope's picture, asked on the worker, and PCSX2's frame never "
              "read on the main loop");
        card[200] = 0x33;
        cab::writeBytes(f.path, card);
        w.leaving(s, 4100);
        w.finish(s, 4200);
        check(shots == 1 && frames == 1 && readPicture(s.savePicture, &pw, &ph, &red) &&
                  red == 240,
              "as it is left, saved within the second: PCSX2's frame, read before its window goes");
        savepicture::prune(u, 507);
    }

    std::printf("\ngamescope's picture cut to the game\n");
    {
        // ONE SITTING of an emulator run as a program whose save changes
        // once, gamescope's picture made of `px`: the size of the save's
        // picture it leaves and the red of its leftmost pixel half way down.
        GameSession base;
        base.romId = 508;
        base.title = "Arcade Archives VIPER PHASE 1";
        base.fsStem = "Arcade Archives VIPER PHASE 1";
        base.saveTag = "eden";
        base.standaloneCore = "eden";
        base.saveDir = storage::savesDir(u, "switch", 508, "eden");
        base.dirSaveRoot = base.saveDir + "/nand/user/save";
        cab::writeBytes(base.dirSaveRoot + "/0000/progress.bin", std::string("0"));
        base.dirAtLaunch = listDirSave(base);
        int sitting = 0;
        auto cut = [&](const std::vector<uint8_t>& px, unsigned w, unsigned h, int* pw, int* ph,
                       int* left) {
            std::vector<uint8_t> png;
            ui::encodePNG(px, w, h, png);
            Game prog;
            prog.askProgram = [png](const std::string& path) { return cab::writeBytes(path, png); };
            Watch wa;
            GameSession s = base;
            wa.begin(s, u, prog, 0);
            cab::writeBytes(s.dirSaveRoot + "/0000/progress.bin", std::to_string(++sitting));
            wa.tick(s, 1000);
            wa.tick(s, 2000);
            for (uint64_t t = 2500; t <= 4000; t += 500) later(wa, s, t);
            settle(wa, s, 4000);
            int red = 0;
            *pw = *ph = 0;
            const bool ok = readPicture(s.savePicture, pw, ph, &red);
            *left = leftRed(s.savePicture);
            wa.finish(s, 4100);
            savepicture::prune(u, base.romId);
            return ok;
        };
        int pw = 0, ph = 0, left = 0;
        std::vector<uint8_t> px = plain(1920, 1080, 0);
        fill(px, 1920, 240, 0, 1680, 1080, 200);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 480 && left == 200,
              "a 4:3 game on a 16:9 screen: the black bars down both sides cut off, 4:3 kept");
        std::vector<uint8_t> ps2 = plain(3840, 2160, 0);
        fill(ps2, 3840, 480, 0, 3360, 2160, 201);
        check(cut(ps2, 3840, 2160, &pw, &ph, &left) && pw == 640 && ph == 480 && left == 201,
              "PlayStation 2 in its own window at 4K, a 4:3 game: cut to the game");
        px = plain(1920, 1080, 0);
        fill(px, 1920, 239, 0, 1679, 1080, 202);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 480 && left == 202,
              "bars two pixels apart, as an emulator rounds: each cut at the game's own edge");
        px = plain(1920, 1080, 0);
        fill(px, 1920, 240, 6, 1680, 1074, 203);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 480 && left == 203,
              "a game with a few black lines of its own above and below: still cut");
        // FlatOut's title (2026-11-21): bands across the top and bottom, and
        // a border of the game's own beside the picture between them.
        px = plain(1920, 1080, 0);
        fill(px, 1920, 240, 0, 1680, 200, 211);
        fill(px, 1920, 240, 880, 1680, 1080, 211);
        fill(px, 1920, 252, 200, 1680, 880, 211);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 480,
              "a border of the game's own down one side of its picture: its edge still found, "
              "cut");
        px = plain(1920, 1080, 0);
        fill(px, 1920, 252, 0, 1680, 1080, 212);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 484 && left == 212,
              "a black stripe of the game's own down one side, the whole height: cut there, "
              "nothing lit lost");
        px = plain(1920, 1080, 204);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 360 && left == 204,
              "a widescreen game fills the screen: nothing cut");
        // DARK SCENES WITH BLACK EDGES, as the save pictures RomM holds from
        // phase 5 show them: kept whole, never cut to what is lit.
        px = plain(1920, 1080, 0);
        fill(px, 1920, 810, 440, 1110, 640, 205);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 360 && left == 0,
              "a logo in the middle of black, the same black each side: kept whole");
        px = plain(1920, 1080, 0);
        fill(px, 1920, 216, 96, 220, 954, 206);
        fill(px, 1920, 1700, 96, 1704, 954, 206);
        fill(px, 1920, 216, 96, 1704, 100, 206);
        fill(px, 1920, 216, 950, 1704, 954, 206);
        fill(px, 1920, 600, 300, 1300, 700, 206);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 360 && left == 0,
              "a framed panel in the middle of black, its sides lit most of the way down but "
              "not reaching the top and bottom: kept whole");
        px = plain(1920, 1080, 0);
        fill(px, 1920, 100, 0, 1600, 1080, 207);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 360 && left == 0,
              "black of different widths each side: kept whole");
        px = plain(1920, 1080, 0);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 360,
              "a black picture: kept whole");
        px = plain(1920, 1080, 0);
        fill(px, 1920, 240, 200, 1680, 880, 208);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 360 && left == 0,
              "a 4:3 game showing a film with black above and below: its edges unproven, "
              "kept whole");
        px = plain(1920, 1080, 0);
        fill(px, 1920, 0, 140, 1920, 940, 209);
        check(cut(px, 1920, 1080, &pw, &ph, &left) && pw == 640 && ph == 360 && left == 209,
              "black above and below is the game's own: never cut");
        // A CORE'S OWN FRAME is the game's shape already, its own black sides
        // and all (the head of savepicture.h).
        Watch wc;
        GameSession sc = battery(509);
        Game core;
        core.battery = game.battery;
        core.frame = [&](std::vector<uint8_t>* rgba, unsigned* w, unsigned* h) {
            *rgba = plain(800, 600, 0);
            fill(*rgba, 800, 100, 0, 700, 600, 210);
            *w = 800;
            *h = 600;
            return true;
        };
        wc.begin(sc, u, core, 0);
        ram[700] = 0x77;
        wc.tick(sc, 1000);
        wc.tick(sc, 2000);
        settle(wc, sc, 2000);
        int red = 0;
        check(readPicture(sc.savePicture, &pw, &ph, &red) && pw == 640 && ph == 480 &&
                  leftRed(sc.savePicture) == 0,
              "a core's own frame with black sides of its own: sent as it is");
        wc.finish(sc, 2100);
        savepicture::prune(u, 509);
    }

    std::printf("\nthe shrink\n");
    {
        std::vector<uint8_t> px(4 * 2 * 4, 0);
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 4; ++x)
                px[(y * 4 + x) * 4] = ((x + y) % 2) ? 255 : 0;
        unsigned w = 4, h = 2;
        ui::shrinkRGBA(px, w, h, 2);
        check(w == 2 && h == 1 && px.size() == 8 && px[0] == 127 && px[4] == 127,
              "each new pixel the average of the ones it covers");
        unsigned w2 = 320, h2 = 240;
        std::vector<uint8_t> small = plain(320, 240, 5);
        ui::shrinkRGBA(small, w2, h2, 640);
        check(w2 == 320 && h2 == 240, "a picture no wider already is left as it is");
    }

    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace savepicture
