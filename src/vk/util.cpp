#include "lc1/vk/common.hpp"
#include <filesystem>
#include <fstream>

namespace lc1 {

std::string result_string(vk::Result result)
{
    return result_string(static_cast<VkResult>(result));
}

std::string result_string(VkResult result)
{
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
        return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    case VK_ERROR_UNKNOWN:
        return "VK_ERROR_UNKNOWN";
    default:
        break;
    }
    return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
}

// apiVersion*, not version*: both callers pass API versions, and only these
// mask off the variant bits.
std::string version_string(uint32_t version)
{
    return std::to_string(vk::apiVersionMajor(version)) + "." +
           std::to_string(vk::apiVersionMinor(version)) + "." +
           std::to_string(vk::apiVersionPatch(version));
}

char const *device_type_string(vk::PhysicalDeviceType type)
{
    switch (type) {
    case vk::PhysicalDeviceType::eIntegratedGpu:
        return "integrated";
    case vk::PhysicalDeviceType::eDiscreteGpu:
        return "discrete";
    case vk::PhysicalDeviceType::eVirtualGpu:
        return "virtual";
    case vk::PhysicalDeviceType::eCpu:
        return "cpu";
    default:
        return "other";
    }
}

std::string read_file(std::filesystem::path const &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Failed to open file: " + path.string());

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace lc1
