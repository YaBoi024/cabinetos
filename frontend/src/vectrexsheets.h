// The Vectrex games' screen sheets (#224, docs/PROJECT.md open question 51).
// Every retail cartridge shipped with a translucent printed sheet that sat in
// front of the console's screen: colour over the white lines, and the art
// and control legend the game never drew. This finds a game's sheet, keeps
// the pause menu's Overlays choice, and draws the sheet over the core's
// picture (play.cpp, App::drawGamePicture).
//
// The sheets are libretro/overlay-borders' Game Overlay files, fetched for
// the image by tools/fetch-vectrex-overlays.py; which game takes which is
// this project's table, frontend/data/vectrex-overlays.json. Both are
// installed in /usr/share/cabinetos/vectrex (build_files/install-frontend.sh).

#pragma once

#include <map>
#include <string>

namespace ui {
class Renderer;
}

namespace vectrexsheets {

// The sheet a game takes, its file name without ".png", or "" for none.
// `table` is the text of vectrex-overlays.json. RomM's md5 of the ROM first;
// failing that, of every row's name tokens found in the game's name or its
// file name (both normalised: lower case, anything but a letter or a digit a
// single space), the longest. A winning row that is not historical gives "",
// so "3D Mine Storm" never borrows Mine Storm's sheet.
std::string sheetFor(const std::string& table, const std::string& md5,
                     const std::string& name, const std::string& fsName);

// Where the sheets and their table are: $CABINETOS_VECTREX, else the image's
// copy.
std::string folder();

// For the game being started, on the frame thread with GL current
// (App::setUpLoadedGame): the last game's sheet dropped, and this one's read
// and uploaded when the platform is the Vectrex and the game has one. The
// Overlays choice is read here too.
void prepare(const std::string& platformSlug, const std::string& md5,
             const std::string& name, const std::string& fsName);
// The sheet dropped, when the game is left (App::finishExit).
void release();

// This game has a sheet, so the pause menu offers the Overlays row.
bool ready();

// THE OVERLAYS ROW: one choice for the system, kept by its RomM platform slug
// in the console's settings the way a screen look is (looks.h), so a choice
// in one Vectrex game holds for all. On by default.
bool on();
void setOn(bool on);

// THE FIT, for the launch and the option check (optcheck.h): a Vectrex game
// drawn under its sheet is given the same vecx_scale_x and vecx_scale_y,
// which shrink the core's picture about its centre, keeping its shape, so
// what the game draws at the edges of its screen stays inside the sheet's
// window instead of under its frame; every other game, the core's own 1.
// Overlays Off gives the whole picture back, live (pausemenu.cpp).
// The first form decides from the game and the Overlays choice; the second
// is told, so the check can test both answers.
std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug,
                                               const std::string& md5, const std::string& name,
                                               const std::string& fsName);
std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug, bool fitted);

// The sheet over exactly the core's picture rectangle, in canvas points:
// alpha composited, premultiplied source over, at full opacity (the files
// carry their own 20% in the play area), linearly sampled. Nothing when there
// is no sheet or Overlays is Off.
void draw(ui::Renderer& r, float x, float y, float w, float h);

// The table's rules against the repository's table, data/vectrex-overlays.json
// (run from frontend/, as every self-test is): --vectrex-sheets-test.
int test();

}  // namespace vectrexsheets
