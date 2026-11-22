// THE SAVE'S OWN PICTURE (#346, save channels): a picture of the game the
// moment it saved, which goes up with that save inside the snapshot, so
// RomM's website shows a save from this console with its picture, as it shows
// one from RomM's own web player or Argosy (MMagTech, 2026-11-19). Its own file
// because watching a save while the game runs is neither the gatherings' job
// (saves.cpp reads a save once, as the game is left) nor the push's
// (savepush.h sends what was marked).
//
// WHEN. Nothing tells the console that a game saved: a save is bytes that
// change. So the watch looks at the game's save once a second while it runs,
// and takes the picture at the first look that agrees with the one before and
// differs from the save at the last picture: about a second after the game
// has finished writing it. It is RomM's own web player's rule (two agreeing
// looks a second apart, its emulatorJSRuntime.ts at 5.5.0-alpha.2), picked by
// MMagTech's rule for the whole step, the one most in line with the other
// apps. A later save in the sitting takes another, and the newest is the one
// that goes, since its save is the one that goes. So the picture is of the
// last moment the save changed: a game that uses its battery as work space
// (Pokémon Red unpacks its pictures of Pokémon there) changes it with nobody
// saving, and RomM's web player would upload at those moments too.
//
// WHAT IT LOOKS AT is what the gatherings count as the save (saves.h), so a
// picture is taken for the change that is sent: a battery, the core's save
// memory; the files a core writes (cab::writtenFile), by their size and time,
// then their bytes, with the rule a gathering has for a file the core writes
// by starting up (cab::holdsASave); a save folder, the part of it that is the
// save (listDirSave, or the whole of an emulator run as a program's), any
// file in it. AN EMULATOR THAT KEEPS ITS SAVE INSIDE ITSELF UNTIL THE GAME
// CLOSES shows no change while it runs, so no picture is taken and RomM shows
// its own icon: Sega CD (Genesis Plus GX), Neo Geo Pocket Color (Beetle NGP),
// 3DO (Opera), the arcade boards (FinalBurn Neo, MAME 2003-Plus, Flycast's
// Naomi and Atomiswave, sm2-emu, Supermodel), and Xbox, whose saves are read
// out of its drive only once xemu has closed (xboxhdd.h). RomM's web player
// and Argosy find a save the same way, so they see no moment for these
// emulators either. The save table names the libretro cores' files of this
// kind (catalog::SaveFile::writtenAtClose) and the watch does not look at
// them: Flycast also writes a board's memory whenever a state is made, which
// is not the game saving.
//
// THE PICTURE. The game's own frame, the menu never in it (Core::snapshot), no
// wider than kMaxWidth, its shape kept: made that small on the graphics chip
// before it is read, so the game does not wait on a whole 4K frame (8 to 23 ms
// on the reference console). For an emulator run as a program, and for
// PlayStation 2 in its own window (#226) while it plays, gamescope's own
// picture of the game's plane, as Screenshot takes a program's
// (App::standaloneScreenshot), asked from the worker and read back once
// gamescope has written it, made as small: PCSX2's own read-back kept the
// console's main loop, which feeds PS2's sound, waiting 48 ms (1 s while PCSX2
// was not drawing yet), and gamescope's capture costs PCSX2 no frame
// (measured 2026-11-19). As a PS2 game is left, PCSX2's read-back, since its
// window goes the moment the game unloads. A PNG, as RomM's web player sends
// and this console's states carry, encoded off the game's thread.
//
// GAMESCOPE'S PICTURE CUT TO THE GAME (cutBars). It is of the whole screen,
// so a game narrower than the screen (a 4:3 game on a 16:9 television) comes
// with black bars down both sides, where the other apps send the emulator's
// own frame, the game's shape and nothing else (RomM's web player, Argosy).
// The bars are cut off, so the picture follows each game's own shape: a
// widescreen game fills the screen and nothing is cut. One rule for every
// game, read from the picture alone, and only when the picture proves the
// black is bars: black down both sides as wide as each other, give or take a
// hundredth of the width, something of the game lit beside each in half the
// rows, and the game lit within a sixteenth of the top and the bottom, as a
// game fitted to the screen's height is. A dark scene (a logo in the middle
// of black, a box of words, a framed panel, a film letterboxed inside a 4:3
// game) proves none of it and goes whole, bars and all, as it did before.
// Only the sides are cut: no system here draws wider than the television,
// so black above and below is the game's own (a film's letterbox), and the
// other apps keep it too. A core's frame is the game's shape already and is
// never cut.
//
// AS THE GAME IS LEFT, while it is still on screen (leaving): a save that
// changed since the last picture and has not settled takes its picture then,
// as RomM's web player does on leaving. A program that went by itself (it
// quit or fell over) with its save changed since the last picture sends none
// (finish): a picture of an earlier moment would be a wrong one.
//
// KEPT ON THIS CONSOLE UNTIL IT GOES WITH ITS SAVE: one file per picture in the
// person's pending folder (pathFor), named by the game and the time, so a push
// already on its way never sends a newer picture with an older save. The
// session holds the newest (GameSession::savePicture); the game's owed push
// names it (cache::OwedPush::savePicturePath), sends it as RomM's
// `save_screenshot` part, and deletes it with the marker (pending.h). A
// picture no owed push names is deleted when its game next starts, so a
// sitting whose save never went (a battery lost to a power cut) never lends
// its picture to a later save. An emulator run as a program writes each
// picture into its playing note (standalone.h), so a save folder a power cut
// left unsent goes up with the picture its sitting took.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "dirsave.h"
#include "gamesession.h"
#include "users.h"

namespace savepicture {

// How often the watch looks, and so how long a save has to stay as it is
// before its picture is taken.
inline constexpr uint64_t kLookMs = 1000;
// The widest a picture is, as Argosy's (its SaveScreenshotCapture.MAX_WIDTH):
// RomM's website draws it in a 4:3 tile and opens it 440 wide.
inline constexpr unsigned kMaxWidth = 640;
// How long the leave waits for gamescope to finish writing a picture it was
// asked for, as Screenshot waits (App::pumpStandaloneShot).
inline constexpr uint64_t kProgramWaitMs = 10000;

// THE GAME'S PART, so the self-test can play it: the battery as it is now
// (Core::readSaveRam; false for none), the game's own frame no wider than
// kMaxWidth (Core::snapshot; on the frame thread), and gamescope asked for its
// picture of the game into a file (false when it could not be asked; on the
// worker), for an emulator run as a program and PS2 in its own window. With
// both, gamescope's is the picture in play and the frame the picture as the
// game is left. A game without a part leaves it empty.
struct Game {
    std::function<bool(std::vector<uint8_t>*)> battery;
    std::function<bool(std::vector<uint8_t>*, unsigned*, unsigned*)> frame;
    std::function<bool(const std::string& path)> askProgram;
};
// The part of a game in this process's core (Core::shared), with gamescope's
// for PS2 in its own window, and of an emulator run as a program
// (`gamescopectl screenshot`).
Game coreGame();
Game programGame();

// Where a picture taken at `ms` (milliseconds since 1970) is kept, and the
// start every one of a game's pictures has, in the person's pending folder.
std::string pathFor(const storage::User& who, int romId, int64_t ms);
std::string prefixFor(int romId);

// The game's pictures on this console that no owed push names, deleted.
void prune(const storage::User& who, int romId);

// Gamescope's picture of the whole screen (straight RGBA) cut to the game:
// the black bars down both sides cut off when the picture proves they are
// bars (the head of this file). False, the picture as it was, otherwise, and
// `why` says what was not proven. `width` becomes the game's; the height
// stays.
bool cutBars(std::vector<uint8_t>& rgba, unsigned& width, unsigned height, std::string* why);

// ONE GAME'S WATCH, owned by the App and used on the frame thread.
class Watch {
public:
    Watch();
    ~Watch();
    Watch(const Watch&) = delete;
    Watch& operator=(const Watch&) = delete;

    // A game has started, its saves restored and the session's baselines
    // taken: what the save is now is where the watch starts from. Pictures of
    // the game no owed push names go (prune). `nowMs` here and below is one
    // clock that only goes forward (SDL_GetTicks).
    void begin(GameSession& sess, const storage::User& who, Game game, uint64_t nowMs);
    // Once a frame while the game runs. Looks once every kLookMs, and takes
    // in a picture once it is made.
    void tick(GameSession& sess, uint64_t nowMs);
    // As the game is left, while it is still on screen. Once.
    void leaving(GameSession& sess, uint64_t nowMs);
    // Once the game has gone and before its saves are marked
    // (savepush::mark): a picture still being made is finished, and a game
    // that went with no `leaving` (a program that quit or fell over) whose
    // save changed since its last picture keeps none. The watch stops.
    void finish(GameSession& sess, uint64_t nowMs);

private:
    struct State;
    // The picture taken now (the head of this file); one wanted while one is
    // being made waits, the newest, and goes next.
    void take(GameSession& sess, uint64_t nowMs, const char* why, bool leaving = false);
    // A picture made taken into the session, the next one started, and
    // gamescope's file followed.
    void pump(GameSession& sess, uint64_t nowMs);
    std::unique_ptr<State> s_;
};

// `--save-picture-test`: the watch's rules against a pretend game, in a
// scratch root. Exits.
int test();

}  // namespace savepicture
