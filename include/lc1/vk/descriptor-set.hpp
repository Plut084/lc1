#pragma once

#include "lc1/vk/buffer.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lc1 {

// Owns one set and its uniform buffers. Device and pool must outlive it; the
// pool must allow individual set frees. bindings must describe the supplied
// layout exactly. Supports single uniform buffers and borrowed combined image samplers.
// Mutate only after all prior GPU use has finished; no implicit GPU waits.
class DescriptorSet {
  public:
    DescriptorSet(Device const &device, vk::raii::DescriptorPool const &pool,
                  vk::raii::DescriptorSetLayout const &layout,
                  std::span<vk::DescriptorSetLayoutBinding const> bindings);
    DescriptorSet(DescriptorSet const &) = delete;
    DescriptorSet &operator=(DescriptorSet const &) = delete;
    DescriptorSet(DescriptorSet &&) noexcept = default;
    DescriptorSet &operator=(DescriptorSet &&) = delete;
    ~DescriptorSet() = default;

    void add_uniform(std::uint32_t binding, vk::DeviceSize size);
    // Image view and sampler are borrowed; keep them alive for all GPU uses of this set.
    void set_image(std::uint32_t binding, vk::DescriptorImageInfo const &info);
    void upload(std::uint32_t binding, std::span<std::byte const> data);
    // Reject binding a partially initialized set before reaching Vulkan.
    vk::raii::DescriptorSet const &raii() const;

  private:
    Device const *device_; // Non-owning, non-null; device must outlive this object.
    std::vector<vk::DescriptorSetLayoutBinding> bindings_;
    std::unordered_map<std::uint32_t, Buffer> uniforms_;
    std::unordered_set<std::uint32_t> images_;
    // Destroy the set before its buffers; the enclosing frame owns the pool.
    vk::raii::DescriptorSet handle_;
};

} // namespace lc1
