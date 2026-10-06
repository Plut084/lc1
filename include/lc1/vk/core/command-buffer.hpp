#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/resources/descriptor-heap.hpp"

namespace lc1 {

class CommandBuffer {
  public:
    CommandBuffer(vk::raii::CommandBuffer &&handle) : handle_(std::move(handle)) {}

    vk::raii::CommandBuffer &raii() { return handle_; }

    // The caller must wait for completion before resetting this command buffer.
    void reset()
    {
        handle_.reset({});
        bound_heap_ = nullptr;
    }

    void bind_descriptor_heap(DescriptorHeap &heap)
    {
        if (bound_heap_ != &heap) {
            heap.bind_to_command_buffer(*this);
            bound_heap_ = &heap;
        }
    }

    template <typename T> void push_data(std::span<T> data);

  private:
    vk::raii::CommandBuffer handle_;
    DescriptorHeap *bound_heap_ = nullptr;
};

template <typename T> inline void CommandBuffer::push_data(std::span<T> data)
{
    if (!bound_heap_)
        fail("CommandBuffer::push_data: no heap bound to this command buffer");

    if (data.empty() || data.size_bytes() % 4 != 0 ||
        data.size_bytes() > bound_heap_->descriptor_heap_properties_.maxPushDataSize)
        fail("CommandBuffer::push_data: size must be nonzero, a multiple of 4, and within "
             "maxPushDataSize");
    vk::PushDataInfoEXT push_data_info{
        .data = {.address = data.data(), .size = data.size_bytes()},
    };
    handle_.pushDataEXT(push_data_info);
}

} // namespace lc1
