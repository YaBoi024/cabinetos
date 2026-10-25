// Controls: an arcade game's buttons moved to other buttons, per game, per
// kind of controller and per person (decided with MMagTech 2026-10-24 and
// 2026-10-25, docs/decisions/2026-10-25-arcade-controls.md). The pause
// menu's row and its panel are the App's, in controlsflow.cpp.
//
// ONLY THE TWO ARCADE CORES (fightstick::isArcadeCore). Every other system
// plays by position, as before, with nothing to change.
//
// A ROW IS ONE OF THE GAME'S BUTTONS, as the core declared it for port 0
// (core.h, inputDescriptors) with an id among the ten below. The lever,
// Select and Start are never rows. Labels are the core's own words with a
// leading "B<n>: " taken off (MAME 2003-Plus's "B1: Jab Punch" reads "Jab
// Punch"); MAME 2003-Plus's rows are put in its Classic table's order, as a
// fight stick's are, FBNeo's stay in the order it declared them.
//
// A CONTROL IS A BUTTON ON THE PAD, named by the RetroPad id it reads as
// with nothing moved (play.cpp, readGameInput): B the bottom face button, A
// the right, Y the left, X the top, L and R the shoulders, L2 and R2 the
// triggers, L3 and R3 the stick clicks. A press can give a row one of the
// first eight, B among them, never L3 or R3 (the pause pair), and never
// Start, Select, the d-pad or the shortcut button, which are not controls
// here at all. Pressing the control a row already has changes nothing.
//
// SWAP, NEVER OVERWRITE. A row given a control another row has swaps with
// it; given a free one, it moves. No two rows ever share a control, and a
// row that had one never loses it, except to a row that had none (a fight
// stick leaves FBNeo's combination buttons and a ninth button without one,
// fightstick.h). A control no row has keeps sending what it sent, or, when
// a row took its place, takes the place that row left: a button the core
// did not name is never lost.
//
// PER KIND OF CONTROLLER (fightstick::kind): every pad of a kind shares one
// set of changes. The changes are made over what the pad already does, the
// fight-stick layout for a stick the console knows and the plain table for
// any other pad, and applied in play.cpp's readGameInput in its place.
//
// KEPT AS ONE FILE PER GAME, users/<id> - <name>/controls/<romId> -
// <title>.json (storage::controlsDir), for a person to read over File
// access: per kind, the controller's name beside its key, and only the rows
// that changed, as the row's label and the control's RetroPad name. Written
// whole at every change (fileio.h, writeBytes), read when the game starts
// and when the panel opens. A line that names no row or no control is left
// out and logged, never a crash. Never sent to RomM, and outside cache/.
// Reset deletes the file; a default is never written.
//
// THE RULES HAVE NO SDL IN THEM, so --controls-test walks every case.

#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core.h"
#include "fightstick.h"

namespace controls {

// The ten controls, in the order a pad's buttons are read off it.
constexpr int kControls = 10;
constexpr cab::Button kControl[kControls] = {cab::B, cab::A, cab::Y, cab::X, cab::L,
                                             cab::R, cab::L2, cab::R2, cab::L3, cab::R3};
constexpr int kNone = -1;

// Whether a press of this control can give a row its place: the eight, not
// the stick clicks.
bool isTarget(int control);
// "B", "L2": the control's RetroPad name, as the file and the log write it,
// "None" for kNone; and back, with any other word below kNone.
const char* word(int control);
int fromWord(const std::string& w);
// The label without a leading "B<n>: ".
std::string trimLabel(const std::string& label);

// One of the game's buttons: the RetroPad id it is read on, and its words.
struct Row {
    unsigned id = 0;
    std::string label;
};

// The game's rows, and the declared buttons that are not rows, for the log.
struct Rows {
    std::vector<Row> rows;
    std::vector<cab::InputDescriptor> hidden;
};
Rows rowsFrom(const std::string& core, const std::vector<cab::InputDescriptor>& port0);

// WHAT EACH CONTROL SENDS, by the id it reads as: the id the core hears, or
// kNone. Only the ten are ever set; every other button passes as it is.
using Map = std::array<int, 16>;
// Every control sends its own id: a pad, with nothing moved.
Map plain();
// What a fight stick's layout makes each control send (fightstick::apply).
Map fromLayout(const fightstick::Layout& layout);

// A kind's changes: a row's id and the control it is on now, for each row
// that is not where `def` puts it.
using Changes = std::vector<std::pair<unsigned, int>>;

// Each row's control, kNone for none, in the rows' order: `def`'s, with the
// changes over it. Changes that cannot all hold (a hand edit putting two rows
// on one control) are made one at a time as a press would make them.
std::vector<int> assignment(const std::vector<Row>& rows, const Map& def, const Changes& ch);
// The changes that give `a`, against `def`.
Changes changesFor(const std::vector<Row>& rows, const Map& def, const std::vector<int>& a);
// Gives row `row` the control `target`, by the rule above. False, with
// nothing changed, when the target is not one of the eight, or is already
// the row's.
bool assign(const std::vector<Row>& rows, const Map& def, Changes& ch, int row, int target);
// What each control sends for assignment `a`.
Map sends(const std::vector<Row>& rows, const Map& def, const std::vector<int>& a);
// The pad's buttons through the map, before core.setPad. A trigger's pull
// goes with the id its control sends; a button sending L2 or R2 pulls it all
// the way.
void apply(const Map& map, cab::PadState& pad);

// ---- The file ------------------------------------------------------------------

struct KindLines {
    std::string name;   // the controller's name, for a person reading the file
    std::vector<std::pair<std::string, std::string>> buttons;   // label, control word
};
using File = std::map<std::string, KindLines>;   // by kind

std::string toJson(const File& f);
// Anything that is not what toJson writes is left out, and said in `notes`.
File fromJson(const std::string& text, std::vector<std::string>& notes);
// A kind's lines as changes against the game's rows; a label that is no
// row's is left out and said.
Changes resolve(const KindLines& k, const std::vector<Row>& rows, std::vector<std::string>& notes);
// A kind's changes as lines; `keep` are lines that name no row now, kept as
// they were.
KindLines linesFor(const std::string& name, const std::vector<Row>& rows, const Changes& ch,
                   const std::vector<std::pair<std::string, std::string>>& keep);

// ---- The game in play (SDL from here) -------------------------------------------

// A game starts (launch.cpp, startPlaying): its file is read for the person
// playing when it is an arcade game, and everything of the last game goes.
void beginGame(const std::string& core, int romId, const std::string& title);
// Once a frame while a game plays (play.cpp): the core's newest buttons, the
// rows made again and logged each time it declares them anew.
void follow();
// Whether the game has rows: the pause menu's row is offered only then.
bool offered();
const std::vector<Row>& rows();
// Bumped whenever the rows or the changes do, for a screen showing them.
unsigned generation();

// The map for the pad in `port` (fightstick's layout for it, or null), or
// null when its kind has no changes, which leaves the pad as it was.
const Map* forPort(int port, SDL_Gamepad* gp, const fightstick::Layout* layout);

// The panel's side: what `def` gives each row with this kind's changes,
// and a press giving one a control, written at once. `name` is the
// controller's, for the file.
std::vector<int> current(const std::string& kind, const Map& def);
bool give(const std::string& kind, const std::string& name, const Map& def, int row, int target);
// Reads the file again (the panel opening: it may have been edited).
void reload();
// Reset to default: the file goes, and every kind's changes with it.
void reset();

// A control in the pad's own words: its face buttons as SDL labels them
// (A, Cross), the rest LB, RB, LT, RT, L3, R3; "None" for kNone.
std::string name(SDL_Gamepad* gp, int control);
// The control a pad's button is, for a press while the panel listens: the
// face buttons and the shoulders, else kNone.
int controlOf(SDL_GamepadButton b);

// --controls-test: the rules above, every case, no SDL. 0 when all pass.
int test();

}  // namespace controls
