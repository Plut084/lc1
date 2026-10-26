#include "lc1/vk/presentation/swapchain-policy.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace lc1 {

vk::SurfaceFormatKHR pick_surface_format(std::span<vk::SurfaceFormatKHR const> formats)
{
    if (formats.empty()) {
        fail("surface reports no supported formats");
    }
    // A sole UNDEFINED format means the application may choose the format.
    // The reported color space still applies.
    if (formats.size() == 1 && formats.front().format == vk::Format::eUndefined) {
        return vk::SurfaceFormatKHR{.format = vk::Format::eB8G8R8A8Srgb,
                                    .colorSpace = formats.front().colorSpace};
    }
    for (auto const &format : formats) {
        if (format.format == vk::Format::eB8G8R8A8Srgb &&
            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return format;
        }
    }
    return formats.front();
}

vk::PresentModeKHR pick_present_mode(std::span<vk::PresentModeKHR const> modes)
{
    for (auto const preferred : {vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eFifo}) {
        if (std::ranges::find(modes, preferred) != modes.end()) {
            return preferred;
        }
    }
    fail("surface reports neither MAILBOX nor FIFO present mode");
}

vk::CompositeAlphaFlagBitsKHR pick_composite_alpha(vk::CompositeAlphaFlagsKHR supported)
{
    constexpr std::array candidates{
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
        vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
        vk::CompositeAlphaFlagBitsKHR::eInherit,
    };
    for (auto const candidate : candidates) {
        if (supported & candidate) {
            return candidate;
        }
    }
    fail("surface reports no supported composite alpha mode");
}

std::uint32_t pick_image_count(vk::SurfaceCapabilitiesKHR const &caps)
{
    // A zero maxImageCount means there is no surface-imposed upper bound.
    auto const maximum =
        caps.maxImageCount == 0 ? std::numeric_limits<std::uint32_t>::max() : caps.maxImageCount;
    if (caps.minImageCount == 0 || caps.minImageCount > maximum) {
        fail("surface reports invalid image count limits");
    }
    return caps.minImageCount < maximum ? caps.minImageCount + 1 : caps.minImageCount;
}

bool compute_extent(vk::Extent2D framebuffer, vk::SurfaceCapabilitiesKHR const &caps,
                    vk::Extent2D *out)
{
    if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        // The compositor is telling us the size. Use it verbatim: substituting
        // the framebuffer size here is what makes Wayland report
        // VK_SUBOPTIMAL_KHR on every frame, forever.
        *out = caps.currentExtent;
        return out->width != 0 && out->height != 0;
    }

    if (framebuffer.width == 0 || framebuffer.height == 0)
        return false;

    out->width =
        std::clamp(framebuffer.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    out->height =
        std::clamp(framebuffer.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return out->width != 0 && out->height != 0;
}

} // namespace lc1
