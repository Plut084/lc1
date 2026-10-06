#include "lc1/vk/core/device.hpp"

#include "lc1/vk/core/instance.hpp"
#include "lc1/vk/core/memory.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <print>
#include <span>
#include <string_view>

namespace lc1 {
namespace {

bool supports_extensions(vk::raii::PhysicalDevice const &device,
                         std::span<char const *const> required_extensions)
{
    auto const available = device.enumerateDeviceExtensionProperties();
    return std::ranges::all_of(required_extensions, [&](char const *required) {
        return std::ranges::any_of(available, [&](vk::ExtensionProperties const &extension) {
            return std::strcmp(extension.extensionName, required) == 0;
        });
    });
}

std::optional<uint32_t> find_graphics_present_family(vk::raii::PhysicalDevice const &device,
                                                     vk::SurfaceKHR surface)
{
    auto const families = device.getQueueFamilyProperties();
    for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); ++i) {
        if (!(families[i].queueFlags & vk::QueueFlagBits::eGraphics) ||
            !(families[i].queueFlags & vk::QueueFlagBits::eCompute))
            continue;
        if (device.getSurfaceSupportKHR(i, surface) == vk::False) {
            continue;
        }
        return i;
    }
    return std::nullopt;
}

// One declaration of the enabled features, used for both selection and creation.
FeatureChain required_features()
{
    // Set bits by type so adding or reordering chain nodes needs no parallel edit here.
    FeatureChain features;
    features.get<vk::PhysicalDeviceFeatures2>().features = vk::PhysicalDeviceFeatures{
        .sampleRateShading = vk::True,
        .samplerAnisotropy = vk::True,
        // Slang loads heap acceleration-structure addresses as 64-bit integers.
        .shaderInt64 = vk::True,
    };
    features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = vk::True;
    features.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress = vk::True;
    features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = vk::True;
    features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = vk::True;
    features.get<vk::PhysicalDeviceVulkan14Features>().maintenance5 = vk::True;
    features.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure =
        vk::True;
    features.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery = vk::True;
    features.get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>().descriptorHeap = vk::True;
    features.get<vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>().shaderUntypedPointers =
        vk::True;
    features.get<vk::PhysicalDeviceShaderObjectFeaturesEXT>().shaderObject = vk::True;
    return features;
}

bool supports_required_features(vk::raii::PhysicalDevice const &physical_device,
                                FeatureChain const &required)
{
    auto const available = get_features_chain(physical_device);
    auto const &have = available.get<vk::PhysicalDeviceFeatures2>().features;
    auto const &want = required.get<vk::PhysicalDeviceFeatures2>().features;
    auto const &have11 = available.get<vk::PhysicalDeviceVulkan11Features>();
    auto const &want11 = required.get<vk::PhysicalDeviceVulkan11Features>();
    auto const &have13 = available.get<vk::PhysicalDeviceVulkan13Features>();
    auto const &want13 = required.get<vk::PhysicalDeviceVulkan13Features>();
    auto const &have14 = available.get<vk::PhysicalDeviceVulkan14Features>();
    auto const &want14 = required.get<vk::PhysicalDeviceVulkan14Features>();
    // When adding a requested feature, add its support comparison here too.
    return available.get<vk::PhysicalDeviceShaderObjectFeaturesEXT>().shaderObject &&
           available.get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>().descriptorHeap &&
           available.get<vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>()
               .shaderUntypedPointers &&
           available.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress &&
           available.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>()
               .accelerationStructure &&
           available.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery &&
           have.sampleRateShading >= want.sampleRateShading &&
           have.samplerAnisotropy >= want.samplerAnisotropy &&
           have.shaderInt64 >= want.shaderInt64 &&
           have11.shaderDrawParameters >= want11.shaderDrawParameters &&
           have13.synchronization2 >= want13.synchronization2 &&
           have13.dynamicRendering >= want13.dynamicRendering &&
           have14.maintenance5 >= want14.maintenance5;
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

PickedDevice pick_physical_device(vk::raii::Instance const &instance, vk::SurfaceKHR surface,
                                  std::span<char const *const> required_extensions,
                                  FeatureChain const &features)
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
        if (!supports_extensions(device, required_extensions))
            continue;
        if (!supports_required_features(device, features))
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
             "ray query, acceleration structures, buffer device address, all requested features, "
             "and a graphics+compute+present queue family)");
    }
    return best;
}

} // namespace

Device::Device(Instance const &instance, vk::SurfaceKHR surface,
               std::span<char const *const> required_extensions)
    : physical_{nullptr}, device_{nullptr}, alloc_{nullptr}, queue_{nullptr}
{
    try {
        std::vector<char const *> extensions{required_extensions.begin(),
                                             required_extensions.end()};
        extensions.push_back(vk::EXTDescriptorHeapExtensionName);
        extensions.push_back(vk::KHRShaderUntypedPointersExtensionName);
        extensions.push_back(vk::EXTShaderObjectExtensionName);
        for (auto const *extension :
             {vk::KHRAccelerationStructureExtensionName, vk::KHRRayQueryExtensionName,
              vk::KHRDeferredHostOperationsExtensionName})
            if (std::ranges::none_of(extensions, [extension](auto const *existing) {
                    return std::strcmp(existing, extension) == 0;
                }))
                extensions.push_back(extension);
        auto feature_chain = required_features();
        auto picked = pick_physical_device(instance.raii(), surface, extensions, feature_chain);
        auto const available_extensions = picked.device.enumerateDeviceExtensionProperties();
        if (std::ranges::any_of(available_extensions,
                                [](auto const &extension) {
                                    return std::string_view{extension.extensionName.data()} ==
                                           "VK_KHR_portability_subset";
                                }) &&
            std::ranges::none_of(extensions, [](auto const *extension) {
                return std::string_view{extension} == "VK_KHR_portability_subset";
            }))
            extensions.push_back("VK_KHR_portability_subset");
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

        // pEnabledFeatures stays null: features go through the pNext chain.

        vk::DeviceCreateInfo info;
        info.setPNext(&feature_chain.get())
            .setQueueCreateInfos(queue_info)
            .setPEnabledExtensionNames(extensions);
        // We don't set validation layers, as not required (and not necessary).

        device_ = physical_.createDevice(info);

        // instance, device and pVulkanFunctions must stay null: the raii
        // constructor fills them from the raii objects, function pointers
        // included, which is what makes VMA work under VK_NO_PROTOTYPES.
        // vulkanApiVersion must be set, or VMA assumes 1.0.
        vma::AllocatorCreateInfo const alloc_info{
            .flags = vma::AllocatorCreateFlagBits::eBufferDeviceAddress |
                     vma::AllocatorCreateFlagBits::eKhrMaintenance5,
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

Device::Device(Instance const &instance, vk::raii::PhysicalDevice physical,
               std::uint32_t queue_family, DeviceRequirements const &requirements)
    : physical_(std::move(physical)), queue_family_(queue_family), device_([&] {
          FeatureChain feature_chain{};
          for (auto const &feature : requirements.features) {
              feature_value(feature_chain, feature) = vk::True;
          }

          // The strings in requirements outlive this createDevice call.
          std::vector<char const *> extensions;
          extensions.reserve(requirements.extensions.size());
          for (auto const &extension : requirements.extensions) {
              extensions.push_back(extension.c_str());
          }

          vk::DeviceCreateInfo device_ci{
              .pNext = &feature_chain.get(),
          };
          vk::DeviceQueueCreateInfo queue_ci{
              .queueFamilyIndex = queue_family_,
          };
          constexpr auto queue_priority = 1.0F;
          queue_ci.setQueuePriorities(queue_priority);
          device_ci.setQueueCreateInfos(queue_ci);
          device_ci.setPEnabledExtensionNames(extensions);
          return physical_.createDevice(device_ci);
      }()),
      alloc_(instance.raii(), device_,
             vma::AllocatorCreateInfo{
                 .flags = vma::AllocatorCreateFlagBits::eBufferDeviceAddress |
                          vma::AllocatorCreateFlagBits::eKhrMaintenance5,
                 .physicalDevice = *physical_,
                 .vulkanApiVersion = requirements.vulkan_version,
             }),
      queue_(device_.getQueue(queue_family_, 0))
{
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
