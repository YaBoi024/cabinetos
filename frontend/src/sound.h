// The console's own voice: the short sounds the interface makes when somebody
// moves around it.
//
// MMagTech, 2026-09-21: *"i also think a navigation sound of some sort would be
// nice and later we have the option in settings to turn it off."* Both halves
// are built here: the sounds, and the switch. The switch is Settings' Interface
// sounds row, Off, Quiet, Medium or Loud (`setLevel`), and it is remembered.
//
// THE SOUNDS ARE SYNTHESISED, NOT RECORDED, and that is a deliberate choice
// with three reasons behind it:
//
//   NOTHING TO SHIP AND NOTHING TO LICENCE. This project keeps docs/LICENCES.md
//   because it cares where its parts came from. A recorded UI click is somebody
//   else's work with somebody else's terms attached, and four short sounds are
//   not worth a new class of obligation in the image.
//
//   THEY BECOME NUMBERS, like everything else in the design system. A click
//   that is "880 Hz, 45 ms, exponential decay" can be argued with in the same
//   way a focus scale can. A .wav can only be replaced.
//
//   THEY COST NOTHING. Each cue is a few thousand samples, built once at
//   startup and pushed at the device when it is asked for. No files, no
//   decoder, no disk.
//
// A UI SOUND IS NOT THE GAME'S SOUND. This opens its OWN stream on the default
// device rather than sharing the core's: the core's stream runs at whatever
// sample rate the emulated machine wants, it is opened and closed with the
// game, and the pause menu has to be able to click while a game is running.
// Two streams on one device is what SDL3's audio model is for.

#pragma once

#include <string>

namespace sound {

// What happened, not what it sounds like. The caller says "focus moved" and
// this file decides what that is — which is the only way the set stays
// consistent when somebody retunes it.
enum class Cue {
    Move,      // focus moved to another thing
    Activate,  // something was chosen
    Back,      // a screen was left
    Edge,      // focus tried to move and there was nothing there
    Type,      // a key typed on the on-screen keyboard (#389)
};

// Opens the device and builds the cues. Returns false when there is no audio
// device, which is not an error: a console with no sound card is still a
// console, and every play() after that does nothing.
bool init();

// THE SWITCH SETTINGS WILL OWN. Off means silent, and nothing else about the
// interface changes — there is no visual compensation for a muted console,
// because a person who turns the sounds off has said what they want.
void setEnabled(bool on);
// FORK (handheld): a finger dragging through a menu makes no Move and no Edge sound, as a
// scrolled list on a phone makes none. A tap still clicks. On while a drag's steps are sent.
void setTouchQuiet(bool on);

// 0..1. The default is deliberately low: this is punctuation under whatever
// else is happening, and a navigation click that competes with a game's music
// is one nobody keeps switched on.
void setVolume(float v);

// THE ROW IN SETTINGS, docs/SETTINGS.md: one row, left and right, and saved.
// What each level is in numbers lives in sound.cpp and nowhere else, so
// retuning Quiet is a one-line change that every console picks up, because
// what is saved is the word and not the number.
enum class Level { Off, Quiet, Medium, Loud };
constexpr int kLevelCount = 4;

// Sets the switch and the volume together. Medium is what the console
// sounded like before there was a choice.
void setLevel(Level l);
Level level();
// For tuning on the television (--ui-sound-levels): what Quiet, Medium and
// Loud mean, 0..1. Whatever wins goes back into sound.cpp.
void setLevelVolumes(float quiet, float medium, float loud);
// As a person reads it on the row: "Off", "Quiet", ...
const char* levelName(Level l);
// As it is stored in config/settings.json: "off", "quiet", ...
const char* levelWord(Level l);
// The stored word back to a level. False, and `out` untouched, for anything
// else, so a hand-edited file cannot set something that is not on the row.
bool levelFromWord(const std::string& word, Level* out);

// THE ACHIEVEMENT CHIME (#74), with its own row in Display and Sound and its
// own level: MMagTech, 2026-10-06, did not want it tied to the menu clicks.
// Off, Quiet, Medium or Loud, the same words and volumes as Interface sounds.
// Played when a RetroAchievements pop-up arrives, whatever Interface sounds
// is set to.
void setChimeLevel(Level l);
Level chimeLevel();
void playChime();

// A LinkUp invite arriving, once, at the Interface sounds level: Off is silent.
void playInvite();

// Plays a cue, or does nothing if there is no device or the switch is off.
// Safe to call from the frame loop as often as input arrives — a cue that is
// already sounding is simply overlapped rather than queued, so running along a
// shelf ticks once per card instead of building a backlog of clicks.
void play(Cue c);

}  // namespace sound
