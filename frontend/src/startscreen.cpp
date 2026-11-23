// The start-up screen (startscreen.h): the Afterburner cabinet, drawn rather
// than shipped, the CabinetOS wordmark under it, the line naming the stage of
// the boot that is taking the time, and the one still frame showWaiting puts
// on the television while that work runs. Moved out of setup.cpp in the
// restructure (#398).

#include "startscreen.h"

#include <cstdio>

#include <SDL3/SDL.h>

#include "text.h"
#include "ui.h"

namespace setup {

using ui::Color;

// THE AFTERBURNER CABINET, DRAWN RATHER THAN SHIPPED.
//
// docs/PROJECT.md, "The icon has two variants and the second one is earned":
// the warm-screened cabinet with the lit power switch marks a platform that
// runs more emulators at full power. Cabinet's Mac app had it; CabinetOS
// earned it with PlayStation 2 and GameCube. So this console draws THAT one,
// not the cyan version `tools/make_icon.swift` generates.
//
// Five rounded rectangles and four circles, which is all the icon is. Drawn
// means sharp at any panel size, no image asset in the OS image, and the
// marquee and screen can be animated later — which is what the boot splash
// wants when somebody builds it.
//
// The numbers are the icon's own, in its 1024 space, sampled from
// AppIconMac.appiconset because that PNG was then the only definition of
// the Afterburner variant. `s` scales that space; `ox`/`oy` place it.
static void drawCabinet(ui::Renderer& r, float ox, float oy, float s, float a) {
    auto R = [&](float x, float y, float w, float h, float rad, Color c) {
        c.a *= a;
        r.draw(ui::Rect{ox + x * s, oy + y * s, w * s, h * s, rad * s, c});
    };
    auto dot = [&](float cx, float cy, float rad, Color c) {
        R(cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, rad, c);
    };

    const Color body  = Color::rgb(0xF2EEE8);
    const Color panel = Color::rgb(0xD7D1C8);

    // A GRADIENT IS A TEXTURE, NOT A STACK OF RECTANGLES.
    //
    // The first two attempts drew the ramps as 3 and then 30 strips, because
    // this renderer has no gradient rect. Both read as corduroy — MMagTech:
    // *"that looks really bad"* — and the second one looked worse than the
    // first, because thirty seams are more obviously wrong than two. Widening
    // the overlap to two device pixels did not help either, which was the clue
    // that the seam was not an overlap problem at all.
    //
    // The renderer already draws textured quads with linear filtering and a
    // rounded clip — `drawImage` and the QR code both use it. So the ramp is a
    // 64-texel texture and the GPU interpolates it: no seams, at any size,
    // because there are no interior edges to seam.
    //
    // It also makes the marquee animatable for free. A sweep of light across
    // it is `u0`/`u1` moving over time, which is what the boot splash in
    // docs/PROJECT.md describes when it says the marquee lights.
    // `vertical` makes the texture 1xN instead of Nx1. Rotating the quad's UVs
    // was the first attempt and it came out sideways AND reversed, which is a
    // lot of guessing about someone else's rotation convention to save four
    // characters. A texture shaped the way it is used needs no convention.
    auto rampTexture = [](Color from, Color to, bool vertical) {
        GLuint tex = 0;
        constexpr int kN = 64;
        unsigned char px[kN * 4];
        for (int i = 0; i < kN; ++i) {
            const float f = static_cast<float>(i) / (kN - 1);
            px[i * 4 + 0] = static_cast<unsigned char>((from.r + (to.r - from.r) * f) * 255);
            px[i * 4 + 1] = static_cast<unsigned char>((from.g + (to.g - from.g) * f) * 255);
            px[i * 4 + 2] = static_cast<unsigned char>((from.b + (to.b - from.b) * f) * 255);
            px[i * 4 + 3] = 255;
        }
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vertical ? 1 : kN, vertical ? kN : 1,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    };
    // Built once. This screen is drawn a handful of times per boot and the
    // textures outlive it deliberately.
    static GLuint marqueeTex =
        rampTexture(Color::rgb(0xFF96CA), Color::rgb(0xFFCB6E), /*vertical=*/false);
    static GLuint screenTex =
        rampTexture(Color::rgb(0xFEE08F), Color::rgb(0xEB5736), /*vertical=*/true);

    auto ramp = [&](float x, float y, float w, float h, float rad, GLuint tex) {
        const float px = ox + x * s, py = oy + y * s;
        const float pw = w * s, ph = h * s;
        r.drawTextured(px, py, pw, ph, tex, 0, 0, 1, 1, Color::white(a), false, 0.0f,
                       px, py, pw, ph, rad * s);
    };

    R(268, 130, 488, 670, 42, body);

    // The marquee, pink to amber, left to right.
    ramp(318, 176, 390, 84, 16, marqueeTex);

    // **NO SWEEP ACROSS THE MARQUEE, AND IT WAS BUILT AND REMOVED.**
    // MMagTech asked for one, then looked at it: *"don't like that, the static
    // was better."* He is right — a light crossing the sign pulls the eye to
    // the one part of the screen with nothing to say, and the icon is better
    // still than moving.
    //
    // **The need it was meant to serve is real and is met elsewhere**: this
    // screen can be up for ninety seconds and has to prove it has not stalled.
    // That is done with a NUMBER THAT CHANGES rather than with motion — the
    // seconds spent waiting for the server, and the games counted as they
    // arrive. Information rather than decoration, and it cannot look tacky
    // because it is not a flourish.

    ramp(316, 292, 394, 284, 18, screenTex);

    R(248, 600, 527, 112, 20, panel);
    dot(348, 656, 30, Color::rgb(0x3A3444));
    dot(500, 656, 25, Color::rgb(0xEC405C));
    dot(583, 656, 24, Color::rgb(0xFFC457));

    // THE LIT POWER SWITCH, which is the whole point of this variant: the
    // machine is ON. Its glow is three fading discs, largest first.
    dot(692, 656, 54, Color::rgb(0xFFB27A, 0.10f));
    dot(692, 656, 40, Color::rgb(0xFFB27A, 0.16f));
    dot(692, 656, 28, Color::rgb(0xFFB27A, 0.22f));
    R(668, 646, 48, 20, 10, Color::rgb(0xFFF0E2));

    R(303, 795, 419, 55, 14, panel);
}

void drawCabinetIcon(ui::Renderer& r, float x, float y, float h, float a) {
    // The cabinet spans x 248..775 and y 130..850 of its own 1024 space.
    const float s = h / 720.0f;
    drawCabinet(r, x - 248.0f * s, y - 130.0f * s, s, a);
}

void drawStartup(ui::Renderer& r, ui::TextRenderer& t, const char* detail, float alpha,
                 const char* hint) {
    const float sc = r.scale();

    r.drawBackdrop({ui::palette::kBackdropTop, ui::palette::kBackdropMid,
                    ui::palette::kBackdropBottom, 0.55f}, alpha);

    // CENTRED, AND NOT A SETUP STEP. This used to be "Starting up" at the
    // setup inset with the prose under it, which dressed the last beat of a
    // boot as a page in a wizard. MMagTech, 2026-09-22: *"the one we have now
    // is a bit dull and plain."*
    //
    // docs/PROJECT.md is explicit about what a boot screen says: *"the
    // wordmark 'CabinetOS' and nothing else. A boot screen names the machine;
    // it does not explain it."* So "Starting up" is gone — it labelled a state
    // that is self-evident — and what is left is the icon, the name, and one
    // quiet line saying which stage is taking the time.
    //
    // THE STAGE LINE IS NOT DECORATION. This screen can be up for ninety
    // seconds when the server is slow, and one sentence that never changes for
    // ninety seconds is indistinguishable from a hang.
    // **THE CABINET IS SIZED FROM THE WORDMARK, and that order matters.**
    // MMagTech, 2026-09-22: *"it looks weird that the CabinetOS is smaller in
    // width than the cabinet."* It did — the cabinet was a fixed 460 tall and
    // the name came out narrower, so the mark and the name floated as two
    // objects instead of locking into one.
    //
    // Measuring the text first and deriving the cabinet's width from it means
    // they are the same width on any panel and at any font metric, rather than
    // agreeing only at the size somebody happened to tune. Nothing here is a
    // constant that has to be re-tuned when the type changes.
    const char* kWordmark = "CabinetOS";
    const ui::TextStyle markStyle = ui::TextStyle::LargeTitle;
    const float ww = t.measure(kWordmark, markStyle, sc);

    // The icon's cabinet spans x 248..775 and y 130..850 in its own 1024 space.
    // The cabinet occupies x 248..775 and y 130..850 of the icon's own space,
    // so `oy` is where y=0 would be and the artwork starts 130 below it. The
    // first version subtracted the cabinet's HEIGHT instead of its bottom edge
    // and the wordmark landed on top of the base.
    constexpr float kIconW = 527.0f, kIconTop = 130.0f, kIconBottom = 850.0f;
    const float s = ww / kIconW;
    const float cabTop = 150.0f;
    const float oy = cabTop - kIconTop * s;
    drawCabinet(r, (ui::kCanvasWidth - ww) * 0.5f - 248.0f * s, oy, s, alpha);

    float y = oy + kIconBottom * s + 56.0f;
    t.draw(r, kWordmark, (ui::kCanvasWidth - ww) * 0.5f,
           y + t.ascent(markStyle, sc), markStyle, Color::white(0.94f * alpha), sc);
    y += t.lineHeight(markStyle, sc) + 16.0f;

    if (detail) {
        const float dw = t.measure(detail, ui::TextStyle::Body, sc);
        t.draw(r, detail, (ui::kCanvasWidth - dw) * 0.5f,
               y + t.ascent(ui::TextStyle::Body, sc), ui::TextStyle::Body,
               Color::white(0.42f * alpha), sc);
        y += t.lineHeight(ui::TextStyle::Body, sc) + 8.0f;
    }
    if (hint) {
        const float hw = t.measure(hint, ui::TextStyle::Body, sc);
        t.draw(r, hint, (ui::kCanvasWidth - hw) * 0.5f, y + t.ascent(ui::TextStyle::Body, sc),
               ui::TextStyle::Body, Color::white(0.80f * alpha), sc);
    }
}

void showWaiting(const Deps& d, const char* detail) {
    if (!d.window || !d.renderer || !d.text) return;
    ui::Renderer& r = *d.renderer;
    ui::TextRenderer& t = *d.text;

    // **PUMP FIRST, OR THIS DRAWS AT THE WRONG SIZE ON EVERY BOOT.**
    //
    // MMagTech, 2026-09-22: *"I see the starting up screen appear and it's a
    // tinier image in the bottom left with the rest of the screen black."*
    // Measured: the drawable reported 1920x1080 while the panel is 3840x2160,
    // so this drew a quarter-size frame into a 4K framebuffer — in the bottom
    // left, because that is where GL's origin is.
    //
    // SDL creates the window at its requested size and gamescope resizes it
    // immediately afterwards; the size arrives as an EVENT. Nothing had pumped
    // the queue by the time this ran, so `SDL_GetWindowSizeInPixels` answered
    // with the size before the compositor had its say. The main loop pumps and
    // gets it right, which is why only this screen was wrong.
    //
    // THIS IS NOT A TEST-RIG ARTEFACT. It is every boot on a 4K panel, and
    // this screen exists precisely because the console used to show nothing
    // for the seconds — up to ninety — that reaching the server and pulling
    // sixteen hundred games takes.
    SDL_PumpEvents();
    int dw = 0, dh = 0;
    SDL_GetWindowSizeInPixels(d.window, &dw, &dh);
    if (dw <= 0 || dh <= 0) return;
    std::fprintf(stderr, "[waiting] drawable %dx%d\n", dw, dh);
    r.beginFrame(dw, dh);
    drawStartup(r, t, detail, 1.0f);

    // Without this the frame goes into the scene texture and never reaches the
    // window — see the note at the end of Flow::draw.
    r.presentScene();
    SDL_GL_SwapWindow(d.window);
}

}  // namespace setup
