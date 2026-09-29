#pragma once

#include "lc1/vk/common.hpp"
#include "lc1/vk/shader.hpp"

namespace lc1 {

class Device;
class Swapchain;
class ShaderStages;

class Pipeline {
  public:
    Pipeline(Device const &device, ShaderStages const &shader_stages,
             std::vector<vk::Format> const &color_attachment_formats,
             vk::Format depth_attachment_format, vk::SampleCountFlagBits samples);

    // Set 0: what changes every frame (the uniform buffer). One set per frame
    // in flight.
    vk::raii::DescriptorSetLayout const &frame_set_layout() const { return frame_set_layout_; }
    // Set 1: what changes per draw (the texture). One set per Material.
    vk::raii::DescriptorSetLayout const &material_set_layout() const
    {
        return material_set_layout_;
    }
    // bindDescriptorSets needs it.
    vk::raii::PipelineLayout const &layout() const { return layout_; }
    vk::raii::Pipeline const &raii() const { return graphics_pipeline_; }

  private:
    // In creation order, so each is destroyed before what it was built from.
    vk::raii::DescriptorSetLayout frame_set_layout_{nullptr};
    vk::raii::DescriptorSetLayout material_set_layout_{nullptr};
    vk::raii::PipelineLayout layout_{nullptr};
    vk::raii::Pipeline graphics_pipeline_{nullptr};
};

} // namespace lc1
