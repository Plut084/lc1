#include "lc1/vk/descriptor-set.hpp"

#include "lc1/vk/device.hpp"

#include <algorithm>

namespace lc1 {

DescriptorSet::DescriptorSet(Device const &device, vk::raii::DescriptorPool const &pool,
                             vk::raii::DescriptorSetLayout const &layout,
                             std::span<vk::DescriptorSetLayoutBinding const> bindings)
    : device_{&device}, bindings_{bindings.begin(), bindings.end()}, handle_{[&] {
          for (std::size_t i = 0; i < bindings.size(); ++i) {
              auto const &binding = bindings[i];
              if ((binding.descriptorType != vk::DescriptorType::eUniformBuffer &&
                   binding.descriptorType != vk::DescriptorType::eCombinedImageSampler) ||
                  binding.descriptorCount != 1)
                  fail("DescriptorSet requires single uniform-buffer or combined-image-sampler "
                       "bindings");
              for (std::size_t j = 0; j < i; ++j)
                  if (bindings[j].binding == binding.binding)
                      fail("duplicate descriptor layout binding {}", binding.binding);
          }
          vk::DescriptorSetAllocateInfo info{.descriptorPool = *pool};
          info.setSetLayouts(*layout);
          auto sets = device.raii().allocateDescriptorSets(info);
          return std::move(sets.front());
      }()}
{
}

void DescriptorSet::add_uniform(std::uint32_t binding, vk::DeviceSize size)
{
    if (std::ranges::none_of(bindings_, [binding](auto const &entry) {
            return entry.binding == binding &&
                   entry.descriptorType == vk::DescriptorType::eUniformBuffer;
        }))
        fail("unknown uniform binding {}", binding);
    if (uniforms_.contains(binding))
        fail("uniform binding {} is already initialized", binding);

    auto const limit = device_->raii_physical().getProperties().limits.maxUniformBufferRange;
    if (size == 0 || size > limit)
        fail("uniform buffer size {} must be between 1 and {}", size, limit);

    auto [entry, inserted] =
        uniforms_.try_emplace(binding, *device_, size, vk::BufferUsageFlagBits::eUniformBuffer,
                              vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
    vk::DescriptorBufferInfo const buffer_info{
        .buffer = *entry->second.raii(), .offset = 0, .range = size};
    vk::WriteDescriptorSet const write{
        .dstSet = *handle_,
        .dstBinding = binding,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &buffer_info,
    };
    device_->raii().updateDescriptorSets(write, {});
}

void DescriptorSet::set_image(std::uint32_t binding, vk::DescriptorImageInfo const &info)
{
    if (std::ranges::none_of(bindings_, [binding](auto const &entry) {
            return entry.binding == binding &&
                   entry.descriptorType == vk::DescriptorType::eCombinedImageSampler;
        }))
        fail("unknown combined image sampler binding {}", binding);
    vk::WriteDescriptorSet const write{
        .dstSet = *handle_,
        .dstBinding = binding,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &info,
    };
    device_->raii().updateDescriptorSets(write, {});
    images_.insert(binding);
}

void DescriptorSet::upload(std::uint32_t binding, std::span<std::byte const> data)
{
    auto const found = uniforms_.find(binding);
    if (found == uniforms_.end())
        fail("uniform binding {} is not initialized", binding);
    found->second.upload(data);
}

vk::raii::DescriptorSet const &DescriptorSet::raii() const
{
    if (uniforms_.size() + images_.size() != bindings_.size())
        fail("cannot bind descriptor set: {} of {} bindings initialized",
             uniforms_.size() + images_.size(), bindings_.size());
    return handle_;
}

} // namespace lc1
