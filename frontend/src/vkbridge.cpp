// The bridge from Vulkan to GL: the image a Vulkan core's picture crosses on,
// its GL face (a GL memory object, or an EGLImage from a dmabuf), and
// present, which copies the core's last picture across once a frame. Moved
// out of vkhost.cpp in the restructure (#398); vkhost.cpp builds the device
// and answers the core, and what the two share is in vkhost_state.h.

#include "vkhost.h"

#include <SDL3/SDL.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "gpu.h"
#include "vkhost_state.h"

namespace cab::vk {
namespace state {

// --- The fourcc the exported image is described by -------------------------
//
// Spelled out rather than including <drm_fourcc.h>, which would put libdrm's
// headers in the build for four constants. VK_FORMAT_R8G8B8A8_UNORM stores R
// at the lowest address; DRM names its formats by the little-endian word, so
// the same bytes are ABGR8888. Getting this pair backwards produces a picture
// with the red and blue channels swapped, which looks like a bug in the core.
constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
           (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24);
}
constexpr uint32_t kDrmFormatAbgr8888 = fourcc('A', 'B', '2', '4');
constexpr uint64_t kDrmModifierLinear = 0;

// --- What the core last handed over ----------------------------------------
//
// Copied rather than pointed at. libretro says the retro_vulkan_image pointer
// is valid only until retro_video_refresh returns, and the frontend here reads
// it later, on the GL thread. Keeping the pointer is a use-after-free that
// would show up as a torn or stale picture rather than a crash.
std::mutex gImageLock;
retro_vulkan_image gCoreImage{};
bool gHaveCoreImage = false;
uint32_t gCoreSrcQueueFamily = VK_QUEUE_FAMILY_IGNORED;
std::vector<VkSemaphore> gCoreSemaphores;

// --- The exported image, and its GL face -----------------------------------
VkImage gShared = VK_NULL_HANDLE;
VkDeviceMemory gSharedMemory = VK_NULL_HANDLE;
unsigned gSharedWidth = 0, gSharedHeight = 0;
bool gSharedInitialised = false;  // has it ever been written, i.e. is a layout known
EGLImage gEglImage = EGL_NO_IMAGE;
GLuint gMemoryObject = 0;
GLuint gTexture = 0;
unsigned gFrameWidth = 0, gFrameHeight = 0;

std::string gDescription;

// WHAT THE BRIDGE BETWEEN THE TWO APIS ACTUALLY COSTS, counted rather than
// argued about. The import itself is free — the GL texture and the VkImage are
// the same memory — so the only cost is the blit into the exported image and
// the fence waited on before GL reads it. That is a number, and a number is
// the only thing that can say whether the fence is worth replacing with an
// exported semaphore.
uint64_t gPresentCount = 0;
double gPresentSeconds = 0.0;
// Split, because "the copy is slow" and "we are waiting for the core's own
// frame to finish on the same queue" are different problems with opposite
// fixes, and they add up to the same number.
double gPresentRecordSeconds = 0.0;
double gPresentWaitSeconds = 0.0;

// EGL 1.5's own entry points rather than the KHR ones, because the attribute
// list below is EGLAttrib — the KHR variant takes EGLint, which is 32-bit and
// cannot carry a 64-bit DRM format modifier.
PFNEGLCREATEIMAGEPROC gEglCreateImage = nullptr;
PFNEGLDESTROYIMAGEPROC gEglDestroyImage = nullptr;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC gGlEglImageTargetTexture2D = nullptr;

// GL_EXT_memory_object_fd, AND IT IS THE PRIMARY ROUTE RATHER THAN A FALLBACK.
//
// THE REASON IS WHAT THE CONSOLE ACTUALLY RUNS ON. Under gamescope, SDL picks
// the **x11** video driver — gamescope embeds an Xwayland server — so the GL
// context is GLX and there is no EGL display in the process at all. The first
// version of vkhost.cpp imported the picture as an EGLImage, which worked
// perfectly under SDL's offscreen driver and failed on the television with
// `EGL_NOT_INITIALIZED`, sixty times a second, while a PlayStation 2 game
// played with sound and a black screen.
//
// A GL memory object does not care which windowing API made the context. It
// is also the FASTER route: an opaque handle lets the image keep
// VK_IMAGE_TILING_OPTIMAL, where the dmabuf path has to fall back to linear.
//
// The EGLImage path stays for a machine whose GL lacks these two extensions.
PFNGLCREATEMEMORYOBJECTSEXTPROC gGlCreateMemoryObjects = nullptr;
PFNGLDELETEMEMORYOBJECTSEXTPROC gGlDeleteMemoryObjects = nullptr;
PFNGLIMPORTMEMORYFDEXTPROC gGlImportMemoryFd = nullptr;
PFNGLTEXSTORAGEMEM2DEXTPROC gGlTexStorageMem2D = nullptr;
PFNGLMEMORYOBJECTPARAMETERIVEXTPROC gGlMemoryObjectParameteriv = nullptr;
bool gHaveMemoryObject = false;

// THE DISPLAY IS SDL'S, NOT EGL'S, AND THAT DISTINCTION COST A BLACK SCREEN.
//
// `eglGetCurrentDisplay()` worked under SDL's offscreen driver and returned
// EGL_NO_DISPLAY in the real session under gamescope, so the console booted a
// PlayStation 2 game, made sound, and drew nothing — sixty failed imports a
// second into the journal.
//
// core_hwrender.cpp already had the rule and this did not follow it: "SDL's,
// not EGL's: SDL created this context, and on a driver where the two disagree
// the one that made the context is the one to ask." Same rule, same reason,
// one file along.
EGLDisplay currentDisplay() {
    EGLDisplay fromSdl = reinterpret_cast<EGLDisplay>(SDL_EGL_GetCurrentDisplay());
    EGLDisplay fromEgl = eglGetCurrentDisplay();
    // The default display, which is what SDL itself would have initialised.
    // Asking for it does not create a second one: eglGetDisplay returns the
    // same handle for the same native display, and SDL has already
    // initialised it.
    EGLDisplay fromDefault = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    static bool said = false;
    if (!said) {
        said = true;
        std::fprintf(stderr,
                     "[vulkan] EGL display: SDL says %p, eglGetCurrentDisplay says %p, "
                     "EGL_DEFAULT_DISPLAY is %p, video driver is %s\n",
                     fromSdl, fromEgl, fromDefault,
                     SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");
    }
    if (fromSdl) return fromSdl;
    if (fromEgl != EGL_NO_DISPLAY) return fromEgl;
    return fromDefault;
}

void destroyShared() {
    if (gEglImage != EGL_NO_IMAGE && gEglDestroyImage) {
        gEglDestroyImage(currentDisplay(), gEglImage);
        gEglImage = EGL_NO_IMAGE;
    }
    if (gTexture) {
        glDeleteTextures(1, &gTexture);
        gTexture = 0;
    }
    if (gMemoryObject) {
        gGlDeleteMemoryObjects(1, &gMemoryObject);
        gMemoryObject = 0;
    }
    if (gShared != VK_NULL_HANDLE) {
        gApi.destroyImage(gDevice, gShared, nullptr);
        gShared = VK_NULL_HANDLE;
    }
    if (gSharedMemory != VK_NULL_HANDLE) {
        gApi.freeMemory(gDevice, gSharedMemory, nullptr);
        gSharedMemory = VK_NULL_HANDLE;
    }
    gSharedWidth = gSharedHeight = 0;
    gSharedInitialised = false;
}

// Allocates the one image the picture crosses on, and hands the same memory to
// EGL as a texture.
//
// LINEAR TILING ON THE DMABUF ROUTE, AND THAT IS A MEASURED ANSWER RATHER
// THAN THE EASY ONE. (The memory-object route, preferred, is optimally tiled;
// below.)
//
// The obvious worry is that writing into a linear image is slow, and that an
// optimally-tiled one negotiated through VK_EXT_image_drm_format_modifier
// would be faster. That was BUILT AND MEASURED on the A9 on 2026-09-20, with
// four modifiers agreed between Vulkan and EGL and the driver picking
// 0x200000010401b04, and it is not worth having:
//
//   |                       | linear   | optimal, negotiated |
//   | PS2 native 640x448    | 0.269 ms | 0.247 ms            |
//   | PS2 4x upscale        | 1.323 ms | 1.349 ms            |
//   | wall clock, 1800 fr   | 5.71 s   | 5.89 s              |
//
// Nothing, and slightly worse at the resolution it was supposed to help most.
//
// THE REASON IS THE THING WORTH KEEPING, because it kills both halves of the
// idea. Splitting the cost showed the copy is 0.012 ms of CPU to record and
// submit, and everything else is waiting on a fence — 0.219 ms at native,
// 1.297 ms at 4x. That wait is not this host being slow. It is the frontend
// discovering that the EMULATOR has not finished drawing the frame yet,
// because the core's rendering is queued ahead of our copy on the same queue.
// At a 4x upscale the PlayStation 2 simply takes longer to draw.
//
// So the tiling was never the cost, and neither is the fence: replacing it
// with an exported semaphore would not make the GPU finish sooner, it would
// only let the CPU go and do something else in the meantime — and at that
// moment the CPU's next job is to draw the UI with the picture it is waiting
// for. Both "optimisations" optimise something that is not the bottleneck.
//
// Do not rebuild either of these without a measurement that contradicts the
// table above. The negotiation is about 150 lines and it bought nothing.
bool createShared(unsigned width, unsigned height, std::string* err) {
    destroyShared();

    // THE TWO ROUTES DIFFER IN THE HANDLE AND THEREFORE IN THE TILING.
    //
    // An OPAQUE fd is the driver's own handle, so the image keeps
    // VK_IMAGE_TILING_OPTIMAL and GL is told the same. A dmabuf is a handle
    // anything on the machine can understand, and that generality is paid for
    // in linear tiling. Optimal is both faster and simpler here, which is why
    // the memory-object route is preferred when GL has it.
    const VkExternalMemoryHandleTypeFlagBits handleType =
        gHaveMemoryObject ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
                          : VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkExternalMemoryImageCreateInfo ext{};
    ext.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    ext.handleTypes = handleType;

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.pNext = &ext;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {width, height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = gHaveMemoryObject ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (gApi.createImage(gDevice, &ici, nullptr, &gShared) != VK_SUCCESS) {
        *err = "could not create the image the picture crosses on";
        return false;
    }

    VkMemoryRequirements req{};
    gApi.getImageMemoryRequirements(gDevice, gShared, &req);

    VkPhysicalDeviceMemoryProperties mem{};
    gApi.getMemoryProperties(gGpu, &mem);
    uint32_t typeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if (!(req.memoryTypeBits & (1u << i))) continue;
        if (mem.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            typeIndex = i;
            break;
        }
        if (typeIndex == UINT32_MAX) typeIndex = i;
    }
    if (typeIndex == UINT32_MAX) {
        *err = "no memory type on this device can hold an exportable image";
        return false;
    }

    VkExportMemoryAllocateInfo exportInfo{};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    exportInfo.handleTypes = handleType;

    // Dedicated, because a dmabuf is a whole allocation. Exporting a
    // suballocation hands EGL a file descriptor for memory that also holds
    // something else.
    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.image = gShared;
    exportInfo.pNext = &dedicated;

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.pNext = &exportInfo;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = typeIndex;

    if (gApi.allocateMemory(gDevice, &mai, nullptr, &gSharedMemory) != VK_SUCCESS) {
        *err = "could not allocate exportable memory for the picture";
        return false;
    }
    if (gApi.bindImageMemory(gDevice, gShared, gSharedMemory, 0) != VK_SUCCESS) {
        *err = "could not bind the exportable memory";
        return false;
    }

    // Only the dmabuf route needs a layout: a memory object carries the
    // driver's own tiling and GL never asks how the rows are arranged.
    VkSubresourceLayout layout{};
    if (!gHaveMemoryObject) {
        VkImageSubresource sub{};
        sub.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        gApi.getImageSubresourceLayout(gDevice, gShared, &sub, &layout);
        if (layout.rowPitch == 0) {
            *err = "the driver reported a zero row pitch for the exported image";
            return false;
        }
    }

    VkMemoryGetFdInfoKHR fdInfo{};
    fdInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fdInfo.memory = gSharedMemory;
    fdInfo.handleType = handleType;
    int fd = -1;
    if (gApi.getMemoryFdKHR(gDevice, &fdInfo, &fd) != VK_SUCCESS || fd < 0) {
        *err = "the device would not export the picture as a dmabuf";
        return false;
    }

    // --- Route one: a GL memory object ------------------------------------
    //
    // GL takes ownership of the descriptor on success, exactly as EGL does
    // below, so it is closed here only on the failure paths.
    if (gHaveMemoryObject) {
        while (glGetError() != GL_NO_ERROR) {
        }  // start from a clean slate, so the checks below mean something

        gGlCreateMemoryObjects(1, &gMemoryObject);
        // DEDICATED, and it has to match how the memory was allocated. The
        // allocation above carries VkMemoryDedicatedAllocateInfo; importing it
        // as non-dedicated is undefined and on radeonsi it is a black texture.
        const GLint dedicated = GL_TRUE;
        gGlMemoryObjectParameteriv(gMemoryObject, GL_DEDICATED_MEMORY_OBJECT_EXT,
                                   &dedicated);
        gGlImportMemoryFd(gMemoryObject, req.size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd);
        if (GLenum e = glGetError(); e != GL_NO_ERROR) {
            ::close(fd);
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "GL would not import the picture's memory: 0x%04x "
                          "(%llu bytes)",
                          e, static_cast<unsigned long long>(req.size));
            *err = buf;
            return false;
        }

        glGenTextures(1, &gTexture);
        glBindTexture(GL_TEXTURE_2D, gTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // The tiling has to be declared before the storage is attached, and it
        // has to be the same one the VkImage was created with.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT);
        gGlTexStorageMem2D(GL_TEXTURE_2D, 1, GL_RGBA8, static_cast<GLsizei>(width),
                           static_cast<GLsizei>(height), gMemoryObject, 0);
        const GLenum storageError = glGetError();
        glBindTexture(GL_TEXTURE_2D, 0);
        if (storageError != GL_NO_ERROR) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "GL imported the memory and would not make a texture of "
                          "it: 0x%04x (%ux%u)",
                          storageError, width, height);
            *err = buf;
            return false;
        }

        gSharedWidth = width;
        gSharedHeight = height;
        std::fprintf(stderr,
                     "[vulkan] picture target %ux%u, shared as a GL memory object, "
                     "optimally tiled\n",
                     width, height);
        return true;
    }

    // --- Route two: an EGLImage from a dmabuf ------------------------------
    //
    // For a context EGL actually owns. Tried without a modifier first: naming
    // one is only legal where that EGL advertises it for the format, and the
    // answer differs between displays on the same machine.
    EGLDisplay dpy = currentDisplay();
    if (dpy == EGL_NO_DISPLAY) {
        ::close(fd);
        *err = "there is no EGL display to import the picture into";
        return false;
    }

    const EGLAttrib implicitAttribs[] = {
        EGL_WIDTH, static_cast<EGLAttrib>(width),
        EGL_HEIGHT, static_cast<EGLAttrib>(height),
        EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLAttrib>(kDrmFormatAbgr8888),
        EGL_DMA_BUF_PLANE0_FD_EXT, static_cast<EGLAttrib>(fd),
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, static_cast<EGLAttrib>(layout.offset),
        EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLAttrib>(layout.rowPitch),
        EGL_NONE};

    gEglImage = gEglCreateImage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr,
                                implicitAttribs);
    const EGLint implicitError = eglGetError();

    EGLint explicitError = EGL_SUCCESS;
    if (gEglImage == EGL_NO_IMAGE) {
        const EGLAttrib explicitAttribs[] = {
            EGL_WIDTH, static_cast<EGLAttrib>(width),
            EGL_HEIGHT, static_cast<EGLAttrib>(height),
            EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLAttrib>(kDrmFormatAbgr8888),
            EGL_DMA_BUF_PLANE0_FD_EXT, static_cast<EGLAttrib>(fd),
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, static_cast<EGLAttrib>(layout.offset),
            EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLAttrib>(layout.rowPitch),
            EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
            static_cast<EGLAttrib>(kDrmModifierLinear & 0xffffffffu),
            EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
            static_cast<EGLAttrib>(kDrmModifierLinear >> 32),
            EGL_NONE};
        gEglImage = gEglCreateImage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                                    nullptr, explicitAttribs);
        explicitError = eglGetError();
    }

    if (gEglImage == EGL_NO_IMAGE) {
        ::close(fd);
        // SAYING WHICH ERROR, because the first version of this line said only
        // that the import failed, and that cost a black screen on the
        // television with no way to tell a bad descriptor from a rejected
        // layout from an EGL that was never initialised. A probe that reports
        // a failure without reporting its cause is the fourth lying instrument
        // this project has had to fix.
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "EGL would not import the picture as an image: 0x%04x with the "
                      "dmabuf's own layout, 0x%04x with an explicit linear one "
                      "(%ux%u, pitch %llu)",
                      implicitError, explicitError, width, height,
                      static_cast<unsigned long long>(layout.rowPitch));
        *err = buf;
        return false;
    }

    glGenTextures(1, &gTexture);
    glBindTexture(GL_TEXTURE_2D, gTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gGlEglImageTargetTexture2D(GL_TEXTURE_2D, static_cast<GLeglImageOES>(gEglImage));
    glBindTexture(GL_TEXTURE_2D, 0);

    gSharedWidth = width;
    gSharedHeight = height;
    std::fprintf(stderr, "[vulkan] picture target %ux%u, exported as a dmabuf\n",
                 width, height);
    return true;
}

// Rounded up so a core that changes its internal resolution by a few pixels
// does not reallocate the image and re-import it into EGL every frame.
unsigned bucket(unsigned v) { return ((v + 255u) / 256u) * 256u; }

// Which way the picture crosses into GL, a GL memory object or an EGLImage
// from a dmabuf, with the entry points each needs; false with the reason
// when neither is there. createContext asks before it builds anything.
bool choosePictureRoute(std::string* err) {
    // SDL's loader, not EGL's, for the same reason core_hwrender.cpp gives:
    // SDL made this context, and on x11 it may not have made it with EGL at
    // all.
    auto glProc = [](const char* name) { return SDL_GL_GetProcAddress(name); };
    gGlCreateMemoryObjects =
        reinterpret_cast<PFNGLCREATEMEMORYOBJECTSEXTPROC>(glProc("glCreateMemoryObjectsEXT"));
    gGlDeleteMemoryObjects =
        reinterpret_cast<PFNGLDELETEMEMORYOBJECTSEXTPROC>(glProc("glDeleteMemoryObjectsEXT"));
    gGlImportMemoryFd =
        reinterpret_cast<PFNGLIMPORTMEMORYFDEXTPROC>(glProc("glImportMemoryFdEXT"));
    gGlTexStorageMem2D =
        reinterpret_cast<PFNGLTEXSTORAGEMEM2DEXTPROC>(glProc("glTexStorageMem2DEXT"));
    gGlMemoryObjectParameteriv = reinterpret_cast<PFNGLMEMORYOBJECTPARAMETERIVEXTPROC>(
        glProc("glMemoryObjectParameterivEXT"));
    gHaveMemoryObject = gGlCreateMemoryObjects && gGlDeleteMemoryObjects &&
                        gGlImportMemoryFd && gGlTexStorageMem2D &&
                        gGlMemoryObjectParameteriv;
    // The control, in the family of --core-options-off. There are two ways to
    // get a Vulkan picture into a GL texture and they fail differently; being
    // able to pick one from outside is what tells a bug in the bridge apart
    // from a bug in the core.
    if (const char* forced = SDL_getenv("CABINETOS_VK_ROUTE")) {
        if (std::strcmp(forced, "egl") == 0) gHaveMemoryObject = false;
    }

    gEglCreateImage =
        reinterpret_cast<PFNEGLCREATEIMAGEPROC>(eglGetProcAddress("eglCreateImage"));
    gEglDestroyImage =
        reinterpret_cast<PFNEGLDESTROYIMAGEPROC>(eglGetProcAddress("eglDestroyImage"));
    gGlEglImageTargetTexture2D = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    // The EGL route hands a dmabuf, which a GPU without the extension cannot
    // export (#489, EMU-02).
    const bool haveEglImage = gEglCreateImage && gEglDestroyImage &&
                              gGlEglImageTargetTexture2D && gpu::vulkan().externalMemoryDmaBuf;

    std::fprintf(stderr,
                 "[vulkan] picture route: %s (video driver %s)\n",
                 gHaveMemoryObject ? "GL memory object, optimally tiled"
                 : haveEglImage    ? "EGLImage from a dmabuf, linear"
                                   : "NONE",
                 SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");

    if (!gHaveMemoryObject && !haveEglImage) {
        *err =
            "this GL can neither import a memory object nor a dmabuf, so nothing "
            "a Vulkan core drew could reach the screen";
        return false;
    }
    return true;
}

}  // namespace state

using namespace state;

std::string screenDeviceUuid() {
    if (!SDL_GL_GetCurrentContext()) return "";
    if (!SDL_GL_ExtensionSupported("GL_EXT_memory_object")) return "";
    // The indexed query: DEVICE_UUID_EXT is one per device of the context,
    // NUM_DEVICE_UUIDS_EXT of them, and a context here has one.
    auto getBytes = reinterpret_cast<PFNGLGETUNSIGNEDBYTEI_VEXTPROC>(
        SDL_GL_GetProcAddress("glGetUnsignedBytei_vEXT"));
    if (!getBytes) return "";
    GLint count = 0;
    glGetIntegerv(GL_NUM_DEVICE_UUIDS_EXT, &count);
    if (count < 1) {
        while (glGetError() != GL_NO_ERROR) {}
        return "";
    }
    GLubyte uuid[GL_UUID_SIZE_EXT] = {};
    getBytes(GL_DEVICE_UUID_EXT, 0, uuid);
    while (glGetError() != GL_NO_ERROR) {}
    return gpu::uuidString(uuid);
}

bool present() {
    const auto started = std::chrono::steady_clock::now();
    struct Tally {
        const std::chrono::steady_clock::time_point& t0;
        bool counted = false;
        ~Tally() {
            if (!counted) return;
            gPresentSeconds +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            ++gPresentCount;
        }
    } tally{started};

    retro_vulkan_image image{};
    uint32_t srcFamily = VK_QUEUE_FAMILY_IGNORED;
    std::vector<VkSemaphore> semaphores;
    {
        std::lock_guard<std::mutex> lock(gImageLock);
        if (!gHaveCoreImage) return false;
        image = gCoreImage;
        srcFamily = gCoreSrcQueueFamily;
        semaphores.swap(gCoreSemaphores);
    }
    if (gDevice == VK_NULL_HANDLE || image.image_view == VK_NULL_HANDLE) return false;

    // Size comes from what the core told video_refresh, set by core_picture.cpp
    // before this is called — see setFrameSize. Guarded rather than assumed: a
    // zero-sized blit is a validation error and an undefined picture.
    if (gFrameWidth == 0 || gFrameHeight == 0) return false;

    const unsigned wantW = bucket(gFrameWidth);
    const unsigned wantH = bucket(gFrameHeight);
    if (wantW > gSharedWidth || wantH > gSharedHeight) {
        std::string err;
        if (!createShared(wantW > gSharedWidth ? wantW : gSharedWidth,
                          wantH > gSharedHeight ? wantH : gSharedHeight, &err)) {
            // ONCE. This is called every frame, and the first version of this
            // printed the same line sixty times a second into the journal of a
            // console somebody was trying to read.
            static std::string lastSaid;
            if (lastSaid != err) {
                lastSaid = err;
                std::fprintf(stderr, "[vulkan] %s\n", err.c_str());
            }
            return false;
        }
    }

    std::lock_guard<std::mutex> queueLock(gQueueLock);

    gApi.resetCommandBuffer(gCmd, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    gApi.beginCommandBuffer(gCmd, &bi);

    const bool transferOwnership =
        srcFamily != VK_QUEUE_FAMILY_IGNORED && srcFamily != gQueueFamily;

    VkImageMemoryBarrier toSrc{};
    toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toSrc.oldLayout = image.image_layout;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = transferOwnership ? srcFamily : VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = transferOwnership ? gQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    toSrc.image = image.create_info.image;
    toSrc.subresourceRange = image.create_info.subresourceRange;

    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    // UNDEFINED the first time, GENERAL after — GENERAL because that is the
    // layout EGL reads the exported memory in, and transitioning away from it
    // behind EGL's back is how a picture comes out as noise.
    toDst.oldLayout = gSharedInitialised ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = gShared;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier before[] = {toSrc, toDst};
    gApi.cmdPipelineBarrier(gCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                            2, before);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<int32_t>(gFrameWidth),
                          static_cast<int32_t>(gFrameHeight), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {static_cast<int32_t>(gFrameWidth),
                          static_cast<int32_t>(gFrameHeight), 1};
    gApi.cmdBlitImage(gCmd, image.create_info.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      gShared, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                      VK_FILTER_NEAREST);

    VkImageMemoryBarrier backToCore = toSrc;
    backToCore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    backToCore.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    backToCore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    backToCore.newLayout = image.image_layout;
    backToCore.srcQueueFamilyIndex = transferOwnership ? gQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    backToCore.dstQueueFamilyIndex = transferOwnership ? srcFamily : VK_QUEUE_FAMILY_IGNORED;

    VkImageMemoryBarrier toGeneral{};
    toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneral.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toGeneral.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    toGeneral.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image = gShared;
    toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier after[] = {backToCore, toGeneral};
    gApi.cmdPipelineBarrier(gCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                            2, after);
    gApi.endCommandBuffer(gCmd);

    std::vector<VkPipelineStageFlags> waitStages(semaphores.size(),
                                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &gCmd;
    si.waitSemaphoreCount = static_cast<uint32_t>(semaphores.size());
    si.pWaitSemaphores = semaphores.empty() ? nullptr : semaphores.data();
    si.pWaitDstStageMask = waitStages.empty() ? nullptr : waitStages.data();

    gApi.resetFences(gDevice, 1, &gFence);
    if (gApi.queueSubmit(gQueue, 1, &si, gFence) != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] the copy would not submit\n");
        return false;
    }
    const auto submitted = std::chrono::steady_clock::now();
    gPresentRecordSeconds += std::chrono::duration<double>(submitted - started).count();
    // WAITED ON THE CPU, AND THAT IS THE CORRECT ORDER RATHER THAN THE LAZY
    // ONE. GL is about to sample memory Vulkan has just written, and the two
    // APIs share no timeline here. An exported semaphore would move this wait
    // onto the GPU; until something measures a cost, a fence is the version
    // that is definitely right.
    gApi.waitForFences(gDevice, 1, &gFence, VK_TRUE, UINT64_MAX);
    gPresentWaitSeconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - submitted).count();
    tally.counted = true;

    gSharedInitialised = true;
    gSyncIndex = (gSyncIndex + 1u) % kSyncIndices;

    return true;
}

void presentCost(uint64_t& frames, double& seconds) {
    frames = gPresentCount;
    seconds = gPresentSeconds;
}

void presentCostSplit(double& recordSeconds, double& waitSeconds) {
    recordSeconds = gPresentRecordSeconds;
    waitSeconds = gPresentWaitSeconds;
}

GLuint texture() { return gTexture; }

void frameUV(float& u0, float& v0, float& u1, float& v1) {
    if (gSharedWidth == 0 || gSharedHeight == 0) {
        u0 = v0 = 0.0f;
        u1 = v1 = 1.0f;
        return;
    }
    const float su = static_cast<float>(gFrameWidth) / static_cast<float>(gSharedWidth);
    const float sv = static_cast<float>(gFrameHeight) / static_cast<float>(gSharedHeight);
    // Top-down. The blit writes row 0 of the core's picture to row 0 of the
    // exported image, and a dmabuf imported into GL is sampled with (0,0) at
    // the first row in memory — so unlike the GLES hardware path there is no
    // flip to undo here. See core.h for why that path has one.
    u0 = 0.0f;
    v0 = 0.0f;
    u1 = su;
    v1 = sv;
}

void setFrameSize(unsigned width, unsigned height) {
    gFrameWidth = width;
    gFrameHeight = height;
}

}  // namespace cab::vk
