#include "lc1/vk/instance.hpp"

#include "lc1/vk/loader.hpp"
#include "vk/checks.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <vector>

namespace lc1 {
namespace {

// The signature matches vk::PFN_DebugUtilsMessengerCallbackEXT so it can be
// handed to setPfnUserCallback without a cast -- that setter's C-typedef
// overload is deprecated in this header version.
VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT /*types*/,
    vk::DebugUtilsMessengerCallbackDataEXT const *data, void * /*user_data*/)
{
    // hpp hands this over as the FlagBits enum even though the ABI is a
    // bitmask, so widen it before testing individual bits.
    auto const severity_flags = vk::DebugUtilsMessageSeverityFlagsEXT{severity};

    char const *label = "info";
    if (severity_flags & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
        label = "ERROR";
    }
    else if (severity_flags &
             vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
        label = "warning";
    }

    // pMessageIdName carries the VUID -- by far the most useful field here.
    std::println(stderr, "[vk {}] {}\n    {}", label,
                 data->pMessageIdName != nullptr ? data->pMessageIdName
                                                 : "(no id)",
                 data->pMessage != nullptr ? data->pMessage : "(no message)");

    if (severity_flags & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
        // A validation error means we are already in undefined territory. Stop
        // at the first one rather than scrolling past hundreds of cascading
        // reports.
        std::fflush(stderr);
        std::abort();
    }
    return VK_FALSE;
}

} // namespace

Instance::Instance(VulkanLoader const &loader,
                   std::vector<char const *> extensions,
                   std::vector<char const *> layers)
{
    try {
        vk::raii::Context const &context = loader.raii();

        auto const loader_version = context.enumerateInstanceVersion();
        if (loader_version < vk::ApiVersion14) {
            fail("Vulkan loader reports " + version_string(loader_version) +
                 "; this build requires 1.4 or newer");
        }

        // 1.4 is both the target and the minimum; device.cpp checks the
        // device against the same version.

        vk::InstanceCreateInfo info;

        constexpr vk::ApplicationInfo app_info{
            .pApplicationName = "lc1",
            .applicationVersion = vk::makeVersion(0, 1, 0),
            .pEngineName = "lc1",
            .engineVersion = vk::makeVersion(0, 1, 0),
            .apiVersion = vk::ApiVersion14};
        info.setPApplicationInfo(&app_info);

        // Enables flag and extension for MacOS portability.
        info.setFlags(vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR);
        extensions.push_back(vk::KHRPortabilityEnumerationExtensionName);

        info.setPEnabledExtensionNames(extensions);
        info.setPEnabledLayerNames(layers);

        // TODO: Temporarily disabled, maybe turn on later.
        /*if (false) {
            // Synchronization validation is the tool that catches the failure
            // mode this skeleton is most exposed to: a missing or wrong-stage
            // barrier, which is a silent no-op until it corrupts a frame on
            // someone else's driver. It is slow and very chatty, hence debug
            // builds only. Never combine it with
            // VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT -- they are
            // incompatible.
            std::array const enabled_features{
                vk::ValidationFeatureEnableEXT::eSynchronizationValidation,
            };
            vk::ValidationFeaturesEXT validation_features;
            validation_features.setEnabledValidationFeatures(enabled_features);
            info.setPNext(&validation_features);
        }*/

        check_extensions(extensions, context);
        check_layers(layers, context);

        handle_ = context.createInstance(info);
    }
    catch (vk::SystemError const &error) {
        fail(std::string("Vulkan instance setup failed: ") + error.what());
    }
    catch (lc1::Error const &error) {
        fail(std::string("Vulkan instance setup failed: ") + error.what());
    }
}

void Instance::setup_debug_messenger()
{
    vk::DebugUtilsMessengerCreateInfoEXT messenger_info;
    // WARNING|ERROR only, not VERBOSE: sync validation is loud enough
    // already.
    messenger_info
        .setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
        .setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)
        .setPfnUserCallback(debug_callback);
    messenger_ = vk::raii::DebugUtilsMessengerEXT(handle_, messenger_info);
}

} // namespace lc1
