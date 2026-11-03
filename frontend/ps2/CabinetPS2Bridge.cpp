// The flat C face CabinetOS's frontend opens with dlopen.
//
// **WHY A SHARED OBJECT RATHER THAN LINKING PCSX2 INTO THE FRONTEND.** Two
// reasons, and either alone would decide it.
//
// The licence is the hard one. PCSX2 is GPLv3 and this repository is MIT.
// docs/LICENCES.md rests its whole position on one sentence — "the cores are
// `dlopen`ed rather than statically linked" — which is what keeps the frontend
// binary its own work rather than part of a combined GPLv3 one. Every libretro
// core already arrives this way; PlayStation 2 arrives the same way, and the
// argument does not have to be reopened.
//
// The practical one is the build. PCSX2 needs about thirty development
// packages the frontend's own builder image does not have and should not grow;
// its Containerfile is small on purpose, because it also builds every libretro
// core. Keeping the emulator behind a `.so` means the frontend's Makefile does
// not change at all.
//
// So this is deliberately a C API and deliberately dull: no C++ types cross it,
// nothing is thrown, and every function is safe to call when nothing is
// running. That is the same contract the libretro cores meet, and it is what
// lets frontend/src/core.cpp treat PlayStation 2 as one more thing it loads.

#include "CabinetPS2Host.h"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
	std::thread s_vm_thread;
	std::string s_error;
	std::atomic<bool> s_started{false};

	// Widescreen for the next start, set by cps2_set_widescreen (#368).
	std::atomic<bool> s_widescreen{true};

	// The disc code for the next start, set by cps2_set_disc_code (#416).
	// Set and read on the frontend's thread, just before cps2_start.
	std::string s_disc_code;

	// The frame most recently handed over, kept so that `cps2_take_frame` can
	// lend a pointer that stays valid until the NEXT call. The frontend
	// uploads it to a texture immediately and never keeps it, which is the
	// same lifetime a libretro core's `video_refresh` buffer has.
	CabinetPS2::Frame s_frame;
	std::vector<int16_t> s_audio;

	// What each DualShock 2 was last asked to do with its motors, 0 to 1,
	// large then small. Written on PCSX2's CPU thread, read by the frontend's.
	std::atomic<float> s_motor[2][2];
} // namespace

// RUMBLE, WITHOUT PATCHING PCSX2 (#149). Every pad type with motors reports
// them through this one function, which in PCSX2's own frontends drives the
// bound controller. Here nothing is bound, so the call is taken over at link
// time instead: compile.sh links with --wrap for its mangled name, which
// sends every call from the pad code here. The real function is not called;
// with no bindings it would do nothing. `pad_index` is PCSX2's unified slot,
// 0 and 1 for the two ports without a multitap, which is all this host
// offers (CabinetPS2::SetPad).
extern "C" void __wrap__ZN12InputManager24SetPadVibrationIntensityEjff(
	unsigned pad_index, float large_or_single, float small)
{
	if (pad_index >= 2)
		return;
	s_motor[pad_index][0].store(large_or_single);
	s_motor[pad_index][1].store(small);
}

extern "C" {

// Starts a game. Returns 1 on success. Blocks only long enough to get the VM
// running; the emulator itself runs on a thread of its own from here on,
// because PCSX2's VMManager::Execute does not return until the game stops.
int cps2_start(const char* disc, const char* bios_dir, const char* memcards_dir, const char* memory_card,
	const char* scratch_dir, const char* resources, float upscale, int anisotropy, int fast_boot)
{
	if (s_started.load())
		return 0;

	CabinetPS2::Config config;
	config.disc_path = disc ? disc : "";
	config.bios_dir = bios_dir ? bios_dir : "";
	config.disc_code = s_disc_code;
	config.memcards_dir = memcards_dir ? memcards_dir : "";
	config.memory_card = memory_card ? memory_card : "";
	config.scratch_dir = scratch_dir ? scratch_dir : "";
	config.resources_dir = resources ? resources : "";
	config.upscale = upscale > 0.0f ? upscale : 1.0f;
	config.anisotropy = anisotropy;
	config.fast_boot = fast_boot != 0;
	config.widescreen = s_widescreen.load();
	config.stop_after = 0;

	s_error.clear();
	s_started.store(true);

	s_vm_thread = std::thread([config] {
		std::string error;
		if (!CabinetPS2::Run(config, &error))
			s_error = error;
		s_started.store(false);
	});

	return 1;
}

// Why the last start failed, or an empty string. Valid until the next call to
// cps2_start.
const char* cps2_error(void)
{
	return s_error.c_str();
}

// Whether the emulated machine is actually running, as opposed to still being
// built. The frontend asks this for the same reason it asks a libretro core:
// tearing a machine down while it is still being assembled frees memory out
// from under the thread doing the assembling.
int cps2_running(void)
{
	return CabinetPS2::IsRunning() ? 1 : 0;
}

// Asks the game to stop and WAITS for it. Blocking is the point: PCSX2 flushes
// the memory card during shutdown, so a frontend that captured the card before
// this returned would upload the card as it was when the game started. The
// libretro path has the same rule about retro_unload_game and it was paid for
// once already.
void cps2_stop(void)
{
	CabinetPS2::RequestStop();
	if (s_vm_thread.joinable())
		s_vm_thread.join();
	s_started.store(false);
}

// Freezes the emulated machine. The frontend calls this when the in-game
// overlay opens: every libretro core stops because the frame loop stops
// stepping it, and a PlayStation 2 on a thread of its own would otherwise
// carry on being played behind the menu.
void cps2_set_paused(int paused)
{
	CabinetPS2::SetPaused(paused != 0);
}

void cps2_set_pad(unsigned port, uint32_t buttons, float left_x, float left_y, float right_x, float right_y,
	float left_trigger, float right_trigger)
{
	CabinetPS2::Pad pad;
	pad.buttons = buttons;
	pad.leftX = left_x;
	pad.leftY = left_y;
	pad.rightX = right_x;
	pad.rightY = right_y;
	pad.leftTrigger = left_trigger;
	pad.rightTrigger = right_trigger;
	CabinetPS2::SetPad(port, pad);
}

// The motors of the pad in `port`, 0 to 1, as the game last set them.
void cps2_get_rumble(unsigned port, float* large, float* small)
{
	*large = port < 2 ? s_motor[port][0].load() : 0.0f;
	*small = port < 2 ? s_motor[port][1].load() : 0.0f;
}

// Lends the newest frame if there is one newer than `since`. The pointer stays
// valid until the next call. Returns 1 when something new was handed over.
int cps2_take_frame(const uint32_t** pixels, unsigned* width, unsigned* height, uint64_t* serial)
{
	if (!CabinetPS2::TakeFrame(&s_frame, *serial))
		return 0;

	*pixels = s_frame.pixels.data();
	*width = s_frame.width;
	*height = s_frame.height;
	*serial = s_frame.serial;
	return 1;
}

// The window PCSX2 presents to, set before cps2_start; 0 for none, which is
// every frame read back for cps2_take_frame (#226). See
// CabinetPS2::SetWindow. Optional for the frontend: an older library lacks it
// and plays the old way.
void cps2_set_window(const char* display, unsigned long window, unsigned width, unsigned height)
{
	CabinetPS2::SetWindow(display, window, width, height);
}

// PCSX2's "Widescreen 16:9" patch on or off for the next cps2_start (#368);
// on until told. Optional for the frontend, as cps2_set_window is: an older
// library lacks it and every patched game stays widescreen.
void cps2_set_widescreen(int on)
{
	s_widescreen.store(on != 0);
}

// The game's disc code as RomM reads it (`SLUS-21090`) for the next
// cps2_start, null or empty for none; it picks the BIOS by the game's region
// (#416). Optional for the frontend: an older library lacks it and takes the
// first BIOS it finds.
void cps2_set_disc_code(const char* code)
{
	s_disc_code = code ? code : "";
}

// 1 when the running game's patch file has a "Widescreen 16:9" section, known
// once its program has booted; 0 before that and with no game.
int cps2_has_widescreen_patch(void)
{
	return CabinetPS2::HasWidescreenPatch() ? 1 : 0;
}

// Reads the picture on screen back once, for a screenshot, and waits for it.
// Returns 1 when cps2_take_frame now has it.
int cps2_capture_frame(unsigned timeout_ms)
{
	return CabinetPS2::CaptureFrame(timeout_ms) ? 1 : 0;
}

// Fills `dest` with up to `max_frames` stereo frames and returns how many were
// written. Interleaved 16-bit, the same shape every libretro core produces.
unsigned cps2_drain_audio(int16_t* dest, unsigned max_frames)
{
	if (!dest || max_frames == 0)
		return 0;

	s_audio.clear();
	CabinetPS2::DrainAudio(&s_audio, max_frames);

	const unsigned frames = static_cast<unsigned>(s_audio.size() / 2);
	if (frames > 0)
		std::memcpy(dest, s_audio.data(), s_audio.size() * sizeof(int16_t));
	return frames;
}

unsigned cps2_audio_sample_rate(void)
{
	return CabinetPS2::AudioSampleRate();
}

// Live performance, for the frontend's own speed reading and for the readback
// cost the picture path rests on.
void cps2_metrics(float* fps, float* speed, double* readback_us)
{
	const CabinetPS2::Metrics m = CabinetPS2::GetMetrics();
	if (fps)
		*fps = m.fps;
	if (speed)
		*speed = m.speed;
	if (readback_us)
		*readback_us = m.readback_us;
}

// RETROACHIEVEMENTS (#74). The frontend runs rcheevos itself, as it does for
// every libretro core, so all this library owes it is the emulated memory and
// a tick once a frame. CabinetPS2::Memory and CabinetPS2::SetFrameCallback
// say which memory and which point in the frame, and why they are the ones
// PCSX2's own achievements code uses.
//
// region 0 = EE main RAM (32 MB, RetroAchievements PS2 addresses
// 0x0000000-0x1FFFFFF), region 1 = EE scratchpad (16 KB, 0x2000000-0x2003FFF).
// Returns the base pointer and sets *size; NULL and *size 0 when no game is
// running or the region is not one of the two. Valid from when cps2_running()
// first returns 1 until cps2_stop returns. Both are LIVE: the CPU thread
// writes them while the game runs, so a read is only a consistent frame from
// inside the frame callback, which is where rcheevos reads.
uint8_t* cps2_memory(unsigned region, size_t* size)
{
	if (!size)
		return nullptr;
	return CabinetPS2::Memory(region, size);
}

// cb(user) once per emulated frame (vsync), ON PCSX2'S CPU THREAD, from
// Host::PumpMessagesOnCPUThread, only while the game is Running (not paused,
// not stopping) and only once its ELF has booted: nothing during the BIOS,
// matching PCSX2's Achievements::FrameUpdate, which runs rc_client_do_frame
// only when VMManager::Internal::HasBootedELF() and rc_client_idle before.
// The frontend keeps calling rc_client_idle on its own thread, so the BIOS
// seconds need nothing from here. NULL clears it. Safe at any time, before cps2_start included,
// and from inside cb. The pair changes as one, and once this returns the old
// cb is not running and will not run again, so the frontend may free what
// `user` points at straight after clearing it. cb must not wait on a lock
// held by whoever calls this.
void cps2_set_frame_callback(void (*cb)(void* user), void* user)
{
	CabinetPS2::SetFrameCallback(cb, user);
}

// The version of PCSX2 behind this, so the frontend can print what it is
// actually running rather than what it believes it pinned. Every libretro core
// answers the same question through retro_get_system_info, and the reason is
// the same: a core that does not know which revision it is cannot be audited.
const char* cps2_version(void)
{
	return CABINETOS_PS2_VERSION;
}

} // extern "C"
