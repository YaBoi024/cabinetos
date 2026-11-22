// The game's picture: the video callback, the software upload, the frame
// digest the determinism checks read, and the snapshot a save state carries.
// Moved out of core.cpp in the restructure (#398); Core::uploadFrame (core.cpp)
// takes the PlayStation 2, Vulkan and GLES cases and hands the rest to
// uploadSoftwareFrame here.

#include "core.h"
#include "core_state.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "libretro.h"
#include "imagecodec.h"
#include "ps2.h"
#include "vkhost.h"

namespace cab {
namespace corestate {

// Says once, for a turned picture, what the core hands over and what ends up
// on the screen.
//
// IT EXISTS BECAUSE THE TWO SIZES THE CORE REPORTS DISAGREE AND NEITHER IS
// WRONG. MAME 2003-Plus declares 224x256 in its av_info for Arkanoid — that is
// the picture as SHOWN, already turned — and then hands back a 256x224 buffer
// every frame, which is the board's own sideways output. The layout has to use
// the second one and gets the first one for free in the launch log, so an
// honest line that prints both is the difference between reading this in a
// minute and inferring it off a photograph.
void announceRotation(unsigned width, unsigned height) {
    // Not a function-local static: this process plays one game after another,
    // and a static would say it once in the life of the console rather than
    // once per game. Cleared beside gRotation in loadGame.
    if (gRotationAnnounced || gRotation == 0) return;
    gRotationAnnounced = true;
    const bool quarter = (gRotation & 1u) != 0u;
    std::fprintf(stderr,
                 "[core] the core hands back %ux%u and asks for %u degrees "
                 "counter-clockwise; shown as %ux%u\n",
                 width, height, gRotation * 90, quarter ? height : width,
                 quarter ? width : height);
}

void videoRefresh(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        // Not a buffer at all: the core has already drawn this frame into our
        // texture, and this call is only telling us how much of it is picture.
        // There is nothing to copy and nothing to upload.
        //
        // Said out loud once, because "the core is running but the screen is
        // black" has two completely different causes — the core has not
        // presented anything, or it has and the picture is not reaching the
        // screen — and they lead to opposite places. Without this line the
        // two are indistinguishable from outside.
        if (!gHWFrame) {
            std::fprintf(stderr, "[core] first hardware frame: %ux%u into the "
                                 "%ux%u target\n", width, height, gHWTargetW,
                         gHWTargetH);
        }
        gHWFrame = true;
        gFrameW = width;
        gFrameH = height;
        gFrameDirty = true;
        // retro_vulkan_image carries no size, so this is the only place the
        // frontend learns how much of the core's image is picture. See
        // vkhost.h, setFrameSize.
        if (gHWVulkan) vk::setFrameSize(width, height);
        announceRotation(width, height);
        return;
    }
    if (!data) return;  // "same picture as last time"
    gHWFrame = false;
    gFrame.resize(pitch * height);
    std::memcpy(gFrame.data(), data, pitch * height);
    gFrameW = width;
    gFrameH = height;
    gFramePitch = pitch;
    gFrameDirty = true;
    announceRotation(width, height);
}

}  // namespace corestate

using namespace corestate;

// uploadFrame's software path: the core's own buffer, as videoRefresh copied
// it, into texture_. Core::uploadFrame (core.cpp) takes the PlayStation 2,
// Vulkan and GLES cases first.
bool Core::uploadSoftwareFrame() {
    if (!gFrameDirty || gFrameW == 0 || gFrameH == 0) return texture_ != 0;
    gFrameDirty = false;

    if (!texture_) {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        // Nearest, deliberately. A Game Boy picture is 160x144 on a 4K panel
        // and every pixel is a deliberate choice by someone in 1989; smoothing
        // it is a decision the shader layer should make on purpose, not one
        // the upload makes by accident.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // A BRAND NEW TEXTURE HAS NO STORAGE, WHATEVER THE LAST GAME'S SIZE WAS.
        //
        // THIS IS THE SECOND-GAME BLACK SCREEN — found 2026-09-21, reported as
        // "black with audio" against 3DO, TurboGrafx and Dreamcast over several
        // days and recorded in docs/NEXT-SESSION.md as not reproducible.
        //
        // It is entirely reproducible once you know the rule: PLAY ONE GAME,
        // LEAVE IT, AND PLAY ANOTHER OF THE SAME PIXEL DIMENSIONS.
        //
        //   unload() deletes this texture and sets texture_ to 0. It does NOT
        //   reset frameWidth_/frameHeight_ — the only place those are cleared
        //   is the PlayStation 2 branch of loadGame, which no libretro core
        //   takes. So the next game arrives with the PREVIOUS game's size still
        //   recorded, `sizeChanged` comes out false, and the upload below calls
        //   glTexSubImage2D on a texture that has just been generated and never
        //   allocated. That is GL_INVALID_OPERATION: nothing is uploaded, the
        //   texture samples as black, and the core carries on producing perfect
        //   frames and perfectly good audio into a picture nobody can see.
        //
        // Which is why it looked like a per-game fault and was not: Air Zonk
        // then Devil's Crush are both 256x240 and the second is black; Air Zonk
        // then a SNES game is 256x240 then 256x224, `sizeChanged` is true, and
        // it works. The first game after a restart always works, because these
        // start at zero.
        //
        // Fixed here rather than in unload(), because this is the only place
        // that knows the texture has no storage — and it stays correct however
        // the texture came to be missing.
        frameWidth_ = 0;
        frameHeight_ = 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const bool sizeChanged = (gFrameW != frameWidth_ || gFrameH != frameHeight_);
    frameWidth_ = gFrameW;
    frameHeight_ = gFrameH;

    if (gPixelFormat == RETRO_PIXEL_FORMAT_RGB565) {
        // Uploaded as-is: libretro's RGB565 and GL_UNSIGNED_SHORT_5_6_5 pack
        // the channels the same way, so there is no conversion to do. The
        // pitch may exceed the width, which is what UNPACK_ROW_LENGTH is for.
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(gFramePitch / 2));
        if (sizeChanged) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, gFrameW, gFrameH, 0, GL_RGB,
                         GL_UNSIGNED_SHORT_5_6_5, gFrame.data());
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gFrameW, gFrameH, GL_RGB,
                            GL_UNSIGNED_SHORT_5_6_5, gFrame.data());
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else {
        // 0RGB1555 and XRGB8888 have no direct GLES equivalent, so they are
        // widened on the CPU. Both are rare in this core set and both are
        // small pictures; if a big one ever lands here, this becomes a shader.
        static std::vector<uint8_t> rgba;
        rgba.resize(static_cast<size_t>(gFrameW) * gFrameH * 4);
        for (unsigned y = 0; y < gFrameH; ++y) {
            const uint8_t* src = gFrame.data() + static_cast<size_t>(y) * gFramePitch;
            uint8_t* dst = rgba.data() + static_cast<size_t>(y) * gFrameW * 4;
            for (unsigned x = 0; x < gFrameW; ++x) {
                if (gPixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
                    const uint32_t p = reinterpret_cast<const uint32_t*>(src)[x];
                    dst[x * 4 + 0] = (p >> 16) & 0xFF;
                    dst[x * 4 + 1] = (p >> 8) & 0xFF;
                    dst[x * 4 + 2] = p & 0xFF;
                } else {
                    const uint16_t p = reinterpret_cast<const uint16_t*>(src)[x];
                    dst[x * 4 + 0] = ((p >> 10) & 0x1F) << 3;
                    dst[x * 4 + 1] = ((p >> 5) & 0x1F) << 3;
                    dst[x * 4 + 2] = (p & 0x1F) << 3;
                }
                dst[x * 4 + 3] = 255;
            }
        }
        if (sizeChanged) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gFrameW, gFrameH, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, rgba.data());
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gFrameW, gFrameH, GL_RGBA,
                            GL_UNSIGNED_BYTE, rgba.data());
        }
    }
    return true;
}

uint64_t Core::frameDigest() const {
    if (gHWFrame) {
        // A readback, and the only one anywhere in this file. It exists for
        // the determinism checks, which run under --state-test and never in
        // the player: there is no CPU-side copy of a hardware frame to hash,
        // so the choice is to read it back here or to have no video half of
        // the comparison at all for exactly the cores whose parity is least
        // certain.
        if (!gHWFBO || gFrameW == 0 || gFrameH == 0) return 0;
        std::vector<uint8_t> px(static_cast<size_t>(gFrameW) * gFrameH * 4);
        GLint bound = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        glBindFramebuffer(GL_FRAMEBUFFER, gHWFBO);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, static_cast<GLsizei>(gFrameW), static_cast<GLsizei>(gFrameH),
                     GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(bound));
        uint64_t hh = 1469598103934665603ull;
        for (uint8_t b : px) {
            hh ^= b;
            hh *= 1099511628211ull;
        }
        return hh;
    }
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (unsigned y = 0; y < gFrameH; ++y) {
        const uint8_t* row = gFrame.data() + static_cast<size_t>(y) * gFramePitch;
        const size_t bytes = static_cast<size_t>(gFrameW) *
                             (gPixelFormat == RETRO_PIXEL_FORMAT_XRGB8888 ? 4 : 2);
        // The visible pixels only. Padding between rows is whatever the core
        // left there and is not part of the picture.
        for (size_t i = 0; i < bytes; ++i) {
            h ^= row[i];
            h *= 1099511628211ull;
        }
    }
    return h;
}

bool Core::snapshot(std::vector<uint8_t>& rgba, unsigned& width, unsigned& height,
                    unsigned maxWidth) const {
    // PLAYSTATION 2 (#130): its frame is not the core's, it is the texture
    // uploadFrame fills from PCSX2 (ps2::takeFrame), RGBA, top row first, at
    // the game's own resolution. So a screenshot works in every game where
    // the shortcut button does. Not a save state's picture: PS2 has none.
    const bool ps2Frame = isPs2();
    // WITH ITS OWN WINDOW (#226) nothing is copied off the GPU each frame, so
    // the picture on screen is read back once, now, for this.
    if (ps2Frame && ps2::windowed()) {
        const uint32_t* pixels = nullptr;
        unsigned pw = 0, ph = 0;
        if (!ps2::captureFrame(1000) || !ps2::takeFrame(&pixels, pw, ph) || pw == 0 || ph == 0)
            return false;
        rgba.assign(reinterpret_cast<const uint8_t*>(pixels),
                    reinterpret_cast<const uint8_t*>(pixels) + static_cast<size_t>(pw) * ph * 4);
        width = pw;
        height = ph;
        ui::shrinkRGBA(rgba, width, height, maxWidth);
        return true;
    }
    unsigned w = ps2Frame ? frameWidth_ : gFrameW;
    unsigned h = ps2Frame ? frameHeight_ : gFrameH;
    if (w == 0 || h == 0) return false;
    if (ps2Frame && !texture_) return false;
    // THE SIZE IT IS READ AT: whole, or no wider than `maxWidth` as it is
    // shown, so a quarter-turned game is measured by its height (#346, the
    // save's picture).
    const unsigned r = gRotation & 3u;
    const unsigned shownW = (r & 1u) ? h : w;
    unsigned rw = w, rh = h;
    if (maxWidth > 0 && shownW > maxWidth) {
        rw = std::max(1u, static_cast<unsigned>(
                              (static_cast<uint64_t>(w) * maxWidth + shownW / 2) / shownW));
        rh = std::max(1u, static_cast<unsigned>(
                              (static_cast<uint64_t>(h) * maxWidth + shownW / 2) / shownW));
    }
    std::vector<uint8_t> px;

    // Read a GL framebuffer's bottom-left fw x fh into px, top row first when
    // `bottomUp` says row zero is the picture's bottom.
    auto readFBO = [&](GLuint fbo, bool bottomUp, unsigned fw, unsigned fh) {
        px.assign(static_cast<size_t>(fw) * fh * 4, 0);
        GLint bound = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, static_cast<GLsizei>(fw), static_cast<GLsizei>(fh), GL_RGBA,
                     GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(bound));
        if (!bottomUp) return;
        const size_t row = static_cast<size_t>(fw) * 4;
        std::vector<uint8_t> tmp(row);
        for (unsigned y = 0; y < fh / 2; ++y) {
            uint8_t* a = px.data() + y * row;
            uint8_t* b = px.data() + (fh - 1 - y) * row;
            std::memcpy(tmp.data(), a, row);
            std::memcpy(a, b, row);
            std::memcpy(b, tmp.data(), row);
        }
    };

    // MADE SMALL ON THE GRAPHICS CHIP, THEN READ, so the game does not wait
    // on a whole frame crossing to the processor (a 4K frame read whole took
    // 8 to 23 ms on the reference console, 2026-11-19). Halved by blits while
    // it is at least twice the size wanted, each step an even average of
    // four pixels, and the last step to the size itself. The blits keep the
    // picture's way up, so `bottomUp` is the source's.
    auto readGL = [&](GLuint src, bool bottomUp) -> bool {
        if (rw == w && rh == h) {
            readFBO(src, bottomUp, w, h);
            return true;
        }
        GLint readBound = 0, drawBound = 0, texBound = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readBound);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawBound);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texBound);
        const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        GLuint curFbo = src, curTex = 0;
        unsigned cw = w, ch = h;
        bool ok = true;
        while (ok && (cw != rw || ch != rh)) {
            const bool halve = cw / 2 >= rw && ch / 2 >= rh;
            const unsigned nw = halve ? cw / 2 : rw, nh = halve ? ch / 2 : rh;
            GLuint tex = 0, fbo = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, static_cast<GLsizei>(nw),
                           static_cast<GLsizei>(nh));
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex,
                                   0);
            ok = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            if (ok) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, curFbo);
                glBlitFramebuffer(0, 0, static_cast<GLint>(cw), static_cast<GLint>(ch), 0, 0,
                                  static_cast<GLint>(nw), static_cast<GLint>(nh),
                                  GL_COLOR_BUFFER_BIT, GL_LINEAR);
            }
            if (curTex) {
                glDeleteFramebuffers(1, &curFbo);
                glDeleteTextures(1, &curTex);
            }
            curFbo = fbo;
            curTex = tex;
            cw = nw;
            ch = nh;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readBound));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawBound));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texBound));
        if (scissor) glEnable(GL_SCISSOR_TEST);
        if (ok) readFBO(curFbo, bottomUp, rw, rh);
        if (curTex) {
            glDeleteFramebuffers(1, &curFbo);
            glDeleteTextures(1, &curTex);
        }
        return ok;
    };

    if (ps2Frame || gHWVulkan) {
        // The exported image is an ordinary GL texture (vkhost), top row
        // first, the picture in its top-left corner. present() waits on its
        // fence, so what is there is complete. PS2's texture is read the
        // same way.
        const GLuint tex = ps2Frame ? texture_ : vk::texture();
        if (!tex) return false;
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        GLint bound = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(bound));
        if (ok) ok = readGL(fbo, /*bottomUp=*/false);
        glDeleteFramebuffers(1, &fbo);
        if (!ok) return false;
        w = rw;
        h = rh;
    } else if (gHWFrame) {
        // glReadPixels hands back row zero = the framebuffer's y = 0, which
        // is the picture's bottom exactly when the core says so.
        if (!gHWFBO) return false;
        if (!readGL(gHWFBO, gHW.bottom_left_origin)) return false;
        w = rw;
        h = rh;
    } else {
        if (gFrame.size() < gFramePitch * h) return false;
        px.assign(static_cast<size_t>(w) * h * 4, 0);
        for (unsigned y = 0; y < h; ++y) {
            const uint8_t* src = gFrame.data() + static_cast<size_t>(y) * gFramePitch;
            uint8_t* dst = px.data() + static_cast<size_t>(y) * w * 4;
            for (unsigned x = 0; x < w; ++x, dst += 4) {
                uint8_t r8, g8, b8;
                if (gPixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
                    uint32_t p;
                    std::memcpy(&p, src + x * 4, 4);
                    r8 = (p >> 16) & 0xFF; g8 = (p >> 8) & 0xFF; b8 = p & 0xFF;
                } else {
                    uint16_t p;
                    std::memcpy(&p, src + x * 2, 2);
                    if (gPixelFormat == RETRO_PIXEL_FORMAT_RGB565) {
                        r8 = ((p >> 11) & 0x1F) << 3; g8 = ((p >> 5) & 0x3F) << 2; b8 = (p & 0x1F) << 3;
                    } else {
                        r8 = ((p >> 10) & 0x1F) << 3; g8 = ((p >> 5) & 0x1F) << 3; b8 = (p & 0x1F) << 3;
                    }
                }
                dst[0] = r8; dst[1] = g8; dst[2] = b8; dst[3] = 0xFF;
            }
        }
        // A frame drawn on the processor is small already for almost every
        // core; one that is not is made small here.
        if (rw != w) ui::shrinkRGBA(px, w, h, rw);
    }
    // Opaque whatever the core left in alpha: a PNG with holes in it shows
    // the page behind.
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 0xFF;

    // THE QUARTER TURN, as the player's vertex shader turns it (ui.cpp, uRot:
    // the destination's corner reads the source at (1 - y, x) for one step
    // anticlockwise), so a vertical arcade game's picture stands upright.
    if (r == 0) {
        rgba = std::move(px);
        width = w;
        height = h;
        return true;
    }
    const unsigned ow = (r & 1u) ? h : w, oh = (r & 1u) ? w : h;
    rgba.assign(static_cast<size_t>(ow) * oh * 4, 0);
    for (unsigned dy = 0; dy < oh; ++dy) {
        for (unsigned dx = 0; dx < ow; ++dx) {
            unsigned sx, sy;
            if (r == 1) { sx = w - 1 - dy; sy = dx; }
            else if (r == 2) { sx = w - 1 - dx; sy = h - 1 - dy; }
            else { sx = dy; sy = h - 1 - dx; }
            std::memcpy(rgba.data() + (static_cast<size_t>(dy) * ow + dx) * 4,
                        px.data() + (static_cast<size_t>(sy) * w + sx) * 4, 4);
        }
    }
    width = ow;
    height = oh;
    return true;
}

}  // namespace cab
