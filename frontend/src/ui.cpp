#include "ui.h"
#include "platform.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace ui {

float kCanvasWidth = kCanvasDesignWidth;
namespace {

// The eight shader sources (shaders.inc, included here and nowhere else).
#include "shaders.inc"

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[ui] shader compile failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* vs, const char* fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[ui] program link failed: %s\n", log);
        glDeleteProgram(p);
        p = 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

}  // namespace

bool Renderer::init() {
    program_ = link(kQuadVS, kQuadFS);
    backdropProgram_ = link(kBackdropVS, kBackdropFS);
    texturedProgram_ = link(kTexturedVS, kTexturedFS);
    // Reuses the solid path's vertex shader: same geometry, same rounding,
    // same edges.
    blurProgram_ = link(kQuadVS, kGlassFS);
    glowProgram_ = link(kQuadVS, kGlowFS);
    if (!program_ || !backdropProgram_ || !texturedProgram_ || !blurProgram_ ||
        !glowProgram_)
        return false;

    wloc_.canvas = glGetUniformLocation(glowProgram_, "uCanvas");
    wloc_.picture = glGetUniformLocation(glowProgram_, "uPicture");
    wloc_.peak = glGetUniformLocation(glowProgram_, "uPeak");
    wloc_.shadow = glGetUniformLocation(glowProgram_, "uShadow");
    wloc_.rect = glGetUniformLocation(glowProgram_, "uRect");

    gloc_.canvas = glGetUniformLocation(blurProgram_, "uCanvas");
    gloc_.rect = glGetUniformLocation(blurProgram_, "uRect");
    gloc_.radius = glGetUniformLocation(blurProgram_, "uRadius");
    gloc_.tint = glGetUniformLocation(blurProgram_, "uTint");
    gloc_.tex = glGetUniformLocation(blurProgram_, "uScene");
    gloc_.lod = glGetUniformLocation(blurProgram_, "uLod");
    gloc_.alpha = glGetUniformLocation(blurProgram_, "uAlpha");

    loc_.canvas = glGetUniformLocation(program_, "uCanvas");
    loc_.rect = glGetUniformLocation(program_, "uRect");
    loc_.radius = glGetUniformLocation(program_, "uRadius");
    loc_.fill = glGetUniformLocation(program_, "uFill");
    loc_.fillBottom = glGetUniformLocation(program_, "uFillBottom");
    loc_.edgeLight = glGetUniformLocation(program_, "uEdgeLight");
    loc_.shape = glGetUniformLocation(program_, "uShape");
    loc_.border = glGetUniformLocation(program_, "uBorder");
    loc_.borderColor = glGetUniformLocation(program_, "uBorderColor");
    loc_.shadow = glGetUniformLocation(program_, "uShadowParams");
    loc_.shadowColor = glGetUniformLocation(program_, "uShadowColor");
    loc_.shadowVS = glGetUniformLocation(program_, "uShadow");

    bloc_.top = glGetUniformLocation(backdropProgram_, "uTop");
    bloc_.mid = glGetUniformLocation(backdropProgram_, "uMid");
    bloc_.bottom = glGetUniformLocation(backdropProgram_, "uBottom");
    bloc_.midStop = glGetUniformLocation(backdropProgram_, "uMidStop");
    bloc_.alpha = glGetUniformLocation(backdropProgram_, "uAlpha");

    tloc_.canvas = glGetUniformLocation(texturedProgram_, "uCanvas");
    tloc_.rect = glGetUniformLocation(texturedProgram_, "uRect");
    tloc_.uv = glGetUniformLocation(texturedProgram_, "uUV");
    tloc_.tint = glGetUniformLocation(texturedProgram_, "uTint");
    tloc_.opaque = glGetUniformLocation(texturedProgram_, "uOpaque");
    tloc_.tex = glGetUniformLocation(texturedProgram_, "uTex");
    tloc_.single = glGetUniformLocation(texturedProgram_, "uSingleChannel");
    tloc_.lod = glGetUniformLocation(texturedProgram_, "uLod");
    tloc_.clip = glGetUniformLocation(texturedProgram_, "uClip");
    tloc_.clipRadius = glGetUniformLocation(texturedProgram_, "uClipRadius");
    tloc_.rot = glGetUniformLocation(texturedProgram_, "uRot");

    // GLES 3 still requires a bound vertex array even when every attribute is
    // derived from gl_VertexID and nothing is read from a buffer.
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    return true;
}

void Renderer::drawTextured(float x, float y, float w, float h, GLuint texture,
                            float u0, float v0, float u1, float v1, const Color& tint,
                            bool singleChannel, float lodBias, float clipX, float clipY,
                            float clipW, float clipH, float clipRadius, bool opaque,
                            int rotation) {
    glUseProgram(texturedProgram_);
    glUniform2f(tloc_.canvas, kCanvasWidth, kCanvasHeight);
    y += offsetY_;
    x += offsetX_;
    if (clipW > 0) {
        clipY += offsetY_;
        clipX += offsetX_;
    }
    glUniform4f(tloc_.rect, x, y, w, h);
    glUniform4f(tloc_.uv, u0, v0, u1, v1);
    // The transition alpha, the same as Renderer::draw applies to a shape.
    // `opaque` is the one thing it must not touch: a running game's frame has
    // its alpha thrown away on purpose, and fading it would be fading the game.
    glUniform4f(tloc_.tint, tint.r, tint.g, tint.b,
                opaque ? tint.a : tint.a * contentAlpha_);
    glUniform1i(tloc_.single, singleChannel ? 1 : 0);
    glUniform1f(tloc_.lod, lodBias);
    glUniform4f(tloc_.clip, clipX, clipY, clipW, clipH);
    glUniform1f(tloc_.clipRadius, clipRadius);
    glUniform1i(tloc_.opaque, opaque ? 1 : 0);
    glUniform1i(tloc_.rot, rotation & 3);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(tloc_.tex, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::setScissor(float x, float y, float w, float h) {
    // Canvas points to device pixels, through the same letterboxed viewport
    // beginFrame set up. GL's origin is the BOTTOM left and the canvas's is the
    // top left, so the y is flipped here rather than at every call site.
    // Relative to the viewport IN FORCE, not to the letterbox: inside the
    // scene target the canvas starts at 0, and on the window it starts at the
    // letterbox plus the pixel shift.
    const int px = ox_ + static_cast<int>(x * scale_);
    const int pw = static_cast<int>(w * scale_);
    const int ph = static_cast<int>(h * scale_);
    const int py = oy_ + static_cast<int>((kCanvasHeight - y - h) * scale_);
    glEnable(GL_SCISSOR_TEST);
    glScissor(px, py, pw > 0 ? pw : 0, ph > 0 ? ph : 0);
}

void Renderer::clearScissor() { glDisable(GL_SCISSOR_TEST); }

bool Renderer::beginOffscreen(int width, int height) {
    if (offscreenFBO_ && (offscreenW_ != width || offscreenH_ != height)) endOffscreen();
    if (!offscreenFBO_) {
        glGenTextures(1, &offscreenTex_);
        glBindTexture(GL_TEXTURE_2D, offscreenTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenFramebuffers(1, &offscreenFBO_);
        glBindFramebuffer(GL_FRAMEBUFFER, offscreenFBO_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               offscreenTex_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::fprintf(stderr, "[ui] offscreen %dx%d incomplete\n", width, height);
            endOffscreen();
            return false;
        }
        offscreenW_ = width;
        offscreenH_ = height;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, offscreenFBO_);
    // Where the finished frame goes. presentScene used to bind 0 unconditionally
    // — the WINDOW — so with --render-size the composited frame went to the
    // window while saveFrame read the offscreen target, and a 1920x1080 capture
    // came back as the window's contents in one corner of a black frame. It only
    // shows when both are in play: an offscreen render AND frosted glass, which
    // is every screen that has a pill or a panel on it.
    targetFBO_ = offscreenFBO_;
    return true;
}

void Renderer::endOffscreen() {
    targetFBO_ = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (offscreenFBO_) glDeleteFramebuffers(1, &offscreenFBO_);
    if (offscreenTex_) glDeleteTextures(1, &offscreenTex_);
    offscreenFBO_ = offscreenTex_ = 0;
    offscreenW_ = offscreenH_ = 0;
}

void Renderer::shutdown() {
    endOffscreen();
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    if (backdropProgram_) glDeleteProgram(backdropProgram_);
    if (texturedProgram_) glDeleteProgram(texturedProgram_);
    if (blurProgram_) glDeleteProgram(blurProgram_);
    if (glowProgram_) glDeleteProgram(glowProgram_);
    if (sceneFBO_) glDeleteFramebuffers(1, &sceneFBO_);
    if (sceneTex_) glDeleteTextures(1, &sceneTex_);
    sceneFBO_ = sceneTex_ = 0;
    vao_ = program_ = backdropProgram_ = texturedProgram_ = blurProgram_ = 0;
}

void Renderer::presentScene() {
    if (sceneFBO_ == 0) return;   // nothing captured; solid panels only
    // Mip the scene. This IS the blur: level 4 is a sixteenth-scale box
    // average, level 6 a sixty-fourth, and sampling one back up is what a
    // "material" looks like.
    glBindTexture(GL_TEXTURE_2D, sceneTex_);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO_);
    // Clear the WHOLE drawable first, not just the viewport. The letterbox
    // bars are outside the canvas, so nothing ever draws into them — and
    // without this they keep whatever the previous frame left there, which
    // showed as a ghost strip of the last frame across the top of the screen.
    glViewport(0, 0, drawableW_, drawableH_);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, transparentBackground_ ? 0.0f : 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // The pixel shift lands HERE, on the finished picture, and nowhere else:
    // the scene was drawn unshifted, and everything drawn after this point —
    // glass, text, the overlay — goes through this same viewport, so the whole
    // frame moves as one. The strip it uncovers was cleared to black above.
    ox_ = vx_ + shiftX_;
    oy_ = vy_ - shiftY_;   // GL's y runs up; the canvas's runs down
    glViewport(ox_, oy_, vw_, vh_);
    glDisable(GL_BLEND);
    // The finished scene goes back exactly where it was drawn: no switch's
    // rise. (Blending is off here, so the fade cannot touch it either.)
    const float keepOffset = offsetY_, keepOffsetX = offsetX_;
    offsetY_ = offsetX_ = 0.0f;
    drawTextured(0, 0, kCanvasWidth, kCanvasHeight, sceneTex_, 0, 1, 1, 0,
                 Color{1, 1, 1, 1}, false);
    offsetY_ = keepOffset;
    offsetX_ = keepOffsetX;
    glEnable(GL_BLEND);
    scenePresented_ = true;
}

void Renderer::captureSnapshot() {
    if (vw_ <= 0 || vh_ <= 0) return;
    if (!snapTex_) glGenTextures(1, &snapTex_);
    glBindTexture(GL_TEXTURE_2D, snapTex_);
    if (snapW_ != vw_ || snapH_ != vh_) {
        // RGB, not RGBA: a copy may drop the framebuffer's alpha but may not
        // invent one, and the window's may have none. It is drawn opaque.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, vw_, vh_, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        snapW_ = vw_;
        snapH_ = vh_;
    }
    // The canvas's own rectangle of the finished frame, letterbox excluded,
    // so drawing it back over 0..1920 x 0..1080 lands pixel for pixel.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, targetFBO_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ox_, oy_, vw_, vh_);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO_);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::drawSnapshot(float alpha) {
    if (!snapTex_ || alpha <= 0.001f) return;
    // Opaque: the copy has no alpha of its own, and the tint's alpha is the
    // dissolve. Rows run bottom-up in a copied framebuffer, hence v 1 to 0.
    const float keepOffset = offsetY_, keepOffsetX = offsetX_;
    offsetY_ = offsetX_ = 0.0f;
    drawTextured(0, 0, kCanvasWidth, kCanvasHeight, snapTex_, 0, 1, 1, 0,
                 Color{1, 1, 1, alpha}, false, 0.0f, 0, 0, 0, 0, 0, /*opaque=*/true);
    offsetY_ = keepOffset;
    offsetX_ = keepOffsetX;
}

void Renderer::drawBiasGlow(float x, float y, float w, float h, float peak) {
    if (peak <= 0.0f) return;
    glUseProgram(glowProgram_);
    // The vertex shader is the shared one, so it needs a rect to cover — the
    // whole canvas here, since the glow lives everywhere the picture is not.
    glUniform4f(wloc_.rect, 0, 0, kCanvasWidth, kCanvasHeight);
    if (wloc_.shadow >= 0) glUniform1f(wloc_.shadow, 0.0f);
    glUniform2f(wloc_.canvas, kCanvasWidth, kCanvasHeight);
    glUniform4f(wloc_.picture, x, y, w, h);
    glUniform1f(wloc_.peak, peak);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::drawSafeAreaGuides() {
    // Two boxes: the design system's own content inset, and a 5% overscan
    // allowance. On a monitor both are decoration; on a television the outer
    // one is roughly where a panel starts eating the picture, and anything
    // important outside it will be cropped on somebody's set.
    const Color inner = Color::rgb(0x58E8F6, 0.55f);
    const Color outer = Color::rgb(0xEC405C, 0.55f);
    auto box = [&](float inset, const Color& c) {
        const float t = 2.0f;
        const float w = kCanvasWidth - inset * 2, h = kCanvasHeight - inset * 2;
        draw(Rect{inset, inset, w, t, 0, c});
        draw(Rect{inset, inset + h - t, w, t, 0, c});
        draw(Rect{inset, inset, t, h, 0, c});
        draw(Rect{inset + w - t, inset, t, h, 0, c});
    };
    box(kSafeInset, inner);
    box(kCanvasWidth * 0.05f, outer);
}

void Renderer::drawGlass(const Rect& r, float blur, const Color& tint) {
    if (!scenePresented_) {
        // No scene to blur — fall back to a solid fill of the same shape, so a
        // caller never has to know whether capture happened.
        Rect solid = r;
        solid.fill = Color{tint.r, tint.g, tint.b, 0.92f};
        draw(solid);
        return;
    }
    glUseProgram(blurProgram_);
    glUniform2f(gloc_.canvas, kCanvasWidth, kCanvasHeight);
    glUniform4f(gloc_.rect, r.x + offsetX_, r.y + offsetY_, r.w, r.h);
    glUniform1f(gloc_.radius, r.radius);
    glUniform4f(gloc_.tint, tint.r, tint.g, tint.b, tint.a);
    glUniform1f(gloc_.lod, blur);
    glUniform1f(gloc_.alpha, contentAlpha_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex_);
    glUniform1i(gloc_.tex, 0);
    // The vertex shader sizes its own quad from the shadow radius; glass has
    // none, so it must be told zero or the panel grows.
    GLint shadowVS = glGetUniformLocation(blurProgram_, "uShadow");
    if (shadowVS >= 0) glUniform1f(shadowVS, 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::beginFrame(int drawableWidth, int drawableHeight) {
    // Letterbox rather than stretch. A 16:9 panel is the normal case and this is
    // a no-op there; anything else keeps the design's proportions and puts the
    // slack in bars, which is what a console does.
    //
    // FORK (handheld): a screen wider than 16:9 widens the canvas to fit (see
    // kCanvasWidth in ui.h), to at most 2.4 to 1; anything else is as before.
    kCanvasWidth = kCanvasDesignWidth;
    if (platform::fillsWideScreens() && drawableWidth > 0 && drawableHeight > 0) {
        constexpr float kWidest = 2.4f;
        const float aspect = static_cast<float>(drawableWidth) / static_cast<float>(drawableHeight);
        kCanvasWidth = std::max(kCanvasDesignWidth, kCanvasHeight * std::min(aspect, kWidest));
    }
    float scale = std::min(static_cast<float>(drawableWidth) / kCanvasWidth,
                           static_cast<float>(drawableHeight) / kCanvasHeight);
    scale_ = scale;
    int vw = static_cast<int>(kCanvasWidth * scale);
    int vh = static_cast<int>(kCanvasHeight * scale);
    drawableW_ = drawableWidth;
    drawableH_ = drawableHeight;
    vx_ = (drawableWidth - vw) / 2;
    vy_ = (drawableHeight - vh) / 2;
    vw_ = vw;
    vh_ = vh;
    scenePresented_ = false;

    // The scene is drawn into a texture so panels can blur it. Sized to the
    // viewport rather than the drawable, since the letterbox bars have nothing
    // worth blurring in them.
    if (sceneW_ != vw || sceneH_ != vh) {
        if (sceneFBO_) glDeleteFramebuffers(1, &sceneFBO_);
        if (sceneTex_) glDeleteTextures(1, &sceneTex_);
        glGenTextures(1, &sceneTex_);
        glBindTexture(GL_TEXTURE_2D, sceneTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vw, vh, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Clamp, so a blur near an edge repeats the edge rather than wrapping
        // the opposite side of the screen into it.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &sceneFBO_);
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               sceneTex_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::fprintf(stderr, "[ui] scene target incomplete; glass disabled\n");
            glDeleteFramebuffers(1, &sceneFBO_);
            glDeleteTextures(1, &sceneTex_);
            sceneFBO_ = sceneTex_ = 0;
        }
        sceneW_ = vw;
        sceneH_ = vh;
    }
    if (sceneFBO_) {
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO_);
        ox_ = oy_ = 0;
        glViewport(0, 0, vw, vh);
    } else {
        ox_ = vx_ + shiftX_;
        oy_ = vy_ - shiftY_;
        glViewport(ox_, oy_, vw, vh);
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0, 0, 0, transparentBackground_ ? 0.0f : 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindVertexArray(vao_);
}

void Renderer::drawBackdrop(const Gradient& g, float alpha) {
    glUseProgram(backdropProgram_);
    glUniform1f(bloc_.alpha, alpha);
    glUniform3f(bloc_.top, g.top.r, g.top.g, g.top.b);
    glUniform3f(bloc_.mid, g.mid.r, g.mid.g, g.mid.b);
    glUniform3f(bloc_.bottom, g.bottom.r, g.bottom.g, g.bottom.b);
    glUniform1f(bloc_.midStop, g.midStop);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::draw(const Rect& r) {
    glUseProgram(program_);
    glUniform2f(loc_.canvas, kCanvasWidth, kCanvasHeight);
    glUniform4f(loc_.rect, r.x + offsetX_, r.y + offsetY_, r.w, r.h);
    glUniform1f(loc_.radius, r.radius);
    // THE SCREEN'S TRANSITION APPLIES TO SHAPES TOO. It used to reach only
    // pictures and text, so a screen arriving drew its panels at full strength
    // on the first frame and faded the words in on top of them — which is a
    // large part of why switching tabs looked snappy. MMagTech, 2026-09-24.
    const float k = contentAlpha_;
    glUniform4f(loc_.fill, r.fill.r, r.fill.g, r.fill.b, r.fill.a * k);
    // Flat unless the caller asked for a gradient, so nothing else changes.
    const Color bottom = r.gradient ? r.fillBottom : r.fill;
    glUniform4f(loc_.fillBottom, bottom.r, bottom.g, bottom.b, bottom.a * k);
    glUniform4f(loc_.edgeLight, r.edgeLight.r, r.edgeLight.g, r.edgeLight.b,
                r.edgeLight.a * k);
    glUniform1i(loc_.shape, static_cast<int>(r.shape));
    glUniform1f(loc_.border, r.border);
    glUniform4f(loc_.borderColor, r.borderColor.r, r.borderColor.g, r.borderColor.b,
                r.borderColor.a * k);
    glUniform2f(loc_.shadow, r.shadowBlur, r.shadowOffsetY);
    glUniform4f(loc_.shadowColor, r.shadowColor.r, r.shadowColor.g, r.shadowColor.b,
                r.shadowColor.a * k);
    // The vertex shader needs the blur too, to grow its own geometry enough
    // for the falloff to have somewhere to land.
    if (loc_.shadowVS >= 0) glUniform1f(loc_.shadowVS, r.shadowBlur);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

bool Renderer::saveFrame(const char* path, int w, int h) const {
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // A 24-bit BMP: no encoder, no dependency, and every tool reads it.
    const uint32_t rowBytes = static_cast<uint32_t>(w) * 3;
    const uint32_t padding = (4 - (rowBytes % 4)) % 4;
    const uint32_t imageBytes = (rowBytes + padding) * h;
    const uint32_t fileBytes = 54 + imageBytes;

    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "[ui] cannot write %s\n", path);
        return false;
    }
    uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    std::memcpy(header + 2, &fileBytes, 4);
    uint32_t offset = 54;
    std::memcpy(header + 10, &offset, 4);
    uint32_t dibSize = 40;
    std::memcpy(header + 14, &dibSize, 4);
    int32_t sw = w, sh = h;  // positive height: BMP rows run bottom-up, like GL
    std::memcpy(header + 18, &sw, 4);
    std::memcpy(header + 22, &sh, 4);
    uint16_t planes = 1, bpp = 24;
    std::memcpy(header + 26, &planes, 2);
    std::memcpy(header + 28, &bpp, 2);
    std::memcpy(header + 34, &imageBytes, 4);
    std::fwrite(header, 1, sizeof(header), f);

    std::vector<uint8_t> row(rowBytes + padding, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = pixels.data() + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            row[x * 3 + 0] = src[x * 4 + 2];  // BMP is BGR
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 0];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    std::fprintf(stderr, "[ui] wrote %s (%dx%d)\n", path, w, h);
    return true;
}

}  // namespace ui
