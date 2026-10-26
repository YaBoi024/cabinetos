// Settings: a side list of categories on the left, the chosen one's rows on the
// right, both on screen at once.
//
// THE SHAPE IS NOT CABINET'S, ON PURPOSE. MMagTech, 2026-09-24: Settings is
// *"the one screen where we can diverge a bit from the others"*. Cabinet's tvOS
// app opened each category as its own page, which costs an open and a back for
// every look. Here walking down the list changes the right side at once, and a
// television is wide enough to show both. docs/PROJECT.md, open question 31.
//
// THE SCREEN IS HANDED ITS ROWS AND HOLDS NO OPINION ABOUT THEM, the same
// division every other screen follows: the app reads the network, the drives
// and the account, builds the model, and turns a pressed row's id into
// whatever it means. Nothing here touches the disk or the network.
//
// NEVER LET FOCUS LAND ON A ROW THAT DOES NOTHING, the rule first run paid for.
// A row that only shows a value, and a row with nothing to do right now, are
// both drawn and both skipped by focus; the second is drawn dimmed.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "qrtexture.h"
#include "screens.h"
#include "tapspots.h"

namespace screens {

struct SettingsRow {
    enum class Kind {
        Info,      // shows a value; cannot be focused unless `pressable` (Version)
        Action,    // opens or does something; has a chevron
        Toggle,    // On or Off, shown as the value; pressing flips it
        Choice,    // one of a few, shown as the value; left and right move it
        Disabled,  // built, but nothing to do right now; dimmed, not focused,
                   // and no explanation (MMagTech: "just gray out the menu
                   // option")
    };
    SettingsRow() = default;
    SettingsRow(Kind k, int i, std::string t, std::string d, std::string v)
        : kind(k), id(i), title(std::move(t)), detail(std::move(d)), value(std::move(v)) {}

    Kind kind = Kind::Info;
    int id = 0;               // what the app is handed back when it is pressed
    std::string title;
    std::string detail;       // the second line, or empty
    std::string value;        // right-aligned, or empty
    // Choice rows only: what left and right walk through, and where it is.
    // The value shown is choices[choice]; `value` is ignored.
    std::vector<std::string> choices;
    int choice = 0;
    // Info rows only: focus can land on it and pressing it hands back `id`,
    // with no chevron, because it opens nothing. About's Version, which
    // shows Developer access when pressed seven times.
    bool pressable = false;
};

struct SettingsCategory {
    std::string name;
    std::vector<SettingsRow> rows;
    // A QR CODE IN PLACE OF ROWS, with the address under it: How to's Wiki
    // (#314). Landing on the category shows it; nothing to press.
    std::string qr;
};

class SettingsScreen {
public:
    // Replaces the model. Where focus is survives it when it still can, so the
    // app can rebuild after a toggle without throwing the person back to the
    // top of the list.
    void setCategories(std::vector<SettingsCategory> cats);

    // Arriving is animated every time. Focus goes to the category list, on the
    // category that was last open, because a person coming back to Settings
    // usually came back for the same thing.
    void enter();
    // See LibraryScreen::settleArrival.
    void settleArrival() { appear_.settle(1.0f); }

    // For a capture: open on a category, with focus in its rows if it has one
    // that can be focused.
    void focusCategory(int index, bool intoRows);

    // Where a Choice row is now, after a SettingChoice came back for it. -1 if
    // there is no such row.
    int choiceOf(int id) const;

    // WHETHER FOCUS IS ON THIS SCREEN AT ALL. While it is up in the bar (or
    // on the account panel), nothing here is lit: no category is focused and
    // none has its ground. MMagTech, 2026-09-24, walking along the bar past
    // Settings to the account chip: *"nothing on settings should be
    // highlighted until moving down onto accounts"*. The app says so every
    // frame; coming back grows the focus in rather than snapping it.
    void setHasFocus(bool on);

    void tick(float dt);
    Result key(Nav n);
    // FORK (handheld): a finger on the screen. True when it landed on a category
    // or a row, with what pressing that would do in `out`. See tapspots.h.
    bool tapAt(float x, float y, Result* out);
    void draw(Ctx& c);
    // Everything here is glass, so it all draws after presentScene.
    void drawGlass(Ctx& c);

private:
    bool focusable(int cat, int row) const;
    TapSpots taps_;   // FORK (handheld)
    int firstFocusable(int cat) const;
    void retargetFocus();

    std::vector<SettingsCategory> cats_;
    int cat_ = 0;
    int prevCat_ = -1;         // the category fading out after a change
    int row_ = -1;             // -1 while focus is in the category list
    bool hasFocus_ = true;
    design::Animated appear_;
    design::Animated focus_;
    design::Animated scroll_;
    design::Animated paneChange_;
    ui::QrTexture qr_;
    std::string qrFor_;
};

}  // namespace screens
