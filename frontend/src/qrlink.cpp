// The link screen: a QR code and a code to approve on a phone. Adding an
// account, the Tailscale sign-in, the diagnostic report and What's new each
// show one (qrlink.h). Moved out of screens.cpp in the restructure (#398).

#include "qrlink.h"

#include "qr.h"

namespace screens {

// --- Adding an account ------------------------------------------------------

void QrLinkScreen::open() {
    url_.clear();
    code_.clear();
    error_.clear();
    // THE LAST CODE'S PICTURE GOES TOO (#485, SETTINGS-06 and HOME-12): kept,
    // a new link whose code would not encode was drawn beside the old QR.
    qr_.set(qr::Code{});
    forkQr_.set(qr::Code{});   // FORK (handheld): no second code unless asked for
    forkUrl_.clear();
    baked_.reset();
    busy_ = true;
    appear_.retarget(0.0f, 0.0f);
    appear_.retarget(1.0f, 0.3f);
}

void QrLinkScreen::setPairing(const std::string& url, const std::string& code) {
    url_ = url;
    code_ = code;
    busy_ = false;
    std::string err;
    // THE QR IS THE SERVER'S URL AND NOT A SHAPE WE GUESSED. `romm.h` records
    // what happens otherwise: a fabricated one scans perfectly and lands on a
    // page saying the code does not exist.
    const qr::Code c = qr::encode(url, &err);
    qr_.set(c);   // an invalid code leaves no picture, never the last one
    if (!c.valid()) error_ = err;   // the address and the code below are still usable
}

void QrLinkScreen::setBaked(wikiqr::Which which) {
    // The address without its scheme, as How to writes the Wiki's: it is
    // read off the screen, and the code is the way in.
    const std::string url = wikiqr::url(which);
    url_ = url.substr(url.find("//") + 2);
    code_ = url_;
    busy_ = false;
    baked_ = which;
    qr_.set(wikiqr::qrCode(which));
}

void QrLinkScreen::setBakedWithFork(wikiqr::Which which, const std::string& forkUrl) {
    setBaked(which);
    std::string err;
    const qr::Code c = qr::encode(forkUrl, &err);
    forkQr_.set(c);   // an invalid code leaves no picture: the screen shows the one code
    if (!c.valid()) return;
    forkUrl_ = forkUrl.substr(forkUrl.find("//") == std::string::npos ? 0 : forkUrl.find("//") + 2);
}

void QrLinkScreen::setText(std::string title, std::vector<std::string> lines,
                               std::string busy) {
    title_ = std::move(title);
    lines_ = std::move(lines);
    busyLine_ = std::move(busy);
}

void QrLinkScreen::setBusy(bool on) { busy_ = on; }
void QrLinkScreen::setError(const std::string& err) { error_ = err; busy_ = false; }

void QrLinkScreen::tick(float dt) { appear_.tick(dt); }

Result QrLinkScreen::key(Nav n) {
    // ONE WAY OUT AND IT IS BACK. Nothing here is a choice — it is a code
    // somebody is copying onto a phone — so a focus ring would have nowhere to
    // go and would only invite a press that does nothing.
    if (n == Nav::Back) return {Action::Back, 0};
    return {};
}

void QrLinkScreen::draw(Ctx& c) {
    const float a = appear_.value();

    // THE SAME SHAPE AS FIRST RUN'S PAIRING STEP, and the first version of this
    // screen was not. MMagTech, 2026-09-22: *"the add user screen seemed way
    // too jarring visually in presentation and text."* It was bare text on the
    // gradient — the only screen in the console with no material under it —
    // with a 76pt title and a 76pt code competing, and a raw URL at 38pt bold
    // dominating the middle of it.
    //
    // setup.cpp already solved this screen: prose on the left, the thing you
    // act on in a panel on the right, one line along the bottom. Its own
    // comment says why the shape is constant — *"so the flow does not appear
    // to jump between five unrelated screens"* — and adding somebody is the
    // same job as pairing the first somebody. The numbers below are its
    // numbers, deliberately.
    constexpr float kInset = 80.0f;
    constexpr float kTitleTop = 150.0f;
    constexpr float kProseWidth = 760.0f;
    constexpr float kPanelX = 1020.0f;
    constexpr float kPanelY = 168.0f;
    constexpr float kPanelW = ui::kCanvasWidth - kPanelX - kInset;
    constexpr float kPanelH = 660.0f;
    constexpr float kFooterY = 900.0f;

    // --- the left column: what is happening and why --------------------------
    float y = kTitleTop;
    c.text.draw(c.r, title_, kInset,
                y + c.text.ascent(ui::TextStyle::LargeTitle, c.sc),
                ui::TextStyle::LargeTitle, ui::Color::white(0.96f * a), c.sc);
    y += c.text.lineHeight(ui::TextStyle::LargeTitle, c.sc) + 28.0f;

    // PROSE IS `Body`, WHICH IS WHAT setup.cpp USES AND WHAT THIS SCREEN DID
    // NOT. It was Title3 — 38pt against the 76pt title — and two near-headline
    // sizes stacked is most of why it read as shouting.
    //
    // Written as short lines rather than wrapped, because `wrap` lives in
    // setup.cpp behind `hardWrap` and this does not need either: the strings
    // are fixed, they are mine, and they are well inside a 760pt column at
    // 29pt. **If a line here ever grows, measure it** — that is the rule a
    // truncated tile caption already bought once.
    const std::vector<std::string> lines =
        busy_ ? std::vector<std::string>{busyLine_} : lines_;
    for (const std::string& line : lines) {
        c.text.draw(c.r, line, kInset, y + c.text.ascent(ui::TextStyle::Body, c.sc),
                    ui::TextStyle::Body, ui::Color::white(0.72f * a), c.sc);
        y += c.text.lineHeight(ui::TextStyle::Body, c.sc);
    }

    // THE ADDRESS AND THE CODE GO IN THE PROSE COLUMN, and putting them in the
    // panel was the second mistake on this screen. setup.cpp says why in its
    // own words: *"the pairing step puts the address and the code in the prose
    // column, because they are the things somebody reads out or types — the QR
    // is only a shortcut past typing them."* In the panel they were also white
    // text over a light glass card, which is the contrast the QR's own white
    // background creates, and the address was touching the panel's bottom edge.
    if (!code_.empty()) {
        y += 30.0f;
        // FORK (handheld): with two codes, each address is named: the original's
        // first (the line below), then this fork's.
        const bool two = !forkUrl_.empty() && forkQr_.valid();
        if (two) {
            c.text.draw(c.r, "Original CabinetOS", kInset,
                        y + c.text.ascent(ui::TextStyle::Body, c.sc), ui::TextStyle::Body,
                        ui::Color::white(0.72f * a), c.sc);
            y += c.text.lineHeight(ui::TextStyle::Body, c.sc);
        }
        if (!url_.empty()) {
            // THE WHOLE LINK, code and all, as first run shows it. It was
            // shown without `?user_code=`, and typed into a browser that
            // opens RomM's page with nowhere to put the code (MMagTech, with
            // no phone to scan the QR, 2026-10-06).
            std::string shown = url_;
            if (shown.rfind("http://", 0) == 0) shown = shown.substr(7);
            c.text.draw(c.r, shown, kInset,
                        y + c.text.ascent(ui::TextStyle::Body, c.sc),
                        ui::TextStyle::Body, ui::palette::kScreenCyan, c.sc);
            y += c.text.lineHeight(ui::TextStyle::Body, c.sc);
        }
        if (two) {
            y += 28.0f;
            c.text.draw(c.r, "CabinetOS Handheld", kInset,
                        y + c.text.ascent(ui::TextStyle::Body, c.sc), ui::TextStyle::Body,
                        ui::Color::white(0.72f * a), c.sc);
            y += c.text.lineHeight(ui::TextStyle::Body, c.sc);
            c.text.draw(c.r, forkUrl_, kInset, y + c.text.ascent(ui::TextStyle::Body, c.sc),
                        ui::TextStyle::Body, ui::palette::kScreenCyan, c.sc);
            y += c.text.lineHeight(ui::TextStyle::Body, c.sc);
        }
        // NO SEPARATE "Code" LINE: the link carries the code, and showing it
        // twice read as a mistake (MMagTech, 2026-10-06).
    }

    // AN OUTCOME BELONGS IN THE PROSE COLUMN, not over the code. This is where
    // "nobody was added" lands, and it is the whole reason that case stays on
    // this screen instead of returning to a panel that looks unchanged.
    if (!error_.empty()) {
        y += 30.0f;
        std::string rest = error_;
        for (int guard = 0; guard < 6 && !rest.empty(); ++guard) {
            size_t cut = rest.size();
            while (cut > 0 &&
                   c.text.measure(rest.substr(0, cut), ui::TextStyle::Callout, c.sc) > kProseWidth) {
                const size_t sp = rest.rfind(' ', cut - 1);
                if (sp == std::string::npos) break;
                cut = sp;
            }
            c.text.draw(c.r, rest.substr(0, cut), kInset,
                        y + c.text.ascent(ui::TextStyle::Callout, c.sc),
                        ui::TextStyle::Callout, ui::Color::white(0.92f * a), c.sc);
            y += c.text.lineHeight(ui::TextStyle::Callout, c.sc);
            rest = (cut >= rest.size()) ? std::string() : rest.substr(cut + 1);
        }
    }

    // --- the right column: the thing you act on ------------------------------
    //
    // **NO GLASS PANEL BEHIND THE CODE, AND THERE WAS ONE.** MMagTech, looking
    // at it: *"why the giant white box around the qrcode."* Because there were
    // two boxes — a white card inside a light glass panel — and only one of
    // them earns its place.
    //
    // THE WHITE CARD IS NOT DECORATION. It is the quiet zone: four modules of
    // real white around the symbol, which `qr.h` records as measured rather
    // than assumed — the same code drawn flush to its edge does not decode at
    // all, and with the margin it decodes every time. It cannot go.
    //
    // The panel could, and did. It was copied from setup.cpp's shape, where
    // the same panel also holds rows of networks and controllers at the other
    // steps; here it only ever holds the QR, so it was a box around a card
    // holding nothing else. The card is the material on this side now.

    if (code_.empty()) return;

    // Everything that is words lives in the column on the left; this side is
    // the shortcut past typing them. Bigger than it was, because it no longer
    // has to leave room around itself inside something else — and a code that
    // is photographed from a sofa cannot be too large.
    // FORK (handheld): two codes one above the other, a label under each. The
    // original's, exactly as it was drawn (the cabinet over its middle), on top;
    // this fork's GitHub page below it.
    if (forkQr_.valid() && qr_.valid()) {
        constexpr float kCode = 400.0f;      // each code's square
        constexpr float kLabelGap = 8.0f;    // from the code to its label
        constexpr float kBlockGap = 18.0f;   // from one label to the next code
        const float labelH = c.text.lineHeight(ui::TextStyle::Body, c.sc);
        const float block = kCode + kLabelGap + labelH;
        const float top = (ui::kCanvasHeight - (2.0f * block + kBlockGap)) * 0.5f;
        const float x = kPanelX + (kPanelW - kCode) * 0.5f;
        const char* const names[2] = {"Original CabinetOS", "CabinetOS Handheld"};
        for (int i = 0; i < 2; ++i) {
            const float qy2 = top + static_cast<float>(i) * (block + kBlockGap);
            if (i == 0) {
                qr_.draw(c.r, x, qy2, kCode);
                if (baked_) wikiqr::drawPatch(*baked_, c.r, x, qy2, kCode, 1.0f);
            } else {
                forkQr_.draw(c.r, x, qy2, kCode);
            }
            const float w = c.text.measure(names[i], ui::TextStyle::Body, c.sc);
            c.text.draw(c.r, names[i], x + (kCode - w) * 0.5f,
                        qy2 + kCode + kLabelGap + c.text.ascent(ui::TextStyle::Body, c.sc),
                        ui::TextStyle::Body, ui::Color::white(0.85f * a), c.sc);
        }
        return;
    }
    const float side = 560.0f;
    const float qx = kPanelX + (kPanelW - side) * 0.5f;
    const float qy = kPanelY + (kPanelH - side) * 0.5f;
    if (qr_.valid()) qr_.draw(c.r, qx, qy, side);
    // At full strength, as the code under it is drawn.
    if (baked_ && qr_.valid()) wikiqr::drawPatch(*baked_, c.r, qx, qy, side, 1.0f);

    // --- NOTHING ALONG THE BOTTOM, and both lines that were here are gone ---
    //
    // "They are added to this console. Switching to them is separate." was
    // dropped at MMagTech's request: it answers a question nobody has asked
    // yet, at the moment they are trying to scan a code, and the panel names
    // who was added the instant it happens.
    //
    // **"B to go back" WAS WORSE — IT WAS WRONG.** MMagTech: *"b doesnt let me
    // go back like it suggests."* It does not, because SDL maps face buttons
    // by POSITION: `SDL_GAMEPAD_BUTTON_EAST` is Back everywhere in this
    // product, and on a Switch Pro Controller the east button is physically
    // **A**. South is Activate, and south is B. The screen was naming the one
    // button that does the opposite of what it claimed.
    //
    // **SO NOTHING HERE NAMES A PHYSICAL BUTTON.** No other screen in this
    // product does, and this is why: the letter depends on the pad, and a
    // console that will meet Switch, Xbox and PlayStation controllers cannot
    // put one in a string. If a hint is ever wanted here it has to come from
    // the pad SDL actually reports, not from a constant.
}

}  // namespace screens
