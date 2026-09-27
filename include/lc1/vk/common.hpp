#pragma once

// OUT_OF_DATE is control flow for the frame loop ("recreate at the top of the
// next frame"), so it comes back as a result instead of an exception. Without
// this, hpp's success codes for acquire/present cover SUBOPTIMAL but not
// OUT_OF_DATE. SURFACE_LOST still throws; draw_frame catches it by type.
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

} // namespace lc1

// For calls whose only legal result is VK_SUCCESS. Deliberately NOT used on
// vkAcquireNextImageKHR / vkQueuePresentKHR: those legally return
// VK_SUBOPTIMAL_KHR and VK_ERROR_OUT_OF_DATE_KHR, which the frame loop handles
// as control flow.
#define VK_CHECK(expr)                                                                             \
    do {                                                                                           \
        const VkResult vk_check_result_ = (expr);                                                  \
        if (vk_check_result_ != VK_SUCCESS) {                                                      \
            lc1::fail(std::string(#expr " failed: ") + lc1::result_string(vk_check_result_) +      \
                      " at " __FILE__ ":" + std::to_string(__LINE__));                             \
        }                                                                                          \
    } while (false)
