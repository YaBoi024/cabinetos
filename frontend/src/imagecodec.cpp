// The decoders and the encoder behind imagecodec.h: libpng, libjpeg, and
// SDL3_image for every other format, one at a time behind decodeOther's mutex.
// Moved out of image.cpp in the restructure (#398).

#include "imagecodec.h"

#include <SDL3_image/SDL_image.h>
#include <jpeglib.h>
#include <png.h>

#include <algorithm>
#include <csetjmp>
#include <cstring>
#include <mutex>

namespace ui {
namespace {

bool decodePNG(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, int& w, int& h) {
    png_image img{};
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&img, in.data(), in.size())) return false;
    img.format = PNG_FORMAT_RGBA;
    out.resize(PNG_IMAGE_SIZE(img));
    if (!png_image_finish_read(&img, nullptr, out.data(), 0, nullptr)) {
        png_image_free(&img);
        return false;
    }
    w = static_cast<int>(img.width);
    h = static_cast<int>(img.height);
    return true;
}

struct JpegError {
    jpeg_error_mgr mgr;
    jmp_buf escape;
};

void jpegFail(j_common_ptr cinfo) {
    // libjpeg's default error handler calls exit(). A malformed cover from a
    // server must not be able to take the console down with it.
    longjmp(reinterpret_cast<JpegError*>(cinfo->err)->escape, 1);
}

bool decodeJPEG(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, int& w, int& h) {
    jpeg_decompress_struct cinfo{};
    JpegError err{};
    cinfo.err = jpeg_std_error(&err.mgr);
    err.mgr.error_exit = jpegFail;
    if (setjmp(err.escape)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, in.data(), static_cast<unsigned long>(in.size()));
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    // Ask libjpeg for RGBA directly where it can; otherwise take RGB and widen.
    cinfo.out_color_space = JCS_EXT_RGBA;
    if (!jpeg_start_decompress(&cinfo)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    w = static_cast<int>(cinfo.output_width);
    h = static_cast<int>(cinfo.output_height);
    const int comps = cinfo.output_components;
    out.resize(static_cast<size_t>(w) * h * 4);

    std::vector<uint8_t> row(static_cast<size_t>(w) * comps);
    while (cinfo.output_scanline < cinfo.output_height) {
        uint8_t* ptr = row.data();
        const int y = static_cast<int>(cinfo.output_scanline);
        jpeg_read_scanlines(&cinfo, &ptr, 1);
        uint8_t* dst = out.data() + static_cast<size_t>(y) * w * 4;
        if (comps == 4) {
            std::memcpy(dst, row.data(), static_cast<size_t>(w) * 4);
        } else if (comps == 3) {
            for (int x = 0; x < w; ++x) {
                dst[x * 4 + 0] = row[x * 3 + 0];
                dst[x * 4 + 1] = row[x * 3 + 1];
                dst[x * 4 + 2] = row[x * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
        } else if (comps == 1) {
            for (int x = 0; x < w; ++x) {
                dst[x * 4 + 0] = dst[x * 4 + 1] = dst[x * 4 + 2] = row[x];
                dst[x * 4 + 3] = 255;
            }
        }
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return true;
}

}  // namespace

bool encodePNG(const std::vector<uint8_t>& rgba, unsigned width, unsigned height,
               std::vector<uint8_t>& out) {
    if (width == 0 || height == 0 || rgba.size() < static_cast<size_t>(width) * height * 4)
        return false;
    // RGB, not RGBA: a quarter smaller, and there is no transparency to keep.
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0, j = 0; j < rgb.size(); i += 4, j += 3) {
        rgb[j] = rgba[i];
        rgb[j + 1] = rgba[i + 1];
        rgb[j + 2] = rgba[i + 2];
    }
    png_image img{};
    img.version = PNG_IMAGE_VERSION;
    img.width = width;
    img.height = height;
    img.format = PNG_FORMAT_RGB;
    png_alloc_size_t size = 0;
    if (!png_image_write_to_memory(&img, nullptr, &size, 0, rgb.data(), 0, nullptr) ||
        size == 0)
        return false;
    out.resize(size);
    if (!png_image_write_to_memory(&img, out.data(), &size, 0, rgb.data(), 0, nullptr)) {
        out.clear();
        return false;
    }
    out.resize(size);
    return true;
}

void shrinkRGBA(std::vector<uint8_t>& rgba, unsigned& width, unsigned& height,
                unsigned maxWidth) {
    if (maxWidth == 0 || width <= maxWidth || height == 0 ||
        rgba.size() < static_cast<size_t>(width) * height * 4)
        return;
    const unsigned tw = maxWidth;
    const unsigned th = std::max(1u, static_cast<unsigned>(
                                         (static_cast<uint64_t>(height) * tw + width / 2) / width));
    std::vector<uint8_t> out(static_cast<size_t>(tw) * th * 4);
    for (unsigned y = 0; y < th; ++y) {
        const unsigned y0 = static_cast<unsigned>(static_cast<uint64_t>(y) * height / th);
        const unsigned y1 = std::max(y0 + 1, static_cast<unsigned>(
                                                 static_cast<uint64_t>(y + 1) * height / th));
        for (unsigned x = 0; x < tw; ++x) {
            const unsigned x0 = static_cast<unsigned>(static_cast<uint64_t>(x) * width / tw);
            const unsigned x1 = std::max(x0 + 1, static_cast<unsigned>(
                                                     static_cast<uint64_t>(x + 1) * width / tw));
            uint32_t sum[4] = {0, 0, 0, 0};
            for (unsigned sy = y0; sy < y1; ++sy) {
                const uint8_t* row = rgba.data() + (static_cast<size_t>(sy) * width + x0) * 4;
                for (unsigned sx = x0; sx < x1; ++sx, row += 4)
                    for (int c = 0; c < 4; ++c) sum[c] += row[c];
            }
            const uint32_t n = (y1 - y0) * (x1 - x0);
            uint8_t* dst = out.data() + (static_cast<size_t>(y) * tw + x) * 4;
            for (int c = 0; c < 4; ++c) dst[c] = static_cast<uint8_t>(sum[c] / n);
        }
    }
    rgba = std::move(out);
    width = tw;
    height = th;
}

// EVERY FORMAT RomM TAKES FOR A COVER (MMagTech, 2026-10-13: "any image format
// a cover can be according to RomM's standards"). RomM 5.4 accepts PNG, JPEG,
// WebP, GIF, AVIF, BMP and TIFF (`ALLOWED_IMAGE_EXTENSIONS`), keeps an
// uploaded cover in the format it came in, stores a large cover as the bytes
// the metadata site sent, and writes an animated cover's small copy as WebP
// even when it is named .png. Until now anything but PNG and JPEG decoded to
// nothing and the tile stayed blank, silently (#341).
//
// PNG and JPEG keep their own decoders above. SDL3_image reads the rest: GIF
// and BMP itself, WebP, AVIF and TIFF through libwebp, libavif and libtiff,
// which it loads the first time one is needed. An animated picture shows its
// first frame. One at a time: these are rare, and SDL3_image's first load of a
// decoder library is not made for four workers arriving at once.
static bool decodeOther(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, int& w, int& h) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    SDL_IOStream* io = SDL_IOFromConstMem(in.data(), in.size());
    if (!io) return false;
    SDL_Surface* src = IMG_Load_IO(io, true);
    if (!src) return false;
    SDL_Surface* rgba = SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(src);
    if (!rgba) return false;
    bool ok = rgba->w > 0 && rgba->h > 0 && SDL_LockSurface(rgba);
    if (ok) {
        w = rgba->w;
        h = rgba->h;
        const size_t row = static_cast<size_t>(w) * 4;
        out.resize(row * h);
        const auto* px = static_cast<const uint8_t*>(rgba->pixels);
        for (int y = 0; y < h; ++y)
            std::memcpy(out.data() + row * y, px + static_cast<size_t>(rgba->pitch) * y, row);
        SDL_UnlockSurface(rgba);
    }
    SDL_DestroySurface(rgba);
    return ok;
}

bool decodeImage(const std::vector<uint8_t>& enc, std::vector<uint8_t>& rgba, int& w,
                 int& h) {
    if (enc.size() < 12) return false;
    static const uint8_t kPNG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (std::memcmp(enc.data(), kPNG, 8) == 0) return decodePNG(enc, rgba, w, h);
    if (enc[0] == 0xFF && enc[1] == 0xD8) return decodeJPEG(enc, rgba, w, h);
    return decodeOther(enc, rgba, w, h);
}

}  // namespace ui
