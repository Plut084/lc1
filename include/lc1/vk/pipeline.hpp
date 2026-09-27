#pragma once

#include "lc1/vk/common.hpp"
#include "lc1/vk/shader.hpp"

namespace lc1 {

class Device;
class Swapchain;
class ShaderStages;

class Pipeline {
  public:
    Pipeline(Device const &device, Swapchain const &swapchain, ShaderStages const &shader_stages);

    // Descriptor sets for this pipeline are allocated with this layout.
    vk::raii::DescriptorSetLayout const &descriptor_set_layout() const
    {
        return descriptor_set_layout_;
    }
    // bindDescriptorSets needs it.
    vk::raii::PipelineLayout const &layout() const { return layout_; }
    vk::raii::Pipeline const &raii() const { return graphics_pipeline_; }

  private:
    // In creation order, so each is destroyed before what it was built from.
    vk::raii::DescriptorSetLayout descriptor_set_layout_{nullptr};
    vk::raii::PipelineLayout layout_{nullptr};
    vk::raii::Pipeline graphics_pipeline_{nullptr};
};

} // namespace lc1
