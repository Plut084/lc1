#pragma once

#include "lc1/vk/core/common.hpp"

#include <cstdint>
#include <span>

namespace lc1 {

// Application preferences, separate from querying capabilities and creating
// the swapchain. Callers may use these defaults or supply their own choices.
// Prefers B8G8R8A8_SRGB + SRGB_NONLINEAR, otherwise the first supported pair.
vk::SurfaceFormatKHR pick_surface_format(std::span<vk::SurfaceFormatKHR const> formats);

// Prefers MAILBOX, falling back to FIFO.
vk::PresentModeKHR pick_present_mode(std::span<vk::PresentModeKHR const> modes);

// Prefers an opaque window, otherwise the first supported alpha mode.
vk::CompositeAlphaFlagBitsKHR pick_composite_alpha(vk::CompositeAlphaFlagsKHR supported);

// Requests one more than the minimum, capped by the surface maximum.
std::uint32_t pick_image_count(vk::SurfaceCapabilitiesKHR const &caps);

// Returns false when the window is minimized or the surface has no usable
// extent. Zero extent is not merely useless -- VkRenderingInfo::renderArea
// requires a nonzero extent, so we must not record a frame at all in that case.
bool compute_extent(vk::Extent2D framebuffer, vk::SurfaceCapabilitiesKHR const &caps,
                    vk::Extent2D *out);

} // namespace lc1
