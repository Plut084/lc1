#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/resources/shader.hpp"

#include <array>
#include <span>

namespace lc1 {

class Device;
class ShaderStages;

enum class PipelineKind { Lit, Shadow, ShadowPreview, ShadowVisibility, ShadowTemporal, ToneMap };

class Pipeline {
  public:
    static constexpr std::array<vk::DescriptorSetLayoutBinding, 6> material_bindings{{
        {.binding = 0,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 1,
         .descriptorType = vk::DescriptorType::eUniformBuffer,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 2,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 3,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 4,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 5,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
    }};
    static constexpr std::array<vk::DescriptorSetLayoutBinding, 2> tone_map_bindings{{
        {.binding = 0,
         .descriptorType = vk::DescriptorType::eUniformBuffer,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 1,
         .descriptorType = vk::DescriptorType::eSampledImage,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
    }};
    static constexpr std::array<vk::DescriptorSetLayoutBinding, 9> frame_bindings{{
        {.binding = 0,
         .descriptorType = vk::DescriptorType::eUniformBuffer,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        {.binding = 1,
         .descriptorType = vk::DescriptorType::eUniformBuffer,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 4,
         .descriptorType = vk::DescriptorType::eAccelerationStructureKHR,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 5,
         .descriptorType = vk::DescriptorType::eSampledImage,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 6,
         .descriptorType = vk::DescriptorType::eUniformBuffer,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 7,
         .descriptorType = vk::DescriptorType::eSampledImage,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 8,
         .descriptorType = vk::DescriptorType::eSampledImage,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 9,
         .descriptorType = vk::DescriptorType::eSampledImage,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
        {.binding = 10,
         .descriptorType = vk::DescriptorType::eSampledImage,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
    }};
    // Legacy depth-map diagnostics do not bind ray-query resources. Object and
    // material layouts remain identical; changing set 0 requires rebinding sets.
    static constexpr std::array<vk::DescriptorSetLayoutBinding, 2> shadow_map_frame_bindings{{
        {.binding = 2,
         .descriptorType = vk::DescriptorType::eUniformBuffer,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        {.binding = 3,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eFragment},
    }};
    static constexpr std::array<vk::DescriptorSetLayoutBinding, 1> object_bindings{{
        {.binding = 0,
         .descriptorType = vk::DescriptorType::eUniformBuffer,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
    }};

    Pipeline(Device const &device, ShaderStages const &shader_stages,
             std::vector<vk::Format> const &color_attachment_formats,
             vk::Format depth_attachment_format, vk::SampleCountFlagBits samples,
             PipelineKind kind = PipelineKind::Lit);

    // Set 0: what changes every frame (the uniform buffer). One set per frame
    // in flight.
    vk::raii::DescriptorSetLayout const &frame_set_layout() const { return frame_set_layout_; }
    // Set 1: model matrix, one set per draw per frame slot.
    vk::raii::DescriptorSetLayout const &object_set_layout() const { return object_set_layout_; }
    // Set 2: immutable material parameters and textures. One set per Material.
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
    vk::raii::DescriptorSetLayout object_set_layout_{nullptr};
    vk::raii::DescriptorSetLayout material_set_layout_{nullptr};
    vk::raii::PipelineLayout layout_{nullptr};
    vk::raii::Pipeline graphics_pipeline_{nullptr};
};

} // namespace lc1
