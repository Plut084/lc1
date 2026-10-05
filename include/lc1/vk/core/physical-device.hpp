#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/instance.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace lc1 {

using FeatureChain = vk::StructureChain<
    vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
    vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features,
    vk::PhysicalDeviceVulkan14Features, vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
    vk::PhysicalDeviceRayQueryFeaturesKHR, vk::PhysicalDeviceDescriptorHeapFeaturesEXT,
    vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>;

namespace detail {

template <class Chain> struct FeatureChainTraits;

// Derive both the member-pointer alternatives and the query from the chain's types.
// Vulkan 1.0 feature bits are nested in PhysicalDeviceFeatures2::features.
template <class... Features>
struct FeatureChainTraits<vk::StructureChain<vk::PhysicalDeviceFeatures2, Features...>> {
    using Feature =
        std::variant<vk::Bool32 vk::PhysicalDeviceFeatures::*, vk::Bool32 Features::*...>;

    static auto query(vk::raii::PhysicalDevice const &physical_device)
    {
        return physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, Features...>();
    }
};

} // namespace detail

using Extension = std::string;
using Feature = detail::FeatureChainTraits<FeatureChain>::Feature;

struct DeviceRequirements {
    std::uint32_t vulkan_version;
    std::vector<Extension> extensions;
    std::vector<Feature> features;
};

inline FeatureChain get_features_chain(vk::raii::PhysicalDevice const &physical_device)
{
    return detail::FeatureChainTraits<FeatureChain>::query(physical_device);
}

/// @brief Extracts the value of a feature from a feature chain.
template <class Chain> auto &feature_value(Chain &chain, Feature const &feature)
{
    return std::visit(
        [&]<class T>(vk::Bool32 T::*member) -> decltype(auto) {
            if constexpr (std::same_as<T, vk::PhysicalDeviceFeatures>) {
                return chain.template get<vk::PhysicalDeviceFeatures2>().features.*member;
            }
            else {
                return chain.template get<T>().*member;
            }
        },
        feature);
}

inline std::vector<vk::raii::PhysicalDevice>
filter_physical_devices(Instance const &instance, DeviceRequirements const &requirements)
{
    auto get_available_extensions = [](vk::raii::PhysicalDevice const &physical_device) {
        std::set<Extension> available_extensions;
        for (auto const &ext_props : physical_device.enumerateDeviceExtensionProperties()) {
            available_extensions.insert(ext_props.extensionName);
        }
        return available_extensions;
    };

    std::vector<vk::raii::PhysicalDevice> filtered;

    for (auto const &physical_device : instance.raii().enumeratePhysicalDevices()) {
        auto const available_extensions = get_available_extensions(physical_device);
        auto extension_supported = [&](Extension const &extension) {
            return available_extensions.contains(extension);
        };
        auto const features_chain = get_features_chain(physical_device);
        auto feature_supported = [&](Feature const &feature) {
            return feature_value(features_chain, feature) == vk::True;
        };
        if (requirements.vulkan_version > physical_device.getProperties().apiVersion) {
            continue;
        }
        if (!std::ranges::all_of(requirements.extensions, extension_supported)) {
            continue;
        }
        if (!std::ranges::all_of(requirements.features, feature_supported)) {
            continue;
        }
        filtered.push_back(physical_device);
    }

    return filtered;
}

} // namespace lc1
