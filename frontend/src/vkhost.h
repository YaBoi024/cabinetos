// The Vulkan half of the core host.
//
// WHAT THIS BUYS THE PRODUCT. PlayStation 2 and GameCube — 85 games on the
// reference server, the largest tier this console cannot play. Both were
// measured on the A9 running inside the existing host and both failed for the
// same reason, which is not a bug in either of them:
//
//   * PCSX2's graphics synthesiser refuses an OpenGL ES context outright.
//     "OpenGL is not supported. Only OpenGL 3.2 was found", and then it draws
//     nothing while the emulated machine runs perfectly and makes sound.
//   * Dolphin renders from a thread of its own. It booted Mario Kart, ran
//     fifty seconds of emulated time and produced correct audio while the
//     whole 3840x2160 frame stayed at the letterbox glow, because a GL context
//     belongs to ONE THREAD and the frontend's thread is not the one drawing.
//
// VULKAN IS NOT A WORKAROUND FOR THOSE TWO, IT IS THE ABSENCE OF WHAT CAUSES
// THEM. There is no current context in Vulkan and no thread affinity: a device
// and a queue are objects any thread may use. That is the whole reason a real
// emulator can be hosted through it and cannot be hosted through GLES, and it
// is why this is the right answer rather than the cheap one.
//
// It also serves three things already on the books. Open question 20 asks for
// exactly this and names them: PS3 needs it (RPCS3's good renderer is Vulkan),
// N64 wants it (parallel-RDP is already compiled into the core here and cannot
// be reached), and Dreamcast benefits (Flycast's Vulkan renderer is the faster
// one).
//
// ---------------------------------------------------------------------------
//
// THE UI STAYS ON OPENGL ES, AND THAT IS A DECISION RATHER THAN A SHORTCUT.
//
// Every screen this console has was drawn in GLES and looked at on a
// television. Rewriting all of it in Vulkan would put every one of them back
// in the air to gain nothing a person could see. Two facts settle it:
//
//   1. The test VM HAS NO VULKAN DEVICE — measured, `--gpu-probe` says "the
//      only Vulkan device here is a software one". A Vulkan-only frontend
//      could not run on the machine this project is developed on.
//   2. The picture can cross between the two APIs with no copy at all. Vulkan
//      exports the image it drew as a dmabuf and EGL imports the same memory
//      as a texture. Measured present on the A9's radeonsi:
//      VK_KHR_external_memory_fd, VK_EXT_external_memory_dma_buf,
//      EGL_EXT_image_dma_buf_import.
//
// So the shape is: cores that want Vulkan get a real Vulkan device, and what
// they draw arrives in the same `GLuint` the UI already knows how to put on
// the screen. `Core::texture()` does not change meaning, and neither does the
// player.
//
// ---------------------------------------------------------------------------
//
// THE PICTURE'S ROUTE, AND WHY THERE IS A BLIT IN IT.
//
//      core's VkImage  ──vkCmdBlitImage──►  our VkImage  ──dmabuf──►  EGLImage
//                                           (exported)                    │
//                                                                    GL texture
//
// The core owns the image it drew and may recreate it whenever its internal
// resolution changes; libretro is explicit that the frontend may not keep the
// pointer. Ours is allocated in 256-point steps and grown when a frame
// outgrows it, not every frame, and is the thing EGL
// holds, because an EGLImage is bound to one piece of memory for its life —
// re-importing every frame would be a syscall and a driver allocation per
// frame. So the blit is what makes the import stable, and it is also where
// the queue-family ownership transfer libretro requires happens.
//
// THE PICTURE OCCUPIES A CORNER OF THE TARGET, exactly as it does for the
// GLES hardware path, which is why `frameUV` exists there and here.

#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <string>

// Declared rather than included, so that core.h's users do not all pull in
// the Vulkan headers. See vkhost.cpp.
struct retro_hw_render_context_negotiation_interface_vulkan;
struct retro_hw_render_interface_vulkan;

namespace cab::vk {

// Whether a Vulkan core could be served at all: a usable device AND the two
// export extensions. This is `gpu::vulkan().available` and nothing more — it
// asks no questions of its own so that there is exactly one answer in the
// process to "can this machine do Vulkan".
bool available();

// THE GPU THE SCREEN IS ON, as the GL context's device UUID in gpu.h's form,
// or empty when this GL cannot say. Needs the context current: under
// gamescope it is on the GPU its Xwayland draws with, the screen's. gpu.cpp
// matches it against each Vulkan device (gpu::screenIs), and that match is
// what makes the memory object route legal at all: an opaque fd is defined
// only within one device (#659). From GL_EXT_memory_object, which the route
// already needs.
std::string screenDeviceUuid();

// What the core handed over with SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE,
// which arrives BEFORE the device is created and is how a core says which
// device and which extensions it needs. Dolphin uses it; a core that does not
// gets a device of our choosing.
//
// Storing rather than acting: the negotiation interface may legitimately
// arrive either side of SET_HW_RENDER, so the device is not built until
// createContext.
void setNegotiation(const retro_hw_render_context_negotiation_interface_vulkan* n);

// Builds the instance, picks the device and creates the queue — asking the
// core first if it gave us a negotiation interface, as the contract requires.
// Also allocates the exported image and imports it into GL, so it must be
// called on the thread that owns the GL context.
//
// Returns false and fills `err` with a line that names what to change.
bool createContext(std::string* err);
void destroyContext();

// Handed to the core through RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE. Null
// until createContext has succeeded, which is the contract: the core may not
// ask for this before context_reset.
const retro_hw_render_interface_vulkan* renderInterface();

// --- Per frame -------------------------------------------------------------

// Copies whatever the core last set into the exported image, so the GL
// texture below holds it. Call on the GL thread, once a frame, after runFor.
//
// Returns false when there is nothing to copy, which is the ordinary state
// before a core's first frame rather than an error.
bool present();

// Forget the picture the core last handed over, so present() draws nothing
// new until the core hands over another: after a state is loaded, when a core
// may have rebuilt its renderer and destroyed that image (Core::loadState).
void forgetCoreImage();

// The picture, in the frontend's own GL context. Zero until the first
// successful present.
GLuint texture();

// Where the picture sits inside texture(), as texture coordinates, with the
// vertical flip already applied. Same contract as the GLES path's frameUV —
// see core.h — so the player needs no second code path.
void frameUV(float& u0, float& v0, float& u1, float& v1);

// The size of the last picture the core handed over, in pixels, for frameUV.
//
// Set by the host from what the core passed to video_refresh, because
// retro_vulkan_image carries no width or height — only a view and the info it
// was created with. Guessing it from the image's extent would be the core's
// ALLOCATION rather than its picture, and those differ the moment a core
// upscales internally.
void setFrameSize(unsigned width, unsigned height);

// How many pictures crossed from Vulkan into GL, and how long that took in
// total. This is the honest cost of the bridge — the import is free, the blit
// and the fence are not — and it exists so the question "does this hurt
// performance" has an answer somebody measured.
void presentCost(uint64_t& frames, double& seconds);

// The same total, split into recording and submitting the copy against waiting
// for the GPU to finish it. They have opposite fixes — one is the copy, the
// other is this host sitting behind work the CORE queued on the same queue —
// so a single number cannot tell you which to go after.
void presentCostSplit(double& recordSeconds, double& waitSeconds);

}  // namespace cab::vk
