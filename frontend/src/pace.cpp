// The game frame: the core stepped (frozen, redrawn, rewound or run),
// paced to the screen or the speaker, the frame uploaded, the sound
// queued and its rate bent, and the [audio] and [pace] lines (#221), whose
// counts start again at each launch (App::pace) (#398, restructure batch f).

#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "ps2.h"
#include "rumble.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// Every frame while a built-in game runs, after the pads are read (play.cpp).
void App::runGameFrame(cab::Core& core, float dt, bool rewinding) {
    const bool frozen = overlayOpen || stateHold || stateHoldWaiting;
    core.setPaused(frozen);
    // Set when a paused frame was run only to redraw it; its sound is
    // dropped below, as rewind's is.
    bool redrew = false;
    // RUMBLE (#149): what the game asked for, to each player's pad,
    // and nothing while the game is not being played. Rewind replays
    // frames backwards, which is not play either.
    if (core.isPs2()) {
        ps2::pollRumble();
        ps2::report();
    }
    rumble::update(!frozen && !rewinding);

    if (frozen && pauseRedrawFrames > 0 && !core.isPs2()) {
        // A SYSTEM ROW WAS CHANGED IN THE PAUSE MENU (#73): run the
        // game on by a frame or two, silently, so the frozen picture
        // behind the menu is redrawn with the new option. A core
        // reads the change at the start of its next frame and draws
        // it in that frame or the one after. MMagTech on the TV: the
        // colour did not change behind the menu.
        --pauseRedrawFrames;
        core.runFor(1.0 / std::max(core.avInfo().fps, 1.0));
        redrew = true;
    } else if (frozen) {
        // Nothing to step. The last frame stays uploaded, so the
        // menu sits over a frozen picture rather than a black one.
    } else if (shotMode) {
        core.runFor(1.0 / std::max(core.avInfo().fps, 1.0));
    } else if (rewinding) {
        // ONE SNAPSHOT BACK EVERY kRewindStep, held still between, so
        // it can be stopped where wanted. A frame is run from each so
        // there is a picture (its sound is dropped below). With none
        // left it holds on the oldest.
        rewindStepWait -= dt;
        if (rewindStepWait <= 0.0f) {
            rewindStepWait = kRewindStep;
            if (rewindKeep.takeNewest(rewindRaw)) {
                core.loadState(rewindRaw);
                core.runFor(1.0 / std::max(core.avInfo().fps, 1.0));
            }
            rewindLastFrame = core.framesRun();
        }
    } else {
        // THE SPEAKER SETS THE PACE, for every system — 2026-09-27.
        // More than kAudioSyncMs of sound already waiting and the core
        // is not due this frame. This is RetroArch's audio sync (its
        // audio write blocks when the buffer is full, default latency
        // 64 ms), and it replaced a day of per-core fixes: a core that
        // runs fast (Flycast's free-running thread, PPSSPP's double
        // speed) fills the queue and is held; one that
        // delivers sound in bursts (N64) waits instead of having the
        // top of each burst thrown away as crackle; a stall cannot
        // pile up delay. Nothing is discarded to keep time.
        //
        // THE GUARD, which Cabinet paid for (its issue #6): a gate on
        // the speaker draining freezes the game whenever the speaker
        // stops taking sound. Held for more than 250 ms with the queue
        // not going down, the gate lets go and the clock paces the
        // core as before, and says so once.
        static constexpr double kAudioSyncMs = 64.0;
        static uint64_t drainSeenNs = 0;
        static double lastWaitingMs = 0.0;
        static bool saidStuck = false;
        bool holdForSpeaker = false;
        if (audioStream && !core.isPs2() && core.speed() <= 1.0) {
            const uint64_t nowNs = SDL_GetTicksNS();
            const double rate = std::max(core.avInfo().sampleRate, 1.0);
            const double waitingMs =
                SDL_GetAudioStreamQueued(audioStream) / (4.0 * rate) * 1000.0;
            if (waitingMs <= kAudioSyncMs || waitingMs < lastWaitingMs ||
                drainSeenNs == 0) {
                drainSeenNs = nowNs;
                saidStuck = false;
            }
            holdForSpeaker = waitingMs > kAudioSyncMs &&
                             nowNs - drainSeenNs < 250'000'000ull;
            if (waitingMs > kAudioSyncMs && !holdForSpeaker && !saidStuck) {
                std::fprintf(stderr,
                             "[audio] the speaker is not taking sound; pacing by "
                             "the clock\n");
                saidStuck = true;
            }
            lastWaitingMs = waitingMs;
        }
        // One game frame per refresh when locked (see paceEvery),
        // counted in refreshes, not by the clock, so a wobble in the
        // clock never runs two frames or none. A refresh the console
        // was too late for (dt of two refreshes) is run on the next,
        // so the game keeps time as it did on the clock; the rate
        // control below is left with only the rates to take up.
        // Fast forward is paced by the clock, which is what makes it
        // fast.
        const bool locked = paceEvery > 0 && core.speed() <= 1.0;
        int due = 1;
        if (locked) {
            paceCount += std::clamp(
                static_cast<int>(std::lround(dt * paceScreenHz)), 1, 4);
            due = std::min(paceCount / paceEvery, 3);
            paceCount = due > 0 ? paceCount - due * paceEvery : paceCount;
            if (paceCount >= paceEvery) paceCount %= paceEvery;
        }
        // A SNAPSHOT EVERY HALF SECOND OF PLAY, counted in the game's
        // own frames so fast forward keeps them half a game-second
        // apart. Taken here, compressed on rewind's worker. Not while a
        // disc change has the lid open (#162): the history is dropped
        // when it closes anyway.
        const uint64_t every = static_cast<uint64_t>(
            std::max(1.0, std::round(core.avInfo().fps * 0.5)));
        auto snapshot = [&] {
            if (rewindAvailable != 1 || session.discInserting ||
                core.framesRun() < rewindLastFrame + every)
                return false;
            rewindLastFrame = core.framesRun();
            const uint64_t t0 = SDL_GetTicksNS();
            if (core.saveState(rewindRaw) && !rewindRaw.empty()) {
                const double ms = (SDL_GetTicksNS() - t0) / 1e6;
                if (ms > rewindWorstMs) rewindWorstMs = ms;
                rewindKeep.offer(rewindRaw);
            }
            return true;
        };
        int ran = 0;
        if (!holdForSpeaker && due > 0) {
            // RetroAchievements checks each frame run here, and only
            // here: this is play (achievements.h).
            core.setAfterFrame(ra::frame);
            // A CORE WHOSE SAVE RUNS A FRAME (N64, #254) has its
            // snapshot taken as this refresh's first frame, not after
            // it: after, the save hid the run's picture and made a
            // frame of sound more, which the speaker held a frame for.
            // The clock's budget loses that frame too (Core::saveState).
            int runs = due;
            if (core.saveRunsFrame() && snapshot()) {
                ran = 1;
                --runs;
            }
            if (!locked) ran += core.runFor(dt);
            else for (int i = 0; i < runs; ++i) ran += core.runFrame();
            core.setAfterFrame(nullptr);
        }
        // HOW SMOOTH IT WAS (#221): a frame shown with no new game frame
        // behind it is a repeat, and every run past the first in one
        // frame is a game frame nobody saw. Said with the [audio] line.
        if (due > 0) ++pace.shown;
        if (due == 0) ran = 1;   // not a repeat: the screen is faster than the game
        if (ran == 0) ++pace.repeated;
        if (ran == 0 && holdForSpeaker) ++pace.held;
        if (ran > 1) pace.skipped += static_cast<uint64_t>(ran - 1);
        pace.dtSum += dt;
        // After the runs for every other core, and for that one until
        // its first save shows what it does.
        if (!core.saveRunsFrame()) snapshot();
    }
    // Frozen, rewinding or PlayStation 2 (which checks on PCSX2's
    // own thread): the session is kept alive without reading memory.
    if (frozen || rewinding || core.isPs2()) ra::idle();
    ra::sampleTick();
    core.uploadFrame();

    if (audioStream) {
        const std::vector<int16_t>& samples = core.drainAudio();
        // Fast forward's sound is discarded, not played fast: four
        // times the samples would only pile up behind the picture.
        // Rewind's too: it is a frame's sound played forwards.
        // A SAFETY NET, NOT THE PACING — 2026-09-27. The speaker sets
        // the pace above, and a batch bigger than the gate is QUEUED,
        // not trimmed: the gate then waits it out, which is what
        // RetroArch's blocking audio write does. Flycast's threaded
        // rendering (its default, "highly recommended") hands over a
        // loading burst of ~380 ms that drains within seconds; trimmed
        // at 250 ms it was audible skipping. So this only catches a
        // second of sound in one call, which is broken, not bursty. It
        // used to be the pacing, at 64 ms, and threw away the top of
        // every N64 burst as crackle. The history: When a game stalls, the speaker plays silence;
        // when it catches up, the sound it owed queued up behind and
        // never drained, so every stall made the sound a little later
        // for the rest of the session. Crazy Taxi 2 sat 200 ms behind
        // its picture this way even with Flycast held to the clock.
        // 64 ms is RetroArch's default audio latency
        // (DEFAULT_OUT_LATENCY), which is also what keeps RetroArch
        // from piling up. What does not fit is dropped: one small jump
        // in the sound instead of a delay that lasts. Measured against
        // this stream only, so it is the same on any machine.
        static constexpr double kMaxWaitingMs = 1000.0;
        if (!samples.empty() && core.speed() <= 1.0 && !rewinding && !redrew) {
            const double bytesPerMs =
                4.0 * std::max(core.avInfo().sampleRate, 1.0) / 1000.0;
            const int room = static_cast<int>(kMaxWaitingMs * bytesPerMs) -
                             SDL_GetAudioStreamQueued(audioStream);
            const int want = static_cast<int>(samples.size() * sizeof(int16_t));
            const int take = std::max(0, std::min(want, room)) & ~3;
            if (take > 0) SDL_PutAudioStreamData(audioStream, samples.data(), take);
            pace.droppedBytes += static_cast<uint64_t>(want - take);
        }
        // THE RATE CONTROL, while locked (#221). The lock sets the
        // sound's speed from the two rates; this bends it up to 0.5%
        // either way to hold the queue near kPaceTargetMs, which takes
        // up what the rates got wrong (the screen's reported rate, the
        // sound card's own clock, a missed refresh). RetroArch's
        // dynamic rate control, at its default (audio_rate_control_
        // delta 0.005), which is below what an ear can hear.
        // Measured after the frame's sound went in, so always at the
        // same point of the queue's rise and fall.
        if (paceEvery > 0 && !frozen && !rewinding && core.speed() <= 1.0) {
            static constexpr double kPaceTargetMs = 40.0;
            static constexpr double kRateControl = 0.005;
            const double rate = std::max(core.avInfo().sampleRate, 1.0);
            const double queuedMs =
                SDL_GetAudioStreamQueued(audioStream) / (4.0 * rate) * 1000.0;
            const double off =
                std::clamp((queuedMs - kPaceTargetMs) / kPaceTargetMs, -1.0, 1.0);
            paceRatio = static_cast<float>(paceBaseRatio * (1.0 + kRateControl * off));
            SDL_SetAudioStreamFrequencyRatio(audioStream, paceRatio);
        }
        // Said at most every ten seconds, so a system that trips it
        // constantly shows up in the log rather than hiding in it.
        if (pace.droppedBytes > 0 && SDL_GetTicksNS() >= pace.droppedLogAt) {
            const double rate = std::max(core.avInfo().sampleRate, 1.0);
            std::fprintf(stderr, "[audio] dropped %.0f ms of sound to keep it in time\n",
                         pace.droppedBytes / (4.0 * rate) * 1000.0);
            pace.droppedBytes = 0;
            pace.droppedLogAt = SDL_GetTicksNS() + 10'000'000'000ull;
        }
        // HOW MUCH SOUND IS WAITING, every ten seconds of play, beside
        // any sound the safety net below had to drop. Together they
        // say whether a system keeps time. Added for Dreamcast on
        // 2026-09-27.
        const uint64_t nowNs = SDL_GetTicksNS();
        if (nowNs >= pace.audioLogAt) {
            if (pace.audioLogAt != 0) {
                const double rate = std::max(core.avInfo().sampleRate, 1.0);
                const double queuedMs =
                    SDL_GetAudioStreamQueued(audioStream) / (4.0 * rate) * 1000.0;
                // Only the queue: since the speaker paces every
                // core, the core's output against runFor's clock
                // stops meaning anything while it is being held.
                std::fprintf(stderr, "[audio] %.0f ms waiting to play\n", queuedMs);
                // MISSED is refreshes the console itself was too
                // late for: the screen showed the last picture again,
                // whatever the game did. Only known with a screen rate.
                const long long missed =
                    paceScreenHz > 0.0
                        ? std::max(0LL, std::llround(pace.dtSum * paceScreenHz) -
                                            static_cast<long long>(pace.shown) *
                                                std::max(paceEvery, 1))
                        : 0;
                // SOUND PER GAME FRAME against what the core's own rate
                // and frame rate promise. A core above 0 makes more
                // sound than real time and gets held by the speaker
                // (Dreamcast, PROJECT.md #221; N64's +3.4% was rewind's
                // snapshots, each an uncounted frame, #254). The core's counts
                // start at its load, as these do, so the first line's
                // figure is the game's first ten seconds.
                const uint64_t a = core.audioFramesTotal(), f = core.framesRun();
                double perFrame = 0.0;
                if (f > pace.lastFrames && a >= pace.lastAudio && core.avInfo().fps > 0.0)
                    perFrame = (static_cast<double>(a - pace.lastAudio) /
                                (f - pace.lastFrames)) /
                                   (rate / core.avInfo().fps) -
                               1.0;
                pace.lastAudio = a;
                pace.lastFrames = f;
                if (pace.shown > 0)
                    std::fprintf(stderr,
                                 "[pace] %llu frames shown at %.3f Hz: %llu repeated "
                                 "(%llu held for the speaker), %llu game frames "
                                 "skipped, %lld refreshes missed; sound %+.2f%%, "
                                 "sound per game frame %+.2f%%\n",
                                 static_cast<unsigned long long>(pace.shown),
                                 pace.shown / std::max(pace.dtSum, 1e-9),
                                 static_cast<unsigned long long>(pace.repeated),
                                 static_cast<unsigned long long>(pace.held),
                                 static_cast<unsigned long long>(pace.skipped),
                                 missed, (paceRatio - 1.0f) * 100.0f,
                                 perFrame * 100.0);
                pace.shown = pace.repeated = pace.held = pace.skipped = 0;
                pace.dtSum = 0.0;
            }
            pace.audioLogAt = nowNs + 10'000'000'000ull;
        }
    }
}
