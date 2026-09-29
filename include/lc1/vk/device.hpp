#pragma once

#include "lc1/vk/common.hpp"
#include "lc1/vk/memory.hpp"

#include <cstdint>
#include <span>

namespace lc1 {

class Instance;
class Surface;

// Selected GPU, logical device, allocator and the single graphics+present queue.
// Instance must outlive this object. Surface is borrowed only during selection.
class Device {
  public:
    Device(Instance const &instance, Surface const &surface,
           std::span<char const *const> required_extensions);

    // Must be called explicitly before tearing down anything the GPU may still
    // be using: ~vk::raii::Device does not wait.
    void wait_idle() const;

    uint32_t queue_family() const { return queue_family_; }

    // Non-owning; valid only while this Device is.
    vk::raii::PhysicalDevice const &raii_physical() const { return physical_; }
    vk::raii::Device const &raii() const { return device_; }
    MemoryAllocator const &allocator() const { return alloc_; }
    vk::raii::Queue const &raii_queue() const { return queue_; }

    vk::SampleCountFlagBits max_sample_count() const;

  private:
    vk::raii::PhysicalDevice physical_{nullptr};
    vk::raii::Device device_{nullptr};
    MemoryAllocator alloc_{nullptr};
    vk::raii::Queue queue_{nullptr};
    std::uint32_t queue_family_ = 0;
};

} // namespace lc1
