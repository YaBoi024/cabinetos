// CabinetOS's host layer for upstream PCSX2. See CabinetPS2Host.h.
//
// EVERY FUNCTION PCSX2 ASKS FOR IS HERE OR IN CabinetPS2HostStubs.cpp, AND THE
// COUNT IS MEASURED: 55 in the
// `Host` namespace, three `InputManager::ConvertHostKeyboard*`, and the
// `g_host_hotkeys` table — which is a VARIABLE, not a function, and is the one
// a dlopen of an incomplete layer fails on before naming any of the others.
// docs/history/PCSX2-HOST-SURFACE.md, and `cores/build-pcsx2.sh` re-derives the
// list.
//
// MOST OF THEM ARE DELIBERATELY EMPTY, and that is not laziness. A console has
// no clipboard, no file selector, no achievements login, no Big Picture mode to
// exit to and no game list of PCSX2's own — CabinetOS has its own library, from
// RomM. Upstream's own gsrunner answers most of them the same way. **An empty
// body with a reason beside it is the honest answer; the dangerous one is a
// function that pretends to do something.**
//
// Signatures are taken verbatim from upstream's pcsx2-gsrunner/Main.cpp at the
// pinned revision rather than transcribed from Host.h by hand, because a
// mistyped one is a link error at best and an ODR violation at worst.

#include "CabinetPS2Host.h"
#include "CabinetPS2Bios.h"
#include "CabinetPS2Pad.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <thread>
#include <condition_variable>

#include "fmt/format.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"
#include "common/ProgressCallback.h"
#include "common/StringUtil.h"

#include "pcsx2/PrecompiledHeader.h"
#include "pcsx2/Achievements.h"
#include "pcsx2/GS.h"
#include "pcsx2/GS/Renderers/Common/GSDevice.h"
#include "pcsx2/Host.h"
// SIX OF THE 55 ARE DECLARED IN THE FULLSCREEN UI HEADERS RATHER THAN Host.h,
// which is not obvious and cost the first compile: LocaleCircleConfirm,
// RequestExitApplication and RequestExitBigPicture live in FullscreenUI.h, and
// ShouldPreferHostFileSelector, OpenHostFileSelectorAsync and its two callback
// types in ImGuiFullscreen.h. They are still `Host::` functions and the linker
// still demands them; only the declaration moved.
#include "pcsx2/ImGui/FullscreenUI.h"
#include "pcsx2/ImGui/ImGuiFullscreen.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/Memory.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/Patch.h"
#include "pcsx2/PerformanceMetrics.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/SIO/Pad/PadDualshock2.h"
#include "pcsx2/VMManager.h"

// libX11's one call this layer makes, declared by hand: Xlib.h defines `None`,
// `Bool`, `Status` and `Success` as macros, which PCSX2's own headers use as
// names (OsdOverlayPos::None). The library is already linked (compile.sh).
struct _XDisplay;
extern "C" struct _XDisplay* XOpenDisplay(const char* name);

namespace
{
	// Everything PCSX2 is told lives in memory. There is no pcsx2.ini on a
	// console and there must not be: CabinetOS owns configuration, the way it
	// already answers every libretro core's options from catalog::optionOverrides
	// rather than letting a core read a file of its own.
	MemorySettingsInterface s_settings;

	CabinetPS2::Config s_config;
	std::atomic<bool> s_running{false};

	// The frame handover. The GS thread writes, any other thread reads.
	std::mutex s_frame_lock;
	CabinetPS2::Frame s_frame;
	std::atomic<uint64_t> s_frame_serial{0};

	// What the readback costs, in microseconds, as an exponential average.
	// **KEPT BECAUSE THE DESIGN RESTS ON IT.** Reading the finished picture
	// back through the CPU is the simple way to get a PS2 into this frontend
	// and it may turn out to be too slow at a high upscale, in which case the
	// answer is to share PCSX2's Vulkan image instead. That is a decision to
	// make against a number, and this is the number.
	std::atomic<double> s_readback_us{0.0};
	std::atomic<double> s_readback_worst{0.0};
	std::atomic<uint64_t> s_readback_over{0};

	// Skips the readback entirely, for one experiment: is the picture path what
	// makes a game hitch, or is it the emulator? With this on, PCSX2 renders
	// and nothing is taken off it, so anything still stuttering is not ours.
	// Set CABINETOS_PS2_NO_READBACK to use it. Costs nothing when unset.
	bool NoReadback()
	{
		static const bool off = std::getenv("CABINETOS_PS2_NO_READBACK") != nullptr;
		return off;
	}
	// THE WINDOW, when the frontend gave one (#226; SetWindow in the header).
	// Written by the frontend before a game starts, read by the GS thread when
	// it opens its device. The display connection is this layer's own and is
	// opened once, on first use, and kept for the life of the process: every
	// game's window is on the same Xwayland.
	std::mutex s_window_lock;
	std::string s_window_display;
	unsigned long s_window = 0;
	u32 s_window_width = 0, s_window_height = 0;
	struct _XDisplay* s_xdisplay = nullptr;

	bool Windowed()
	{
		std::lock_guard<std::mutex> lock(s_window_lock);
		return s_window != 0;
	}

	// A screenshot asked for while there is a window, and so no readback of
	// every frame. Served by the GS thread's next present, or while paused by
	// the CPU thread queueing the readback onto the GS thread.
	std::atomic<bool> s_capture_requested{false};

	std::atomic<bool> s_stop_requested{false};
	std::atomic<bool> s_paused{false};
	// Whether this game's patch file has a "Widescreen 16:9" section (#368),
	// set on the CPU thread when the game's program boots.
	std::atomic<bool> s_widescreen_patch{false};
	std::atomic<uint64_t> s_frames{0};

	// THE FRONTEND'S PER-FRAME HOOK, for RetroAchievements (#74). A mutex
	// rather than two atomics, because the pair has to change as one (a CPU
	// thread that read the new function with the old `user` would hand
	// rcheevos somebody else's pointer), and because the lock is also what
	// lets SetFrameCallback promise that a cleared callback is not still
	// running. It is taken once a frame and never contended in play: the
	// frontend sets the hook when a game starts and clears it when it stops.
	//
	// The thread_local is how the callback may clear or replace itself: on
	// the CPU thread inside the call the lock is already held, and taking it
	// again would deadlock.
	std::mutex s_frame_callback_lock;
	void (*s_frame_callback)(void*) = nullptr;
	void* s_frame_callback_user = nullptr;
	thread_local bool t_in_frame_callback = false;

	void RunFrameCallback()
	{
		std::lock_guard<std::mutex> lock(s_frame_callback_lock);
		if (!s_frame_callback)
			return;
		t_in_frame_callback = true;
		s_frame_callback(s_frame_callback_user);
		t_in_frame_callback = false;
	}

	// Owned by the GS thread.
	uint32_t s_dumped = 0;
} // namespace

// ---------------------------------------------------------------------------
// The display path. THESE SIX ARE THE ONLY ONES THAT ARE REAL WORK.
//
// Two ways, chosen by the frontend (SetWindow). With a window, PCSX2 presents
// to it and gamescope shows it, the console's window composited over it for
// the pause menu (#226). Without one, surfaceless: PCSX2 renders, every frame
// is read back in BeginPresentFrame, and the frontend draws it; that is what
// a machine without gamescope still gets.
// ---------------------------------------------------------------------------

namespace
{
	// The finished picture into s_frame, for the frontend. On the GS thread.
	void ReadBackFrame()
	{
		const auto started = std::chrono::steady_clock::now();

		u32 width = 0, height = 0;
		std::vector<u32> pixels;

		// Zero for the window size asks for the GS's own internal resolution.
		//
		// **AND apply_aspect IS TRUE, WHICH AN EARLIER VERSION GOT WRONG.**
		//
		// The reasoning then was that the frontend owns aspect and letting
		// PCSX2 correct it too would be correcting it twice — which sounds
		// right and is not, because the frontend can only DERIVE an aspect
		// from the pixel dimensions and a PlayStation 2's pixels are not
		// square. A 640x448 frame drawn at 640/448 is seven per cent too wide,
		// and a 16:9 game is far more wrong than that.
		//
		// PCSX2 knows the answer — it has the game's video mode and its
		// widescreen setting — and hands back a buffer whose shape is already
		// correct, expanded to the larger dimension. The frontend then treats
		// it as square-pixel, which it now is, and needs no declared aspect at
		// all. One correction, made by the half that has the information.
		if (GSSaveSnapshotToMemory(0, 0, true, false, &width, &height, &pixels) && width > 0 && height > 0)
		{
			std::lock_guard<std::mutex> lock(s_frame_lock);
			s_frame.pixels = std::move(pixels);
			s_frame.width = width;
			s_frame.height = height;
			s_frame.serial = s_frame_serial.fetch_add(1) + 1;
		}

		const double us =
			std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
		// Exponential average. A single frame is noise — the first one after a
		// resolution change compiles pipelines and is worth ten of the others.
		const double prev = s_readback_us.load();
		s_readback_us.store(prev == 0.0 ? us : (prev * 0.95 + us * 0.05));

		// The worst one and how often it blew a 60Hz budget. An average cannot
		// describe a stutter; a maximum can.
		if (us > s_readback_worst.load())
			s_readback_worst.store(us);
		if (us > 16666.0)
			s_readback_over.fetch_add(1);
	}
} // namespace

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
	// Unreferenced in this configuration — the linker does not even ask for it
	// — but implemented because upstream's gsrunner and Cabinet's host both do,
	// and building only what the linker complains about leaves this layer one
	// configuration change from a link error.
	return Host::AcquireRenderWindow(false);
}

std::optional<WindowInfo> Host::AcquireRenderWindow(bool recreate_window)
{
	WindowInfo wi;
	wi.surface_scale = 1.0f;

	// THE FRONTEND'S WINDOW, WHEN IT GAVE ONE (#226). PCSX2 makes a Vulkan
	// surface on it (VK_KHR_xlib_surface, X11_API in build-pcsx2.sh) and
	// presents there itself; gamescope shows it, and the console's own
	// window, marked as gamescope's overlay, draws the pause menu over it.
	{
		std::lock_guard<std::mutex> lock(s_window_lock);
		if (s_window != 0)
		{
			if (!s_xdisplay)
				s_xdisplay = XOpenDisplay(s_window_display.empty() ? nullptr : s_window_display.c_str());
			if (s_xdisplay)
			{
				wi.type = WindowInfo::Type::X11;
				wi.display_connection = s_xdisplay;
				wi.window_handle = reinterpret_cast<void*>(static_cast<uintptr_t>(s_window));
				wi.surface_width = s_window_width;
				wi.surface_height = s_window_height;
				Console.WriteLn("[ps2] presenting to window 0x%lx, %ux%u", s_window, s_window_width,
					s_window_height);
				return wi;
			}
			// Said, and then the old way: a picture read back is a game that
			// still plays, where no picture at all is not.
			Console.Error("[ps2] could not open X display '%s'; reading frames back instead",
				s_window_display.c_str());
			s_window = 0;
		}
	}

	// SURFACELESS OTHERWISE. PCSX2 still RENDERS: the GS produces a finished
	// picture every frame and BeginPresentFrame below reads it back for the
	// frontend to draw in its own window; "surfaceless" means only that there
	// is nowhere for PCSX2 to present it to.
	wi.type = WindowInfo::Type::Surfaceless;
	return wi;
}

void Host::ReleaseRenderWindow()
{
	// The window is the frontend's and goes when the game has stopped; the
	// display connection stays for the next game.
}

void Host::BeginPresentFrame()
{
	// CALLED ON THE GS THREAD, once per finished frame. It is the only point
	// at which the picture is complete and still exists, which is why the
	// handover happens here rather than anywhere more convenient.
	const uint64_t frame = s_frames.fetch_add(1);

	// With a window, nothing is read back unless a screenshot asked for it.
	if (Windowed() ? s_capture_requested.exchange(false) : !NoReadback())
		ReadBackFrame();

	if (s_config.dump_count == 0 || s_config.dump_dir.empty())
		return;
	if (frame < s_config.dump_first || s_dumped >= s_config.dump_count)
		return;

	// PCSX2's own snapshot to disk, which is a different thing from the
	// readback above: this one is the finished, aspect-corrected picture, and
	// it is what a person looks at when asking whether the game renders
	// correctly.
	GSQueueSnapshot(Path::Combine(s_config.dump_dir, fmt::format("frame{:05}.png", frame)));
	s_dumped++;
}

// ---------------------------------------------------------------------------
// VM lifecycle. CabinetOS watches these rather than driving from them — Run()
// below owns the sequence — but OnVMDestroyed is how a game that ends by
// itself gets noticed.
// ---------------------------------------------------------------------------

void Host::OnVMStarted()
{
	s_running.store(true);

	// SAY WHICH RENDERER ACTUALLY TOOK, EVERY TIME.
	//
	// Asking for Vulkan and getting it are different things: PCSX2 falls back
	// to its software renderer when a device cannot be created, and a software
	// PlayStation 2 on a machine with a Radeon in it looks like nothing at all
	// until somebody wonders why a game is slow. This console has been caught
	// by a silent fallback more than once — a compositor that reported "no
	// Vulkan-capable GPU" four seconds after selecting one, and a core that
	// substitutes its own boot ROM and says nothing at any log level.
	//
	// So it is printed rather than assumed, and it is printed as a fact about
	// what happened rather than a restatement of what was asked for.
	const char* renderer = "unknown";
	switch (GSConfig.Renderer)
	{
		case GSRendererType::VK: renderer = "Vulkan"; break;
		case GSRendererType::OGL: renderer = "OpenGL"; break;
		case GSRendererType::SW: renderer = "software"; break;
		case GSRendererType::Null: renderer = "null"; break;
		default: break;
	}
	// REPORT WHAT PCSX2 ENDED UP WITH, NOT WHAT IT WAS ASKED FOR.
	//
	// `GSConfig` is the applied configuration, so a setting that did not take —
	// a key nobody reads, a value clamped, a request refused — shows up here as
	// a different number rather than as a picture somebody has to squint at.
	// MMagTech, 2026-09-21: "something is off that didnt look much better", and
	// a frame at native resolution looks identical whether the upscale was
	// ignored or never asked for.
	//
	// ON stderr RATHER THAN THROUGH Console, and that is not a style choice.
	// PCSX2's own console is configured from the settings layer during startup,
	// so anything written before that reaches nobody — the first version of
	// this line printed nothing at all, which is exactly how gsrunner hides its
	// argument errors. stderr always works.
	std::fprintf(stderr, "[ps2] renderer %s, upscale %gx, anisotropy %d%s\n", renderer,
		static_cast<double>(GSConfig.UpscaleMultiplier), GSConfig.MaxAnisotropy,
		(GSConfig.Renderer == GSRendererType::SW || GSConfig.Renderer == GSRendererType::Null)
			? "  — THAT IS NOT THE HARDWARE RENDERER THIS CONSOLE ASKED FOR"
			: "");
}

void Host::OnVMDestroyed()
{
	s_running.store(false);
	Console.WriteLn("[ps2] VM destroyed");
}

void Host::OnGameChanged(const std::string& title, const std::string& elf_override, const std::string& disc_path,
	const std::string& disc_serial, u32 disc_crc, u32 current_crc)
{
	// THE SERIAL AND CRC ARE WORTH PRINTING RATHER THAN SWALLOWING. Cabinet's
	// Mac save states are keyed by exactly these two, so they are what any
	// future question about a state travelling between the two will start from.
	Console.WriteLn(fmt::format("[ps2] game: \"{}\" serial={} crc={:08X}",
		title.empty() ? "(none)" : title, disc_serial.empty() ? "(none)" : disc_serial, disc_crc));

	// WHETHER THE PAUSE MENU OFFERS "Widescreen" (#368): only when the patch
	// file has the section EnableWideScreenPatches switches on by name
	// (Patch.cpp, WS_PATCH_NAME). Asked with the running program's CRC, the
	// way PCSX2 picks the file, so it is known once that program has booted;
	// before then the CRC is 0 and there is no file. Here on the CPU thread,
	// where PCSX2 reads patches.zip itself.
	if (current_crc == 0 || disc_serial.empty())
		return;
	bool found = false;
	for (const Patch::PatchInfo& p : Patch::GetPatchInfo(disc_serial, current_crc, false, false, nullptr))
		found = found || p.name == "Widescreen 16:9";
	Console.WriteLn(fmt::format("[ps2] widescreen patch: {}", !found ? "none" : s_config.widescreen ? "on" : "off for this game"));
	s_widescreen_patch.store(found);
}

void Host::RequestVMShutdown(bool allow_confirm, bool allow_save_state, bool default_save_state)
{
	// The game asked to stop — a PS2 title returning to the browser, say.
	// Nothing is confirmed, because there is nobody to confirm with.
	s_stop_requested.store(true);
	VMManager::SetState(VMState::Stopping);
}

// ---------------------------------------------------------------------------
// Threading.
// ---------------------------------------------------------------------------

void Host::RunOnCPUThread(std::function<void()> function, bool block /* = false */)
{
	// Run() below IS the CPU thread, so anything posted here can simply run.
	// That is only true because nothing else in this layer posts from another
	// thread; if that changes this needs a real queue, the way Cabinet's has
	// one.
	function();
}

void Host::PumpMessagesOnCPUThread()
{
	// The one place the emulator gives the frontend the CPU thread between
	// frames. The frame limit is enforced here rather than by counting from
	// outside, because this is the only point at which stopping is safe.
	CabinetPS2::ApplyPads();
	if (s_config.stop_after != 0 && s_frames.load() >= s_config.stop_after)
		s_stop_requested.store(true);

	if (s_stop_requested.load() && VMManager::GetState() == VMState::Running)
	{
		VMManager::SetState(VMState::Stopping);
		return;
	}

	// THE FRAME HOOK, FOR RETROACHIEVEMENTS (#74), AND THIS IS THE SAME POINT
	// IN THE FRAME PCSX2'S OWN ACHIEVEMENTS CODE CHECKS AT. At the pinned
	// v2.8.2, Counters.cpp's VSyncStart is the end of every emulated frame
	// (every field, on an interlaced game, which is what RetroAchievements
	// counts too) and does, in order:
	//
	//   VMManager::Internal::VSyncOnCPUThread()   -> Achievements::FrameUpdate()
	//   VMManager::Internal::Throttle()           frame pacing, sleeps only
	//   gsPostVsyncStart()                        hands the frame to the GS thread
	//   VMManager::Internal::PollInputOnCPUThread() -> Host::PumpMessagesOnCPUThread()
	//
	// The EE runs no instructions between the first line and the last, so the
	// memory rcheevos sees here is byte for byte what PCSX2's rc_client_do_frame
	// would have seen. This function is the only one of the two that is ours.
	//
	// Only while Running, and not once a stop has been asked for (the return
	// above has already left): PCSX2 calls none of this while paused, because
	// Execute is not running (see the loop in Run), and the frame a stop cuts
	// short is not one to award anything on. It sits ABOVE the pause handling
	// below on purpose: the frame that has just run did run, even if the
	// overlay opens straight after it.
	//
	// AND ONLY ONCE THE GAME'S OWN PROGRAM HAS STARTED, as PCSX2 has it.
	// Achievements::FrameUpdate at the pin calls rc_client_do_frame only
	// `if (VMManager::Internal::HasBootedELF())` and rc_client_idle before
	// that, so no achievement is ever tested against the BIOS's memory while
	// it boots. Fast boot skips the splash but not the BIOS loading the disc,
	// and that is measured at about three and a half seconds of frames on the
	// reference console, every one of them a chance for a condition written
	// against the game's RAM to match bytes that are not the game's. The
	// frontend runs rc_client_idle on its own thread, so nothing is owed here
	// before the ELF; the callback simply starts with the game's first frame.
	if (VMManager::GetState() == VMState::Running && VMManager::Internal::HasBootedELF())
		RunFrameCallback();

	// Pause and resume. Applied here rather than from the calling thread
	// because VMManager::SetState expects the CPU thread, and this is the one
	// place the emulator lends it out.
	const VMState state = VMManager::GetState();
	if (s_paused.load() && state == VMState::Running)
		VMManager::SetState(VMState::Paused);
	else if (!s_paused.load() && state == VMState::Paused)
		VMManager::SetState(VMState::Running);
}

// ---------------------------------------------------------------------------
// The application shell: PCSX2 asking to close, which here means leaving the
// game. The rest of this group (the game list, capture, achievements' sign
// in) answers nothing and is in CabinetPS2HostStubs.cpp.
// ---------------------------------------------------------------------------

void Host::RequestExitApplication(bool allow_confirm)
{
	// PCSX2 asking the whole application to quit means, here, leave the game.
	// The console itself is not something a game may close.
	s_stop_requested.store(true);
}

void Host::RequestExitBigPicture()
{
	// There is no Big Picture mode to leave; the console IS the big picture.
	s_stop_requested.store(true);
}

// ---------------------------------------------------------------------------
// CabinetPS2 — the far smaller surface pointing the other way.
// ---------------------------------------------------------------------------

namespace
{
	bool ConfigureFolders(const CabinetPS2::Config& config, std::string* error)
	{
		EmuFolders::AppRoot = Path::GetDirectory(FileSystem::GetProgramPath());

		// PCSX2 REFUSES TO START WITHOUT ITS RESOURCES FOLDER and says so in one
		// line. Checking here rather than letting it fail later is the
		// difference between a clear message and a puzzle.
		if (!FileSystem::DirectoryExists(config.resources_dir.c_str()))
		{
			*error = fmt::format("PCSX2's resources folder is not at {}. It holds the game "
								 "database, the fonts and the GS shaders, and PCSX2 will not "
								 "start without it.",
				config.resources_dir);
			return false;
		}

		// EVERY FOLDER IS SET EXPLICITLY, AND THAT IS THE POINT.
		//
		// PCSX2 normally derives all of these from one data root. Doing that
		// here put its memory cards, its log and its shader cache under the
		// BIOS directory, which on the reference console is root-owned — so it
		// silently failed to create any of them and then failed to create a
		// card, in three log lines that each looked like a different problem.
		//
		// More importantly the card belongs in the PER-GAME, PER-USER SAVE
		// DIRECTORY, because that is where the rest of this console already
		// expects it: `catalog::saveFiles` says a PlayStation 2 card is
		// `<stem>.ps2` in the save directory, and `filesave.cpp` restores it
		// before launch, captures it after, refuses to upload an unformatted
		// one, and files it on the server under the name Cabinet's Mac uses.
		// Setting this one string is what makes all of that work unchanged.
		EmuFolders::Resources = config.resources_dir;
		EmuFolders::Bios = config.bios_dir;
		EmuFolders::MemoryCards = config.memcards_dir;

		// Everything PCSX2 writes that nobody has to keep. DataRoot is set as
		// well because a few places still read it directly.
		EmuFolders::DataRoot = config.scratch_dir;
		EmuFolders::Cache = Path::Combine(config.scratch_dir, "cache");
		EmuFolders::Logs = Path::Combine(config.scratch_dir, "logs");
		EmuFolders::Snapshots = Path::Combine(config.scratch_dir, "snaps");
		EmuFolders::Savestates = Path::Combine(config.scratch_dir, "sstates");
		EmuFolders::Covers = Path::Combine(config.scratch_dir, "covers");
		EmuFolders::Cheats = Path::Combine(config.scratch_dir, "cheats");
		EmuFolders::Patches = Path::Combine(config.scratch_dir, "patches");
		EmuFolders::Textures = Path::Combine(config.scratch_dir, "textures");
		EmuFolders::GameSettings = Path::Combine(config.scratch_dir, "gamesettings");
		EmuFolders::InputProfiles = Path::Combine(config.scratch_dir, "inputprofiles");
		EmuFolders::Videos = Path::Combine(config.scratch_dir, "videos");
		EmuFolders::UserResources = Path::Combine(config.scratch_dir, "resources");
		EmuFolders::Settings = config.scratch_dir;
		EmuFolders::DebuggerLayouts = Path::Combine(config.scratch_dir, "debuggerlayouts");
		EmuFolders::DebuggerSettings = Path::Combine(config.scratch_dir, "debuggersettings");
		return true;
	}
} // namespace

// Whether the one PS2 thread has done PCSX2's once-per-process setup.
static bool s_cpu_ready = false;

// THE GAME ITSELF, on the one PS2 thread (see CabinetPS2::Run below).
static bool RunOnCPUThread(const CabinetPS2::Config& config, std::string* error)
{
	s_config = config;
	s_stop_requested.store(false);
	// A game left from the pause menu was paused when it stopped; the next
	// one must not start that way.
	s_paused.store(false);
	s_widescreen_patch.store(false);
	s_frames.store(0);
	s_dumped = 0;

	if (!ConfigureFolders(config, error))
		return false;

	const char* hw_error = nullptr;
	if (!VMManager::PerformEarlyHardwareChecks(&hw_error))
	{
		*error = hw_error ? hw_error : "this machine cannot run PCSX2";
		return false;
	}

	// The font. PCSX2's ImGui layer will not initialise without one, and the
	// failure is a null dereference rather than a message.
	{
		const std::string roboto =
			Path::Combine(EmuFolders::Resources, "fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf");
		auto data = FileSystem::MapBinaryFileForRead(roboto.c_str());
		if (data.empty())
		{
			*error = fmt::format("PCSX2's font is missing at {}", roboto);
			return false;
		}
		std::vector<ImGuiManager::FontInfo> fonts;
		ImGuiManager::FontInfo fi{};
		fi.data = data;
		fi.exclude_ranges = {};
		fi.face_name = nullptr;
		fi.is_emoji_font = false;
		fonts.push_back(fi);
		ImGuiManager::SetFonts(std::move(fonts));
	}

	// ONCE PER PROCESS. PCSX2 asserts if the base layer is set twice, and it
	// was set on every start, so the second PS2 game in one run of the
	// console aborted it (found on the TV 2026-09-27). The layer is the same
	// object every time; what changes is its contents, cleared and written
	// again below for this game.
	s_settings.Clear();
	if (!Host::Internal::GetBaseSettingsLayer())
		Host::Internal::SetBaseSettingsLayer(&s_settings);
	VMManager::SetDefaultSettings(s_settings, true, true, true, true, true);

	// --- what CabinetOS overrides, and why each one ---

	// Vulkan. The A9 has it, PCSX2 supports it natively, and it is what the
	// frontend's own host already speaks. Open question 20.
	s_settings.SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(GSRendererType::VK));
	// THE UPSCALE, AND WHAT THE NUMBERS MEAN ON A 4K TELEVISION.
	//
	// A PlayStation 2 renders 640x448. The multiplier is against that, so on a
	// 3840x2160 panel showing a 4:3 picture — 2880x2160 of actual screen — the
	// arithmetic is 2160/448 = 4.8. **FIVE IS THE FIRST VALUE THAT IS GENUINELY
	// 4K AND SIX CLEARS IT**; anything below is being stretched by the panel.
	s_settings.SetFloatValue("EmuCore/GS", "upscale_multiplier", s_config.upscale);

	// Anisotropic filtering. PCSX2 defaults it to 0, which is off.
	s_settings.SetIntValue("EmuCore/GS", "MaxAnisotropy", s_config.anisotropy);

	// WIDESCREEN AND NO-INTERLACING PATCHES ON (MMagTech, 2026-10-02, #217).
	// They come from patches.zip in the resources folder, which the PCSX2 team
	// writes and checks game by game; build-pcsx2.sh packs it at a pinned
	// commit. Each switches on, by name, only the patch a game has: a game
	// with none is left exactly as it was, and its picture changes shape only
	// when its own patch asks (Patch.cpp), which the frame readback above then
	// follows. The fixes games need to run are GameIndex.yaml's, not these.
	// The player can turn widescreen off for one game in the pause menu
	// (#368): some patches widen the 3D view and leave the HUD stretched.
	s_settings.SetBoolValue("EmuCore", "EnableWideScreenPatches", config.widescreen);
	s_settings.SetBoolValue("EmuCore", "EnableNoInterlacingPatches", true);

	// PCSX2's own input is off entirely, and stays off. Every pad on this
	// console reaches a game through the frontend, and a second path onto the
	// same hardware is how one button ends up doing two things.
	s_settings.SetBoolValue("InputSources", "SDL", false);
	s_settings.SetBoolValue("InputSources", "XInput", false);
	// `::Pad` IS PCSX2'S, AND THE LEADING COLONS ARE NOT DECORATION. This
	// function is inside `CabinetPS2`, which has a `Pad` of its own — the
	// controller struct the frontend fills in — so an unqualified `Pad::`
	// resolves to that one and fails to compile. It failed exactly once, here,
	// which is the good outcome; the bad one is a name that resolves to the
	// wrong thing and still builds.
	::Pad::ClearPortBindings(s_settings, 0);
	s_settings.ClearSection("Hotkeys");

	// Audio is the frontend's, as it is for every libretro core.
	// Null rather than absent: it is a real backend that produces silence, so
	// the emulator runs correctly rather than pretending.
	// "SDL" NAMES CABINET'S STREAM HERE, NOT SDL'S. The patch in
	// cores/build-pcsx2.sh points that case at CabinetCreateAudioStream, so
	// SPU2 fills a buffer the frontend drains rather than opening a device.
	// Null would be a real backend that produces silence; this one produces
	// sound and gives it to the console.
	//
	// **THE KEY IS `Backend`, AND GETTING IT WRONG IS SILENT.** An earlier
	// version of this line wrote `OutputModule`, which PCSX2 does not read, so
	// the setting had no effect and SPU2 quietly used its own default. The
	// emulator ran perfectly and no sound ever reached the frontend. That is
	// the same class of fault as an unanswered libretro core option, which
	// this project already has a rule about: a key nobody reads is not the
	// value you asked for, it is whatever the default happens to be.
	s_settings.SetStringValue("SPU2/Output", "Backend", "SDL");

	// THE MEMORY CARD, AND ITS NAME IS THE WHOLE POINT.
	//
	// One card, slot 1, named by the frontend after the rom — `cabinet-604.ps2`
	// for Burnout 3. **RomM matches a save row BY FILENAME ALONE**, so that
	// string is the difference between a card the Mac already reads and an
	// orphan row nothing will ever find again. The frontend owns the name;
	// this layer only places it.
	//
	// Slot 2 is DISABLED rather than enabled-and-empty, which is what an
	// earlier version of this did. An enabled slot with no filename is a card
	// PCSX2 names for itself, belonging to no rom, that cannot be synced —
	// exactly the arrangement Cabinet's own PS2MemoryCard.swift rejects and
	// the one the libretro core falls into through
	// `pcsx2_shared_memory_cards`.
	//
	// An empty name means NO CARD AT ALL, which is what the probe uses. A game
	// that cannot find a card says so and carries on; a game that writes to
	// the wrong one is a save nobody can get back.
	s_settings.SetBoolValue("MemoryCards", "Slot1_Enable", !config.memory_card.empty());
	s_settings.SetStringValue("MemoryCards", "Slot1_Filename", config.memory_card.c_str());
	s_settings.SetBoolValue("MemoryCards", "Slot2_Enable", false);
	s_settings.SetStringValue("MemoryCards", "Slot2_Filename", "");
	// Multitap cards would be four more of the same problem.
	for (u32 port = 1; port <= 2; port++)
	{
		for (u32 slot = 2; slot <= 4; slot++)
		{
			s_settings.SetBoolValue(
				"MemoryCards", fmt::format("Multitap{}_Slot{}_Enable", port, slot).c_str(), false);
		}
	}

	// PCSX2's on-screen messages are its frontend talking to its own users in
	// the middle of somebody else's. The log keeps them; the television does not.
	s_settings.SetBoolValue("EmuCore/GS", "OsdShowFPS", false);
	s_settings.SetBoolValue("EmuCore/GS", "OsdShowResolution", false);
	s_settings.SetBoolValue("EmuCore/GS", "OsdShowGSStats", false);
	s_settings.SetBoolValue("EmuCore/GS", "OsdShowMessages", false);
	// AND THE REST OF IT, WHICH NOBODY SAW UNTIL PCSX2 HAD A WINDOW (#226).
	// Surfaceless, its overlay was drawn nowhere; presenting to the
	// television, it is drawn on the game. The pause icon (Indicators) is on
	// by PCSX2's default and would sit over the console's own pause menu.
	s_settings.SetBoolValue("EmuCore/GS", "OsdShowIndicators", false);
	s_settings.SetBoolValue("EmuCore/GS", "OsdShowVideoCapture", false);
	s_settings.SetBoolValue("EmuCore/GS", "OsdShowInputRec", false);
	s_settings.SetIntValue("EmuCore/GS", "OsdMessagesPos", static_cast<int>(OsdOverlayPos::None));
	s_settings.SetIntValue("EmuCore/GS", "OsdPerformancePos", static_cast<int>(OsdOverlayPos::None));

	s_settings.SetBoolValue("Logging", "EnableSystemConsole", true);
	s_settings.SetBoolValue("Logging", "EnableTimestamps", true);
	s_settings.SetBoolValue("Logging", "EnableVerbose", config.verbose_log);

	// VSYNC ON, IN THE QUEUE, WHEN PCSX2 HAS A WINDOW (#226). gamescope never
	// tears, so with vsync off a picture handed over between two refreshes
	// waits in a one-picture mailbox, and the next one replaces it: PCSX2 at
	// 59.94 against the television's 60 drifts in and out of step, and 56
	// pictures a second reached the screen of the 60 it drew (Burnout 3, A9,
	// frames.py, 2026-10-04). Vsync on, the pictures queue and none are lost:
	// 59.8 a second. VsyncEnable alone gives the mailbox again unless PCSX2
	// times itself to the screen (GetEffectiveVSyncMode), so the mailbox is
	// turned off as well. Not timed to the screen (SyncToHostRefreshRate):
	// that runs the game 0.1% fast, and its sound would build up behind the
	// picture in a stream nothing stretches.
	if (Windowed())
	{
		s_settings.SetBoolValue("EmuCore/GS", "VsyncEnable", true);
		s_settings.SetBoolValue("EmuCore/GS", "DisableMailboxPresentation", true);
	}

	if (config.unlimited)
	{
		s_settings.SetBoolValue("EmuCore/GS", "FrameLimitEnable", false);
		s_settings.SetIntValue("EmuCore/GS", "VsyncEnable", 0);
	}

	// ANALOG MODE ON. A DualShock 2 boots in digital mode and the sticks do
	// nothing until the ANALOG button is pressed — which on a console nobody
	// can press, because the frontend does not draw one. The libretro core has
	// the same trap under a different name, `pcsx2_analog_mode1`, which
	// defaults to OFF and reads as dead sticks; it is already in
	// catalog::optionOverrides for exactly this reason.
	for (u32 port = 0; port < 2; port++)
	{
		s_settings.SetStringValue(fmt::format("Pad{}", port + 1).c_str(), "Type", "DualShock2");
		s_settings.SetBoolValue(fmt::format("Pad{}", port + 1).c_str(), "AnalogLight", true);
		s_settings.SetBoolValue(fmt::format("Pad{}", port + 1).c_str(), "AnalogAuto", true);
	}

	s_settings.SetBoolValue("EmuCore", "EnableFastBoot", config.fast_boot);

	// THE BIOS, BY THE GAME'S REGION (#416). With no name PCSX2 takes the
	// first BIOS in the folder, whatever the game. The layer was cleared
	// above, so one game's choice never carries into the next.
	if (const std::string bios = CabinetPS2::ChooseBios(config.bios_dir, config.disc_code); !bios.empty())
		s_settings.SetStringValue("Filenames", "BIOS", bios.c_str());

	VMManager::Internal::LoadStartupSettings();

	// AND AGAIN, BECAUSE LoadStartupSettings RE-DERIVES THEM. It reads the
	// folder names out of the settings layer, which is deliberately empty
	// here, and puts everything back under DataRoot — quietly undoing the
	// block above. Setting them twice is not belt and braces; the second one
	// is the one that survives.
	if (!ConfigureFolders(config, error))
		return false;

	EmuFolders::EnsureFoldersExist();

	// ONCE, ON THIS THREAD, FOR THE LIFE OF THE PROCESS. It allocates the
	// emulated machine's memory and installs PCSX2's page fault handler,
	// which may only be installed once: set up again for a second game, it
	// aborted the console (found 2026-09-27). pcsx2-qt does the same: one
	// CPU thread, initialised once, many VMs created and destroyed on it.
	if (!s_cpu_ready)
	{
		if (!VMManager::Internal::CPUThreadInitialize())
		{
			*error = "PCSX2's CPU thread would not initialise";
			return false;
		}
		s_cpu_ready = true;
	}

	VMManager::ApplySettings();

	VMBootParameters params;
	params.filename = config.disc_path;
	params.fast_boot = config.fast_boot;

	bool ok = false;
	if (VMManager::Initialize(params) == VMBootResult::StartupSuccess)
	{
		if (config.unlimited)
			VMManager::SetLimiterMode(LimiterModeType::Unlimited);

		VMManager::SetState(VMState::Running);
		// PAUSED IS NOT FINISHED. This loop used to run while the state was
		// Running and shut the VM down as soon as it was anything else, so
		// opening the pause menu ended the game and left the console showing
		// its last frame (found on the TV 2026-09-27, there since PS2 first
		// ran in the console). Now it follows pcsx2-qt's EmuThread::run:
		// Running executes, Paused waits, Resetting resets, Stopping (or
		// anything else) leaves. While paused, Execute is not running, so
		// PumpMessagesOnCPUThread is not called and cannot apply a resume or
		// a stop; that is done here instead.
		for (;;)
		{
			const VMState state = VMManager::GetState();
			if (state == VMState::Running)
			{
				VMManager::Execute();
			}
			else if (state == VMState::Paused)
			{
				// A SCREENSHOT OVER THE PAUSE MENU (#226): nothing presents
				// while paused, so the readback is queued onto the GS thread
				// from here, the thread that may feed it.
				if (s_capture_requested.exchange(false))
					MTGS::RunOnGSThread(ReadBackFrame);
				if (s_stop_requested.load())
					VMManager::SetState(VMState::Stopping);
				else if (!s_paused.load())
					VMManager::SetState(VMState::Running);
				else
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			else if (state == VMState::Resetting)
			{
				VMManager::Reset();
			}
			else
			{
				break;
			}
		}

		VMManager::Shutdown(false);
		ok = true;
	}
	else
	{
		*error = fmt::format("PCSX2 would not boot {}", config.disc_path);
	}

	// No CPUThreadShutdown: the thread and what it set up stay for the next
	// game, as in pcsx2-qt, and go with the process.
	s_running.store(false);
	return ok;
}

namespace
{
	// ONE PS2 THREAD FOR THE PROCESS. Each game used to run on a new thread
	// that set PCSX2 up and tore it down again, which PCSX2 does not support
	// (its CPU-thread state is per process). Run now hands the game to this
	// thread and waits, so callers still see one blocking call per game.
	std::mutex s_job_lock;
	std::condition_variable s_job_cv;
	const CabinetPS2::Config* s_job = nullptr;
	bool s_job_done = false;
	bool s_job_ok = false;
	std::string s_job_error;
	bool s_cpu_thread_started = false;
	// AND CLOSED WHEN THE PROCESS ENDS. Its threads (the GS thread among
	// them) must be joined before PCSX2's statics are destroyed, or it
	// asserts on the way out: every console shutdown and restart would end
	// in an abort. So an atexit handler, registered when the thread starts
	// and therefore run before those destructors, asks it to shut PCSX2's
	// CPU thread down and waits, a few seconds at most.
	bool s_quit = false;
	bool s_quit_done = false;

	void CloseCPUThread()
	{
		std::unique_lock<std::mutex> lock(s_job_lock);
		s_quit = true;
		s_job_cv.notify_all();
		s_job_cv.wait_for(lock, std::chrono::seconds(5), [] { return s_quit_done; });
	}
} // namespace

bool CabinetPS2::Run(const Config& config, std::string* error)
{
	std::unique_lock<std::mutex> lock(s_job_lock);
	if (!s_cpu_thread_started)
	{
		s_cpu_thread_started = true;
		std::thread([] {
			for (;;)
			{
				std::unique_lock<std::mutex> l(s_job_lock);
				s_job_cv.wait(l, [] { return s_job != nullptr || s_quit; });
				if (s_quit)
				{
					if (s_cpu_ready)
						VMManager::Internal::CPUThreadShutdown();
					s_quit_done = true;
					s_job_cv.notify_all();
					return;
				}
				const Config job = *s_job;
				l.unlock();
				std::string err;
				const bool ok = RunOnCPUThread(job, &err);
				l.lock();
				s_job = nullptr;
				s_job_ok = ok;
				s_job_error = err;
				s_job_done = true;
				s_job_cv.notify_all();
			}
		}).detach();
		std::atexit(CloseCPUThread);
	}
	s_job = &config;
	s_job_done = false;
	s_job_cv.notify_all();
	s_job_cv.wait(lock, [] { return s_job_done; });
	if (!s_job_ok && error)
		*error = s_job_error;
	return s_job_ok;
}

bool CabinetPS2::TakeFrame(Frame* out, uint64_t since)
{
	if (s_frame_serial.load() <= since)
		return false;

	std::lock_guard<std::mutex> lock(s_frame_lock);
	if (s_frame.serial <= since || s_frame.width == 0)
		return false;

	*out = s_frame; // A copy. See the header for why it is not a loan.
	return true;
}

void CabinetPS2::SetWindow(const char* display, unsigned long window, unsigned width, unsigned height)
{
	std::lock_guard<std::mutex> lock(s_window_lock);
	s_window_display = display ? display : "";
	s_window = window;
	s_window_width = width;
	s_window_height = height;
}

bool CabinetPS2::CaptureFrame(unsigned timeout_ms)
{
	if (!s_running.load())
		return false;
	const uint64_t before = s_frame_serial.load();
	s_capture_requested.store(true);
	const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	while (std::chrono::steady_clock::now() < until)
	{
		if (s_frame_serial.load() > before)
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	s_capture_requested.store(false);
	return false;
}

void CabinetPS2::RequestStop()
{
	s_stop_requested.store(true);
}

void CabinetPS2::SetPaused(bool paused)
{
	s_paused.store(paused);
}

bool CabinetPS2::IsRunning()
{
	return s_running.load();
}

bool CabinetPS2::HasWidescreenPatch()
{
	return s_running.load() && s_widescreen_patch.load();
}

uint8_t* CabinetPS2::Memory(unsigned region, size_t* size)
{
	*size = 0;

	// eeMem is allocated once, by CPUThreadInitialize, and lives as long as
	// the process; it is NOT freed between games (see Run). So the pointer
	// would stay readable after a game ends, but what is in it is then the
	// last game's memory or nothing, and achievements tested against that are
	// wrong rather than absent. Null when no game is running is the honest
	// answer, and it is also what the frontend's contract says.
	if (!s_running.load() || !eeMem)
		return nullptr;

	// THE SAME TWO POINTERS AND THE SAME SIZES PCSX2'S OWN CLIENT READS. At
	// the pinned v2.8.2, Achievements::ClientReadMemory is:
	//
	//   (address < Ps2MemSize::ExposedRam)
	//       ? &eeMem->Main[address]
	//       : &eeMem->Scratch[address - Ps2MemSize::ExposedRam]
	//
	// bounded by GetExposedEEMemorySize(), which is ExposedRam + Scratch. Its
	// comment says why the scratchpad is there at all: "RA uses a fake memory
	// map with the scratchpad directly above physical memory". So region 1
	// starts, in RetroAchievements' numbering, at region 0's size.
	//
	// ExposedRam rather than the 32 MB constant, as PCSX2 has it: it is 32 MB
	// unless the devkit's 128 MB is switched on (EmuCore/CPU ExtraMemory,
	// memSetExtraMemMode), which this console never sets, so region 1 is at
	// 0x2000000. Were it ever set, eeMem->Main is 128 MB long and the
	// scratchpad moves up with it, exactly as it would in PCSX2.
	switch (region)
	{
		case 0:
			*size = Ps2MemSize::ExposedRam;
			return eeMem->Main;
		case 1:
			*size = Ps2MemSize::Scratch;
			return eeMem->Scratch;
		default:
			return nullptr;
	}
}

void CabinetPS2::SetFrameCallback(void (*callback)(void* user), void* user)
{
	// Said once each way, so a log shows whether achievements were watching a
	// game at all. The frontend sets it when a game starts and clears it when
	// it stops, so this is two lines a game, not one a frame. stderr for the
	// reason OnVMStarted gives: PCSX2's Console is not set up before a game.
	std::fprintf(stderr, "[ps2] achievements frame hook %s\n", callback ? "set" : "cleared");

	// From inside the callback, on the CPU thread, the lock is already ours.
	if (t_in_frame_callback)
	{
		s_frame_callback = callback;
		s_frame_callback_user = user;
		return;
	}

	std::lock_guard<std::mutex> lock(s_frame_callback_lock);
	s_frame_callback = callback;
	s_frame_callback_user = user;
}

CabinetPS2::Metrics CabinetPS2::GetMetrics()
{
	Metrics m{};
	m.frames = s_frames.load();

	// THE READBACK FIGURES ARE ABOVE THE EARLY RETURN ON PURPOSE. They describe
	// the run that happened, not a machine that is still going, so asking for
	// them after the game has stopped — which is exactly when a summary is
	// printed — must not answer zero. The first version had them below and
	// reported "avg 0us WORST 0us" for a run that had plainly done the work.
	m.readback_us = s_readback_us.load();
	m.readback_worst_us = s_readback_worst.load();
	m.readback_over_budget = s_readback_over.load();

	// These two are live readings and are honestly zero when nothing is running.
	if (!s_running.load())
		return m;

	m.fps = PerformanceMetrics::GetFPS();
	m.speed = PerformanceMetrics::GetSpeed();
	return m;
}
