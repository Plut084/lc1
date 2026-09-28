#include "lc1/vk/buffer.hpp"

#include "lc1/vk/device.hpp"

namespace lc1 {

Buffer::Buffer(Device const &device, vk::DeviceSize size, vk::BufferUsageFlags usage,
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

void Buffer::upload(std::span<std::byte const> data)
{
    if (data.size() > size_)
        fail("Buffer::upload: data is larger than the buffer");

    // Maps, copies, flushes if the memory is not coherent, and unmaps.
    buffer_.getAllocation().copyFromMemory(data.data(), 0, data.size());
}

} // namespace lc1
