#pragma once

#include "lc1/lights/light.hpp"
#include "lc1/vk/common.hpp"
#include "lc1/vk/frame-resources.hpp"
#include "lc1/vk/gpu-mesh.hpp"
#include "lc1/vk/material.hpp"
#include "lc1/vk/pipeline.hpp"
#include "lc1/vk/render-target.hpp"
#include "lc1/vk/sampler.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace lc1 {

class Device;
class GpuTexture;
class FpsCamera;

// Matches Camera in shaders/shader.slang, field for field.
struct CameraData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 position; // World-space camera position; w is unused.
};

struct ObjectData {
    glm::mat4 model;
    glm::mat4 normal_transform; // Inverse transpose; padded to match shader matrices.
};

static_assert(sizeof(CameraData) == 144 && offsetof(CameraData, position) == 128);
static_assert(sizeof(ObjectData) == 128 && offsetof(ObjectData, normal_transform) == 64);

struct ShadowData {
    glm::mat4 view_projection{1.0F};
    glm::uvec4 light_index{10U, 0U, 0U, 0U}; // x selects the shadowed light; 10 disables it.
};
static_assert(sizeof(ShadowData) == 80 && offsetof(ShadowData, light_index) == 64);

constexpr auto max_num_lights = 10U;
struct LightsData {
    std::uint32_t count{};
    std::array<Light, max_num_lights> lights;
};

// Owns the pipeline, material descriptor pool and sampler. Records using one
// caller-owned set of FrameResources; neither resource counts nor frame-slot
// indices are part of this interface.
//
// Takes the raii command buffer: a non-raii vk::CommandBuffer dispatches
// through VULKAN_HPP_DEFAULT_DISPATCHER, which this project never defines.
//
// Must not outlive the Device it was constructed from.
class Renderer {
  public:
    // Upper bound on make_material calls: the descriptor pool is sized once and
    // cannot grow.
    static constexpr std::uint32_t max_materials = 16;

    // Color/depth images are allocated lazily from the target extent in record.
    Renderer(Device const &device, std::vector<vk::Format> const &color_attachment_formats,
             vk::SampleCountFlagBits samples);

    // Allocates set 2 from this renderer's pool and points it at `texture`.
    // Here rather than in Material because only the renderer has the pool, the
    // sampler, and the pipeline whose layout the set must match. Throws once
    // max_materials sets are live.
    Material make_material(GpuTexture const &texture);

    // Creates one set of drawing resources with its own descriptor pool. The
    // caller decides how many sets to keep and when each is safe to reuse.
    // Capacity counts draws; it is fixed for the lifetime of this resource set.
    FrameResources make_frame_resources(std::uint32_t object_capacity) const;

    // resources must have been made by this renderer, and its previous GPU use
    // must have completed. The target format must match the pipeline's format.
    // Records a rendering pass into an already recording command buffer,
    // outside any rendering pass. The caller owns begin/end and the target's
    // layout transitions; record leaves it in COLOR_ATTACHMENT_OPTIMAL.
    // `target.extent` must be nonzero and fit the attachment.
    void record(vk::raii::CommandBuffer const &command_buffer, FrameResources &resources,
                RenderTarget const &target, FpsCamera const &camera, std::span<Light> lights,
                std::span<DrawItem const> draws, glm::vec3 shadow_center,
                bool preview_shadow = false);

  private:
    // The caller has waited for the resources' previous GPU use.
    void ensure_attachments(FrameResources &resources, vk::Extent2D extent) const;

    Device const &device_;
    vk::Format color_format_;
    vk::Format depth_format_;
    vk::SampleCountFlagBits samples_;
    Pipeline pipeline_;
    vk::Format shadow_format_;
    Pipeline shadow_pipeline_;
    Pipeline shadow_preview_pipeline_;
    vk::raii::Sampler shadow_sampler_;
    // Material sets free themselves back into this pool: the caller must
    // destroy Materials before this renderer. FrameResources own separate pools.
    vk::raii::DescriptorPool material_pool_;
    // One for every material: how to filter and wrap does not depend on which
    // image is sampled.
    Sampler sampler_;
};

} // namespace lc1
