// What this machine's graphics hardware can actually do, asked once at startup.
//
// WHY THIS EXISTS. The console has to run on two machines that differ in the
// one way that matters here: the A9 Max has a Radeon with Vulkan, and the test
// VM has no Vulkan at all — gamescope already rejects it for exactly that
// reason. A host that assumed Vulkan would not run on the machine this project
// is developed on, and a host that assumes GLES cannot play PlayStation 2.
//
// So capability is DISCOVERED, never assumed, which is the rule *Hardware*
// already states and what docs/PROJECT.md open question 20 asks for. This is
// the one place that goes and looks.
//
// WHAT IT IS FOR, IN PRODUCT TERMS. Several of the cores draw with hardware
// and take whatever the host offers. The two heavy systems do not
// have that luxury: PCSX2's graphics synthesiser refuses an OpenGL ES context
// outright — measured on the A9, `OpenGL is not supported. Only OpenGL 3.2 was
// found` — and Dolphin renders from a thread of its own, which GLES cannot
// serve because a GL context belongs to one thread at a time. Vulkan has no
// current context and no thread affinity, which is not a workaround for those
// two faults, it is the absence of the thing that causes them.
//
// WHICH GPU, WHEN THERE ARE TWO (#659): THE ONE THE SCREEN IS ON. The UI draws
// in GL on the GPU the screen is wired to (under gamescope, the one its
// Xwayland draws with), and a Vulkan core's picture reaches the UI's texture
// by importing the memory the core drew into (vkbridge.cpp), which Vulkan
// defines only within one device. The rule until then, "prefer a discrete
// card", put every Vulkan core of a laptop with two GPUs on the card the
// screen was not on, and the import took the console down. So once the
// frontend has its GL context it tells this file the context's device UUID
// (screenIs), and the device is the one whose UUID matches: for the cores,
// and for every emulator the frontend tells a device (Cemu, Xenia, sm2-emu,
// xemu). No match, or no way to ask: the old order, a discrete card first.
// A machine with one GPU lands on it every way, so nothing changes there. A
// `[gpu]` line in the log says which and why. The emulators that pick a
// device for themselves draw in windows of their own, where a picture made
// on one GPU and shown on another is the ordinary laptop case, not this
// fault; they are not told.
//
// NOTHING HERE LINKS THE VULKAN LOADER. It is opened with dlopen at runtime, so
// this binary still starts on a machine with no Vulkan and says so, rather than
// failing to load with a missing library. That also means `ldd` cannot see it —
// the same blind spot `net.cpp`'s nmcli has — so `build_files/build.sh` asserts
// libvulkan.so.1 is in the image the way it already asserts nmcli and pkcheck.

#pragma once

#include <cstdint>
#include <string>

namespace cab::gpu {

// What a Vulkan probe found. `available` is the only field worth branching on;
// the rest is for the report, and for saying WHY when the answer is no.
struct VulkanCaps {
    bool available = false;
    // Why not, when not. One line, in words that name the thing to change:
    // "no libvulkan.so.1 on this machine" is a different problem from "a
    // Vulkan driver is present and reports no devices", and only the second
    // means the GPU.
    std::string reason;

    std::string deviceName;      // "AMD Radeon 890M Graphics (RADV STRIX1)"
    // Its place in the loader's list, for an emulator that is told a device
    // by number rather than by name (Xenia's `vulkan_device`). The loader
    // sorts the list the same way in every process on the machine.
    int deviceIndex = -1;
    // Its VkPhysicalDeviceIDProperties::deviceUUID, 32 lower-case hex digits,
    // for one told a device by UUID (Cemu's `vkDevice`). Empty when unknown.
    std::string deviceUuid;
    // Why this device and not another, in the `[gpu]` line's words: "the
    // only GPU here", "the screen is on it", or the old order with what kept
    // the screen's GPU from being matched. Empty while nothing is usable.
    std::string why;
    uint32_t apiVersion = 0;     // VK_MAKE_API_VERSION packed
    bool discrete = false;
    uint32_t vendorId = 0;       // PCI: 0x1002 AMD, 0x10de NVIDIA, 0x8086 Intel

    // HOW BIG AND HOW FAST, for picture quality's machine class (#63 phase
    // 2, quality.cpp). Zero when the hardware does not say. AMD only today:
    // its driver reports the shader units it has switched on
    // (VK_AMD_shader_core_properties2) and the kernel its top clock
    // (pp_dpm_sclk). NVIDIA reports units but not a clock, Intel neither;
    // both get the class for unknown hardware until there is one of each to
    // measure (MMagTech, 2026-10-04).
    uint32_t computeUnits = 0;   // 16 on the A9's Radeon 890M
    uint32_t topClockMhz = 0;    // 2900 on the A9

    // THE INTEROP EXTENSIONS, AND WHY THEY DECIDE THE WHOLE SHAPE.
    //
    // The UI is drawn in OpenGL ES and every screen that exists was looked at
    // on a television through it. A core rendering with Vulkan produces a
    // VkImage, and something has to get that picture into the texture the UI
    // already knows how to draw.
    //
    // The Linux answer is a dmabuf: Vulkan exports the image as a file
    // descriptor and EGL imports the same memory as a texture, with no copy
    // and no readback. Measured present on the A9 —
    // EGL_EXT_image_dma_buf_import, EGL_EXT_image_dma_buf_import_modifiers and
    // EGL_MESA_image_dma_buf_export are all there on radeonsi.
    //
    // Without a way out a Vulkan core could run and could never be SEEN, so
    // the fd extension is part of "is Vulkan available", not a detail below
    // it. The dmabuf one is needed only by the EGL route; the preferred route
    // (a GL memory object over an opaque fd) does without it (#489, EMU-02).
    bool externalMemoryFd = false;      // VK_KHR_external_memory_fd
    bool externalMemoryDmaBuf = false;  // VK_EXT_external_memory_dma_buf
    bool drmFormatModifier = false;     // VK_EXT_image_drm_format_modifier
};

// Probed once, on first call, and cached. Safe to call before SDL is up: it
// opens no window and touches no display.
const VulkanCaps& vulkan();

// THE GPU THE SCREEN IS ON, as the GL context's device UUID in the form
// deviceUuid has (vk::screenDeviceUuid), or empty when GL cannot say. Told
// by startup.cpp once the context exists, before anything reads the machine
// class; told to a probe that has already answered, it chooses again and
// says so if the answer moved.
void screenIs(const std::string& glDeviceUuid);

// 16 UUID bytes as 32 lower-case hex digits, the one form deviceUuid and
// screenIs use, so GL's and Vulkan's compare as strings.
std::string uuidString(const uint8_t* bytes);

// Human-readable, for `--gpu-probe`. Prints what was found and, when Vulkan is
// not usable, the one line that says which part is missing.
//
// A PROBE THAT PRINTS ONE NUMBER WITHOUT SAYING WHAT IT ASKED IS THE THIRD
// LYING INSTRUMENT THIS PROJECT HAS HAD TO FIX. So this says what it looked
// for as well as what it found.
void report();

// The choice alone, on made-up devices, no GPU needed (run from frontend/
// as every self-test is): --gpu-choice-test.
int test();

// Version numbers as "1.4.341", for the report.
std::string versionString(uint32_t packed);

// The loader's one entry point, or null when Vulkan is not usable here.
//
// Shared rather than opened twice: dlopen'ing an ICD a second time is a way to
// end up with two copies of a driver's thread-local state in one process, and
// the whole point of probing once is that there is a single answer in this
// process to what the hardware can do. `vkhost.cpp` builds everything else out
// of this pointer.
void* instanceProcAddr();

}  // namespace cab::gpu
