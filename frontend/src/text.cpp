#include "text.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "platform.h"
#include "ui.h"

namespace ui {

namespace {
float designSize(TextStyle s) {
    switch (s) {
        case TextStyle::LargeTitle: return 76;
        case TextStyle::Title2: return 48;
        case TextStyle::Title3: return 38;
        case TextStyle::Callout: return 31;
        case TextStyle::Body: return 29;
        case TextStyle::Footnote: return 29;
        case TextStyle::Caption1: return 25;
        case TextStyle::ScreenTitle: return 40;
    }
    return 29;
}
}  // namespace

// FORK (phones): THE PHONE'S OWN TEXT SIZES. A machine that has a text size setting of its own
// (an iPhone's Text Size) says how many window pixels each style is (platform::textPixels), and
// the text is that size on the screen whichever way the phone is held. The design sizes are a
// television's, read from a sofa, and on a phone held upright they came out half again as big.
float styleSize(TextStyle s) {
    const float px = platform::textPixels(static_cast<int>(s));
    if (px > 0.0f && ui::gCanvasScale > 0.0f) return px / ui::gCanvasScale;
    return designSize(s);
}

Weight styleWeight(TextStyle s) {
    switch (s) {
        case TextStyle::LargeTitle:
        case TextStyle::Title2:
        case TextStyle::ScreenTitle: return Weight::Bold;
        case TextStyle::Title3: return Weight::SemiBold;
        default: return Weight::Regular;
    }
}

uint32_t nextCodepoint(std::string_view s, size_t& i) {
    if (i >= s.size()) return 0;
    auto byte = static_cast<unsigned char>(s[i]);
    int extra = 0;
    uint32_t cp = 0;
    if (byte < 0x80) {
        cp = byte;
    } else if ((byte & 0xE0) == 0xC0) {
        cp = byte & 0x1F;
        extra = 1;
    } else if ((byte & 0xF0) == 0xE0) {
        cp = byte & 0x0F;
        extra = 2;
    } else if ((byte & 0xF8) == 0xF0) {
        cp = byte & 0x07;
        extra = 3;
    } else {
        ++i;
        return 0xFFFD;
    }
    if (i + extra >= s.size()) {
        ++i;
        return 0xFFFD;
    }
    for (int k = 1; k <= extra; ++k) {
        auto cont = static_cast<unsigned char>(s[i + k]);
        if ((cont & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }
    i += extra + 1;
    return cp;
}

TextRenderer::~TextRenderer() { shutdown(); }

bool TextRenderer::init(const std::vector<std::string>& paths) {
    auto** lib = reinterpret_cast<FT_Library*>(&library_);
    if (FT_Init_FreeType(lib) != 0) {
        std::fprintf(stderr, "[text] FT_Init_FreeType failed\n");
        return false;
    }

    // The first three paths are Regular, SemiBold, Bold. Anything after them
    // is a fallback tried in order for glyphs the weight faces lack —
    // Noto Sans CJK, so a Japanese game title renders as its title rather than
    // as a row of boxes.
    for (size_t i = 0; i < paths.size(); ++i) {
        FT_Face face = nullptr;
        if (FT_New_Face(*lib, paths[i].c_str(), 0, &face) != 0) {
            std::fprintf(stderr, "[text] cannot open %s\n", paths[i].c_str());
            if (i == 0) return false;  // no primary face is fatal; a gap is not
            continue;
        }
        if (i < 3) {
            faces_.push_back(face);
        } else {
            fallbackFaces_.push_back(face);
        }
    }
    if (faces_.empty()) return false;

    // One atlas. 2048 holds the whole Latin range at every size this UI uses
    // several times over; CJK is sparse enough in practice (a library has tens
    // of Japanese titles, not thousands of distinct glyphs) that it fits too.
    atlasSize_ = 2048;
    glGenTextures(1, &atlas_);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    std::vector<uint8_t> empty(static_cast<size_t>(atlasSize_) * atlasSize_, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasSize_, atlasSize_, 0, GL_RED,
                 GL_UNSIGNED_BYTE, empty.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // One pixel of margin so a linear sample at a glyph's edge cannot pick up
    // its neighbour, which shows as faint specks beside letters.
    penX_ = penY_ = 1;
    rowHeight_ = 0;
    std::fprintf(stderr, "[text] %zu weights, %zu fallback faces, %dx%d atlas\n",
                 faces_.size(), fallbackFaces_.size(), atlasSize_, atlasSize_);
    return true;
}

void TextRenderer::shutdown() {
    for (void* f : faces_) FT_Done_Face(static_cast<FT_Face>(f));
    for (void* f : fallbackFaces_) FT_Done_Face(static_cast<FT_Face>(f));
    faces_.clear();
    fallbackFaces_.clear();
    if (atlas_) glDeleteTextures(1, &atlas_);
    atlas_ = 0;
    if (library_) FT_Done_FreeType(static_cast<FT_Library>(library_));
    library_ = nullptr;
    glyphs_.clear();
}

uint64_t TextRenderer::key(Weight w, int pixelSize, uint32_t cp) const {
    return (static_cast<uint64_t>(w) << 56) | (static_cast<uint64_t>(pixelSize) << 32) |
           cp;
}

bool TextRenderer::packInto(int w, int h, int& outX, int& outY) {
    if (penX_ + w + 1 > atlasSize_) {
        penX_ = 1;
        penY_ += rowHeight_ + 1;
        rowHeight_ = 0;
    }
    if (penY_ + h + 1 > atlasSize_) return false;
    outX = penX_;
    outY = penY_;
    penX_ += w + 1;
    rowHeight_ = std::max(rowHeight_, h);
    return true;
}

const TextRenderer::Glyph& TextRenderer::glyph(Weight w, int pixelSize, uint32_t cp) {
    const uint64_t k = key(w, pixelSize, cp);
    auto it = glyphs_.find(k);
    if (it != glyphs_.end()) return it->second;

    Glyph g;
    size_t weightIndex = std::min(static_cast<size_t>(w), faces_.size() - 1);

    // The requested weight first, then the fallbacks, then give up.
    std::vector<FT_Face> candidates;
    candidates.push_back(static_cast<FT_Face>(faces_[weightIndex]));
    for (void* f : fallbackFaces_) candidates.push_back(static_cast<FT_Face>(f));

    for (FT_Face face : candidates) {
        if (FT_Get_Char_Index(face, cp) == 0) continue;
        if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize)) != 0) continue;
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) continue;

        FT_GlyphSlot slot = face->glyph;
        g.w = static_cast<int>(slot->bitmap.width);
        g.h = static_cast<int>(slot->bitmap.rows);
        g.bearingX = slot->bitmap_left;
        g.bearingY = slot->bitmap_top;
        g.advance = static_cast<float>(slot->advance.x) / 64.0f;
        g.valid = true;

        if (g.w > 0 && g.h > 0) {
            int x = 0, y = 0;
            if (!packInto(g.w, g.h, x, y)) {
                std::fprintf(stderr, "[text] atlas full\n");
                g.valid = false;
                break;
            }
            glBindTexture(GL_TEXTURE_2D, atlas_);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, g.w, g.h, GL_RED, GL_UNSIGNED_BYTE,
                            slot->bitmap.buffer);
            g.u0 = static_cast<float>(x) / atlasSize_;
            g.v0 = static_cast<float>(y) / atlasSize_;
            g.u1 = static_cast<float>(x + g.w) / atlasSize_;
            g.v1 = static_cast<float>(y + g.h) / atlasSize_;
        }
        break;
    }

    auto [inserted, ok] = glyphs_.emplace(k, g);
    return inserted->second;
}

float TextRenderer::measure(std::string_view utf8, TextStyle style, float scale) {
    const int px = std::max(1, static_cast<int>(std::lround(styleSize(style) * scale)));
    const Weight w = styleWeight(style);
    float advance = 0;
    size_t i = 0;
    while (i < utf8.size()) {
        uint32_t cp = nextCodepoint(utf8, i);
        if (!cp) break;
        advance += glyph(w, px, cp).advance;
    }
    return advance / scale;  // back into design points
}

float TextRenderer::ascent(TextStyle style, float scale) {
    const int px = std::max(1, static_cast<int>(std::lround(styleSize(style) * scale)));
    auto face = static_cast<FT_Face>(faces_[std::min(static_cast<size_t>(styleWeight(style)),
                                                     faces_.size() - 1)]);
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));
    return (static_cast<float>(face->size->metrics.ascender) / 64.0f) / scale;
}

float TextRenderer::capHeight(TextStyle style, float scale) {
    const int px = std::max(1, static_cast<int>(std::lround(styleSize(style) * scale)));
    auto face = static_cast<FT_Face>(faces_[std::min(static_cast<size_t>(styleWeight(style)),
                                                     faces_.size() - 1)]);
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));
    // The font's own figure, from its OS/2 table; a font without one gets a
    // capital H measured instead.
    if (auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
        os2 && os2->version >= 2 && os2->sCapHeight > 0 && face->units_per_EM > 0)
        return (static_cast<float>(os2->sCapHeight) * px / face->units_per_EM) / scale;
    if (FT_Load_Char(face, 'H', FT_LOAD_NO_BITMAP) == 0)
        return (static_cast<float>(face->glyph->metrics.horiBearingY) / 64.0f) / scale;
    return ascent(style, scale) * 0.72f;
}

float TextRenderer::lineHeight(TextStyle style, float scale) {
    const int px = std::max(1, static_cast<int>(std::lround(styleSize(style) * scale)));
    auto face = static_cast<FT_Face>(faces_[std::min(static_cast<size_t>(styleWeight(style)),
                                                     faces_.size() - 1)]);
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));
    return (static_cast<float>(face->size->metrics.height) / 64.0f) / scale;
}

std::string TextRenderer::truncate(std::string_view utf8, TextStyle style, float scale,
                                   float maxWidth) {
    if (measure(utf8, style, scale) <= maxWidth) return std::string(utf8);

    const char* kEllipsis = "\xE2\x80\xA6";  // U+2026, one glyph, not three dots
    const float ellipsisWidth = measure(kEllipsis, style, scale);
    const int px = std::max(1, static_cast<int>(std::lround(styleSize(style) * scale)));
    const Weight w = styleWeight(style);

    float advance = 0;
    size_t i = 0, lastFit = 0;
    while (i < utf8.size()) {
        size_t start = i;
        uint32_t cp = nextCodepoint(utf8, i);
        if (!cp) break;
        advance += glyph(w, px, cp).advance / scale;
        if (advance + ellipsisWidth > maxWidth) {
            lastFit = start;
            break;
        }
        lastFit = i;
    }
    return std::string(utf8.substr(0, lastFit)) + kEllipsis;
}

void TextRenderer::draw(Renderer& r, std::string_view utf8, float x, float baselineY,
                        TextStyle style, const Color& color, float scale) {
    const int px = std::max(1, static_cast<int>(std::lround(styleSize(style) * scale)));
    const Weight w = styleWeight(style);

    // Snap the origin to a device pixel. A baseline landing on a half pixel
    // makes every glyph in the line slightly soft, which on a television reads
    // as cheap rather than as antialiasing.
    float penDevice = std::round(x * scale);
    const float baseDevice = std::round(baselineY * scale);

    size_t i = 0;
    while (i < utf8.size()) {
        uint32_t cp = nextCodepoint(utf8, i);
        if (!cp) break;
        const Glyph& g = glyph(w, px, cp);
        if (g.valid && g.w > 0 && g.h > 0) {
            // Device pixels back into design points, so the caller's layout
            // stays in one coordinate system throughout.
            const float gx = (penDevice + static_cast<float>(g.bearingX)) / scale;
            const float gy = (baseDevice - static_cast<float>(g.bearingY)) / scale;
            r.drawTextured(gx, gy, static_cast<float>(g.w) / scale,
                           static_cast<float>(g.h) / scale, atlas_, g.u0, g.v0, g.u1,
                           g.v1, color);
        }
        penDevice += g.advance;
    }
}

// --- Words that fit --------------------------------------------------------
//
// TextRenderer measures and truncates; it does not wrap, because nothing until
// now needed more than one line. A setup screen is mostly prose, so this is
// where wrapping lives.
//
// It breaks on spaces. A word longer than the whole column is handed to
// `hardWrap` below rather than being left to overflow.
std::vector<std::string> hardWrap(ui::TextRenderer& text, const std::string& s,
                                  ui::TextStyle style, float scale, float maxWidth);

std::vector<std::string> wrap(ui::TextRenderer& text, const std::string& s,
                              ui::TextStyle style, float scale, float maxWidth) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::string line;
    size_t at = 0;
    while (at <= s.size()) {
        const size_t sp = s.find(' ', at);
        const std::string word = s.substr(at, sp == std::string::npos ? std::string::npos
                                                                      : sp - at);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && text.measure(candidate, style, scale) > maxWidth) {
            out.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        // A SINGLE WORD WIDER THAN THE COLUMN, which is not a hypothetical
        // here: the pairing URL is one unbroken token and it is the longest
        // string this flow ever draws. Left alone it runs off the column and
        // under the panel beside it.
        if (text.measure(line, style, scale) > maxWidth) {
            for (const std::string& piece : hardWrap(text, line, style, scale, maxWidth))
                out.push_back(piece);
            line.clear();
        }
        if (sp == std::string::npos) break;
        at = sp + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

// Breaks anywhere, on codepoint boundaries.
//
// UTF-8 AWARE, and that is not pedantry: a break in the middle of a multi-byte
// character produces a glyph the font cannot resolve, and a console whose
// library is full of Japanese arcade titles will meet one.
std::vector<std::string> hardWrap(ui::TextRenderer& text, const std::string& s,
                                  ui::TextStyle style, float scale, float maxWidth) {
    std::vector<std::string> out;
    std::string line;
    size_t i = 0;
    while (i < s.size()) {
        const size_t start = i;
        ui::nextCodepoint(s, i);                 // advances i past one character
        const std::string ch = s.substr(start, i - start);
        if (!line.empty() && text.measure(line + ch, style, scale) > maxWidth) {
            out.push_back(line);
            line.clear();
        }
        line += ch;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

}  // namespace ui
