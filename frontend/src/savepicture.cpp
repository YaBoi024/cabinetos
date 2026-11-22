#include "savepicture.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "core.h"
#include "fileio.h"
#include "filesave.h"
#include "imagecodec.h"
#include "job.h"
#include "pending.h"
#include "proc.h"
#include "ps2.h"
#include "saves.h"
#include "standalone.h"
#include "storage.h"

namespace savepicture {
namespace {

int64_t wallMs() {
    struct timespec ts {};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

// WHAT THE SAVE IS AT ONE LOOK (the head of savepicture.h): a battery's bytes,
// each file a core writes by its size and time, and the save folder's
// entries.
struct Look {
    std::vector<uint8_t> battery;
    std::vector<cab::DirEntry> files;
    std::vector<cab::DirEntry> folder;
    bool operator==(const Look& o) const {
        return battery == o.battery && files == o.files && folder == o.folder;
    }
    bool operator!=(const Look& o) const { return !(*this == o); }
};

cab::DirEntry statOf(const std::string& path) {
    cab::DirEntry e;
    e.relPath = path;
    struct stat st;
    if (!path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        e.size = st.st_size;
        e.mtimeNs = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
    }
    return e;
}

// As the gatherings read the save (saves.cpp): a game with a save folder has
// no battery to read.
Look lookAt(const GameSession& sess, const Game& game) {
    Look l;
    if (sess.dirSaveRoot.empty() && game.battery) game.battery(&l.battery);
    // A file the core writes only as the game closes is not looked at
    // (catalog::SaveFile::writtenAtClose): no moment of saving shows there.
    for (const cab::FileSaveState& f : sess.fileSaves)
        l.files.push_back(f.spec.writtenAtClose ? cab::DirEntry{}
                                                : statOf(cab::writtenFile(f.spec, f.path)));
    if (!sess.dirSaveRoot.empty()) l.folder = listDirSave(sess);
    return l;
}

// The game's owed push names `path`: it is that push's to keep.
bool named(const storage::User& who, int romId, const std::string& path) {
    cache::OwedPush p;
    return !path.empty() && cache::owedPush(who, romId, &p) && p.savePicturePath == path;
}

// A picture made: its file written, and the frame it was made from. On the
// encoding worker, so it touches nothing but the pixels it was given and the
// file it writes.
bool writePicture(std::vector<uint8_t> rgba, unsigned w, unsigned h, const std::string& path,
                  std::string* why) {
    ui::shrinkRGBA(rgba, w, h, kMaxWidth);
    std::vector<uint8_t> png;
    if (!ui::encodePNG(rgba, w, h, png)) {
        *why = "the PNG encoder refused the frame";
        return false;
    }
    if (!cab::writeBytes(path, png)) {
        *why = "could not write " + path;
        return false;
    }
    std::fprintf(stderr, "[save] the save's picture, %ux%u, %zu bytes, %s\n", w, h, png.size(),
                 path.c_str());
    return true;
}

// A pixel no brighter than this in red, green and blue is black. A bar is
// drawn at 0 (PCSX2's and xemu's, read at 4K on the reference console,
// 2026-11-21); a little above lets a game's near-black count as black, which
// makes black sides look wider, never a game's edge look like a bar.
constexpr uint8_t kBlack = 4;

bool lit(const uint8_t* px) { return px[0] > kBlack || px[1] > kBlack || px[2] > kBlack; }

}  // namespace

bool cutBars(std::vector<uint8_t>& rgba, unsigned& width, unsigned height, std::string* why) {
    auto keep = [&](const char* reason) {
        if (why) *why = reason;
        return false;
    };
    const unsigned w = width, h = height;
    if (w < 2 || h < 2 || rgba.size() < static_cast<size_t>(w) * h * 4) return keep("no picture");
    auto px = [&](unsigned x, unsigned y) {
        return rgba.data() + (static_cast<size_t>(y) * w + x) * 4;
    };
    auto black = [&](unsigned x) {
        for (unsigned y = 0; y < h; ++y)
            if (lit(px(x, y))) return false;
        return true;
    };
    // THE BLACK DOWN EACH SIDE, a whole column at a time.
    unsigned left = 0;
    while (left < w && black(left)) ++left;
    if (left == w) return keep("all black");
    unsigned right = 0;
    while (black(w - 1 - right)) ++right;
    // Bars worth the name, a hundredth of the width each side at least:
    // thinner is a game's own border, or nothing a picture 640 wide shows.
    if (left < w / 100 || right < w / 100) return keep("no bars");
    // A GAME'S OWN BORDER, black down one side of its picture, up to a
    // hundredth of the width (FlatOut's title has one): allowed for in the
    // two tests that follow.
    const unsigned border = std::max(2u, w / 100);
    // AS WIDE AS EACH OTHER, give or take that border: a game is put in the
    // middle of the screen.
    const unsigned apart = left > right ? left - right : right - left;
    if (apart > border) return keep("black of different widths each side");
    // THE GAME'S OWN EDGES, beside each bar: something lit within that
    // border's width of the bar, in half the rows at least. What is lit in
    // the middle of a dark scene (a logo, a line of words, a box of words)
    // has no edge as tall as that.
    const unsigned x0 = left, x1 = w - 1 - right;
    auto litRows = [&](unsigned from, unsigned to) {
        unsigned n = 0;
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = from; x <= to; ++x)
                if (lit(px(x, y))) {
                    ++n;
                    break;
                }
        return n;
    };
    if (litRows(x0, std::min(x0 + border - 1, x1)) * 2 < h ||
        litRows(x1 - std::min(border - 1, x1 - x0), x1) * 2 < h)
        return keep("the game's edges are dark");
    // THE GAME FROM TOP TO BOTTOM: a game narrower than the screen is
    // fitted to its height, so something of it is lit within a sixteenth of
    // the top and of the bottom. A panel in the middle of a dark scene is
    // not.
    auto rowLit = [&](unsigned y) {
        for (unsigned x = x0; x <= x1; ++x)
            if (lit(px(x, y))) return true;
        return false;
    };
    unsigned top = 0, bottom = 0;
    while (top < h && !rowLit(top)) ++top;
    while (bottom < h && !rowLit(h - 1 - bottom)) ++bottom;
    if (top > h / 16 || bottom > h / 16) return keep("the game does not reach the top and bottom");
    // PROVEN: the bars go. Black above and below stays, as the game drew it.
    const unsigned cw = x1 - x0 + 1;
    std::vector<uint8_t> out(static_cast<size_t>(cw) * h * 4);
    for (unsigned y = 0; y < h; ++y)
        std::copy(px(x0, y), px(x0, y) + static_cast<size_t>(cw) * 4,
                  out.data() + static_cast<size_t>(y) * cw * 4);
    rgba = std::move(out);
    width = cw;
    return true;
}

std::string prefixFor(int romId) { return std::to_string(romId) + ".picture."; }

std::string pathFor(const storage::User& who, int romId, int64_t ms) {
    return storage::pendingDir(who) + "/" + prefixFor(romId) + std::to_string(ms) + ".png";
}

void prune(const storage::User& who, int romId) {
    if (!who.valid() || romId <= 0) return;
    cache::OwedPush p;
    const std::string keep = cache::owedPush(who, romId, &p) ? p.savePicturePath : std::string();
    const std::string dir = storage::pendingDir(who);
    const std::string prefix = prefixFor(romId);
    DIR* d = ::opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> gone;
    while (struct dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        if (dir + "/" + name == keep) continue;
        gone.push_back(dir + "/" + name);
    }
    ::closedir(d);
    for (const std::string& path : gone) {
        cab::removeFile(path);
        std::fprintf(stderr, "[save] game %d: a picture no save went with, deleted: %s\n", romId,
                     path.c_str());
    }
}

struct Watch::State {
    bool on = false;
    storage::User who;
    int romId = 0;
    bool program = false;
    uint64_t lastLookMs = 0;
    Look last;        // the look before
    Look atPicture;   // the save at the last picture (at the start, as it started)
    // Each core file's bytes at the last picture, against which a change of
    // its size or time is checked.
    std::vector<std::vector<uint8_t>> fileBytes;
    // The picture being encoded, and one taken while it was: the newest.
    cab::Job<std::string> job;
    struct Frame {
        std::vector<uint8_t> rgba;
        unsigned w = 0, h = 0;
        std::string path;
    };
    bool frameWaiting = false;
    Frame waiting;
    // A picture asked of gamescope (on `ask`, never the main loop) and being
    // written by it.
    cab::Job<bool> ask;
    std::string shotRaw, shotPath;
    int64_t shotSize = -1;
    uint64_t shotAskedMs = 0, shotCheckedMs = 0;
    bool askAgain = false;
    // The game's part (Game), and whether the game is being left on purpose.
    Game game;
    bool left = false;
};

Watch::Watch() : s_(std::make_unique<State>()) {}
Watch::~Watch() = default;

void Watch::begin(GameSession& sess, const storage::User& who, Game game, uint64_t nowMs) {
    s_->job.wait();
    s_ = std::make_unique<State>();
    sess.savePicture.clear();
    if (sess.romId <= 0 || sess.saveTag.empty() || !who.valid()) return;
    s_->on = true;
    s_->who = who;
    s_->romId = sess.romId;
    s_->program = !sess.standaloneCore.empty();
    s_->game = std::move(game);
    s_->last = s_->atPicture = lookAt(sess, s_->game);
    for (const cab::FileSaveState& f : sess.fileSaves) s_->fileBytes.push_back(f.atLaunch);
    s_->lastLookMs = nowMs;
    prune(who, sess.romId);
}

namespace {

// THE SAVE CHANGED SINCE THE LAST PICTURE, by the gatherings' own rules: the
// battery's bytes, the save folder's entries, or a core file whose bytes are
// new and, while there was no save before, hold one (cab::holdsASave). A
// battery the core reports none of is no change, as syncSave reads it:
// Genesis Plus GX reports none while its battery is blank.
bool changedSince(const GameSession& sess, const Look& now, const Look& atPicture,
                  const std::vector<std::vector<uint8_t>>& fileBytes) {
    if (!now.battery.empty() && now.battery != atPicture.battery) return true;
    if (now.folder != atPicture.folder) return true;
    for (size_t i = 0; i < now.files.size() && i < sess.fileSaves.size(); ++i) {
        if (i < atPicture.files.size() && now.files[i] == atPicture.files[i]) continue;
        const cab::FileSaveState& f = sess.fileSaves[i];
        const std::vector<uint8_t> data =
            now.files[i].size > 0 ? cab::readBytes(now.files[i].relPath) : std::vector<uint8_t>();
        if (data.empty() || (i < fileBytes.size() && data == fileBytes[i])) continue;
        if (!f.hadOne && !cab::holdsASave(data, f.spec.untouched)) continue;
        return true;
    }
    return false;
}

}  // namespace

void Watch::take(GameSession& sess, uint64_t nowMs, const char* why, bool leaving) {
    State& st = *s_;
    const Game& game = st.game;
    const std::string path = pathFor(st.who, st.romId, wallMs());
    // gamescope's own picture of the game's plane, which leaves the menu out,
    // written by gamescope a moment after it is asked: an emulator run as a
    // program's always, PS2's while it plays (the head of savepicture.h).
    if (game.askProgram && (!game.frame || !leaving)) {
        if (!st.shotRaw.empty() || st.job.busy() || st.ask.busy()) {
            st.askAgain = true;
            return;
        }
        const std::string raw = path + ".gamescope.png";
        st.shotRaw = raw;
        st.shotPath = path;
        st.shotSize = -1;
        st.shotAskedMs = st.shotCheckedMs = nowMs;
        // ASKED ON THE WORKER, so the main loop, which feeds a PS2 game its
        // pads and drains its sound, never waits on `gamescopectl`.
        const std::function<bool(const std::string&)> askFor = game.askProgram;
        st.ask.start([askFor, raw](bool& asked, std::string& e) {
            asked = askFor(raw);
            if (!asked) e = "gamescope could not be asked";
            return asked;
        });
        std::fprintf(stderr, "[save] game %d: the save's picture asked of gamescope (%s)\n",
                     st.romId, why);
        return;
    }
    State::Frame f;
    if (!game.frame || !game.frame(&f.rgba, &f.w, &f.h) || f.w == 0 || f.h == 0) {
        std::fprintf(stderr, "[save] game %d: no frame for the save's picture\n", st.romId);
        return;
    }
    f.path = path;
    std::fprintf(stderr, "[save] game %d: the save's picture taken, %ux%u (%s)\n", st.romId, f.w,
                 f.h, why);
    st.waiting = std::move(f);
    st.frameWaiting = true;
    pump(sess, nowMs);
}

void Watch::pump(GameSession& sess, uint64_t nowMs) {
    State& st = *s_;
    // gamescope could not be asked: nothing is coming.
    bool asked = false;
    std::string askErr;
    if (st.ask.take(&asked, &askErr, nullptr) && !asked) {
        std::fprintf(stderr, "[save] game %d: no picture of its save: %s\n", st.romId,
                     askErr.c_str());
        st.shotRaw.clear();
    }
    // A PICTURE MADE: into the session, the one before it deleted unless a
    // push already owed sends it.
    std::string made, err;
    bool ok = false;
    if (st.job.take(&made, &err, &ok)) {
        if (ok) {
            if (!sess.savePicture.empty() && sess.savePicture != made &&
                !named(st.who, st.romId, sess.savePicture))
                cab::removeFile(sess.savePicture);
            sess.savePicture = made;
            // A GAME THAT NOTES IT IS PLAYING (an emulator run as a
            // program, a core whose save is files) notes it where a power
            // cut's recovery reads (finishInterruptedSave,
            // finishInterruptedPlay).
            if (cab::standalone::hasPlayingNote(sess.saveDir))
                cab::standalone::writePlayingNote(sess.saveDir,
                                                  playingNoteFor(sess, sess.saveDir));
        } else {
            std::fprintf(stderr, "[save] game %d: no picture of its save: %s\n", st.romId,
                         err.c_str());
        }
    }
    if (!st.job.busy() && st.frameWaiting) {
        st.frameWaiting = false;
        State::Frame f = std::move(st.waiting);
        st.job.start([f](std::string& out, std::string& e) {
            out = f.path;
            return writePicture(f.rgba, f.w, f.h, f.path, &e);
        });
    }
    // GAMESCOPE'S PICTURE, whole once its size stops changing (as
    // App::pumpStandaloneShot reads it), then read back, made small and
    // encoded on the worker.
    if (!st.shotRaw.empty() && !st.ask.busy() && nowMs - st.shotCheckedMs >= 500 &&
        !st.job.busy()) {
        st.shotCheckedMs = nowMs;
        struct stat fs;
        const bool there = ::stat(st.shotRaw.c_str(), &fs) == 0 && fs.st_size > 0;
        if (!there && nowMs - st.shotAskedMs >= kProgramWaitMs) {
            std::fprintf(stderr, "[save] game %d: gamescope wrote no picture in %llu s\n",
                         st.romId, static_cast<unsigned long long>(kProgramWaitMs / 1000));
            cab::removeFile(st.shotRaw);
            st.shotRaw.clear();
        } else if (there && fs.st_size == st.shotSize) {
            const std::string raw = st.shotRaw, path = st.shotPath;
            const int romId = st.romId;
            st.shotRaw.clear();
            st.job.start([raw, path, romId](std::string& out, std::string& e) {
                out = path;
                std::vector<uint8_t> rgba;
                int w = 0, h = 0;
                const bool read = ui::decodeImage(cab::readBytes(raw), rgba, w, h);
                cab::removeFile(raw);
                if (!read || w <= 0 || h <= 0) {
                    e = "gamescope's picture could not be read";
                    return false;
                }
                // The whole screen's picture cut to the game (cutBars).
                unsigned uw = static_cast<unsigned>(w), uh = static_cast<unsigned>(h);
                std::string kept;
                if (cutBars(rgba, uw, uh, &kept))
                    std::fprintf(stderr,
                                 "[save] game %d: gamescope's %dx%d cut to the game, %ux%u\n",
                                 romId, w, h, uw, uh);
                else
                    std::fprintf(stderr, "[save] game %d: gamescope's %dx%d kept whole: %s\n",
                                 romId, w, h, kept.c_str());
                return writePicture(std::move(rgba), uw, uh, path, &e);
            });
        } else if (there) {
            st.shotSize = fs.st_size;
        }
    }
    if (st.askAgain && st.shotRaw.empty() && !st.job.busy() && !st.ask.busy()) {
        st.askAgain = false;
        take(sess, nowMs, "the save changed again");
    }
}

void Watch::tick(GameSession& sess, uint64_t nowMs) {
    if (!s_->on || s_->left) return;
    State& st = *s_;
    pump(sess, nowMs);
    // THE LOOK, once a second (the head of savepicture.h).
    if (nowMs - st.lastLookMs < kLookMs) return;
    st.lastLookMs = nowMs;
    Look now = lookAt(sess, st.game);
    if (now == st.last && now != st.atPicture) {
        if (changedSince(sess, now, st.atPicture, st.fileBytes))
            take(sess, nowMs, "the save stopped changing");
        st.atPicture = now;
        st.fileBytes.clear();
        for (const cab::DirEntry& e : now.files)
            st.fileBytes.push_back(e.size > 0 ? cab::readBytes(e.relPath) : std::vector<uint8_t>());
    }
    st.last = std::move(now);
}

void Watch::leaving(GameSession& sess, uint64_t nowMs) {
    if (!s_->on || s_->left) return;
    State& st = *s_;
    pump(sess, nowMs);
    st.left = true;
    st.lastLookMs = nowMs;
    Look now = lookAt(sess, st.game);
    if (now != st.atPicture && changedSince(sess, now, st.atPicture, st.fileBytes))
        take(sess, nowMs, "the game is being left", /*leaving=*/true);
    st.atPicture = st.last = std::move(now);
    st.fileBytes.clear();
    for (const cab::DirEntry& e : st.atPicture.files)
        st.fileBytes.push_back(e.size > 0 ? cab::readBytes(e.relPath) : std::vector<uint8_t>());
}

void Watch::finish(GameSession& sess, uint64_t nowMs) {
    if (!s_->on) return;
    State& st = *s_;
    // WHAT IS STILL BEING MADE, waited for: gamescope's file, as long as
    // Screenshot waits for one, then the worker. Nothing new is asked.
    st.askAgain = false;
    struct timespec t0 {};
    ::clock_gettime(CLOCK_MONOTONIC, &t0);
    auto now = [&] {
        struct timespec t {};
        ::clock_gettime(CLOCK_MONOTONIC, &t);
        return nowMs + static_cast<uint64_t>((t.tv_sec - t0.tv_sec) * 1000 +
                                             (t.tv_nsec - t0.tv_nsec) / 1000000);
    };
    while (!st.shotRaw.empty() || st.job.busy() || st.frameWaiting || st.ask.busy()) {
        st.shotCheckedMs = 0;
        pump(sess, now());
        if (st.shotRaw.empty() && !st.job.busy() && !st.frameWaiting && !st.ask.busy()) break;
        if (st.ask.busy()) {
            st.ask.wait();
        } else if (st.job.busy()) {
            st.job.wait();
        } else if (now() - nowMs > kProgramWaitMs + 2000) {
            // Never longer than Screenshot waits, and a little: the leave
            // goes on without the picture.
            cab::removeFile(st.shotRaw);
            st.shotRaw.clear();
        } else {
            ::usleep(100000);
        }
    }
    pump(sess, now());
    // A PROGRAM THAT WENT BY ITSELF with its save changed since the last
    // picture: that picture is of an earlier moment, so none goes.
    if (!st.left && !sess.savePicture.empty()) {
        const Look last = lookAt(sess, st.game);
        if (changedSince(sess, last, st.atPicture, st.fileBytes)) {
            std::fprintf(stderr, "[save] game %d: its save changed after its last picture and "
                                 "the game went by itself: no picture goes with it\n", st.romId);
            if (!named(st.who, st.romId, sess.savePicture)) cab::removeFile(sess.savePicture);
            sess.savePicture.clear();
        }
    }
    st.on = false;
}

namespace {
bool askGamescope(const std::string& path) {
    const proc::Result r = proc::run({"gamescopectl", "screenshot", path}, 5);
    if (!r.ok()) std::fprintf(stderr, "[save] gamescopectl: %s\n", proc::trimmed(r.err).c_str());
    return r.ok();
}
}  // namespace

Game coreGame() {
    Game g;
    g.battery = [](std::vector<uint8_t>* out) { return cab::Core::shared().readSaveRam(*out); };
    g.frame = [](std::vector<uint8_t>* rgba, unsigned* w, unsigned* h) {
        return cab::Core::shared().snapshot(*rgba, *w, *h, kMaxWidth);
    };
    // PS2 IN ITS OWN WINDOW (#226): gamescope's picture while it plays, so
    // the main loop never waits on PCSX2's read-back (the head of this file).
    // Off gamescope every frame is copied already, and the frame is read.
    if (cab::Core::shared().isPs2() && ps2::windowed()) g.askProgram = askGamescope;
    return g;
}

Game programGame() {
    Game g;
    g.askProgram = askGamescope;
    return g;
}

}  // namespace savepicture
