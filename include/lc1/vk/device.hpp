#pragma once

#include "lc1/vk/common.hpp"
#include "lc1/vk/memory.hpp"

#include <cstdint>

namespace lc1 {

class Instance;
class Window;

// Surface + physical device + logical device + the single graphics+present
// queue.
//
// The surface is created here rather than in its own module because device
// *selection* depends on it: vkGetPhysicalDeviceSurfaceSupportKHR is the only
// way to know whether a queue family can actually present to this window.
class Device {
  public:
    // The window is used only inside the constructor, for
    // glfwCreateWindowSurface; Device keeps no reference to it.
    Device(Instance const &instance, Window const &window,
           std::vector<char const *> required_extensions);

    // Must be called explicitly before tearing down anything the GPU may still
    // be using: ~vk::raii::Device does not wait.
    void wait_idle() const;

    uint32_t queue_family() const { return queue_family_; }

    // Non-owning; valid only while this Device is.
    vk::raii::PhysicalDevice const &raii_physical() const { return physical_; }
    vk::raii::SurfaceKHR const &raii_surface() const { return surface_; }
    vk::raii::Device const &raii() const { return device_; }
    MemoryAllocator const &allocator() const { return alloc_; }
    vk::raii::Queue const &raii_queue() const { return queue_; }

  private:
    vk::raii::PhysicalDevice physical_{nullptr};
    // Declared before handle_ so the device is destroyed first.
    vk::raii::SurfaceKHR surface_{nullptr};
    vk::raii::Device device_{nullptr};
    MemoryAllocator alloc_{nullptr};
    vk::raii::Queue queue_{nullptr};
    std::uint32_t queue_family_ = 0;
};

} // namespace lc1
