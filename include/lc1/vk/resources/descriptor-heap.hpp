#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/resources/gpu-buffer.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"

namespace lc1 {

class CommandBuffer;

using HeapIndex = std::uint32_t;

class SamplerHeap {
    friend class DescriptorHeap;

  public:
    SamplerHeap(Device const &device, vk::DeviceSize capacity,
                vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties);

    HeapIndex allocate_sampler();

    void deallocate_sampler(HeapIndex index) { (void)index; } // TODO: implement

  private:
    Device const *device_;
    vk::DeviceSize reserved_size_;
    vk::DeviceSize offset_;
    vk::DeviceSize stride_;
    std::size_t capacity_;
    GpuBuffer buffer_;
    std::size_t size_;
};

class ResourceHeap {
    friend class DescriptorHeap;

  public:
    ResourceHeap(Device const &device, vk::DeviceSize image_capacity,
                 vk::DeviceSize buffer_capacity,
                 vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties);

    HeapIndex allocate_image(GpuImage const &image);
    HeapIndex allocate_buffer(GpuBuffer const &buffer);

    void deallocate_image(HeapIndex index) { (void)index; } // TODO: implement

    void deallocate_buffer(HeapIndex index) { (void)index; } // TODO: implement

  private:
    Device const *device_;
    vk::DeviceSize reserved_size_;
    vk::DeviceSize image_offset_;
    vk::DeviceSize image_stride_;
    std::size_t image_capacity_;
    vk::DeviceSize buffer_offset_;
    vk::DeviceSize buffer_stride_;
    std::size_t buffer_capacity_;
    GpuBuffer buffer_;
    std::size_t image_size_;
    std::size_t buffer_size_;
};

// It's worth noting that:
// (from https://docs.vulkan.org/guide/latest/descriptor_heap.html#_allocating_the_heap)
// Make sure if writing to the heap on the GPU, prior to reading descriptors from the heap on the
// GPU, to properly synchronize with VK_ACCESS_2_RESOURCE_HEAP_READ_BIT_EXT or
// VK_ACCESS_2_SAMPLER_HEAP_READ_BIT_EXT.
class DescriptorHeap {
  public:
    DescriptorHeap(Device const &device, vk::DeviceSize sampler_capacity,
                   vk::DeviceSize image_capacity, vk::DeviceSize buffer_capacity);

    // Logs physical-device support, descriptor sizes/limits and this heap's allocation state.
    // Physical-device support does not imply that a feature was enabled on the logical device.
    void probe() const;

    void bind_to_command_buffer(CommandBuffer &command_buffer);

    template <typename T>
    void push_data(vk::raii::CommandBuffer &command_buffer, std::span<T> data);

    // Returns an index relative to the whole heap, in units of the descriptor
    // type's size, ready for ResourceDescriptorHeap / SamplerDescriptorHeap.
    // Referenced resources must outlive all GPU submissions reading these slots.
    // Slots are append-only until deallocation is implemented.
    std::size_t allocate_sampler();
    std::size_t allocate_image(GpuImage const &image);
    std::size_t allocate_buffer(GpuBuffer const &buffer);

  private:
    Device const *device_;

    vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties_;

    SamplerHeap sampler_heap_;
    ResourceHeap resource_heap_;
};

template <typename T>
inline void DescriptorHeap::push_data(vk::raii::CommandBuffer &command_buffer, std::span<T> data)
{
    if (data.empty() || data.size_bytes() % 4 != 0 ||
        data.size_bytes() > descriptor_heap_properties_.maxPushDataSize)
        fail("DescriptorHeap::push_data: size must be nonzero, a multiple of 4, and within "
             "maxPushDataSize");
    vk::PushDataInfoEXT push_data_info{
        .data = {.address = data.data(), .size = data.size_bytes()},
    };
    command_buffer.pushDataEXT(push_data_info);
}

} // namespace lc1
