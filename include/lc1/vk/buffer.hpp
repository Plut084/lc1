#pragma once

#include "lc1/vk/common.hpp"
#include "lc1/vk/memory.hpp"

#include <cstddef>
#include <span>

namespace lc1 {

class Device;

// A GPU buffer and its VMA allocation. Knows nothing about what it holds:
// vertex, index, uniform and staging buffers differ only in `usage` and
// `allocation_flags`.
class Buffer {
  public:
    // `allocation_flags` is empty for GPU-only buffers. Buffers the CPU writes
    // pass vma::AllocationCreateFlagBits::eHostAccessSequentialWrite.
    Buffer(Device const &device, vk::DeviceSize size, vk::BufferUsageFlags usage,
           vma::AllocationCreateFlags allocation_flags = {});

    // Requires a buffer created with allocation flag
    // vma::AllocationCreateFlagBits::eHostAccessSequentialWrite.
    void upload(std::span<std::byte const> data);

    vk::raii::Buffer const &raii() const { return buffer_; }
    vk::DeviceSize size() const { return size_; }

  private:
    // Owns both the VkBuffer and the VmaAllocation, and frees them together.
    vma::raii::Buffer buffer_{nullptr};
    vk::DeviceSize size_;
};

} // namespace lc1
