#pragma once

#include "lc1/vk/common.hpp"

namespace lc1 {

inline bool
extension_available(std::vector<vk::ExtensionProperties> const &extensions,
                    std::string_view name)
{
    return std::ranges::contains(extensions, name,
                                 [](vk::ExtensionProperties const &ext) {
                                     return std::string_view{ext.extensionName};
                                 });
}

inline bool layer_available(std::vector<vk::LayerProperties> const &layers,
                            std::string_view name)
{
    return std::ranges::contains(layers, name,
                                 [](vk::LayerProperties const &layer) {
                                     return std::string_view{layer.layerName};
                                 });
}

inline void check_extensions(std::vector<char const *> const &extensions,
                             vk::raii::Context const &context)
{
    auto available_extensions = context.enumerateInstanceExtensionProperties();
    for (char const *name : extensions) {
        if (!extension_available(available_extensions, name)) {
            fail(std::string("Vulkan instance extension not available: ") +
                 name);
        }
    }
}

inline void check_extensions(std::vector<char const *> const &extensions,
                             vk::raii::PhysicalDevice const &device)
{
    auto available_extensions = device.enumerateDeviceExtensionProperties();
    for (char const *name : extensions) {
        if (!extension_available(available_extensions, name)) {
            fail(std::string("Vulkan device extension not available: ") + name);
        }
    }
}

inline void check_layers(std::vector<char const *> const &layers,
                         vk::raii::Context const &context)
{
    auto available_layers = context.enumerateInstanceLayerProperties();
    for (char const *name : layers) {
        if (!layer_available(available_layers, name)) {
            fail(std::string("Vulkan instance layer not available: ") + name);
        }
    }
}

} // namespace lc1
