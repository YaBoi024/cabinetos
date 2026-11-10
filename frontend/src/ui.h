// The CabinetOS UI primitives.
//
// Everything the design system describes is a rounded rectangle with some
// combination of fill, border and shadow — a cover card, a settings row, a
// pill, the pause panel. So that is the one primitive this renderer has, drawn
// with a signed-distance field in the fragment shader rather than with geometry,
// which is what makes a 4pt inset rim and a 26px soft shadow cheap and exact at
// any size.
//
// Coordinates are design points in a 1920x1080 space, origin top-left, matching
// docs/PROJECT.md's "The canvas". The projection handles the rest, so a 4K panel
// is a rendering decision and never a layout one.

#pragma once

#include <GLES3/gl3.h>

#include <cstdint>

namespace ui {

// Design-space canvas. Every number in the design system is in these units.
//
// FORK (handheld): THE WIDTH IS WHAT THE SCREEN NEEDS, NOT ALWAYS 1920. The
// design width is the least it is; Renderer::beginFrame widens it for a screen
// wider than 16:9 when platform::fillsWideScreens() says so, and every screen
// that lays itself out from kCanvasWidth then uses the whole width. Unchanged
// (1920) on the console. Read and written on the frame thread.
constexpr float kCanvasDesignWidth = 1920.0f;
extern float kCanvasWidth;
constexpr float kCanvasHeight = 1080.0f;

// The ten-foot safe area. Not a number invented here: it is the reference
// implementation's own `contentInset` for a television, and it exists because a
// physical set crops the edges of the picture. docs/PROJECT.md records the
// hero being resized three times over exactly this, including once where the
// simulator showed it fitting and real hardware did not.
//
// Nothing a person needs to read or reach may sit outside it. Backgrounds and
// artwork may, and should, run to the edge.
constexpr float kSafeInset = 60.0f;

struct Color {
    float r, g, b, a;

    // Values in docs/PROJECT.md are written as hex, so accept them that way and
    // keep the source honest against the document.
    static constexpr Color rgb(uint32_t hex, float alpha = 1.0f) {
        return Color{((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,
                     (hex & 0xFF) / 255.0f, alpha};
    }
    static constexpr Color white(float alpha) { return Color{1, 1, 1, alpha}; }
    static constexpr Color black(float alpha) { return Color{0, 0, 0, alpha}; }
};

// The palette, from docs/PROJECT.md "Branding" and "Colour tokens". Named for
// the role rather than the colour, so a retune changes one line here.
namespace palette {
constexpr Color kBackdropTop = Color::rgb(0x3A2268);
constexpr Color kBackdropMid = Color::rgb(0x120C26);  // at 0.55, not the midpoint
constexpr Color kBackdropBottom = Color::rgb(0x090614);
constexpr Color kSurface = Color::rgb(0x241A3D);       // library tile panel
constexpr Color kScreenCyan = Color::rgb(0x58E8F6);    // candidate accent
constexpr Color kMarqueeAmber = Color::rgb(0xFFC457);
constexpr Color kFocusRim = Color::white(0.85f);
}  // namespace palette

// One rounded rectangle. Anything the design system draws is one of these, or a
// few of them stacked.
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float radius = 0;
    Color fill = Color::white(0);

    // The focus rim: drawn INSIDE the edge, like the reference implementation's
    // inset stroke, so a focused card does not grow by its own border width.
    float border = 0;
    Color borderColor = Color::white(0);

    // A soft drop shadow behind the shape. blur is the falloff distance in
    // points; offsetY pushes it down. The design system's focused-artwork
    // shadow is black @ 55%, blur 26, y +14.
    float shadowBlur = 0;
    float shadowOffsetY = 0;
    Color shadowColor = Color::black(0);

    // A VERTICAL GRADIENT between `fill` at the top and `fillBottom` at the
    // bottom. Off by default, and when off the shape draws exactly as it did
    // before this existed. A large flat panel reads as a hole punched in the
    // screen; a few per cent of gradient reads as a surface.
    bool gradient = false;
    Color fillBottom = Color::white(0);

    // A highlight along the TOP EDGE ONLY, as if lit from above. Alpha 0 is
    // off. This is not the same thing as `border`: a rim of even weight all the
    // way round says "outline", where a top-only highlight says "edge".
    Color edgeLight = Color::white(0);

    // THE OUTLINE, when it is not a rectangle (#267): a favourite's heart,
    // drawn by the same shader so fill, rim and shadow work the same.
    // The shape fits inside the rectangle, centred; `radius` softens its
    // corners. Box is every shape that existed before this.
    enum class Shape { Box, Heart, Trophy, Bolt, Book };
    Shape shape = Shape::Box;
};

// A three-stop vertical gradient, which is exactly what the backdrop is and
// nothing else currently needs.
struct Gradient {
    Color top, mid, bottom;
    float midStop = 0.55f;
};

class Renderer {
public:
    bool init();
    void shutdown();

    // drawableWidth/Height are real pixels; the canvas is scaled to fit and
    // centred, so an overscan-safe layout stays correct on any panel.
    void beginFrame(int drawableWidth, int drawableHeight);
    // `alpha` below 1 fades it over what is already drawn: an account
    // switch's curtain. Two gradient bands were tried for that first and
    // left a line across the screen where they met.
    void drawBackdrop(const Gradient& g, float alpha = 1.0f);
    void draw(const Rect& r);

    // A textured quad in design points. Glyphs use it; cover art and a running
    // core's frame will use it too, which is the point of it existing rather
    // than a text-only path.
    // lodBias forces sampling from a coarser mip level. That is how the
    // blurred echo under an odd-shaped cover is drawn: a box blur that costs a
    // texture fetch rather than a blur pass.
    // `rotation` turns the PICTURE by that many quarter turns anticlockwise,
    // for a vertical arcade board that renders sideways and asks the frontend
    // to turn it round. It is applied to the quad's corners before the texture
    // coordinates are looked up, which is the only place it can go: a
    // ninety-degree turn transposes x and y, and no ordering of u0,v0,u1,v1
    // can say that. It therefore composes with whatever the uv rectangle
    // already says rather than replacing it.
    void drawTextured(float x, float y, float w, float h, GLuint texture, float u0,
                      float v0, float u1, float v1, const Color& tint,
                      bool singleChannel = true, float lodBias = 0.0f, float clipX = 0,
                      float clipY = 0, float clipW = 0, float clipH = 0,
                      float clipRadius = 0, bool opaque = false, int rotation = 0);

    // Device pixels per design point for the frame in progress. Text has to
    // rasterise at device resolution to be crisp on a 4K set, so it needs this.
    float scale() const { return scale_; }

    // A GLOBAL ALPHA ON CONTENT, for screen transitions — new 2026-09-21.
    //
    // Every shape and every picture is multiplied by this. It exists because a
    // screen arriving used to be a hard cut: Home was replaced by the Library
    // between one frame and the next, with nothing in between, and MMagTech on
    // the panel: *"after this transition to the library it isnt smooth."*
    //
    // It is applied in C++ rather than in the shaders, so nothing about the
    // rendering changes when it is 1 — which it is for all but 280ms at a time.
    //
    // THE BACKDROP DOES NOT HONOUR IT, deliberately. The gradient and the lit
    // artwork behind it are the GROUND, and a ground that fades out leaves a
    // hole; they stay put while the content on them changes. That continuity is
    // half of why the transition reads as smooth at all.
    void setContentAlpha(float a) {
        contentRaw_ = a < 0 ? 0 : (a > 1 ? 1 : a);
        contentAlpha_ = contentRaw_ * contentFade_;
    }
    float contentAlpha() const { return contentRaw_; }

    // A SECOND MULTIPLIER, OWNED BY THE APP, for switching top-bar
    // destinations — 2026-09-24. A screen sets its own arrival alpha above and
    // knows nothing about leaving; this fades whatever screen is showing out,
    // and the next one in, underneath whatever the screen itself asks for.
    // AND A RISE, in design points, for the same switch: the arriving screen
    // starts this far down and comes up to where it belongs while it fades
    // in. Shapes, glass and pictures all move; the backdrop and the scene's
    // own presentation do not. Zero outside a switch.
    void setContentOffsetY(float dy) { offsetY_ = dy; }
    float contentOffsetY() const { return offsetY_; }
    // AND ACROSS, for a game's page sliding aside for its manual (#364): the
    // same movement, sideways. Zero outside that slide.
    void setContentOffsetX(float dx) { offsetX_ = dx; }
    float contentOffsetX() const { return offsetX_; }

    void setContentFade(float f) {
        contentFade_ = f < 0 ? 0 : (f > 1 ? 1 : f);
        contentAlpha_ = contentRaw_ * contentFade_;
    }

    // A SCISSOR, IN DESIGN POINTS — new 2026-09-21.
    //
    // A scrolling screen is a window onto a list, and until now nothing said
    // so: the Library's tiles and a platform grid's covers simply scrolled up
    // the canvas and out of the top of it. That was invisible while the top of
    // the screen was empty. It stopped being invisible the moment the top bar
    // became permanent chrome — the grid's title rode up THROUGH "Library
    // Search Settings" and the two drew over each other.
    //
    // So a screen clips its own scrolling region and the bar sits outside it.
    // glScissor rather than a shader clip, because it costs nothing and it
    // applies to shapes, pictures and text alike — text is the one that
    // matters here and the one a per-draw clip rectangle would have missed.
    //
    // The rectangle is in canvas points; the viewport transform and the
    // letterbox offset are applied here so callers never see device pixels.
    void setScissor(float x, float y, float w, float h);
    void clearScissor();

    // PIXEL SHIFT, in whole device pixels, right and down — docs/PROJECT.md,
    // open question 10b, and idle.h for the walk that sets it. Applied where
    // the finished frame meets the window, so every screen and every game
    // moves together and no call site knows it happened.
    void setPixelShift(int dx, int dy) { shiftX_ = dx; shiftY_ = dy; }

    // DRAW ONTO NOTHING INSTEAD OF ONTO BLACK.
    //
    // For the one case where this console is not the only thing on the screen:
    // an emulator that owns its own window and presents for itself, with our
    // menu composited on top by gamescope. There the frame we produce must be
    // TRANSPARENT wherever we have not drawn, or we would black the game out.
    //
    // It changes the two clears and nothing else. The blend function is already
    // right — glBlendFuncSeparate keeps a correct destination alpha — which is
    // why this is a flag and not a second renderer.
    //
    // docs/PROJECT.md, open question 24.
    void setTransparentBackground(bool on) { transparentBackground_ = on; }

    // --- Frosted glass -------------------------------------------------------
    //
    // The single largest contributor to what the reference implementation
    // actually looks like, and the one thing on its list that Linux supplies
    // nothing for. A panel over cover art without it is not "less pretty", it
    // is unreadable — the art shows through and competes with the text.
    //
    // The mechanism: the scene is drawn into an offscreen texture, mipmapped,
    // and a glass panel samples that texture at a coarse level under itself.
    // A mip chain is a box blur that the GPU already had to build, so this is
    // a texture fetch rather than a blur pass — which matters on integrated
    // graphics that also has a PS2 to emulate.
    //
    //   beginFrame -> draw the world -> presentScene -> drawGlass/draw/text
    //
    // Callers that never use glass can ignore all of it; presentScene is a
    // no-op then.
    // Draws the safe area and a 5% overscan allowance, for checking on a real
    // television where the picture actually stops.
    // In-software bias lighting for the dead space around a letterboxed game
    // picture. `peak` is the white opacity right at the picture's edge:
    // 0.025 subtle, 0.04 strong, both measured on a real panel with a slider
    // after two sets guessed from a mockup were wrong.
    void drawBiasGlow(float x, float y, float w, float h, float peak);

    void drawSafeAreaGuides();

    void presentScene();
    bool sceneCaptured() const { return scenePresented_; }

    // A DISSOLVE BETWEEN TWO SCREENS — 2026-09-24. The app keeps one screen at
    // a time, so the outgoing one cannot be drawn next to the incoming one.
    // Instead the finished frame is copied, once, at the moment of the switch,
    // and drawn over the new screen fading away. MMagTech asked for Settings to
    // arrive see-through, with the old screen showing through it; this is that
    // for every top-bar switch, background included.
    //
    // Call captureSnapshot() at the END of a frame, after everything is drawn
    // and before the swap. drawSnapshot() draws it over the canvas at `alpha`.
    void captureSnapshot();
    void drawSnapshot(float alpha);

    // A rounded panel that blurs what is behind it. `blur` is a mip level:
    // roughly 4 is the "thin material" of a pill, 6 the "regular material" of
    // a panel. `tint` is composited over the blur.
    void drawGlass(const Rect& r, float blur, const Color& tint);

    // Reads the frame back and writes a BMP. The VM has no way to show a
    // screenshot to anyone, and "it looked right on my machine" is not a thing
    // this project can say, so the frontend can always photograph itself.
    bool saveFrame(const char* path, int drawableWidth, int drawableHeight) const;

    // Render into an offscreen target of an arbitrary size instead of the
    // window. This is how a 1080p development VM proves its layout on a 4K
    // television it does not have, and how CI checks a panel size nobody owns.
    // Most people's sets are 4K; some are not; neither may be assumed, so both
    // have to be checkable from the machine that is actually here.
    bool beginOffscreen(int width, int height);
    void endOffscreen();

private:
    bool transparentBackground_ = false;
    GLuint program_ = 0;
    GLuint backdropProgram_ = 0;
    GLuint texturedProgram_ = 0;
    float scale_ = 1.0f;
    float contentAlpha_ = 1.0f;   // what draws use: the two below, multiplied
    float contentRaw_ = 1.0f;
    float contentFade_ = 1.0f;
    float offsetY_ = 0.0f;
    float offsetX_ = 0.0f;
    GLuint vao_ = 0;
    GLuint targetFBO_ = 0;    // 0 is the window; an offscreen render redirects it
    GLuint offscreenFBO_ = 0;
    GLuint offscreenTex_ = 0;
    int offscreenW_ = 0, offscreenH_ = 0;

    // The scene, captured so panels can blur it.
    GLuint sceneFBO_ = 0, sceneTex_ = 0;
    GLuint snapTex_ = 0;
    int snapW_ = 0, snapH_ = 0;
    int sceneW_ = 0, sceneH_ = 0;
    bool scenePresented_ = false;
    int vx_ = 0, vy_ = 0, vw_ = 0, vh_ = 0;
    int shiftX_ = 0, shiftY_ = 0;
    int ox_ = 0, oy_ = 0;   // origin of the viewport in force, for the scissor
    int drawableW_ = 0, drawableH_ = 0;
    GLuint blurProgram_ = 0;
    GLuint glowProgram_ = 0;
    struct {
        GLint canvas, rect, radius, tint, tex, lod, alpha;
    } gloc_{};
    struct {
        GLint canvas, picture, peak, shadow, rect;
    } wloc_{};

    struct {
        GLint canvas, rect, radius, fill, border, borderColor, shadow, shadowColor,
            shadowVS, fillBottom, edgeLight, shape;
    } loc_{};
    struct {
        GLint top, mid, bottom, midStop, alpha;
    } bloc_{};
    struct {
        GLint canvas, rect, uv, tint, tex, single, lod, clip, clipRadius, opaque, rot;
    } tloc_{};
};

}  // namespace ui
