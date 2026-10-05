#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/resources/descriptor-heap.hpp"

namespace lc1 {

class CommandBuffer {
  public:
    CommandBuffer(vk::raii::CommandBuffer &&handle) : handle_(std::move(handle)) {}

    vk::raii::CommandBuffer &raii() { return handle_; }

    void bind_descriptor_heap(DescriptorHeap &heap)
    {
        if (bound_heap_ != &heap) {
            heap.bind_to_command_buffer(*this);
            bound_heap_ = &heap;
        }
    }

  private:
    vk::raii::CommandBuffer handle_;
    DescriptorHeap *bound_heap_ = nullptr;
};

} // namespace lc1
