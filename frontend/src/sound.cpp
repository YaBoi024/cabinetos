#include "sound.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace sound {
namespace {

// 48 kHz because it is what the A9's HDMI audio runs at and what every other
// device this is likely to meet runs at. Mono, because a navigation click has
// no business having a position: it is the interface speaking, not something in
// the scene. SDL widens it to the device's channel count.
constexpr int kRate = 48000;

bool gReady = false;
bool gEnabled = true;
float gVolume = 0.22f;
SDL_AudioStream* gStream = nullptr;

// One cue, as samples.
struct Voice {
    std::vector<int16_t> pcm;
};
Voice gVoices[5];
Voice gChime;
Voice gInvite;

// A cue is two sine partials under one exponential envelope, and that is the
// whole synthesiser.
//
// WHY TWO PARTIALS AND NOT ONE. A single sine reads as a test tone — it is too
// pure to sound like an object. A quiet partial a fifth or an octave above the
// first gives it an edge that a television's speakers can actually reproduce,
// which matters more here than it would on headphones.
//
// WHY AN EXPONENTIAL DECAY AND A 3 MS ATTACK. Everything physical that makes a
// short sound decays exponentially, and starting a waveform at full amplitude
// puts a step in the signal that comes out as a click on top of the click. The
// 3 ms ramp costs nothing and removes it.
//
// `bend` slides the pitch across the sound: up for a choice, down for leaving.
// It is what makes Activate and Back read as opposites rather than as two
// unrelated noises.
void build(Voice& v, float hz, float partialHz, float partialGain,
           float seconds, float bend) {
    const int n = static_cast<int>(seconds * kRate);
    v.pcm.resize(static_cast<size_t>(n));
    const int attack = static_cast<int>(0.003f * kRate);
    // Phase is accumulated rather than computed from t, because the pitch bend
    // means the frequency is different at every sample and sin(2*pi*f*t) with a
    // moving f is not a continuous waveform — it jumps, and the jumps are
    // audible as a buzz.
    double phase = 0.0, phase2 = 0.0;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const float f = hz * (1.0f + bend * t);
        const float f2 = partialHz * (1.0f + bend * t);
        phase += 2.0 * M_PI * f / kRate;
        phase2 += 2.0 * M_PI * f2 / kRate;
        // Decays to about 1% of full by the end, whatever the length is.
        float env = std::exp(-4.6f * t);
        if (i < attack) env *= static_cast<float>(i) / static_cast<float>(attack);
        const float s = env * (static_cast<float>(std::sin(phase)) +
                               partialGain * static_cast<float>(std::sin(phase2)));
        // Headroom for the two partials summing, before the runtime volume.
        v.pcm[static_cast<size_t>(i)] =
            static_cast<int16_t>(SDL_clamp(s * 0.55f, -1.0f, 1.0f) * 32767.0f);
    }
}

// THE CHIME: two bell notes a fifth apart, the second struck 110 ms after the
// first and both left to ring, which is the shape a "you got something" sound
// has on every console that has one. Built from the same two-partial voice,
// with a third partial a little above the octave so it reads as a bell rather
// than a beep, and longer: it is an occasion, not punctuation.
void buildChime(Voice& v) {
    const float seconds = 0.9f;
    const int n = static_cast<int>(seconds * kRate);
    std::vector<float> mix(static_cast<size_t>(n), 0.0f);
    auto strike = [&](float hz, float at, float gain) {
        const int start = static_cast<int>(at * kRate);
        const int attack = static_cast<int>(0.003f * kRate);
        double p1 = 0.0, p2 = 0.0, p3 = 0.0;
        for (int i = start; i < n; ++i) {
            const int k = i - start;
            const float t = static_cast<float>(k) / static_cast<float>(n - start);
            float env = std::exp(-5.0f * t);
            if (k < attack) env *= static_cast<float>(k) / static_cast<float>(attack);
            p1 += 2.0 * M_PI * hz / kRate;
            p2 += 2.0 * M_PI * hz * 2.0 / kRate;
            p3 += 2.0 * M_PI * hz * 2.76 / kRate;
            mix[static_cast<size_t>(i)] +=
                gain * env * (static_cast<float>(std::sin(p1)) +
                              0.30f * static_cast<float>(std::sin(p2)) +
                              0.12f * static_cast<float>(std::sin(p3)) * std::exp(-12.0f * t));
        }
    };
    strike(1046.5f, 0.0f, 0.55f);    // C6
    strike(1568.0f, 0.11f, 0.55f);   // G6
    v.pcm.resize(mix.size());
    for (size_t i = 0; i < mix.size(); ++i)
        v.pcm[i] = static_cast<int16_t>(SDL_clamp(mix[i] * 0.55f, -1.0f, 1.0f) * 32767.0f);
}

// THE LINKUP INVITE: somebody reaching you, so it says more than a click and less than
// the achievement's bell. Two notes rising a major third, in the menu clicks' wooden
// voice (a partial at 2.75 times the pitch, quiet), so it belongs with them.
void buildInvite(Voice& v) {
    const float seconds = 0.65f;
    const int n = static_cast<int>(seconds * kRate);
    std::vector<float> mix(static_cast<size_t>(n), 0.0f);
    auto strike = [&](float hz, float at, float gain) {
        const int start = static_cast<int>(at * kRate);
        const int attack = static_cast<int>(0.004f * kRate);
        double p1 = 0.0, p2 = 0.0;
        for (int i = start; i < n; ++i) {
            const int k = i - start;
            const float t = static_cast<float>(k) / static_cast<float>(n - start);
            float env = std::exp(-5.5f * t);
            if (k < attack) env *= static_cast<float>(k) / static_cast<float>(attack);
            p1 += 2.0 * M_PI * hz / kRate;
            p2 += 2.0 * M_PI * hz * 2.75 / kRate;
            mix[static_cast<size_t>(i)] +=
                gain * env * (static_cast<float>(std::sin(p1)) +
                              0.22f * static_cast<float>(std::sin(p2)) * std::exp(-9.0f * t));
        }
    };
    strike(523.25f, 0.0f, 0.60f);    // C5
    strike(659.25f, 0.17f, 0.60f);   // E5
    v.pcm.resize(mix.size());
    for (size_t i = 0; i < mix.size(); ++i)
        v.pcm[i] = static_cast<int16_t>(SDL_clamp(mix[i] * 0.55f, -1.0f, 1.0f) * 32767.0f);
}

}  // namespace

bool init() {
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = kRate;
    gStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                        nullptr, nullptr);
    if (!gStream) {
        // Not an error. Say it once and let every play() be a no-op.
        std::fprintf(stderr, "[sound] no audio device, the interface is silent: %s\n",
                     SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(gStream);

    // THE SET, AND WHAT EACH ONE IS TRYING TO BE.
    //
    // CALMER SINCE #389 (MMagTech, 2026-10-14): the first set (Move at 880 Hz
    // over a 1320 Hz partial) read as "too jarring" against a quiet, minimal
    // screen. He chose, by ear, a blend halfway between an octave lower and a
    // marimba-like "wood": each voice's partial is 2.75 times its pitch, and
    // pitch, partial level, length and bend sit halfway between the two. The
    // first set's numbers, if it is ever wanted back: Move 880/1320 0.30
    // 45 ms; Activate 520/780 0.35 110 ms +0.18; Back 620/930 0.30 90 ms
    // -0.22; Edge 320/480 0.12 55 ms -0.10.
    //
    // MOVE is the one that plays hundreds of times an evening, so it is the
    // shortest and the highest: about 50 ms, and gone before the focus
    // animation it accompanies has finished. Anything longer turns a run along
    // a shelf into a drum roll.
    build(gVoices[0], 482.0f, 1324.0f, 0.185f, 0.0525f, 0.0f);
    // ACTIVATE is lower, longer and rises. It is the sound of something opening
    // and it is allowed to be heard.
    build(gVoices[1], 326.0f, 897.0f, 0.24f, 0.120f, 0.115f);
    // BACK is the same shape falling instead of rising, which is the whole
    // reason it reads as the opposite of Activate rather than as another noise.
    build(gVoices[2], 375.0f, 1031.0f, 0.185f, 0.100f, -0.15f);
    // EDGE is focus hitting a wall — a duller, quieter thud with no partial to
    // speak of, so it reads as "nothing there" rather than as a move that
    // worked. Without it, a controller pressed against the end of a row is
    // silent, which feels like the console stopped listening.
    build(gVoices[3], 211.0f, 580.0f, 0.08f, 0.0625f, -0.075f);
    // TYPE is a key on the on-screen keyboard: Move's voice cut to 20 ms, a
    // tick. An address or a Wi-Fi password is dozens of presses in a row, and
    // anything longer gets tiring (MMagTech: "just like a really short tick").
    // A keyboard with sounds of its own (a wood tap) was tried on the TV,
    // 2026-10-14, and read as "too weird switching": it stays in the family.
    build(gVoices[4], 482.0f, 1324.0f, 0.185f, 0.020f, 0.0f);
    buildChime(gChime);
    buildInvite(gInvite);

    gReady = true;
    std::fprintf(stderr, "[sound] interface audio at %d Hz\n", kRate);
    return true;
}

// THE LEVELS, as volumes. Medium is the 0.22 the console always had. The steps
// are about 10 dB apart (0.07, 0.22, 0.65), because loudness is heard as a
// ratio: equal steps in volume would make Quiet to Medium sound like a much
// bigger move than Medium to Loud. The first try was 5 dB (0.12, 0.40), and
// MMagTech on the TV, 2026-09-24: "the steps are too close in volume".
// The cues keep their own headroom, so anything up to 1.0 does not clip.
float gLevelVolume[kLevelCount] = {0.0f, 0.07f, 0.22f, 0.65f};
constexpr const char* kLevelName[kLevelCount] = {"Off", "Quiet", "Medium", "Loud"};
constexpr const char* kLevelWord[kLevelCount] = {"off", "quiet", "medium", "loud"};
Level gLevel = Level::Medium;

void setLevel(Level l) {
    gLevel = l;
    gEnabled = (l != Level::Off);
    if (gEnabled) gVolume = gLevelVolume[static_cast<int>(l)];
}
Level level() { return gLevel; }
const char* levelName(Level l) { return kLevelName[static_cast<int>(l)]; }
const char* levelWord(Level l) { return kLevelWord[static_cast<int>(l)]; }
bool levelFromWord(const std::string& word, Level* out) {
    for (int i = 0; i < kLevelCount; ++i)
        if (word == kLevelWord[i]) { *out = static_cast<Level>(i); return true; }
    return false;
}

void setLevelVolumes(float quiet, float medium, float loud) {
    gLevelVolume[1] = quiet;
    gLevelVolume[2] = medium;
    gLevelVolume[3] = loud;
    setLevel(gLevel);
}

Level gChimeLevel = Level::Medium;
void setChimeLevel(Level l) { gChimeLevel = l; }
Level chimeLevel() { return gChimeLevel; }

void playChime() {
    const float volume = gLevelVolume[static_cast<int>(gChimeLevel)];
    if (!gReady || volume <= 0.0f || gChime.pcm.empty()) return;
    // Queued behind whatever click is waiting rather than clearing it, as
    // play() does: the chime is the one sound here that must not be cut short.
    // Nothing clicks during play, so in practice nothing is ahead of it.
    std::vector<int16_t> out(gChime.pcm.size());
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<int16_t>(static_cast<float>(gChime.pcm[i]) * volume);
    SDL_PutAudioStreamData(gStream, out.data(), static_cast<int>(out.size() * sizeof(int16_t)));
}

void playInvite() {
    if (!gReady || !gEnabled || gVolume <= 0.0f || gInvite.pcm.empty()) return;
    std::vector<int16_t> out(gInvite.pcm.size());
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<int16_t>(static_cast<float>(gInvite.pcm[i]) * gVolume);
    SDL_PutAudioStreamData(gStream, out.data(), static_cast<int>(out.size() * sizeof(int16_t)));
}

void setEnabled(bool on) { gEnabled = on; }
void setVolume(float v) { gVolume = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void play(Cue c) {
    if (!gReady || !gEnabled || gVolume <= 0.0f) return;
    const Voice& v = gVoices[static_cast<int>(c)];
    if (v.pcm.empty()) return;

    // DO NOT LET CLICKS QUEUE UP. SDL's stream is a queue, so pushing a cue
    // while one is still playing appends it rather than mixing it — and a
    // controller held on the d-pad would build a backlog that keeps ticking
    // after the person has stopped moving, which is the same failure the 180 ms
    // focus tempo exists to prevent. If there is already more than one cue's
    // worth of audio waiting, clear it and start this one now.
    const int queued = SDL_GetAudioStreamQueued(gStream);
    if (queued > static_cast<int>(v.pcm.size() * sizeof(int16_t)))
        SDL_ClearAudioStream(gStream);

    // Scaled here rather than at build time so the volume can move without
    // rebuilding the cues — which is what a Settings slider would want.
    std::vector<int16_t> out(v.pcm.size());
    for (size_t i = 0; i < v.pcm.size(); ++i)
        out[i] = static_cast<int16_t>(static_cast<float>(v.pcm[i]) * gVolume);
    SDL_PutAudioStreamData(gStream, out.data(),
                           static_cast<int>(out.size() * sizeof(int16_t)));
}

}  // namespace sound
