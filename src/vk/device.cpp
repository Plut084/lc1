#include "lc1/vk/device.hpp"

#include "lc1/vk/instance.hpp"
#include "lc1/vk/memory.hpp"
#include "lc1/window.hpp"
#include "vk/checks.hpp"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <cstring>
#include <optional>
#include <print>

namespace lc1 {
namespace {

bool has_swapchain_extension(vk::raii::PhysicalDevice const &device)
{
    return std::ranges::any_of(
        device.enumerateDeviceExtensionProperties(), [](vk::ExtensionProperties const &extension) {
            return std::strcmp(extension.extensionName, vk::KHRSwapchainExtensionName) == 0;
        });
}

std::optional<uint32_t> find_graphics_present_family(vk::raii::PhysicalDevice const &device,
                                                     vk::SurfaceKHR surface)
{
    auto const families = device.getQueueFamilyProperties();
    for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); ++i) {
        if (!(families[i].queueFlags & vk::QueueFlagBits::eGraphics))
            continue;
        if (device.getSurfaceSupportKHR(i, surface) == vk::False) {
            continue;
        }
        return i;
    }
    return std::nullopt;
}

bool supports_required_features(vk::raii::PhysicalDevice const &physical_device)
{
    // Query through the *aggregate* Vulkan13Features struct. At device creation
    // we enable the same aggregate; chaining the promoted
    // VkPhysicalDeviceDynamicRenderingFeatures / ...Synchronization2Features
    // alongside it is explicitly forbidden.
    auto const chain =
        physical_device
            .getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features,
                          vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    auto const &features = chain.get<vk::PhysicalDeviceFeatures2>().features;
    auto const &features13 = chain.get<vk::PhysicalDeviceVulkan13Features>();
    auto const &features_extended_dynamic_state =
        chain.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    return features.sampleRateShading == vk::True && features13.dynamicRendering == vk::True &&
           features13.synchronization2 == vk::True &&
           features_extended_dynamic_state.extendedDynamicState == vk::True;
}

int score_device(vk::PhysicalDeviceType type)
{
    switch (type) {
    case vk::PhysicalDeviceType::eDiscreteGpu:
        return 300;
    case vk::PhysicalDeviceType::eIntegratedGpu:
        return 200;
    case vk::PhysicalDeviceType::eVirtualGpu:
        return 100;
    default:
        return 50;
    }
}

// The queue family is found while filtering, so it is returned rather than
// looked up again by the caller.
struct PickedDevice {
    vk::raii::PhysicalDevice device;
    uint32_t queue_family;
};

PickedDevice pick_physical_device(vk::raii::Instance const &instance, vk::SurfaceKHR surface)
{
    auto devices = instance.enumeratePhysicalDevices();
    if (devices.empty())
        fail("no Vulkan-capable physical devices found");

    // Optional override for multi-GPU machines: LC1_DEVICE=<substring of
    // deviceName>.
    char const *const want = std::getenv("LC1_DEVICE");
    bool const filtered = want != nullptr && *want != '\0';

    PickedDevice best{.device = nullptr, .queue_family = 0};
    int best_score = -1; // negative means nothing has been picked yet

    for (auto &device : devices) {
        auto const props = device.getProperties();

        // llvmpipe is enumerated as a real physical device here, and nothing
        // reorders devices for us: VK_LAYER_NV_optimus is dormant unless
        // __NV_PRIME_RENDER_OFFLOAD=1 is set, and VK_LAYER_MESA_device_select
        // only filters. Excluding CPU outright is what stops this game from
        // silently rendering on the CPU rasterizer.
        if (props.deviceType == vk::PhysicalDeviceType::eCpu)
            continue;
        if (props.apiVersion < vk::ApiVersion14)
            continue;
        if (filtered && std::strstr(props.deviceName, want) == nullptr)
            continue;
        if (!has_swapchain_extension(device))
            continue;
        if (!supports_required_features(device))
            continue;

        auto const family = find_graphics_present_family(device, surface);
        if (!family)
            continue;

        int const score = score_device(props.deviceType);
        if (score > best_score) {
            best_score = score;
            best = {.device = std::move(device), .queue_family = *family};
        }
    }

    // best_score, not the handle, says whether anything was picked: a raii
    // wrapper offers no null-handle comparison.
    if (best_score < 0) {
        if (filtered) {
            fail("LC1_DEVICE=\"{}\" matched no usable device", want);
        }
        fail("no suitable Vulkan 1.4 device found (need a non-CPU device with "
             "VK_KHR_swapchain, dynamicRendering, synchronization2, "
             "sampleRateShading, and a graphics+present queue family)");
    }
    return best;
}

} // namespace

Device::Device(Instance const &instance, Window const &window,
               std::vector<char const *> required_extensions)
    : physical_{nullptr}, surface_{nullptr}, device_{nullptr}, alloc_{nullptr}, queue_{nullptr}
{
    try {
        // GLFW creates the surface through the C API; adopt it immediately so
        // the raii object owns it.
        VkSurfaceKHR raw_surface = VK_NULL_HANDLE;
        VK_CHECK(
            glfwCreateWindowSurface(instance.handle(), window.handle(), nullptr, &raw_surface));
        surface_ = vk::raii::SurfaceKHR{instance.raii(), raw_surface};

        auto picked = pick_physical_device(instance.raii(), *surface_);
        physical_ = std::move(picked.device);
        queue_family_ = picked.queue_family;

        auto const props = physical_.getProperties();

        auto const props2 = physical_.getProperties2<vk::PhysicalDeviceProperties2,
                                                     vk::PhysicalDeviceDriverProperties>();
        auto const &driver = props2.get<vk::PhysicalDeviceDriverProperties>();

        // deviceName and driverName are std::array<char, N> underneath, which
        // std::format would print as a list of chars; .data() prints the
        // string.
        std::println("[lc1] device:   {} ({}, {})", props.deviceName.data(),
                     device_type_string(props.deviceType), driver.driverName.data());
        // driverVersion lives on VkPhysicalDeviceProperties, not on the driver
        // properties struct. Its encoding is vendor-defined, so print it as
        // hex.
        std::println("[lc1] api:      {}, driver version 0x{:08x}, conformance "
                     "{}.{}.{}.{}",
                     version_string(props.apiVersion), props.driverVersion,
                     driver.conformanceVersion.major, driver.conformanceVersion.minor,
                     driver.conformanceVersion.subminor, driver.conformanceVersion.patch);

        float const priority = 1.0F;
        vk::DeviceQueueCreateInfo queue_info;
        queue_info.setQueueFamilyIndex(queue_family_).setQueueCount(1).setQueuePriorities(priority);

        vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                           vk::PhysicalDeviceVulkan13Features,
                           vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
            feature_chain{
                // sampleRateShading is what makes the pipeline's
                // VkPipelineMultisampleStateCreateInfo::sampleShadingEnable
                // legal, and the pipeline always sets it.
                {.features = {.sampleRateShading = 1, .samplerAnisotropy = 1}},
                {.shaderDrawParameters = 1},
                {.synchronization2 = 1, .dynamicRendering = 1},
                {.extendedDynamicState = 1},
            };

        // pEnabledFeatures stays null: features go through the pNext chain.

        vk::DeviceCreateInfo info;
        info.setPNext(&feature_chain.get())
            .setQueueCreateInfos(queue_info)
            .setPEnabledExtensionNames(required_extensions);
        // We don't set validation layers, as not required (and not necessary).

        check_extensions(required_extensions, physical_);

        device_ = physical_.createDevice(info);

        // instance, device and pVulkanFunctions must stay null: the raii
        // constructor fills them from the raii objects, function pointers
        // included, which is what makes VMA work under VK_NO_PROTOTYPES.
        // vulkanApiVersion must be set, or VMA assumes 1.0.
        vma::AllocatorCreateInfo const alloc_info{
            .physicalDevice = *physical_,
            .vulkanApiVersion = vk::ApiVersion14,
        };
        alloc_ = vma::raii::Allocator{instance.raii(), device_, alloc_info};

        queue_ = device_.getQueue(queue_family_, 0);
    }
    catch (vk::SystemError const &error) {
        fail("Vulkan device setup failed: {}", error.what());
    }
}

void Device::wait_idle() const
{
    try {
        device_.waitIdle();
    }
    catch (vk::SystemError const &error) {
        fail("vkDeviceWaitIdle failed: {}", error.what());
    }
}

vk::SampleCountFlagBits Device::max_sample_count() const
{
    auto physical_props = physical_.getProperties();
    vk::SampleCountFlags counts = physical_props.limits.framebufferColorSampleCounts &
                                  physical_props.limits.framebufferDepthSampleCounts;
    if (counts & vk::SampleCountFlagBits::e64)
        return vk::SampleCountFlagBits::e64;
    if (counts & vk::SampleCountFlagBits::e32)
        return vk::SampleCountFlagBits::e32;
    if (counts & vk::SampleCountFlagBits::e16)
        return vk::SampleCountFlagBits::e16;
    if (counts & vk::SampleCountFlagBits::e8)
        return vk::SampleCountFlagBits::e8;
    if (counts & vk::SampleCountFlagBits::e4)
        return vk::SampleCountFlagBits::e4;
    if (counts & vk::SampleCountFlagBits::e2)
        return vk::SampleCountFlagBits::e2;
    return vk::SampleCountFlagBits::e1;
}

} // namespace lc1
