#include "vkhost.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "gpu.h"
#include "libretro_vulkan.h"
#include "vkhost_state.h"

namespace cab::vk {
namespace state {

Api gApi;

VkInstance gInstance = VK_NULL_HANDLE;
VkPhysicalDevice gGpu = VK_NULL_HANDLE;
VkDevice gDevice = VK_NULL_HANDLE;
VkQueue gQueue = VK_NULL_HANDLE;
uint32_t gQueueFamily = 0;
bool gDeviceFromCore = false;

// A SURFACE WITH NO WINDOW BEHIND IT, AND IT IS NOT A TRICK.
//
// Dolphin's Vulkan backend decides whether it has a display by whether the
// frontend handed it a VkSurfaceKHR, and with none it renders to its embedded
// framebuffer and never presents — which is exactly what was measured: thirty
// seconds of emulated Mario Kart, correct audio, and set_image never called
// once.
//
// Every surface and swapchain call is then INTERCEPTED by the core itself
// (DolphinLibretro/Vulkan.cpp replaces vkCreateSwapchainKHR, vkAcquireNextImage
// and vkQueuePresentKHR wholesale, and hands the picture over through
// set_image instead). So the surface is a token that says "there is a display"
// and is never presented to. VK_EXT_headless_surface is precisely the object
// for that: a real VkSurfaceKHR with no window.
//
// The alternative would be a second hidden SDL window created for Vulkan,
// which is a window the console would own, have to size, and have to keep off
// the television. This is the smaller thing.
VkSurfaceKHR gSurface = VK_NULL_HANDLE;

VkCommandPool gPool = VK_NULL_HANDLE;
VkCommandBuffer gCmd = VK_NULL_HANDLE;
VkFence gFence = VK_NULL_HANDLE;

// THE QUEUE LOCK IS NOT OPTIONAL AND IT IS THE WHOLE REASON THIS WORKS.
//
// libretro's Vulkan contract says a core may submit to this queue from any
// thread, and must take the frontend's lock to do it. That is exactly what
// Dolphin does — it has a CPU thread, a video thread and a DVD thread — and
// it is the thing OpenGL ES could not express, because a GL context belongs
// to one thread and there is no lock that can change that.
std::mutex gQueueLock;

// Which of the kSyncIndices (vkhost_state.h) the core is on this frame;
// present moves it on.
// Atomic: the cores' render threads read it (getSyncIndex) while present
// moves it on the GL thread (#483, EMU-03).
std::atomic<uint32_t> gSyncIndex{0};

retro_hw_render_interface_vulkan gInterface{};
const retro_hw_render_context_negotiation_interface_vulkan* gNegotiation = nullptr;

// --- Callbacks the core calls ----------------------------------------------

void RETRO_CALLCONV setImage(void* handle, const retro_vulkan_image* image,
                             uint32_t numSemaphores, const VkSemaphore* semaphores,
                             uint32_t srcQueueFamily) {
    std::lock_guard<std::mutex> lock(gImageLock);
    if (!image) {
        gHaveCoreImage = false;
        return;
    }
    if (!gHaveCoreImage)
        std::fprintf(stderr, "[vulkan] the core handed over its first picture\n");
    gCoreImage = *image;
    gHaveCoreImage = true;
    gCoreSrcQueueFamily = srcQueueFamily;
    gCoreSemaphores.assign(semaphores, semaphores + numSemaphores);
}

uint32_t RETRO_CALLCONV getSyncIndex(void* handle) { return gSyncIndex; }
uint32_t RETRO_CALLCONV getSyncIndexMask(void* handle) { return (1u << kSyncIndices) - 1u; }

void RETRO_CALLCONV setCommandBuffers(void* handle, uint32_t numCmd,
                                      const VkCommandBuffer* cmd) {
    // Deliberately not implemented, and deliberately not silent.
    //
    // This is an OPTIMISATION in the libretro contract — it lets a core
    // amortise vkQueueSubmit by handing its command buffers to the frontend
    // instead of submitting them itself. A core only uses it if it is told it
    // can, and nothing here tells it that, so reaching this is a core doing
    // something the interface did not offer. Saying so beats dropping frames
    // that nobody can account for.
    static bool said = false;
    if (!said) {
        said = true;
        std::fprintf(stderr,
                     "[vulkan] the core handed over command buffers; this host "
                     "submits its own and does not take them\n");
    }
}

// UNDER THE QUEUE LOCK (#483, EMU-05). vkDeviceWaitIdle needs every queue of
// the device externally synchronised, and present() submits to the same queue
// on the GL thread; Mesa's waits by submitting to the queue, so the two could
// submit at once. paraLLEl-RDP's own device wait does the same (Granite's
// Device::wait_idle_nolock, under its queue lock callback).
//
// IT CANNOT DEADLOCK THE CORES WE RUN ON VULKAN, read in their sources at the
// pinned commits (2026-10-19): Dolphin (1a0f972, DolphinLibretro/Vulkan.cpp),
// PPSSPP (c989c25, VulkanFrameData.cpp, VulkanContext.cpp), Flycast (a172e00,
// vk_context_lr.h) and Mupen64Plus-Next's paraLLEl-RDP (f275caf, rdp.cpp,
// device.cpp) hold lock_queue only around one submit or one idle wait, never
// while calling wait_sync_index (from their image acquire, or paraLLEl's
// begin_frame), and all hand set_image no semaphores, so present() holds the
// lock only over GPU work already submitted. A new Vulkan core is checked the
// same way. RetroArch's own wait_sync_index is empty (gfx/drivers/vulkan.c,
// read at 07bd4ca): it waits in its swap instead, as present() does here.
void RETRO_CALLCONV waitSyncIndex(void* handle) {
    if (!gDevice || !gApi.deviceWaitIdle) return;
    std::lock_guard<std::mutex> lk(gQueueLock);
    gApi.deviceWaitIdle(gDevice);
}

void RETRO_CALLCONV lockQueue(void* handle) { gQueueLock.lock(); }
void RETRO_CALLCONV unlockQueue(void* handle) { gQueueLock.unlock(); }

void RETRO_CALLCONV setSignalSemaphore(void* handle, VkSemaphore semaphore) {
    // Nothing to do: this host waits on a fence before it reads the core's
    // image, so the image is free again by the time control returns. A
    // semaphore would only matter once the wait is moved onto the GPU.
}

// --- Loading ---------------------------------------------------------------

template <typename T>
void loadInstance(T& fn, const char* name) {
    fn = reinterpret_cast<T>(gApi.getInstanceProcAddr(gInstance, name));
}
template <typename T>
void loadDevice(T& fn, const char* name) {
    fn = reinterpret_cast<T>(gApi.getDeviceProcAddr(gDevice, name));
}

bool pickQueueFamily(VkPhysicalDevice gpu, uint32_t* out) {
    uint32_t n = 0;
    gApi.getQueueFamilyProperties(gpu, &n, nullptr);
    std::vector<VkQueueFamilyProperties> props(n);
    gApi.getQueueFamilyProperties(gpu, &n, props.data());
    // GRAPHICS and COMPUTE both, which the libretro contract requires of the
    // queue handed to a core.
    for (uint32_t i = 0; i < n; ++i) {
        const VkQueueFlags want = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((props[i].queueFlags & want) == want) {
            *out = i;
            return true;
        }
    }
    return false;
}

// THE DEVICE gpu.cpp CHOSE, found again in this instance by its UUID: the
// GPU the screen is on (#659, gpu.h). This instance is the core's, made with
// the core's own application info, so the device is matched here rather
// than carried over as a handle. The old order, a discrete card first and
// then anything that is not software, remains for an instance whose loader
// cannot read UUIDs, so that the two never silently disagree.
bool chooseGpu(std::string* err) {
    uint32_t count = 0;
    gApi.enumeratePhysicalDevices(gInstance, &count, nullptr);
    if (count == 0) {
        *err = "no Vulkan devices";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    gApi.enumeratePhysicalDevices(gInstance, &count, devices.data());
    VkPhysicalDeviceProperties best{};
    const std::string& want = gpu::vulkan().deviceUuid;
    if (gApi.getPhysicalDeviceProperties2 && want.size() == 32) {
        for (VkPhysicalDevice d : devices) {
            VkPhysicalDeviceIDProperties id{};
            id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
            VkPhysicalDeviceProperties2 p2{};
            p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            p2.pNext = &id;
            gApi.getPhysicalDeviceProperties2(d, &p2);
            if (gpu::uuidString(id.deviceUUID) == want) {
                gGpu = d;
                best = p2.properties;
                break;
            }
        }
    }
    if (gGpu == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[vulkan] the GPU the console chose is not in the core's list; "
                             "taking a discrete card first\n");
        for (VkPhysicalDevice d : devices) {
            VkPhysicalDeviceProperties p{};
            gApi.getPhysicalDeviceProperties(d, &p);
            if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
            const bool better = gGpu == VK_NULL_HANDLE ||
                                (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
                                 best.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
            if (better) {
                gGpu = d;
                best = p;
            }
        }
    }
    if (gGpu == VK_NULL_HANDLE) {
        *err = "the only Vulkan device here is a software one";
        return false;
    }
    gDescription = std::string(best.deviceName) + ", Vulkan " +
                   gpu::versionString(best.apiVersion);
    return true;
}

// The device extensions this host needs whatever the core wants, because
// without them the picture cannot leave Vulkan. They are passed to the core's
// create_device so that a core-created device carries them too — which is what
// the libretro contract means by "the frontend will request certain
// extensions".
std::vector<const char*> gRequiredDeviceExtensions;

// The ones without which the picture cannot leave Vulkan at all. They are
// passed to the core's create_device too, which is what the libretro contract
// means by "the frontend will request certain extensions". The dmabuf one
// only where the GPU has it: only the EGL route needs it (#489, EMU-02), and
// asking for one the GPU lacks fails vkCreateDevice.
//
// VK_EXT_image_drm_format_modifier is deliberately NOT here. It was added,
// measured and taken out again — see createShared (vkbridge.cpp) for the numbers.
void buildDeviceExtensionList() {
    gRequiredDeviceExtensions = {
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    };
    if (gpu::vulkan().externalMemoryDmaBuf)
        gRequiredDeviceExtensions.push_back(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
}

bool createOwnDevice(std::string* err) {
    if (!pickQueueFamily(gGpu, &gQueueFamily)) {
        *err = "no queue family here does both graphics and compute";
        return false;
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo q{};
    q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    q.queueFamilyIndex = gQueueFamily;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &q;
    dci.enabledExtensionCount = static_cast<uint32_t>(gRequiredDeviceExtensions.size());
    dci.ppEnabledExtensionNames = gRequiredDeviceExtensions.data();

    if (gApi.createDevice(gGpu, &dci, nullptr, &gDevice) != VK_SUCCESS) {
        *err = "vkCreateDevice failed";
        return false;
    }
    return true;
}

// A CORE THAT DROPS THE EXTENSIONS IT IS HANDED, and Flycast is one — 2026-09-27.
//
// Flycast's create_device (core/rend/vulkan/vk_context_lr.cpp, at our pin and
// on upstream master alike) receives required_device_extensions and never
// reads it: it builds its own list from what IT wants and creates the device
// with that. So Dreamcast came up on a device without external memory, and
// loadDeviceApi refused it with "this device will not export memory as a file
// descriptor" on a GPU that exports perfectly well. Dolphin reads the list,
// which is why GameCube played on the same boot. RetroArch never notices:
// it presents from Vulkan, so the picture never has to leave it.
//
// No patch to the core. It looks up vkCreateDevice through the
// get_instance_proc_addr WE pass (it links no Vulkan symbol at all — `nm -D`
// on the shipped .so shows none), so we hand it a lookup that answers that
// one name with a wrapper adding whatever of our list is missing. That is
// what the v2 negotiation interface formalises; Flycast only speaks v1.
//
// Only extensions the GPU has are added. Asking for one it lacks fails device
// creation, and Flycast uses vulkan-hpp with exceptions, so that failure would
// be a throw through a C boundary rather than the clean refusal it gets now.
PFN_vkCreateDevice gRealCreateDevice = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL createDeviceWithOurs(VkPhysicalDevice gpu,
                                                    const VkDeviceCreateInfo* info,
                                                    const VkAllocationCallbacks* alloc,
                                                    VkDevice* device) {
    std::vector<VkExtensionProperties> supported;
    auto enumerate = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        gApi.getInstanceProcAddr(gInstance, "vkEnumerateDeviceExtensionProperties"));
    if (enumerate) {
        uint32_t n = 0;
        enumerate(gpu, nullptr, &n, nullptr);
        supported.resize(n);
        enumerate(gpu, nullptr, &n, supported.data());
        supported.resize(n);
    }
    auto has = [](const char* const* names, size_t count, const char* want) {
        for (size_t i = 0; i < count; ++i)
            if (std::strcmp(names[i], want) == 0) return true;
        return false;
    };
    std::vector<const char*> names(info->ppEnabledExtensionNames,
                                   info->ppEnabledExtensionNames + info->enabledExtensionCount);
    unsigned added = 0;
    for (const char* need : gRequiredDeviceExtensions) {
        if (has(names.data(), names.size(), need)) continue;
        bool gpuHasIt = false;
        for (const auto& p : supported)
            if (std::strcmp(p.extensionName, need) == 0) { gpuHasIt = true; break; }
        if (!gpuHasIt) continue;
        names.push_back(need);
        ++added;
    }
    if (added)
        std::fprintf(stderr,
                     "[vulkan] the core left out %u of the extensions this host needs; "
                     "added them to its device\n",
                     added);
    VkDeviceCreateInfo withOurs = *info;
    withOurs.enabledExtensionCount = static_cast<uint32_t>(names.size());
    withOurs.ppEnabledExtensionNames = names.data();
    return gRealCreateDevice(gpu, &withOurs, alloc, device);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL coreGetInstanceProcAddr(VkInstance instance,
                                                                 const char* name) {
    if (name && std::strcmp(name, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&createDeviceWithOurs);
    if (name && std::strcmp(name, "vkGetInstanceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&coreGetInstanceProcAddr);
    return gApi.getInstanceProcAddr(instance, name);
}

bool createCoreDevice(std::string* err) {
    gRealCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(
        gApi.getInstanceProcAddr(gInstance, "vkCreateDevice"));
    if (!gRealCreateDevice) {
        *err = "no vkCreateDevice";
        return false;
    }
    retro_vulkan_context ctx{};
    VkPhysicalDeviceFeatures features{};
    const VkPhysicalDevice handed = gGpu;
    const bool ok = gNegotiation->create_device(
        &ctx, gInstance, gGpu, gSurface, coreGetInstanceProcAddr,
        gRequiredDeviceExtensions.data(),
        static_cast<unsigned>(gRequiredDeviceExtensions.size()), nullptr, 0, &features);
    if (!ok) {
        // NOT fatal, and the contract says so in as many words: "the frontend
        // will attempt to fall back to default device creation, as if this
        // function was never called". Said out loud because a core declining
        // to make its own device is worth knowing when a game misbehaves.
        std::fprintf(stderr,
                     "[vulkan] the core would not create its own device; falling "
                     "back to one of ours\n");
        return false;
    }
    if (ctx.device == VK_NULL_HANDLE || ctx.queue == VK_NULL_HANDLE) {
        *err = "the core reported success from create_device and handed back nothing";
        return false;
    }
    // A core may make its device on a GPU of its own choosing; the contract
    // allows it. Said out loud, because on a machine with two GPUs that is
    // the picture crossing from one to the other to reach the screen, which
    // is the fault the choice above exists to avoid (#659).
    if (ctx.gpu != VK_NULL_HANDLE && ctx.gpu != handed) {
        VkPhysicalDeviceProperties p{};
        gApi.getPhysicalDeviceProperties(ctx.gpu, &p);
        std::fprintf(stderr,
                     "[vulkan] the core made its device on %s, not the GPU it was handed; "
                     "its picture will cross GPUs to reach the screen\n",
                     p.deviceName);
        gDescription = std::string(p.deviceName) + ", Vulkan " + gpu::versionString(p.apiVersion);
    }
    if (ctx.gpu != VK_NULL_HANDLE) gGpu = ctx.gpu;
    gDevice = ctx.device;
    gQueue = ctx.queue;
    gQueueFamily = ctx.queue_family_index;
    gDeviceFromCore = true;
    return true;
}

bool loadDeviceApi(std::string* err) {
    gApi.getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        gApi.getInstanceProcAddr(gInstance, "vkGetDeviceProcAddr"));
    if (!gApi.getDeviceProcAddr) {
        *err = "no vkGetDeviceProcAddr";
        return false;
    }
    loadDevice(gApi.destroyDevice, "vkDestroyDevice");
    loadDevice(gApi.getDeviceQueue, "vkGetDeviceQueue");
    loadDevice(gApi.deviceWaitIdle, "vkDeviceWaitIdle");
    loadDevice(gApi.createCommandPool, "vkCreateCommandPool");
    loadDevice(gApi.destroyCommandPool, "vkDestroyCommandPool");
    loadDevice(gApi.allocateCommandBuffers, "vkAllocateCommandBuffers");
    loadDevice(gApi.beginCommandBuffer, "vkBeginCommandBuffer");
    loadDevice(gApi.endCommandBuffer, "vkEndCommandBuffer");
    loadDevice(gApi.resetCommandBuffer, "vkResetCommandBuffer");
    loadDevice(gApi.cmdPipelineBarrier, "vkCmdPipelineBarrier");
    loadDevice(gApi.cmdBlitImage, "vkCmdBlitImage");
    loadDevice(gApi.queueSubmit, "vkQueueSubmit");
    loadDevice(gApi.createFence, "vkCreateFence");
    loadDevice(gApi.destroyFence, "vkDestroyFence");
    loadDevice(gApi.waitForFences, "vkWaitForFences");
    loadDevice(gApi.resetFences, "vkResetFences");
    loadDevice(gApi.createImage, "vkCreateImage");
    loadDevice(gApi.destroyImage, "vkDestroyImage");
    loadDevice(gApi.getImageMemoryRequirements, "vkGetImageMemoryRequirements");
    loadDevice(gApi.getImageSubresourceLayout, "vkGetImageSubresourceLayout");
    loadDevice(gApi.allocateMemory, "vkAllocateMemory");
    loadDevice(gApi.freeMemory, "vkFreeMemory");
    loadDevice(gApi.bindImageMemory, "vkBindImageMemory");
    loadDevice(gApi.getMemoryFdKHR, "vkGetMemoryFdKHR");

    if (!gApi.getMemoryFdKHR) {
        *err =
            "this device will not export memory as a file descriptor, so nothing "
            "it draws could reach the screen";
        return false;
    }
    return true;
}

}  // namespace state

using namespace state;

bool available() { return gpu::vulkan().available; }

void setNegotiation(const retro_hw_render_context_negotiation_interface_vulkan* n) {
    gNegotiation = n;
    if (n)
        std::fprintf(stderr, "[vulkan] the core will negotiate its own device (interface v%u)\n",
                     n->interface_version);
}

bool createContext(std::string* err) {
    if (gDevice != VK_NULL_HANDLE) return true;
    if (!available()) {
        *err = gpu::vulkan().reason;
        return false;
    }

    gApi.getInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(gpu::instanceProcAddr());
    if (!gApi.getInstanceProcAddr) {
        *err = "no Vulkan loader";
        return false;
    }

    // The GL half before any Vulkan object exists: with no way for a picture
    // to reach GL, nothing a Vulkan core drew could reach the screen
    // (vkbridge.cpp).
    if (!choosePictureRoute(err)) return false;

    gApi.createInstance = reinterpret_cast<PFN_vkCreateInstance>(
        gApi.getInstanceProcAddr(nullptr, "vkCreateInstance"));

    // THE CORE'S OWN APPLICATION INFO DECIDES THE API VERSION, and the
    // contract says so: apiVersion here "controls the target core Vulkan
    // version for instance level functionality". Ignoring it is how a core
    // that needs 1.2 ends up on a 1.0 instance and fails somewhere far away
    // from the cause.
    VkApplicationInfo fallback{};
    fallback.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    fallback.pApplicationName = "CabinetOS";
    fallback.apiVersion = VK_API_VERSION_1_1;
    const VkApplicationInfo* app = &fallback;
    if (gNegotiation && gNegotiation->get_application_info) {
        const VkApplicationInfo* fromCore = gNegotiation->get_application_info();
        if (fromCore) app = fromCore;
    }

    // BUT NEVER BELOW 1.1 WHERE THE LOADER HAS IT, which is what RetroArch
    // does (gfx/common/vulkan_common.c: "Vulkan 1.0 drivers are completely
    // irrelevant these days") — 2026-09-27. PPSSPP asks for 1.0 and then its
    // memory allocator calls vkGetBufferMemoryRequirements2, a 1.1 function
    // its loader only fetches on a 1.1 instance: a call through a null
    // pointer that took the whole console down the moment PSP started on
    // Vulkan. A loader without 1.1 keeps what the core asked for, as
    // RetroArch's does.
    VkApplicationInfo raised{};
    if (app->apiVersion < VK_API_VERSION_1_1) {
        auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            gApi.getInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
        uint32_t supported = VK_API_VERSION_1_0;
        if (enumerateVersion && enumerateVersion(&supported) == VK_SUCCESS &&
            supported >= VK_API_VERSION_1_1) {
            raised = *app;
            raised.apiVersion = VK_API_VERSION_1_1;
            app = &raised;
        }
    }

    // Surface support is asked for, not assumed: a machine without it still
    // runs every Vulkan core that does not need a display token, and one that
    // does gets a clear line rather than a black picture.
    gApi.enumerateInstanceExtensions =
        reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
            gApi.getInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
    bool haveHeadless = false;
    if (gApi.enumerateInstanceExtensions) {
        uint32_t n = 0;
        gApi.enumerateInstanceExtensions(nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> exts(n);
        if (n) gApi.enumerateInstanceExtensions(nullptr, &n, exts.data());
        bool haveSurface = false;
        for (const auto& e : exts) {
            if (std::strcmp(e.extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0)
                haveSurface = true;
            if (std::strcmp(e.extensionName, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME) == 0)
                haveHeadless = true;
        }
        haveHeadless = haveHeadless && haveSurface;
    }
    std::vector<const char*> instanceExts;
    if (haveHeadless) {
        instanceExts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        instanceExts.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
    }

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = app;
    ici.enabledExtensionCount = static_cast<uint32_t>(instanceExts.size());
    ici.ppEnabledExtensionNames = instanceExts.empty() ? nullptr : instanceExts.data();
    if (gApi.createInstance(&ici, nullptr, &gInstance) != VK_SUCCESS) {
        *err = "could not create a Vulkan instance";
        return false;
    }

    loadInstance(gApi.destroyInstance, "vkDestroyInstance");
    loadInstance(gApi.enumeratePhysicalDevices, "vkEnumeratePhysicalDevices");
    loadInstance(gApi.getPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties");
    loadInstance(gApi.getPhysicalDeviceProperties2, "vkGetPhysicalDeviceProperties2");
    loadInstance(gApi.getQueueFamilyProperties, "vkGetPhysicalDeviceQueueFamilyProperties");
    loadInstance(gApi.getMemoryProperties, "vkGetPhysicalDeviceMemoryProperties");
    loadInstance(gApi.createDevice, "vkCreateDevice");

    if (!chooseGpu(err)) {
        destroyContext();
        return false;
    }
    buildDeviceExtensionList();

    if (haveHeadless) {
        gApi.createHeadlessSurface = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
            gApi.getInstanceProcAddr(gInstance, "vkCreateHeadlessSurfaceEXT"));
        loadInstance(gApi.destroySurface, "vkDestroySurfaceKHR");
        if (gApi.createHeadlessSurface) {
            VkHeadlessSurfaceCreateInfoEXT hs{};
            hs.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
            if (gApi.createHeadlessSurface(gInstance, &hs, nullptr, &gSurface) != VK_SUCCESS)
                gSurface = VK_NULL_HANDLE;
        }
    }
    std::fprintf(stderr, "[vulkan] display token: %s\n",
                 gSurface != VK_NULL_HANDLE
                     ? "a headless surface, which is what a core checks for"
                     : "NONE — a core that needs one will render and never present");

    // The core first, as the contract requires, then ours.
    bool made = false;
    if (gNegotiation && gNegotiation->create_device) {
        std::string ignored;
        made = createCoreDevice(&ignored);
    }
    if (!made && !createOwnDevice(err)) {
        destroyContext();
        return false;
    }
    if (!loadDeviceApi(err)) {
        destroyContext();
        return false;
    }
    if (gQueue == VK_NULL_HANDLE) gApi.getDeviceQueue(gDevice, gQueueFamily, 0, &gQueue);

    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = gQueueFamily;
    if (gApi.createCommandPool(gDevice, &pci, nullptr, &gPool) != VK_SUCCESS) {
        *err = "could not create a command pool";
        destroyContext();
        return false;
    }
    VkCommandBufferAllocateInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbi.commandPool = gPool;
    cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbi.commandBufferCount = 1;
    if (gApi.allocateCommandBuffers(gDevice, &cbi, &gCmd) != VK_SUCCESS) {
        *err = "could not allocate a command buffer";
        destroyContext();
        return false;
    }
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (gApi.createFence(gDevice, &fci, nullptr, &gFence) != VK_SUCCESS) {
        *err = "could not create a fence";
        destroyContext();
        return false;
    }

    std::fprintf(stderr, "[vulkan] %s, queue family %u, device %s\n",
                 gDescription.c_str(), gQueueFamily,
                 gDeviceFromCore ? "created by the core" : "created by the frontend");

    gInterface = {};
    gInterface.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    gInterface.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    gInterface.handle = nullptr;
    gInterface.instance = gInstance;
    gInterface.gpu = gGpu;
    gInterface.device = gDevice;
    gInterface.get_device_proc_addr = gApi.getDeviceProcAddr;
    gInterface.get_instance_proc_addr = gApi.getInstanceProcAddr;
    gInterface.queue = gQueue;
    gInterface.queue_index = gQueueFamily;
    gInterface.set_image = setImage;
    gInterface.get_sync_index = getSyncIndex;
    gInterface.get_sync_index_mask = getSyncIndexMask;
    gInterface.set_command_buffers = setCommandBuffers;
    gInterface.wait_sync_index = waitSyncIndex;
    gInterface.lock_queue = lockQueue;
    gInterface.unlock_queue = unlockQueue;
    gInterface.set_signal_semaphore = setSignalSemaphore;
    return true;
}

void destroyContext() {
    if (gDevice != VK_NULL_HANDLE && gApi.deviceWaitIdle) gApi.deviceWaitIdle(gDevice);
    destroyShared();
    if (gFence != VK_NULL_HANDLE) {
        gApi.destroyFence(gDevice, gFence, nullptr);
        gFence = VK_NULL_HANDLE;
    }
    if (gPool != VK_NULL_HANDLE) {
        gApi.destroyCommandPool(gDevice, gPool, nullptr);
        gPool = VK_NULL_HANDLE;
    }
    // A DEVICE THE CORE CREATED IS STILL OURS TO DESTROY (#489, EMU-04). The
    // contract: "Device provided to frontend is owned by the frontend, but any
    // additional device resources must be freed by core in destroy_device
    // callback" (libretro_vulkan.h). So the core frees its extras first, if it
    // has a destroy_device, and the device goes as our own does. Every Vulkan
    // core shipped passes none and left the device to us, and until this each
    // N64, PSP, Dreamcast, GameCube or Wii game left one behind, measured on
    // the A9 at about 1 MiB of VRAM and 0.7 MiB of GTT a game (Hydro Thunder,
    // ten launches in one run; flat since).
    // This runs after context_destroy and retro_unload_game (Core::unloadGame).
    if (gDevice != VK_NULL_HANDLE) {
        if (gDeviceFromCore && gNegotiation && gNegotiation->destroy_device)
            gNegotiation->destroy_device();
        if (gApi.destroyDevice) gApi.destroyDevice(gDevice, nullptr);
        gDevice = VK_NULL_HANDLE;
    }
    if (gSurface != VK_NULL_HANDLE && gApi.destroySurface) {
        gApi.destroySurface(gInstance, gSurface, nullptr);
        gSurface = VK_NULL_HANDLE;
    }
    if (gInstance != VK_NULL_HANDLE && gApi.destroyInstance) {
        gApi.destroyInstance(gInstance, nullptr);
        gInstance = VK_NULL_HANDLE;
    }
    gGpu = VK_NULL_HANDLE;
    gQueue = VK_NULL_HANDLE;
    gDeviceFromCore = false;
    gHaveCoreImage = false;
    gFrameWidth = gFrameHeight = 0;
    gDescription.clear();
    gInterface = {};
}

void forgetCoreImage() {
    std::lock_guard<std::mutex> lock(gImageLock);
    gHaveCoreImage = false;
}

const retro_hw_render_interface_vulkan* renderInterface() {
    return gDevice == VK_NULL_HANDLE ? nullptr : &gInterface;
}

}  // namespace cab::vk
