#pragma once

// First, so VMA sees VK_NO_PROTOTYPES and the same VULKAN_HPP_* configuration.
// Otherwise VMA_IMPLEMENTATION defaults to VMA_STATIC_VULKAN_FUNCTIONS and
// references vk* symbols that nothing links.
#include "lc1/vk/common.hpp"

#include <vk_mem_alloc_raii.hpp>

namespace lc1 {

using MemoryAllocator = vma::raii::Allocator;

} // namespace lc1
