#include "lc1/vk/resources/gpu-buffer.hpp"

#include "lc1/vk/core/device.hpp"

namespace lc1 {

GpuBuffer::GpuBuffer(Device const &device, vk::DeviceSize size, vk::BufferUsageFlags usage,
                     vma::AllocationCreateFlags allocation_flags)
    : buffer_{device.allocator().createBuffer(
          {
              .size = size,
              .usage = usage,
              .sharingMode = vk::SharingMode::eExclusive,
          },
          {
              .flags = allocation_flags,
              // VMA picks the memory type from usage and flags: DEVICE_LOCAL
              // for GPU-only buffers, HOST_VISIBLE when the CPU writes.
              .usage = vma::MemoryUsage::eAuto,
          })},
      size_{size}
{
}

GpuBuffer::GpuBuffer(Device const &device, vk::DeviceSize size, vk::BufferUsageFlags2 usage,
                     vma::AllocationCreateFlags allocation_flags, vk::DeviceSize min_alignment)
    : buffer_{[&] {
          vk::BufferUsageFlags2CreateInfo const usage_info{.usage = usage};
          // Keep the pNext data alive until buffer creation has completed.
          return device.allocator().createBufferWithAlignment(
              {
                  .pNext = &usage_info,
                  .size = size,
                  .sharingMode = vk::SharingMode::eExclusive,
              },
              {
                  .flags = allocation_flags,
                  .usage = vma::MemoryUsage::eAuto,
              },
              min_alignment);
      }()},
      size_{size}
{
}

void GpuBuffer::upload(std::span<std::byte const> data)
{
    if (data.size() > size_)
        fail("GpuBuffer::upload: data is larger than the buffer");

    // Maps, copies, flushes if the memory is not coherent, and unmaps.
    buffer_.getAllocation().copyFromMemory(data.data(), 0, data.size());
}

void *GpuBuffer::mapped_address() const
{
    auto *address = buffer_.getAllocation().getInfo().pMappedData;
    if (!address)
        fail("GpuBuffer has no persistent mapping");
    return address;
}

void GpuBuffer::flush(vk::DeviceSize offset, vk::DeviceSize size) const
{
    if (offset > size_ || size > size_ - offset)
        fail("GpuBuffer::flush: range exceeds the buffer");
    buffer_.getAllocation().flush(offset, size);
}

} // namespace lc1
