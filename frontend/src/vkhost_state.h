// The Vulkan host's shared state (#398). vkhost.cpp builds the device and
// answers the core, from the core's own threads as well as the GL thread;
// the restructure splits the host into files by what they do, and everything
// those files share is declared here, in one named namespace, under the file
// that defines it, so each piece has one owner. Nothing outside the host
// includes this. The host's files name these unqualified, through
// `using namespace state;`.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include "libretro_vulkan.h"

namespace cab::vk::state {

// Every Vulkan entry point the host uses, resolved through the loader gpu.cpp
// already opened. Nothing links libvulkan — see gpu.h.
struct Api {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;

    PFN_vkCreateInstance createInstance = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 getPhysicalDeviceProperties2 = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueueFamilyProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getMemoryProperties = nullptr;
    PFN_vkCreateDevice createDevice = nullptr;

    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkGetDeviceQueue getDeviceQueue = nullptr;
    PFN_vkDeviceWaitIdle deviceWaitIdle = nullptr;
    PFN_vkCreateCommandPool createCommandPool = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;
    PFN_vkCmdBlitImage cmdBlitImage = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkCreateFence createFence = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
    PFN_vkResetFences resetFences = nullptr;
    PFN_vkCreateImage createImage = nullptr;
    PFN_vkDestroyImage destroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements getImageMemoryRequirements = nullptr;
    PFN_vkGetImageSubresourceLayout getImageSubresourceLayout = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkBindImageMemory bindImageMemory = nullptr;
    PFN_vkGetMemoryFdKHR getMemoryFdKHR = nullptr;

    PFN_vkEnumerateInstanceExtensionProperties enumerateInstanceExtensions = nullptr;
    PFN_vkCreateHeadlessSurfaceEXT createHeadlessSurface = nullptr;
    PFN_vkDestroySurfaceKHR destroySurface = nullptr;
};

// Two, not one. The core is told a sync index each frame so it can recycle its
// own per-frame resources without stalling; with a single index it would have
// to wait for the GPU every frame, which is the stutter this exists to avoid.
constexpr uint32_t kSyncIndices = 2;

// Defined in vkhost.cpp.
extern Api gApi;
extern VkPhysicalDevice gGpu;
extern VkDevice gDevice;
extern VkQueue gQueue;
extern uint32_t gQueueFamily;
extern VkCommandBuffer gCmd;
extern VkFence gFence;
extern std::mutex gQueueLock;
extern std::atomic<uint32_t> gSyncIndex;

// Defined in vkbridge.cpp.
extern std::mutex gImageLock;
extern retro_vulkan_image gCoreImage;
extern bool gHaveCoreImage;
extern uint32_t gCoreSrcQueueFamily;
extern std::vector<VkSemaphore> gCoreSemaphores;
extern unsigned gFrameWidth, gFrameHeight;
extern std::string gDescription;
void destroyShared();
bool choosePictureRoute(std::string* err);

}  // namespace cab::vk::state
