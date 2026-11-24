// Cover art for the CabinetOS frontend.
//
// Three things this has to get right, and all three come from the reference
// implementation having got them wrong first.
//
// IT MUST NEVER BLOCK THE FRAME LOOP. A shelf scrolls past dozens of covers and
// each one is a JPEG decode; doing that on the thread that drives the core is
// how a console stutters. Decoding happens on workers, the GL upload happens on
// the frame thread because GL is not thread-safe, and a cover that is not ready
// simply is not drawn yet.
//
// IT MUST HAVE A BUDGET. A library is thousands of games. Textures are evicted
// least-recently-used once the budget is passed, and anything drawn this frame
// is never evicted.
//
// WHERE THE BYTES COME FROM IS NOT ITS BUSINESS. Today they are files on disk;
// in Phase 4 they are authenticated RomM requests. That is one std::function,
// so the cache never learns what a server is.

#pragma once

#include <GLES3/gl3.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ui {

class Renderer;
struct Color;

// How a picture is fitted into the box it is given.
enum class Fit {
    // Cover the box, cropping the overflow. What a cover of the right shape
    // wants.
    Fill,
    // Show all of it, letterboxing. What a screenshot or an odd shape wants.
    Contain,
};

struct Image {
    GLuint texture = 0;
    int width = 0, height = 0;
    bool ready = false;
    bool failed = false;
    // 0..1, driven by the 350ms fade the design system gives asynchronous
    // content. Art arriving over a network is the common case and it must
    // never snap in.
    float fade = 0.0f;

    float aspect() const {
        return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    }

    // Covers more than this far from 3:4 are fitted over a blurred echo of
    // themselves instead of being cropped, so the whole cover stays visible and
    // the letterbox bars are the art's own colours rather than dead space.
    // The threshold is the reference implementation's own.
    bool oddAspect() const { return ready && std::abs(aspect() - 0.75f) > 0.06f; }
};

class ImageCache {
public:
    // Returns the encoded bytes for a key, or empty on failure. Called on a
    // worker thread, so it must be safe to call concurrently.
    using Loader = std::function<std::vector<uint8_t>(const std::string& key)>;

    ~ImageCache();

    bool init(size_t budgetBytes, int workerCount, Loader loader);
    void shutdown();

    // Never blocks. The first call for a key queues a load and returns a
    // not-ready image; later calls return the same one, ready once it is.
    const Image& get(const std::string& key);
    // The picture for a key if it is here and drawable, and nothing otherwise:
    // never asks for it. For the cover a changed one replaces, which is only
    // worth drawing while it is still in hand.
    const Image* held(const std::string& key);

    // Once per frame, on the thread that owns the GL context: uploads whatever
    // the workers finished, advances fades, and evicts down to the budget.
    void pump(float dt);

    size_t bytesResident() const { return residentBytes_; }
    int pendingCount() const;
    // Images asked for that are not on screen at full strength yet: queued,
    // being decoded, waiting to upload, or still fading in. Unlike
    // pendingCount it sees the ones a worker is holding. Frame thread only.
    int settlingCount() const;

    // Forgets every image that failed to load, so the next get() asks again.
    // For the server coming back (#88): a cover that failed offline is
    // otherwise failed for the rest of the session. Frame thread only.
    void forgetFailed();

    // Reads a file. The default loader, and what Phase 4 replaces.
    static std::vector<uint8_t> readFile(const std::string& path);

private:
    struct Entry {
        Image image;
        uint64_t lastUsedFrame = 0;
        size_t bytes = 0;
        bool requested = false;
    };
    struct Decoded {
        std::string key;
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;
        bool ok = false;
    };

    void workerLoop();
    void evictToBudget();

    Loader loader_;
    std::unordered_map<std::string, Entry> entries_;

    std::vector<std::thread> workers_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> requests_;
    std::deque<Decoded> finished_;
    std::atomic<bool> stopping_{false};

    size_t budgetBytes_ = 0;
    size_t residentBytes_ = 0;
    uint64_t frame_ = 0;
};

// Draws a picture into a box, honouring the fit and the fade. The odd-aspect
// case draws the blurred echo underneath by itself; callers do not have to know
// the rule, which is the point.
void drawImage(Renderer& r, const Image& img, float x, float y, float w, float h,
               Fit fit, float alpha, float cornerRadius = 0.0f);

// A raw texture straight to the screen, with none of the cache's machinery.
// The running core's frame goes through here: it is not cached, not faded in,
// and its size is decided by the core rather than by a layout.
//
// The texture coordinates are not decoration. A hardware-rendered core draws
// into a target sized to its declared maximum and uses a corner of it, bottom
// row first, so "the whole texture, the right way up" is true of a software
// core and false of Flycast. Core::frameUV answers both cases; this just
// takes the answer.
//
// `opaque` throws the texture's alpha away and draws the colour at full
// strength. That is what a GAME FRAME wants: the alpha in an emulated
// machine's framebuffer belongs to the machine, and PPSSPP hands back frames
// whose alpha is nearly zero — which drew Lumines at 1.5% brightness and
// looked exactly like a core rendering black.
//
// `rotation` is quarter turns anticlockwise, and it is the core's own answer
// to RETRO_ENVIRONMENT_SET_ROTATION. A vertical arcade board had its monitor
// bolted into the cabinet sideways, so it renders a sideways picture and asks
// for it to be turned; ignoring the ask plays every TATE shmup on its side.
// It composes with the texture coordinates rather than replacing them, so a
// hardware-rendered vertical board gets its corner of the target, its vertical
// flip AND its turn.
void drawImageTexture(Renderer& r, GLuint texture, float x, float y, float w, float h,
                      float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1,
                      bool opaque = false, int rotation = 0);

}  // namespace ui
