#include "gpu.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// VK_NO_PROTOTYPES because nothing here links the loader — every entry point
// is resolved through vkGetInstanceProcAddr after dlopen. See gpu.h.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

namespace cab::gpu {
namespace {

VulkanCaps gCaps;
bool gProbed = false;

// The handle is deliberately NOT closed. The core host needs the same loader a
// moment later, and dlclose on a Vulkan ICD is a well-known way to lose a
// process — drivers register atexit handlers and thread-local state that does
// not survive being unmapped. It costs one mapping for the life of the
// process, which a console can afford.
void* gLoader = nullptr;
PFN_vkGetInstanceProcAddr gGetInstanceProcAddr = nullptr;

// One GPU the loader lists, software ones left out, in the loader's order.
// Everything VulkanCaps says is read for every device once, so the choice
// can be made again when the screen's GPU becomes known (screenIs) without
// a second instance.
struct Device {
    int index = -1;
    std::string name;
    std::string uuid;
    uint32_t vendorId = 0;
    uint32_t apiVersion = 0;
    bool discrete = false;
    uint32_t computeUnits = 0;
    uint32_t topClockMhz = 0;
    bool externalMemoryFd = false;
    bool externalMemoryDmaBuf = false;
    bool drmFormatModifier = false;
};
std::vector<Device> gDevices;
// The screen's GPU, from screenIs. Empty until told, and in a probe with no
// window.
std::string gScreenUuid;

constexpr size_t kNone = static_cast<size_t>(-1);

bool has(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const auto& e : list)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

// The highest clock in an amdgpu `pp_dpm_sclk`, whose lines read
// "2: 2900Mhz *". 0 when the file is missing or says nothing.
uint32_t topSclkMhz(const std::string& path) {
    std::ifstream in(path);
    uint32_t top = 0;
    std::string line;
    while (std::getline(in, line)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const unsigned long mhz = std::strtoul(line.c_str() + colon + 1, nullptr, 10);
        if (mhz > top) top = static_cast<uint32_t>(mhz);
    }
    return top;
}

// The same device's top clock from the kernel. By PCI address when Vulkan
// gave one; otherwise the first card with the same vendor and device ids,
// which is the same chip whichever of two identical cards it is.
uint32_t amdTopClockMhz(const std::string& pciAddress, uint32_t vendor, uint32_t device) {
    if (!pciAddress.empty())
        return topSclkMhz("/sys/bus/pci/devices/" + pciAddress + "/pp_dpm_sclk");
    for (int i = 0; i < 16; ++i) {
        const std::string dev = "/sys/class/drm/card" + std::to_string(i) + "/device/";
        unsigned v = 0, d = 0;
        std::ifstream fv(dev + "vendor"), fd(dev + "device");
        if (!(fv >> std::hex >> v) || !(fd >> std::hex >> d)) continue;
        if (v == vendor && d == device) return topSclkMhz(dev + "pp_dpm_sclk");
    }
    return 0;
}

// THE RULE (#659, gpu.h): the GPU the screen is on. With one GPU, that one,
// whatever the screen says. Otherwise the old order, a discrete card first
// and then the first listed, with `why` saying what kept the screen's GPU
// from being matched. kNone with no device at all.
size_t pick(const std::vector<Device>& devices, const std::string& screen, std::string* why) {
    if (devices.empty()) return kNone;
    if (devices.size() == 1) {
        *why = "the only GPU here";
        return 0;
    }
    if (!screen.empty()) {
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].uuid == screen) {
                *why = "the screen is on it";
                return i;
            }
        }
    }
    size_t best = 0;
    for (size_t i = 1; i < devices.size(); ++i)
        if (devices[i].discrete && !devices[best].discrete) best = i;
    *why = std::string(devices[best].discrete ? "a discrete card" : "the first GPU listed") +
           (screen.empty() ? "; the screen's GPU is not known"
                           : "; the screen's GPU is not among the Vulkan devices");
    return best;
}

// Fills gCaps from the pick, and says so in the log when the answer is new
// or has moved.
void choose() {
    std::string why;
    const size_t i = pick(gDevices, gScreenUuid, &why);
    if (i == kNone) return;
    const Device& d = gDevices[i];
    const bool changed = d.index != gCaps.deviceIndex || why != gCaps.why;
    gCaps.deviceName = d.name;
    gCaps.deviceIndex = d.index;
    gCaps.deviceUuid = d.uuid;
    gCaps.why = why;
    gCaps.apiVersion = d.apiVersion;
    gCaps.discrete = d.discrete;
    gCaps.vendorId = d.vendorId;
    gCaps.computeUnits = d.computeUnits;
    gCaps.topClockMhz = d.topClockMhz;
    gCaps.externalMemoryFd = d.externalMemoryFd;
    gCaps.externalMemoryDmaBuf = d.externalMemoryDmaBuf;
    gCaps.drmFormatModifier = d.drmFormatModifier;

    // A device with no way to hand a picture to GL can run a core and can
    // never show one, so it does not count as available. See gpu.h. The
    // preferred route (a GL memory object over an opaque fd) needs only the
    // fd extension; the dmabuf one is for the EGL route alone (#489, EMU-02).
    if (!d.externalMemoryFd) {
        gCaps.available = false;
        gCaps.reason =
            "this Vulkan device cannot export an image's memory, so nothing "
            "it drew could reach the screen";
    } else {
        gCaps.available = true;
        gCaps.reason.clear();
    }
    if (changed)
        std::fprintf(stderr, "[gpu] games draw on %s: %s\n", d.name.c_str(), why.c_str());
}

void probe() {
    gProbed = true;

    gLoader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!gLoader) {
        gCaps.reason = "no libvulkan.so.1 on this machine";
        return;
    }
    gGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(gLoader, "vkGetInstanceProcAddr"));
    if (!gGetInstanceProcAddr) {
        gCaps.reason = "libvulkan.so.1 has no vkGetInstanceProcAddr";
        return;
    }

    auto global = [&](const char* name) {
        return gGetInstanceProcAddr(nullptr, name);
    };
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(global("vkCreateInstance"));
    if (!createInstance) {
        gCaps.reason = "the Vulkan loader would not hand over vkCreateInstance";
        return;
    }

    // Apply 1.1 rather than 1.0. Everything the interop path needs is core in
    // 1.1, and asking for it here is how a driver that only does 1.0 says no
    // now rather than half way through creating an image.
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "CabinetOS";
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    if (createInstance(&ici, nullptr, &instance) != VK_SUCCESS) {
        gCaps.reason = "a Vulkan loader is present and would not create an instance";
        return;
    }

    auto inst = [&](const char* name) { return gGetInstanceProcAddr(instance, name); };
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        inst("vkEnumeratePhysicalDevices"));
    auto getProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        inst("vkGetPhysicalDeviceProperties"));
    auto devExts = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        inst("vkEnumerateDeviceExtensionProperties"));
    auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(inst("vkDestroyInstance"));

    if (!enumerate || !getProps || !devExts) {
        gCaps.reason = "the Vulkan instance is missing entry points it must have";
        if (destroyInstance) destroyInstance(instance, nullptr);
        return;
    }

    uint32_t count = 0;
    enumerate(instance, &count, nullptr);
    if (count == 0) {
        // THIS IS THE TEST VM'S ANSWER, and it is the honest one: a Vulkan
        // driver stack is installed and there is no device behind it. Saying
        // "no Vulkan" without saying which half is missing is what sent this
        // project looking at a GPU that was working fine.
        gCaps.reason = "a Vulkan driver is present and reports no devices";
        destroyInstance(instance, nullptr);
        return;
    }
    std::vector<VkPhysicalDevice> devices(count);
    enumerate(instance, &count, devices.data());
    // Core in 1.1, which the instance asked for.
    auto getProps2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        inst("vkGetPhysicalDeviceProperties2"));

    // Every device but a software one. llvmpipe enumerates as CPU and running
    // a PlayStation 2 on it would be worse than saying no — it is the same
    // trap as cage's software rendering, where the machine looks like it is
    // working and every judgement made on it is worthless.
    gDevices.clear();
    for (size_t i = 0; i < devices.size(); ++i) {
        VkPhysicalDeviceProperties p{};
        getProps(devices[i], &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
        Device d;
        d.index = static_cast<int>(i);
        d.name = p.deviceName;
        d.vendorId = p.vendorID;
        d.apiVersion = p.apiVersion;
        d.discrete = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

        uint32_t extCount = 0;
        devExts(devices[i], nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        if (extCount) devExts(devices[i], nullptr, &extCount, exts.data());
        d.externalMemoryFd = has(exts, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        d.externalMemoryDmaBuf = has(exts, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
        d.drmFormatModifier = has(exts, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);

        // Its UUID, for the screen's match and for an emulator told a device
        // that way (Cemu's `vkDevice`). Asked in the same call: its shader
        // units and PCI address, each only when the device has the extension
        // that defines it.
        std::string pciAddress;
        if (getProps2) {
            VkPhysicalDeviceIDProperties id{};
            id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
            VkPhysicalDeviceShaderCoreProperties2AMD cores{};
            cores.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD;
            VkPhysicalDevicePCIBusInfoPropertiesEXT pci{};
            pci.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
            const bool hasCores = has(exts, VK_AMD_SHADER_CORE_PROPERTIES_2_EXTENSION_NAME);
            const bool hasPci = has(exts, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME);
            void** next = &id.pNext;
            if (hasCores) { *next = &cores; next = &cores.pNext; }
            if (hasPci) { *next = &pci; next = &pci.pNext; }
            VkPhysicalDeviceProperties2 p2{};
            p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            p2.pNext = &id;
            getProps2(devices[i], &p2);
            d.uuid = uuidString(id.deviceUUID);
            if (hasCores) d.computeUnits = cores.activeComputeUnitCount;
            if (hasPci) {
                char addr[32];
                std::snprintf(addr, sizeof addr, "%04x:%02x:%02x.%x", pci.pciDomain, pci.pciBus,
                              pci.pciDevice, pci.pciFunction);
                pciAddress = addr;
            }
        }
        if (d.vendorId == 0x1002)
            d.topClockMhz = amdTopClockMhz(pciAddress, p.vendorID, p.deviceID);
        gDevices.push_back(d);
    }
    destroyInstance(instance, nullptr);

    if (gDevices.empty()) {
        gCaps.reason = "the only Vulkan device here is a software one";
        return;
    }
    choose();
}

}  // namespace

const VulkanCaps& vulkan() {
    if (!gProbed) probe();
    return gCaps;
}

void screenIs(const std::string& glDeviceUuid) {
    gScreenUuid = glDeviceUuid;
    if (!gProbed) probe();
    else if (!gDevices.empty()) choose();
}

std::string uuidString(const uint8_t* bytes) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 16; ++i) {
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 15]);
    }
    return out;
}

void* instanceProcAddr() {
    if (!gProbed) probe();
    return reinterpret_cast<void*>(gGetInstanceProcAddr);
}

std::string versionString(uint32_t packed) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u", VK_API_VERSION_MAJOR(packed),
                  VK_API_VERSION_MINOR(packed), VK_API_VERSION_PATCH(packed));
    return buf;
}

void report() {
    const VulkanCaps& v = vulkan();
    std::printf("Vulkan\n");
    std::printf("  looked for  libvulkan.so.1, a non-software device, and the\n");
    std::printf("              extension that lets a rendered image reach GL\n");
    for (const Device& d : gDevices)
        std::printf("  listed      %d: %s%s, uuid %s\n", d.index, d.name.c_str(),
                    d.discrete ? " (discrete)" : "", d.uuid.c_str());
    if (gScreenUuid.empty())
        std::printf("  screen      GPU not known (a probe has no window to ask)\n");
    else
        std::printf("  screen      on uuid %s\n", gScreenUuid.c_str());
    if (!v.available) {
        std::printf("  NOT USABLE  %s\n", v.reason.c_str());
        if (!v.deviceName.empty()) {
            std::printf("  device      %s: %s\n", v.deviceName.c_str(), v.why.c_str());
            std::printf("  external memory fd      %s\n", v.externalMemoryFd ? "yes" : "NO");
            std::printf("  external memory dma_buf %s\n", v.externalMemoryDmaBuf ? "yes" : "NO");
        }
        std::printf("\n  The console runs on OpenGL ES here, which plays every core it\n");
        std::printf("  ships. PlayStation 2 and GameCube need the Vulkan path.\n");
        return;
    }
    std::printf("  USABLE\n");
    std::printf("  device      %s%s: %s\n", v.deviceName.c_str(), v.discrete ? " (discrete)" : "",
                v.why.c_str());
    std::printf("  size        %u shader units, top clock %u MHz (0: not reported)\n",
                v.computeUnits, v.topClockMhz);
    std::printf("  api         %s\n", versionString(v.apiVersion).c_str());
    std::printf("  external memory fd      yes\n");
    std::printf("  external memory dma_buf %s\n", v.externalMemoryDmaBuf ? "yes" : "no");
    std::printf("  drm format modifier     %s\n", v.drmFormatModifier ? "yes" : "no");
}

int test() {
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++failures;
    };
    auto device = [](int index, const char* name, const char* uuid, bool discrete) {
        Device d;
        d.index = index;
        d.name = name;
        d.uuid = uuid;
        d.discrete = discrete;
        return d;
    };
    // The tester's laptop (#659): a Renoir on the panel, an RTX 3070 beside it.
    const Device renoir = device(0, "AMD Radeon Graphics (RADV RENOIR)", "a1", false);
    const Device rtx = device(1, "NVIDIA GeForce RTX 3070 Laptop GPU (NVK GA104)", "b2", true);
    // A desktop with two AMD cards, and a PC with two integrated GPUs.
    const Device cardA = device(0, "AMD Radeon RX 7800 XT (RADV NAVI32)", "c3", true);
    const Device cardB = device(1, "AMD Radeon RX 6600 (RADV NAVI23)", "d4", true);
    const Device intel = device(1, "Intel(R) Graphics (ADL-N)", "e5", false);
    auto is = [&](const std::vector<Device>& devices, const char* screen, size_t want,
                  const char* why) {
        std::string got;
        const size_t i = pick(devices, screen, &got);
        std::string names;
        for (const Device& d : devices) names += (names.empty() ? "" : " + ") + d.name;
        check(i == want && got == why,
              names + ", screen '" + screen + "' -> " +
                  (i == kNone ? std::string("none") : devices[i].name) + ": " + got);
    };
    // One GPU, the A9: that one, whatever the screen says or cannot say.
    is({renoir}, "", 0, "the only GPU here");
    is({renoir}, "a1", 0, "the only GPU here");
    is({renoir}, "zz", 0, "the only GPU here");
    // The laptop's own panel, then a TV on its HDMI port, which is on the card.
    is({renoir, rtx}, "a1", 0, "the screen is on it");
    is({renoir, rtx}, "b2", 1, "the screen is on it");
    is({rtx, renoir}, "a1", 1, "the screen is on it");
    // The screen's GPU not known, or not a Vulkan device: the old order.
    is({renoir, rtx}, "", 1, "a discrete card; the screen's GPU is not known");
    is({renoir, rtx}, "zz", 1, "a discrete card; the screen's GPU is not among the Vulkan devices");
    // Two AMD cards: the cable decides; with no screen known, the first card.
    is({cardA, cardB}, "d4", 1, "the screen is on it");
    is({cardA, cardB}, "c3", 0, "the screen is on it");
    is({cardA, cardB}, "", 0, "a discrete card; the screen's GPU is not known");
    // No discrete card at all: the first listed.
    is({renoir, intel}, "", 0, "the first GPU listed; the screen's GPU is not known");
    is({renoir, intel}, "e5", 1, "the screen is on it");
    // Nothing usable.
    {
        std::string got;
        check(pick({}, "a1", &got) == kNone, "no device -> none");
    }
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}

}  // namespace cab::gpu
