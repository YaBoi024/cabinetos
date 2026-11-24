#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "imagecodec.h"
#include "ui.h"

namespace ui {
namespace {

// The 350ms the design system gives asynchronous content. Cover art arriving
// over a network is the common case and snapping it in looks like a fault.
constexpr float kFadeDuration = 0.350f;

}  // namespace

std::vector<uint8_t> ImageCache::readFile(const std::string& path) {
    std::vector<uint8_t> data;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        data.resize(static_cast<size_t>(n));
        if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
    }
    std::fclose(f);
    return data;
}

ImageCache::~ImageCache() { shutdown(); }

bool ImageCache::init(size_t budgetBytes, int workerCount, Loader loader) {
    budgetBytes_ = budgetBytes;
    loader_ = loader ? std::move(loader) : Loader(&ImageCache::readFile);
    stopping_ = false;
    for (int i = 0; i < std::max(1, workerCount); ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
    std::fprintf(stderr, "[image] %d workers, %zu MB budget\n",
                 static_cast<int>(workers_.size()), budgetBytes_ / (1024 * 1024));
    return true;
}

void ImageCache::shutdown() {
    if (workers_.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
    for (auto& [key, e] : entries_) {
        if (e.image.texture) glDeleteTextures(1, &e.image.texture);
    }
    entries_.clear();
    residentBytes_ = 0;
}

void ImageCache::workerLoop() {
    for (;;) {
        std::string key;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !requests_.empty(); });
            if (stopping_) return;
            key = std::move(requests_.front());
            requests_.pop_front();
        }

        Decoded out;
        out.key = key;
        std::vector<uint8_t> encoded = loader_(key);
        if (!encoded.empty()) {
            out.ok = decodeImage(encoded, out.rgba, out.width, out.height);
            // SAID, with the file's first bytes, which name its format (#376).
            // A server path is shown up to its query; anything else is a file
            // on the console or a fixed service, and only its kind is shown.
            if (!out.ok) {
                char head[3 * 12 + 1] = {0};
                for (size_t i = 0; i < std::min<size_t>(12, encoded.size()); ++i)
                    std::snprintf(head + 3 * i, 4, "%02x ", encoded[i]);
                const std::string shown = key.rfind("/assets/", 0) == 0
                                              ? key.substr(0, key.find('?'))
                                              : std::string("a picture not from the server");
                std::fprintf(stderr, "[image] could not read %s (%zu bytes, starting %s)\n",
                             shown.c_str(), encoded.size(), head);
            }
        }
        // THE LARGE COVER WHEN THE SMALL ONE WILL NOT DO (#376): missing on the
        // server, or there but not a picture. RomM keeps both side by side
        // (cover/small.<ext>, cover/big.<ext>) and its own pages draw the
        // large one, so a small one can be broken while the large one is fine:
        // blank on Home, a cover on the game's page. Same server, same folder.
        if (!out.ok) {
            const size_t at = key.find("/cover/small.");
            if (at != std::string::npos) {
                std::string big = key;
                big.replace(at, std::string("/cover/small.").size(), "/cover/big.");
                std::vector<uint8_t> large = loader_(big);
                if (!large.empty() && decodeImage(large, out.rgba, out.width, out.height)) {
                    out.ok = true;
                    std::fprintf(stderr, "[covers] %s: the small cover would not do; the large one is used\n",
                                 key.substr(0, key.find('?')).c_str());
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_.push_back(std::move(out));
        }
    }
}

const Image& ImageCache::get(const std::string& key) {
    Entry& e = entries_[key];
    e.lastUsedFrame = frame_;
    if (!e.requested) {
        e.requested = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requests_.push_back(key);
        }
        cv_.notify_one();
    }
    return e.image;
}

const Image* ImageCache::held(const std::string& key) {
    const auto it = entries_.find(key);
    if (it == entries_.end() || !it->second.image.ready) return nullptr;
    it->second.lastUsedFrame = frame_;
    return &it->second.image;
}

int ImageCache::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(requests_.size() + finished_.size());
}

void ImageCache::forgetFailed() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.image.failed && !it->second.image.texture) it = entries_.erase(it);
        else ++it;
    }
}

int ImageCache::settlingCount() const {
    int n = 0;
    for (const auto& [key, e] : entries_)
        if (e.requested && !e.image.failed && (!e.image.ready || e.image.fade < 1.0f)) ++n;
    return n;
}

void ImageCache::pump(float dt) {
    ++frame_;

    std::deque<Decoded> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(finished_);
    }

    for (auto& d : ready) {
        auto it = entries_.find(d.key);
        if (it == entries_.end()) continue;  // evicted while decoding
        Entry& e = it->second;
        if (!d.ok || d.width <= 0 || d.height <= 0) {
            e.image.failed = true;
            continue;
        }
        glGenTextures(1, &e.image.texture);
        glBindTexture(GL_TEXTURE_2D, e.image.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, d.width, d.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, d.rgba.data());
        // Mipmaps serve two purposes: a cover drawn smaller than its source
        // stops shimmering, and the blurred echo an odd-aspect cover sits on is
        // just a high mip level sampled back up, which costs nothing and needs
        // no blur pass.
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        e.image.width = d.width;
        e.image.height = d.height;
        e.image.ready = true;
        e.image.fade = 0.0f;
        // 4/3 for the mip chain, which is what it actually costs.
        e.bytes = static_cast<size_t>(d.width) * d.height * 4 * 4 / 3;
        residentBytes_ += e.bytes;
    }

    for (auto& [key, e] : entries_) {
        if (e.image.ready && e.image.fade < 1.0f) {
            e.image.fade = std::min(1.0f, e.image.fade + dt / kFadeDuration);
        }
    }

    evictToBudget();
}

void ImageCache::evictToBudget() {
    if (residentBytes_ <= budgetBytes_) return;

    std::vector<std::pair<uint64_t, std::string>> candidates;
    for (auto& [key, e] : entries_) {
        // Never evict something drawn this frame or the last: it would be
        // requested again immediately and flash.
        if (!e.image.ready || e.lastUsedFrame + 1 >= frame_) continue;
        candidates.emplace_back(e.lastUsedFrame, key);
    }
    std::sort(candidates.begin(), candidates.end());

    for (auto& [used, key] : candidates) {
        if (residentBytes_ <= budgetBytes_) break;
        auto it = entries_.find(key);
        if (it == entries_.end()) continue;
        if (it->second.image.texture) glDeleteTextures(1, &it->second.image.texture);
        residentBytes_ -= it->second.bytes;
        entries_.erase(it);
    }
}

void drawImageTexture(Renderer& r, GLuint texture, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1, bool opaque, int rotation) {
    r.drawTextured(x, y, w, h, texture, u0, v0, u1, v1, Color{1, 1, 1, 1}, false, 0.0f, 0,
                   0, 0, 0, 0, opaque, rotation);
}

void drawImage(Renderer& r, const Image& img, float x, float y, float w, float h, Fit fit,
               float alpha, float cornerRadius) {
    if (!img.ready || w <= 0 || h <= 0) return;
    const float a = alpha * img.fade;
    if (a <= 0.0f) return;

    const float boxAspect = w / h;
    const float imgAspect = img.aspect();

    if (fit == Fit::Fill && img.oddAspect()) {
        // Fit on a blurred echo of itself rather than crop: the whole cover
        // stays visible, the cell keeps its shape, and the bars are the art's
        // own colours instead of dead space. Straight from the reference
        // implementation, whose own comment calls it the same idea as the
        // launch screen's ambient backdrop.
        //
        // The echo is a high mip level sampled back up and scaled past the
        // edges, which is a box blur for free rather than a blur pass.
        const float over = 1.3f;
        const float ew = w * over, eh = h * over;
        r.drawTextured(x - (ew - w) * 0.5f, y - (eh - h) * 0.5f, ew, eh, img.texture, 0, 0,
                       1, 1, Color{1, 1, 1, a}, false, 4.0f, x, y, w, h, cornerRadius);
        // A scrim, so the echo reads as atmosphere rather than as a second,
        // competing picture.
        r.draw(Rect{x, y, w, h, cornerRadius, Color::black(0.18f * a)});

        float fw = w, fh = h;
        if (imgAspect > boxAspect) {
            fh = w / imgAspect;
        } else {
            fw = h * imgAspect;
        }
        r.drawTextured(x + (w - fw) * 0.5f, y + (h - fh) * 0.5f, fw, fh, img.texture, 0, 0,
                       1, 1, Color{1, 1, 1, a}, false, 0.0f, x, y, w, h, cornerRadius);
        return;
    }

    if (fit == Fit::Contain) {
        float fw = w, fh = h;
        if (imgAspect > boxAspect) {
            fh = w / imgAspect;
        } else {
            fw = h * imgAspect;
        }
        r.drawTextured(x + (w - fw) * 0.5f, y + (h - fh) * 0.5f, fw, fh, img.texture, 0, 0,
                       1, 1, Color{1, 1, 1, a}, false, 0.0f, x, y, w, h, cornerRadius);
        return;
    }

    // Fill: crop by moving the texture coordinates rather than the geometry, so
    // the quad is exactly the cell and nothing overhangs it. The reference
    // implementation had to work around overhang because its layout system
    // sized to the enlarged image; drawing directly, the problem does not
    // arise.
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (imgAspect > boxAspect) {
        const float keep = boxAspect / imgAspect;
        u0 = (1.0f - keep) * 0.5f;
        u1 = 1.0f - u0;
    } else {
        const float keep = imgAspect / boxAspect;
        v0 = (1.0f - keep) * 0.5f;
        v1 = 1.0f - v0;
    }
    r.drawTextured(x, y, w, h, img.texture, u0, v0, u1, v1, Color{1, 1, 1, a}, false, 0.0f,
                   x, y, w, h, cornerRadius);
}

}  // namespace ui
