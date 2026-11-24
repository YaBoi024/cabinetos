// The parts more than one screen draws: the press prompt, and the caption,
// focus, kept-mark, cover and focused-game-line helpers the screens' files and
// Home (home.cpp) share. Moved out of main.cpp and screens.cpp in the
// restructure (#398).
// choice.cpp keeps its own near-copy of the prompt (another button letter, no
// fade), as it was.

#pragma once

#include <string>
#include <vector>

#include "image.h"
#include "screens.h"

void drawPressPrompt(ui::Renderer& r, ui::TextRenderer& text, float sc, const char* before,
                     const char* after, float base, float alpha, const char* button = "A");

namespace screens {

float pillWidth(ui::TextRenderer& text, const std::string& label, float sc);
float pillHeight(ui::TextRenderer& text, float sc);
void drawPill(Ctx& c, const std::string& label, float x, float y, bool selected, float f);

// Defined in screenparts.cpp, each with what it does and why.
std::vector<std::string> wrapTwoLines(ui::TextRenderer& text, const std::string& s,
                                      ui::TextStyle style, float sc, float width);
void applyArtworkFocus(ui::Rect& rect, float f, bool rim);
void drawKeptMark(Ctx& c, const design::Card& card, float x, float y, float w, float radius);
void drawCover(Ctx& c, const design::Card& card, float x, float y, float w, float h,
               float radius, float f, bool rim, bool large = false);
// A COVER THAT RomM CHANGED with the console on keeps its old picture until the
// new one is in (#730): `was`, when it is still in hand. The picture to draw
// under one not fully in yet, or nothing; which of the two to light the room
// with; and that for a card, by its large cover.
const ui::Image* coverUnder(ui::ImageCache& images, const ui::Image& now, const std::string& was);
std::string coverInHand(ui::ImageCache& images, const std::string& cover, const std::string& was);
std::string roomCover(ui::ImageCache& images, const design::Card& card);
void drawFocusedLine(ui::Renderer& r, ui::TextRenderer& text, float sc, const design::Card& card,
                     bool tags, LineNames names, ControllersOn on, float x, float baseline,
                     float room, float nameAlpha);

}  // namespace screens
