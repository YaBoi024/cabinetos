// CabinetOS's host layer for upstream PCSX2.
//
// PCSX2 is not a libretro core and has no retro_run. It is a whole emulator
// that expects a frontend to exist around it: it calls out through the `Host`
// namespace for its settings, its window, its lifecycle and its messages, and
// upstream ships exactly two implementations of that — the Qt desktop app and
// the headless GS runner. Neither can run inside this console, so this is the
// third. Cabinet wrote the same layer for the Mac and called it the same thing.
//
// WHAT IT OWES PCSX2 IS MEASURED, NOT GUESSED: 55 `Host::` functions, three
// `InputManager::ConvertHostKeyboard*` and the `g_host_hotkeys` table. The
// count comes from linking with `-Wl,-z,defs` and reading what the linker
// refuses. docs/history/PCSX2-HOST-SURFACE.md has the whole list and where it
// came from, and `cores/build-pcsx2.sh` re-derives it on demand.
//
// THREADING, AND IT IS THE SAME SHAPE CABINET FOUND ON THE MAC: Run() blocks
// for the entire life of the game and must be given a thread of its own. PCSX2
// starts its own GS thread underneath it. RequestStop() is safe from any
// thread and is the only way out.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CabinetPS2
{
	struct Config
	{
		/// The disc image. CHD, ISO and the other formats CDVD knows.
		std::string disc_path;

		/// Where the PlayStation 2's firmware is. READ ONLY as far as this
		/// console is concerned — it comes from RomM with the game.
		std::string bios_dir;

		/// The game's disc code as RomM reads it (`title_id`, `SLUS-21090`),
		/// empty when RomM has none. It picks the BIOS by the game's region
		/// (CabinetPS2Bios.h, #416).
		std::string disc_code;

		/// Where the memory card goes. **THE PER-GAME, PER-USER SAVE
		/// DIRECTORY**, which is what makes the whole existing save machinery
		/// work unchanged: `catalog::saveFiles` already says a PlayStation 2
		/// card is `<stem>.ps2` in the save directory, and `filesave.cpp`
		/// already restores it before launch, captures it after, refuses to
		/// upload an unformatted one, and files it on the server under the
		/// name Cabinet's Mac uses.
		std::string memcards_dir;

		/// The card itself, a bare filename inside the directory above.
		/// Empty means no card at all, which is safer than a wrong one.
		std::string memory_card;

		/// Somewhere PCSX2 may write things nobody has to keep — its log, its
		/// shader cache, its snapshots.
		///
		/// **IT MUST BE WRITABLE BY THE SESSION USER AND THAT IS NOT FREE.**
		/// The obvious home, beside the BIOS, is root-owned on the reference
		/// console, so PCSX2 silently failed to make its own subdirectories
		/// and then failed to create a memory card. It said so in three log
		/// lines that each looked like a different problem.
		std::string scratch_dir;

		/// PCSX2's own resources folder — its game database, its fonts and its
		/// GS shaders. **STARTUP FAILS WITHOUT IT RATHER THAN DEGRADING**,
		/// which is worth knowing before blaming anything else: it prints
		/// "Resources directory is missing" and stops. It ships with the
		/// emulator, the way PPSSPP's system files already do.
		std::string resources_dir;

		/// Skips the BIOS splash, as every frontend does.
		bool fast_boot = true;

		/// Runs as fast as it can rather than pacing to 60 Hz. For measurement
		/// only: a capped run reports 100% speed whether there is four times
		/// the headroom or none at all.
		bool unlimited = false;

		/// PCSX2's own log. It is the only way to answer questions like whether
		/// the recompilers took, so it is worth having rather than inferring
		/// from behaviour.
		bool verbose_log = false;

		/// Where to write PNG frames, empty for none. The capture goes through
		/// PCSX2's own GSQueueSnapshot, so it is the picture the GS actually
		/// produced rather than anything this layer re-encodes.
		std::string dump_dir;

		/// Which frame to start dumping at, and how many. A PS2 game needs
		/// hundreds of frames to get past its BIOS and logos, so dumping from
		/// zero produces a pile of black.
		uint32_t dump_first = 0;
		uint32_t dump_count = 0;

		/// Stop after this many frames, 0 for no limit.
		uint32_t stop_after = 0;

		/// Anisotropic filtering, 0 (off) or 2/4/8/16.
		///
		/// **IT IS OFF IN PCSX2'S DEFAULTS AND IT IS THE ONE TEXTURE SETTING
		/// WORTH TURNING ON.** It sharpens surfaces seen at a steep angle,
		/// which on a PlayStation 2 is the road ahead in every racing game —
		/// exactly what somebody is looking at when they say the picture is
		/// blurry. It cannot add detail that was never rendered, so it is the
		/// smaller of the two levers; the upscale below is the larger one.
		int anisotropy = 0;

		/// The GS upscale multiplier. 1.0 is the PS2's native resolution.
		float upscale = 1.0f;

		/// PCSX2's "Widescreen 16:9" patch, for a game whose patch file has
		/// one (#217). On unless the player turned it off for this game in
		/// the pause menu (#368).
		bool widescreen = true;

	};

	/// The finished picture, as the GS produced it.
	///
	/// **THIS IS THE SAME SHAPE MOST LIBRETRO CORES ALREADY HAND THE
	/// FRONTEND** — a buffer of pixels, a width and a height —
	/// which is why PS2 needs no new picture path in the UI at all. PCSX2
	/// renders surfacelessly on its own thread and the finished frame is read
	/// back here; the frontend uploads it exactly as it uploads a Mega Drive's.
	///
	/// The alternative was sharing PCSX2's Vulkan image with the frontend
	/// directly, which is faster and needs upstream to enable two external
	/// memory extensions it does not. That is a real option and it is written
	/// up in docs/history/PCSX2-HOST-SURFACE.md; it is not the first thing to
	/// build, because this one costs nothing to try and the cost of the
	/// readback is a measurement rather than a guess.
	struct Frame
	{
		std::vector<uint32_t> pixels; // RGBA, `width * height` of them.
		unsigned width = 0;
		unsigned height = 0;
		uint64_t serial = 0; // Bumped every time a new frame lands.
	};

	/// Takes the newest frame if there is one newer than `since`. Safe from any
	/// thread; returns false and touches nothing when there is nothing new.
	///
	/// Copies rather than lending, deliberately: the GS thread overwrites its
	/// side whenever it likes, and a frontend holding a borrowed buffer across
	/// a draw call is a data race that would show up as tearing on a
	/// television and as nothing at all in a capture.
	bool TakeFrame(Frame* out, uint64_t since);

	/// THE WINDOW PCSX2 PRESENTS TO (#226), set before Run. Zero means none,
	/// and PCSX2 renders surfacelessly with every frame read back for the
	/// frontend to draw, as it did until 2026-10-04.
	///
	/// With a window, PCSX2 presents its own picture and gamescope shows it,
	/// the way Switch, PS3 and the other separate emulators are shown, with
	/// the console's window composited over it for the pause menu. Nothing is
	/// read back, so the copy that held Quality at 3x is gone; TakeFrame then
	/// answers only after CaptureFrame, for a screenshot.
	///
	/// The window is the frontend's, an X11 one under gamescope's Xwayland;
	/// this layer opens a display connection of its own to it (`display` is
	/// its name, empty for $DISPLAY), so PCSX2's threads never share SDL's.
	void SetWindow(const char* display, unsigned long window, unsigned width, unsigned height);

	/// Reads the picture on screen back once, for a screenshot, running or
	/// paused, and waits for it up to `timeout_ms`. TakeFrame then has it.
	bool CaptureFrame(unsigned timeout_ms);

	/// A controller, in **exactly** the shape the frontend already produces.
	///
	/// This mirrors `PadState` in frontend/src/core.h field for field, and
	/// deliberately so: the frontend fills one of those in for every
	/// libretro core, and PlayStation 2 taking a different shape would mean
	/// two input paths to keep in step. This layer does the translation to
	/// PCSX2's DualShock 2 numbering, which is one table in one file.
	///
	/// `buttons` is a bitmask using **libretro's RetroPad ids as bit
	/// positions** — B is bit 0, Start is bit 3, and so on — which is what the
	/// frontend already builds and tests with `(buttons >> id) & 1`.
	struct Pad
	{
		uint32_t buttons = 0;
		float leftX = 0, leftY = 0;
		float rightX = 0, rightY = 0;

		/// HOW FAR THE TRIGGERS ARE PRESSED, 0 to 1, as well as the L2/R2 bits
		/// above — both, for the reason `PadState` gives: a trigger is two
		/// things at once, and the same physical pull has to serve a game that
		/// reads it as a continuous value and one that reads it as a switch.
		///
		/// **ANSWERING THIS FROM THE RIGHT STICK IS A REAL BUG THIS CONSOLE
		/// SHIPPED.** `RETRO_DEVICE_INDEX_ANALOG_BUTTON` is libretro's third
		/// analogue index and its id is a joypad button id, so code that tests
		/// for the LEFT index and treats everything else as the right stick
		/// answers "how far is the trigger pressed" with the stick's Y axis.
		/// That is why Crazy Taxi could not be driven until 2026-09-19.
		float leftTrigger = 0, rightTrigger = 0;
	};

	/// Hands a controller's state to the running game. Safe from any thread;
	/// it is applied on PCSX2's own CPU thread between frames.
	void SetPad(unsigned port, const Pad& pad);

	/// Drains the sound made since the last call, as interleaved 16-bit stereo
	/// — the same shape and the same contract as `Core::drainAudio()`, so the
	/// frontend's mixer needs to learn nothing new.
	///
	/// **THE CONSOLE OWNS AUDIO, AS IT DOES FOR EVERY OTHER CORE.** PCSX2 can
	/// make its own sound through cubeb or SDL and must not here: a second
	/// device open on the same hardware is a second volume, a second latency,
	/// and nothing the in-game overlay can duck. Getting PCSX2 to hand its
	/// sound over instead is this project's one patch to the emulator, and
	/// `cores/build-pcsx2.sh` says why beside it.
	void DrainAudio(std::vector<int16_t>* out, unsigned frames);

	/// The rate PCSX2 is producing at — 48000 for a PlayStation 2.
	unsigned AudioSampleRate();



	/// Boots the disc and runs until RequestStop, the frame limit, or the game
	/// ending. BLOCKS — give it its own thread. Returns false and fills error
	/// if the VM never started.
	bool Run(const Config& config, std::string* error);

	/// Asks the running game to stop. Safe from any thread.
	void RequestStop();

	/// Pauses or resumes the emulated machine.
	///
	/// **THE OVERLAY NEEDS THIS AND NOT-CALLING-runFor IS NOT ENOUGH.** Every
	/// libretro core stops simply because the frame loop stops stepping it —
	/// that is how the in-game menu freezes a game, and it was a real fault
	/// until 2026-09-19 that it did not. PCSX2 runs on a thread of its own and
	/// will happily carry on playing behind the menu unless it is told.
	void SetPaused(bool paused);

	bool IsRunning();

	/// Whether the running game's patch file has a "Widescreen 16:9" section,
	/// the one EnableWideScreenPatches switches on by name (#368). Known once
	/// the game's own program has booted, false before that and with no game.
	/// Safe from any thread.
	bool HasWidescreenPatch();

	/// THE EMULATED MACHINE'S MEMORY, FOR RETROACHIEVEMENTS (#74). Region 0 is
	/// the EE's main RAM, region 1 its 16 KB scratchpad; anything else, or no
	/// running game, is null with `*size` 0.
	///
	/// **THE FRONTEND RUNS rcheevos ITSELF AND THIS LAYER ONLY LENDS IT THE
	/// BYTES.** PCSX2 has an achievements client of its own, and using it would
	/// mean a second RetroAchievements login, a second HTTP stack and a second
	/// set of notifications, drawn by PCSX2's ImGui and looking like nothing
	/// else on the console. Every libretro core already works the other way:
	/// the frontend reads `retro_get_memory_data` and owns the client. So
	/// PlayStation 2 does too, and the two regions here are exactly what
	/// PCSX2's own client reads (Achievements::ClientReadMemory, quoted in
	/// the .cpp), so the addresses mean what the RetroAchievements sets
	/// written against PCSX2 expect them to mean.
	uint8_t* Memory(unsigned region, size_t* size);

	/// Called once per emulated frame, on PCSX2's CPU thread, while the game
	/// is running and nothing has asked it to stop, and only once the game's
	/// own program (its ELF) has started: not during the BIOS boot. That gate
	/// is PCSX2's own, from Achievements::FrameUpdate, which calls
	/// rc_client_do_frame only when VMManager::Internal::HasBootedELF() and
	/// rc_client_idle before; the frontend idles the client itself meanwhile.
	/// Null clears it. Safe from any thread at any time, including from
	/// inside the callback itself.
	///
	/// **WHEN THIS RETURNS, THE OLD CALLBACK IS NOT RUNNING AND WILL NOT RUN
	/// AGAIN.** That is stronger than swapping a pointer, and it is the
	/// guarantee the frontend actually needs: it clears the callback and then
	/// destroys its rcheevos client, and a swap alone would let a call already
	/// in flight on the CPU thread use the client after it was freed. The
	/// price is that the callback must never wait on a lock the thread calling
	/// this holds, or the two wait on each other.
	void SetFrameCallback(void (*callback)(void* user), void* user);

	/// Live performance, all zero when nothing is running.
	struct Metrics
	{
		float fps;
		float speed;
		uint64_t frames;
		/// What reading one finished frame back out of the GS costs. The whole
		/// picture path rests on this being small enough; see TakeFrame.
		double readback_us;

		/// **THE WORST ONE, NOT THE AVERAGE, BECAUSE STUTTER IS A WORST CASE.**
		/// An average hides exactly the thing somebody feels: a mean of 2.7 ms
		/// with an occasional 40 ms in it reads as "fine" and looks like a
		/// hitch. Reported alongside how many frames went over a 60 Hz budget.
		double readback_worst_us;
		uint64_t readback_over_budget;
	};

	Metrics GetMetrics();
} // namespace CabinetPS2
