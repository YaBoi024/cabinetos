#include "coretests.h"

#include <SDL3/SDL.h>
#include <sys/resource.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core.h"

std::optional<int> coreTest(cab::Core& core, const CoreTestOptions& o) {
    const bool audioProbe = o.audioProbe;
    const bool speedTest = o.speedTest;
    const bool stateTest = o.stateTest;

    if (audioProbe) {
        // Which call silences it? Three identical runs that differ only in
        // what is done at the 240-frame mark. Narrowing it to one entry
        // point is the difference between "save states are weird" and a
        // report someone can act on.
        cab::Core& c = core;
        const double step = 1.0 / c.avInfo().fps;
        auto peakOver = [&c, step](int frames) {
            long peak = 0;
            for (int i = 0; i < frames; ++i) {
                c.runFor(step);
                for (int16_t v : c.drainAudio())
                    peak = std::max<long>(peak, std::abs(static_cast<int>(v)));
            }
            return peak;
        };
        if (SDL_getenv("CABINETOS_TRACE")) {
            // Just watch. When does it go quiet, and does it come back?
            std::fprintf(stderr, "[probe] peak per 60 frames (1 second each):\n");
            for (int block = 0; block < 34; ++block) {
                const long p = peakOver(60);
                std::fprintf(stderr, "  %4ds  %6ld%s\n", block + 1, p,
                             p == 0 ? "   <- silent" : "");
            }
            return 0;
        }
        const char* what = SDL_getenv("CABINETOS_PROBE");
        const std::string mode = what ? what : "none";
        const long before = peakOver(240);
        if (mode == "size") {
            c.stateSize();
        } else if (mode == "save") {
            std::vector<uint8_t> st;
            c.saveState(st);
        }
        const long after = peakOver(180);
        std::fprintf(stderr, "[probe] %-5s  before %6ld   after %6ld   %s\n",
                     mode.c_str(), before, after,
                     after == 0 && before > 0 ? "SILENCED" : "ok");
        return 0;
    }

    if (speedTest) {
        // HOW MUCH ROOM A CORE HAS, NOT HOW FAST IT PLAYS. Paced, every
        // core plays at 60 on the A9 and the number says nothing; run back
        // to back, the cost of one frame is what a slower machine has to
        // find inside its 16.7 ms. Written for #147 (recompiler against
        // interpreter). Warm-up first, so loading and the intro's first
        // shader compiles are not counted; then the mean, the 95th
        // percentile and the worst single frame, because a slow frame is
        // a stutter even when the mean is fine.
        const int kWarm = SDL_getenv("CABINETOS_WARM")
                              ? SDL_atoi(SDL_getenv("CABINETOS_WARM"))
                              : 600;
        const int kTimed = SDL_getenv("CABINETOS_TIMED")
                               ? SDL_atoi(SDL_getenv("CABINETOS_TIMED"))
                               : 1800;
        const double step = 1.0 / core.avInfo().fps;
        for (int i = 0; i < kWarm; ++i) {
            core.runFor(step);
            core.drainAudio();
        }
        // CABINETOS_PACED=1 runs at the game's own rate, idle between
        // frames as in play. That idle is where the CPU clocks down, so it
        // is the configuration a power setting has to be judged in; back
        // to back, the CPU never rests and the setting hardly shows.
        const bool paced = SDL_getenv("CABINETOS_PACED") != nullptr;
        const uint64_t frameNs = static_cast<uint64_t>(1e9 / core.avInfo().fps);
        // CPU TIME FOR THE WHOLE PROCESS, every thread, per game frame.
        // The per-call timing below misses a core that emulates on a
        // thread of its own (PPSSPP, Flycast, melonDS threaded): there
        // retro_run only collects a finished frame. This does not.
        auto cpuMs = [] {
            rusage u{};
            getrusage(RUSAGE_SELF, &u);
            return (u.ru_utime.tv_sec + u.ru_stime.tv_sec) * 1e3 +
                   (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e3;
        };
        std::vector<double> ms;
        ms.reserve(kTimed);
        const double cpuStart = cpuMs();
        const uint64_t start = SDL_GetTicksNS();
        int frames = 0;
        while (frames < kTimed) {
            if (paced) {
                const uint64_t due = start + static_cast<uint64_t>(frames) * frameNs;
                const uint64_t now = SDL_GetTicksNS();
                if (due > now) SDL_DelayPrecise(due - now);
            }
            const uint64_t t0 = SDL_GetTicksNS();
            const int ran = core.runFor(step);
            const double took = (SDL_GetTicksNS() - t0) / 1e6;
            core.drainAudio();
            if (ran <= 0) continue;
            for (int r = 0; r < ran; ++r) ms.push_back(took / ran);
            frames += ran;
        }
        const double wall = (SDL_GetTicksNS() - start) / 1e9;
        const double cpu = cpuMs() - cpuStart;
        std::sort(ms.begin(), ms.end());
        double sum = 0;
        for (double v : ms) sum += v;
        std::fprintf(stderr,
                     "[speed] %d frames after %d warm-up: mean %.2f ms, 95th %.2f ms, "
                     "worst %.2f ms, %.0f frames a second (the game wants %.1f)%s; "
                     "CPU %.2f ms a frame on all threads\n",
                     frames, kWarm, sum / ms.size(), ms[ms.size() * 95 / 100],
                     ms.back(), frames / wall, core.avInfo().fps,
                     paced ? ", paced" : "", cpu / frames);
        // Out without unloading. PPSSPP's teardown hangs in this headless
        // path (the product's quit is a different route and works, #87),
        // and a measurement has nothing to save.
        std::fflush(stderr);
        std::_Exit(0);
    }

    if (stateTest) {
        // A save state is only worth anything if what comes back is the
        // same machine. "The core accepted the bytes" is not that: it is
        // exactly what a subtly wrong state also looks like.
        //
        // So: run to a point, snapshot, run on and remember what happened,
        // restore, run the same distance again, and compare. Identical
        // output means the restore put every bit back. This is the
        // reference implementation's own test — "a serialize, run, restore,
        // run round trip produces identical video and audio streams".
        // Deep enough in that the picture is genuinely moving. The first
        // version of this test warmed up 240 frames and compared video on
        // Dr. Mario's TITLE SCREEN — a static image, which matches itself
        // no matter what the machine is doing. It reported PASS and proved
        // nothing. A determinism test has to run somewhere that would
        // actually diverge, and here that is the attract demo.
        const int kWarm = SDL_getenv("CABINETOS_WARM")
                              ? SDL_atoi(SDL_getenv("CABINETOS_WARM"))
                              : 1500;
        const int kRunOn = 300;  // five seconds for a divergence to show
        const double step = 1.0 / core.avInfo().fps;

        // Is the game making any sound, and is the picture actually
        // changing? Everything else here is meaningless if either answer
        // is no, which is exactly the trap the first version fell into.
        long warmPeak = 0;
        for (int i = 0; i < kWarm; ++i) {
            core.runFor(step);
            for (int16_t v : core.drainAudio())
                warmPeak = std::max<long>(warmPeak, std::abs(static_cast<int>(v)));
        }
        std::fprintf(stderr, "[state] peak amplitude during the %d warm-up frames: %ld\n",
                     kWarm, warmPeak);
        // Prove the picture moves before trusting any video comparison.
        const uint64_t vA = core.frameDigest();
        for (int i = 0; i < 30; ++i) core.runFor(step);
        const uint64_t vB = core.frameDigest();
        core.drainAudio();
        std::fprintf(stderr, "[state] picture over 30 frames: %s\n",
                     vA == vB ? "STATIC - this test would prove nothing here"
                              : "moving - a video comparison is meaningful");

        std::vector<uint8_t> state;
        const size_t stateBytes = core.stateSize();
        if (!core.saveState(state)) {
            std::fprintf(stderr, "[state] core produced no state\n");
            return 1;
        }
        std::fprintf(stderr, "[state] %zu bytes at frame %d\n", state.size(), kWarm);
        // A STATE FROM ANOTHER BUILD OR ANOTHER CPU ENGINE (#147). With
        // CABINETOS_STATE_OUT this run's state is kept in a file; with
        // CABINETOS_STATE_IN runs 2 and 3 restore THAT state instead of
        // their own, so an interpreter's state can be loaded by the
        // recompiler. Run 1 against run 2 then compares two engines and
        // may differ without anything being wrong; what has to hold is
        // that the state is accepted and the game plays on from it.
        if (const char* out = SDL_getenv("CABINETOS_STATE_OUT")) {
            const bool ok = SDL_SaveFile(out, state.data(), state.size());
            std::fprintf(stderr, "[state] kept in %s: %s\n", out, ok ? "yes" : SDL_GetError());
        }
        if (const char* in = SDL_getenv("CABINETOS_STATE_IN")) {
            size_t n = 0;
            void* bytes = SDL_LoadFile(in, &n);
            if (!bytes) {
                std::fprintf(stderr, "[state] cannot read %s: %s\n", in, SDL_GetError());
                return 1;
            }
            state.assign(static_cast<uint8_t*>(bytes), static_cast<uint8_t*>(bytes) + n);
            SDL_free(bytes);
            std::fprintf(stderr, "[state] runs 2 and 3 restore %s (%zu bytes)\n", in, n);
        }

        // Video and audio digested SEPARATELY. If they are mixed and the
        // result differs, all you know is "something diverged" — which is
        // the least useful possible answer about a save system.
        struct Digest {
            uint64_t video = 1469598103934665603ull;
            uint64_t audio = 1469598103934665603ull;
            size_t audioBytes = 0;
            int firstVideoDiff = -1;
            int firstAudioDiff = -1;
            std::vector<uint64_t> perFrameVideo, perFrameAudio;
            // The whole audio stream, concatenated. A per-frame hash is
            // sensitive to WHERE the resampler happens to split its output,
            // which is not the same question as whether the samples are the
            // same samples.
            std::vector<int16_t> audioStream;
        };
        auto runAndDigest = [&core, kRunOn, step]() {
            // Drain first. Audio accumulates until somebody takes it, so a
            // run that starts with the warm-up's leftovers in the buffer
            // hashes them and the next run does not. That is a difference
            // in the TEST, and reading it as a difference in the STATE is
            // exactly the wrong conclusion to reach about a save system.
            core.drainAudio();

            Digest d;
            auto mix = [](uint64_t& h, const void* p, size_t n) {
                const auto* b = static_cast<const uint8_t*>(p);
                for (size_t i = 0; i < n; ++i) {
                    h ^= b[i];
                    h *= 1099511628211ull;
                }
            };
            for (int i = 0; i < kRunOn; ++i) {
                core.runFor(step);
                const uint64_t v = core.frameDigest();
                mix(d.video, &v, sizeof(v));
                d.perFrameVideo.push_back(v);

                const std::vector<int16_t>& audio = core.drainAudio();
                uint64_t a = 1469598103934665603ull;
                mix(a, audio.data(), audio.size() * sizeof(int16_t));
                mix(d.audio, &a, sizeof(a));
                d.perFrameAudio.push_back(a);
                d.audioBytes += audio.size() * sizeof(int16_t);
                d.audioStream.insert(d.audioStream.end(), audio.begin(), audio.end());
            }
            return d;
        };

        const Digest first = runAndDigest();
        {
            long p1 = 0;
            for (int16_t v : first.audioStream)
                p1 = std::max<long>(p1, std::abs(static_cast<int>(v)));
            std::fprintf(stderr, "[state] run1 (after save) peak: %ld\n", p1);
        }
        if (!core.loadState(state)) {
            std::fprintf(stderr, "[state] the core REJECTED its own state\n");
            return 1;
        }
        const Digest second = runAndDigest();

        // A third run, restored the same way as the second. This is the
        // control the first two lack: if runs 2 and 3 agree with each other
        // but not with run 1, then restoring is perfectly deterministic and
        // what differs is the PATH taken to get there, not the state. That
        // is a very different finding from "save states are broken", and
        // without this run the two are indistinguishable.
        if (!core.loadState(state)) {
            std::fprintf(stderr, "[state] second restore rejected\n");
            return 1;
        }
        const Digest third = runAndDigest();
        std::fprintf(stderr, "[state] run2 vs run3: video %s, audio %s\n",
                     second.video == third.video ? "MATCH" : "differs",
                     second.audioStream == third.audioStream ? "MATCH" : "differs");

        int videoDiff = -1, audioDiff = -1;
        for (int i = 0; i < kRunOn; ++i) {
            if (videoDiff < 0 && first.perFrameVideo[i] != second.perFrameVideo[i])
                videoDiff = i;
            if (audioDiff < 0 && first.perFrameAudio[i] != second.perFrameAudio[i])
                audioDiff = i;
        }

        std::fprintf(stderr, "[state] video  %016llx vs %016llx  %s\n",
                     static_cast<unsigned long long>(first.video),
                     static_cast<unsigned long long>(second.video),
                     first.video == second.video ? "MATCH"
                                                 : "differs");
        std::fprintf(stderr, "[state] audio  %016llx vs %016llx  %s\n",
                     static_cast<unsigned long long>(first.audio),
                     static_cast<unsigned long long>(second.audio),
                     first.audio == second.audio ? "MATCH" : "differs");
        if (videoDiff >= 0)
            std::fprintf(stderr, "[state] first differing video frame: %d of %d\n",
                         videoDiff, kRunOn);
        std::fprintf(stderr, "[state] audio bytes %zu vs %zu\n", first.audioBytes,
                     second.audioBytes);

        // The audio verdict, with its own confidence attached.
        //
        // Three wrong theories were chased here before the controls killed
        // them: leftover audio in the buffer, the resampler's filter
        // history, and retro_serialize having a side effect. The actual
        // answer was that the GAME IS SILENT — Dr. Mario untouched plays a
        // ding at one second and nothing for the next half minute. A test
        // that reports "audio differs" without first checking there IS any
        // audio is reporting its own blind spot.
        long peak1 = 0, peak2 = 0;
        for (int16_t v : first.audioStream)
            peak1 = std::max<long>(peak1, std::abs(static_cast<int>(v)));
        for (int16_t v : second.audioStream)
            peak2 = std::max<long>(peak2, std::abs(static_cast<int>(v)));

        if (peak1 == 0 && peak2 == 0) {
            std::fprintf(stderr,
                         "[state] audio: INCONCLUSIVE - the game is silent here, so "
                         "this proves nothing either way\n");
        } else if (first.audioStream == second.audioStream) {
            std::fprintf(stderr, "[state] audio: MATCH, sample for sample (%zu samples)\n",
                         first.audioStream.size());
        } else if (peak1 == 0) {
            // Sound on the restored path and none on the continuous one, in
            // a passage the game plays silent, is a transient at the moment
            // of restore rather than a difference in the machine. RetroArch
            // mutes briefly after a state load for exactly this reason.
            std::fprintf(stderr,
                         "[state] audio: a transient on restore (peak %ld against "
                         "silence) - a click at the seam, not lost state. Worth "
                         "muting briefly after a load.\n",
                         peak2);
        } else {
            std::fprintf(stderr,
                         "[state] audio: differs with both paths audible (peaks %ld "
                         "and %ld) - investigate\n",
                         peak1, peak2);
        }

        if (first.video == second.video) {
            std::fprintf(stderr,
                         "[state] PASS - the emulated machine restored exactly "
                         "(%zu byte state)\n",
                         stateBytes);
        } else {
            std::fprintf(stderr, "[state] FAIL - the picture diverged after restore\n");
            return 1;
        }

        // Save RAM, which is a different mechanism entirely and the one
        // people assume is safe once the game says it saved.
        std::vector<uint8_t> sram;
        if (core.saveRAM(sram)) {
            std::fprintf(stderr, "[state] save RAM: %zu bytes\n", sram.size());
        } else {
            std::fprintf(stderr,
                         "[state] save RAM: none exposed (cartridge has no battery, "
                         "or the core writes its own file)\n");
        }
        return 0;
    }
    return std::nullopt;
}
