#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/memory.hpp"

#include <cstddef>
#include <span>

namespace lc1 {

class Device;

// A GPU buffer and its VMA allocation. Knows nothing about what it holds:
// vertex, index, uniform and staging buffers differ only in `usage` and
// `allocation_flags`.
class GpuBuffer {
  public:
    GpuBuffer(Device const &device, vk::DeviceSize size, vk::BufferUsageFlags2 usage,
              vma::AllocationCreateFlags allocation_flags = {}, vk::DeviceSize min_alignment = 1);

    // Requires a buffer created with allocation flag
    // vma::AllocationCreateFlagBits::eHostAccessSequentialWrite.
    void upload(std::span<std::byte const> data);

    vk::DeviceAddress device_address() const
    {
        // getDevice() returns a non-owning vk::Device; reuse the buffer's RAII dispatcher.
        return buffer_.getDevice().getBufferAddress(vk::BufferDeviceAddressInfo{.buffer = *buffer_},
                                                    *buffer_.getDispatcher());
    }

    // Requires eMapped at allocation time; VMA owns the persistent mapping.
    void *mapped_address() const;
    void flush(vk::DeviceSize offset, vk::DeviceSize size) const;

    vk::raii::Buffer const &raii() const { return buffer_; }

    vk::DeviceSize size() const { return size_; }

  private:
    // TODO: DEPRECATED
    // `allocation_flags` is empty for GPU-only buffers. Buffers the CPU writes
    // pass vma::AllocationCreateFlagBits::eHostAccessSequentialWrite.
    GpuBuffer(Device const &device, vk::DeviceSize size, vk::BufferUsageFlags usage,
              vma::AllocationCreateFlags allocation_flags = {});

    // Owns both the VkBuffer and the VmaAllocation, and frees them together.
    vma::raii::Buffer buffer_{nullptr};
    vk::DeviceSize size_;
};

} // namespace lc1
