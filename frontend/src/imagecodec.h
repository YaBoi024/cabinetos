// Pictures to pixels and back: decodeImage (PNG, JPEG, and through SDL3_image
// every other format RomM accepts for a cover), encodePNG (a save state's
// picture) and shrinkRGBA (a save's picture, made small). The image cache's
// workers, the screen looks (screenfx.cpp), the states' pictures, the save's
// picture (savepicture.h) and the disc rig call them. Moved out of image.h in
// the restructure (#398).

#pragma once

#include <cstdint>
#include <vector>

namespace ui {

// Decodes PNG or JPEG to straight RGBA. Format is detected from the magic
// bytes, never from a file extension: a server hands you a content type and a
// body, not a filename.
bool decodeImage(const std::vector<uint8_t>& encoded, std::vector<uint8_t>& rgbaOut,
                 int& widthOut, int& heightOut);

// Straight RGBA to a PNG, for the picture a save state carries (#99). The
// alpha is dropped: a game frame is opaque. False only if libpng refuses.
bool encodePNG(const std::vector<uint8_t>& rgba, unsigned width, unsigned height,
               std::vector<uint8_t>& pngOut);

// Straight RGBA made no wider than `maxWidth`, its shape kept, each new
// pixel the average of the ones it covers (#346, the save's picture). Left
// as it is when it is no wider already. `width` and `height` become the new
// size.
void shrinkRGBA(std::vector<uint8_t>& rgba, unsigned& width, unsigned& height,
                unsigned maxWidth);

}  // namespace ui
