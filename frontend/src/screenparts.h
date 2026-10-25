// The parts more than one screen draws: the press prompt, and the caption,
// focus, kept-mark, cover and focused-game-line helpers the screens' files and
// Home (home.cpp) share. Moved out of main.cpp and screens.cpp in the
// restructure (#398).
// choice.cpp keeps its own near-copy of the prompt (another button letter, no
// fade), as it was.

#pragma once

#include <string>
#include <vector>

#include "screens.h"

void drawPressPrompt(ui::Renderer& r, ui::TextRenderer& text, float sc, const char* before,
                     const char* after, float base, float alpha);

namespace screens {

// Defined in screenparts.cpp, each with what it does and why.
std::vector<std::string> wrapTwoLines(ui::TextRenderer& text, const std::string& s,
                                      ui::TextStyle style, float sc, float width);
void applyArtworkFocus(ui::Rect& rect, float f, bool rim);
void drawKeptMark(Ctx& c, const design::Card& card, float x, float y, float w, float radius);
void drawCover(Ctx& c, const design::Card& card, float x, float y, float w, float h,
               float radius, float f, bool rim, bool large = false);
void drawFocusedLine(ui::Renderer& r, ui::TextRenderer& text, float sc, const design::Card& card,
                     bool tags, bool system, bool reason, float x, float baseline, float room,
                     float nameAlpha);

}  // namespace screens
