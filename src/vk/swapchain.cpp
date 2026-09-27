#include "lc1/vk/swapchain.hpp"

#include "lc1/vk/device.hpp"
#include "lc1/window.hpp"

#include <algorithm>
#include <print>
#include <vector>

namespace lc1 {
namespace {

// Prefers eB8G8R8A8Srgb and eSrgbNonlinear.
vk::SurfaceFormatKHR pick_surface_format(std::vector<vk::SurfaceFormatKHR> const &formats)
{
    // Some drivers report exactly one VK_FORMAT_UNDEFINED entry meaning "no
    // preference". Never pass UNDEFINED onward to vkCreateSwapchainKHR.
    if (formats.size() == 1 && formats[0].format == vk::Format::eUndefined) {
        return vk::SurfaceFormatKHR{.format = vk::Format::eB8G8R8A8Srgb,
                                    .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear};
    }
    for (vk::SurfaceFormatKHR const &format : formats) {
        if (format.format == vk::Format::eB8G8R8A8Srgb &&
            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return format;
        }
    }
    return formats[0];
}

char const *present_mode_name(vk::PresentModeKHR mode)
{
    switch (mode) {
    case vk::PresentModeKHR::eImmediate:
        return "immediate";
    case vk::PresentModeKHR::eMailbox:
        return "mailbox";
    case vk::PresentModeKHR::eFifo:
        return "fifo";
    case vk::PresentModeKHR::eFifoRelaxed:
        return "fifo-relaxed";
    default:
        return "other";
    }
}

vk::PresentModeKHR pick_present_mode(std::vector<vk::PresentModeKHR> const &modes)
{
    for (vk::PresentModeKHR mode : modes) {
        if (mode == vk::PresentModeKHR::eMailbox)
            return mode;
    }
    // The only mode the spec guarantees to exist.
    return vk::PresentModeKHR::eFifo;
}

vk::CompositeAlphaFlagBitsKHR pick_composite_alpha(vk::CompositeAlphaFlagsKHR supported)
{
    constexpr std::array<vk::CompositeAlphaFlagBitsKHR, 4> composite_alpha_candidates{
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
        vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
        vk::CompositeAlphaFlagBitsKHR::eInherit,
    };
    for (vk::CompositeAlphaFlagBitsKHR candidate : composite_alpha_candidates) {
        if (supported & candidate)
            return candidate;
    }
    fail("surface reports no supported composite alpha mode");
}

std::uint32_t pick_image_count(vk::SurfaceCapabilitiesKHR const &caps)
{
    // One more than the minimum (MAILBOX needs the extra buffer to actually
    // queue a frame) but never above maxImageCount when that is nonzero.
    auto image_count = caps.minImageCount + 1;
    // Note that when caps.maxImageCount is 0, it means there is no maximum.
    if (caps.maxImageCount != 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }
    return image_count;
}

// Returns false when the window is minimized or the surface has no usable
// extent. Zero extent is not merely useless -- VkRenderingInfo::renderArea
// requires a nonzero extent, so we must not record a frame at all in that case.
bool compute_extent(Window const &window, vk::SurfaceCapabilitiesKHR const &caps, vk::Extent2D *out)
{
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        // The compositor is telling us the size. Use it verbatim: substituting
        // the framebuffer size here is what makes Wayland report
        // VK_SUBOPTIMAL_KHR on every frame, forever.
        *out = caps.currentExtent;
        return out->width != 0 && out->height != 0;
    }

    FrameExtent const framebuffer = window.framebuffer_extent();
    if (framebuffer.width == 0 || framebuffer.height == 0)
        return false;

    out->width =
        std::clamp(framebuffer.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    out->height =
        std::clamp(framebuffer.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return out->width != 0 && out->height != 0;
}

} // namespace

Swapchain::Swapchain(Device const &device, Window const &window) : device_{device}, window_{window}
{
    if (!recreate()) {
        fail("could not create the initial swapchain: the window has zero "
             "extent");
    }
}

bool Swapchain::recreate()
{
    try {
        vk::SurfaceKHR const surface = *device_.raii_surface();

        auto const caps = device_.raii_physical().getSurfaceCapabilitiesKHR(surface);

        // Check the extent BEFORE touching anything. If the window is minimized
        // there is nothing to render and nothing worth rebuilding, so leave the
        // existing swapchain intact and let the caller block on events.
        vk::Extent2D extent;
        if (!compute_extent(window_, caps, &extent))
            return false;

        if (!(caps.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment)) {
            fail("surface does not support VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT");
        }

        // Present is a queue operation and it consumes the swapchain, so
        // device-idle is the correct guard before destroying any of this.
        device_.raii().waitIdle();

        // Order matters and the member declaration order cannot save us here --
        // this is a rebuild, not a destruction. The image views and semaphores
        // reference the swapchain's images, so they go first.
        images_.clear();
        handle_.clear();

        auto const image_count = pick_image_count(caps);

        auto const availableformats = device_.raii_physical().getSurfaceFormatsKHR(surface);
        if (availableformats.empty())
            fail("surface reports no supported formats");
        auto const surface_format = pick_surface_format(availableformats);

        auto const composite_alpha = pick_composite_alpha(caps.supportedCompositeAlpha);

        auto const available_modes = device_.raii_physical().getSurfacePresentModesKHR(surface);
        auto const present_mode = pick_present_mode(available_modes);

        // Two fields are deliberately left at their defaults.
        // pQueueFamilyIndices: EXCLUSIVE sharing means one queue family, so
        // there is nothing to list. oldSwapchain: passing the old handle makes
        // recreation faster but inverts the destroy ordering (the old swapchain
        // must outlive the new one), and this rebuilds from scratch anyway.
        vk::SwapchainCreateInfoKHR info;
        info.setSurface(surface)
            .setMinImageCount(image_count)
            .setImageFormat(surface_format.format)
            .setImageColorSpace(surface_format.colorSpace)
            .setImageExtent(extent)
            .setImageArrayLayers(1)
            .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
            .setImageSharingMode(vk::SharingMode::eExclusive)
            .setPreTransform(caps.currentTransform)
            .setCompositeAlpha(composite_alpha)
            .setPresentMode(present_mode)
            // Clipped: Happens when windows are in front of others.
            .setClipped(vk::True);

        handle_ = device_.raii().createSwapchainKHR(info);
        format_ = surface_format.format;
        extent_ = extent;

        // Query the ACTUAL image count -- it may exceed what we asked for.
        std::vector<vk::Image> const raw_images = handle_.getImages();

        images_.resize(raw_images.size());
        for (size_t i = 0; i < raw_images.size(); ++i) {
            images_[i].image = raw_images[i];

            vk::ImageViewCreateInfo view_info;
            // subresourceRange's default is all zeros, and levelCount = 0 is
            // invalid -- it has to be spelled out.
            view_info.setImage(raw_images[i])
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(format_)
                .setSubresourceRange(
                    vk::ImageSubresourceRange{.aspectMask = vk::ImageAspectFlagBits::eColor,
                                              .levelCount = 1,
                                              .layerCount = 1})
                // Defaults, don't remap
                .setComponents({.r = vk::ComponentSwizzle::eIdentity,
                                .g = vk::ComponentSwizzle::eIdentity,
                                .b = vk::ComponentSwizzle::eIdentity,
                                .a = vk::ComponentSwizzle::eIdentity});

            images_[i].view = device_.raii().createImageView(view_info);
            images_[i].render_finished = device_.raii().createSemaphore(vk::SemaphoreCreateInfo{});
        }

        std::println("[lc1] swapchain: {}x{}, {} images, {}", extent_.width, extent_.height,
                     raw_images.size(), present_mode_name(present_mode));
        return true;
    }
    catch (vk::SystemError const &error) {
        fail(std::string("swapchain recreation failed: ") + error.what());
    }
}

} // namespace lc1
