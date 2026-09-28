#pragma once

// OUT_OF_DATE is control flow for the frame loop ("recreate at the top of the
// next frame"), so it comes back as a result instead of an exception. Without
// this, hpp's success codes for acquire/present cover SUBOPTIMAL but not
// OUT_OF_DATE. SURFACE_LOST still throws; draw_frame catches it by type.
#include <filesystem>
#define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS

// Makes vk:: structs aggregates, so they can be built with designated
// initializers -- a class with any user-declared constructor is not one.
//
// Not the umbrella VULKAN_HPP_NO_CONSTRUCTORS: that also removes union
// constructors, and vk::ClearValue{}.setColor(...) needs them.
//
// Not VULKAN_HPP_NO_SETTERS either: an array setter assigns the count and the
// pointer together, which designated initialization cannot do.
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

// No global vk* prototypes, so a bare vk* call fails to compile instead of
// bypassing the raii dispatchers. It also switches hpp to the dynamic
// dispatcher. Do not swap it for VULKAN_HPP_DISPATCH_LOADER_DYNAMIC: that
// turns the same mistake into a link error rather than a compile error.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_raii.hpp>

#include "lc1/error.hpp"

#include <cstdint>
#include <string>

namespace lc1 {

std::string result_string(vk::Result result);
std::string result_string(VkResult result);
std::string version_string(uint32_t version);
char const *device_type_string(vk::PhysicalDeviceType type);

// Returns false when the window is minimized or the surface has no usable
// extent. Zero extent is not merely useless -- VkRenderingInfo::renderArea
// requires a nonzero extent, so we must not record a frame at all in that case.
bool compute_extent(vk::Extent2D framebuffer, vk::SurfaceCapabilitiesKHR const &caps,
                    vk::Extent2D *out);

std::string read_file(std::filesystem::path const &path);

// Records one image barrier, which does two jobs at once: it changes `image`'s
// layout from old_layout to new_layout, and it orders memory access around
// that change.
//   src_stage / src_access: the earlier work that must finish, and whose
//     writes must be made available, before the transition happens.
//   dst_stage / dst_access: the later work that must wait for the transition,
//     and that will see those writes.
//   aspect: which part of the image the barrier covers -- eColor for a color
//     image, eDepth (plus eStencil when the format carries one) for a depth
//     image. Required, and deliberately without a default: eColor would be
//     wrong for a depth image, and a wrong aspect is a validation error rather
//     than something the compiler can catch.
// Covers mip level 0, layer 0 only.
void transition_image_layout(vk::raii::CommandBuffer const &command_buffer, vk::Image image,
                             vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                             vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
                             vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access,
                             vk::ImageAspectFlags aspect);

} // namespace lc1

// For calls whose only legal result is VK_SUCCESS. Deliberately NOT used on
// vkAcquireNextImageKHR / vkQueuePresentKHR: those legally return
// VK_SUBOPTIMAL_KHR and VK_ERROR_OUT_OF_DATE_KHR, which the frame loop handles
// as control flow.
#define VK_CHECK(expr)                                                                             \
    do {                                                                                           \
        const VkResult vk_check_result_ = (expr);                                                  \
        if (vk_check_result_ != VK_SUCCESS) {                                                      \
            lc1::fail("{} failed: {} at {}:{}", #expr, lc1::result_string(vk_check_result_),       \
                      __FILE__, __LINE__);                                                         \
        }                                                                                          \
    } while (false)
