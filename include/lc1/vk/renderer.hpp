#pragma once

#include "lc1/vk/common.hpp"
#include "lc1/vk/frame-resources.hpp"
#include "lc1/vk/material.hpp"
#include "lc1/vk/mesh.hpp"
#include "lc1/vk/pipeline.hpp"
#include "lc1/vk/render-target.hpp"
#include "lc1/vk/sampler.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace lc1 {

class Device;
class Texture;
class FpsCamera;

// Matches Camera in shaders/shader.slang, field for field.
struct CameraData {
    glm::mat4 view;
    glm::mat4 proj;
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
    Material make_material(Texture const &texture);

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
                RenderTarget const &target, FpsCamera const &camera,
                std::span<DrawItem const> draws);

  private:
    // The caller has waited for the resources' previous GPU use.
    void ensure_attachments(FrameResources &resources, vk::Extent2D extent) const;

    Device const &device_;
    vk::Format color_format_;
    vk::Format depth_format_;
    vk::SampleCountFlagBits samples_;
    Pipeline pipeline_;
    // Material sets free themselves back into this pool: the caller must
    // destroy Materials before this renderer. FrameResources own separate pools.
    vk::raii::DescriptorPool material_pool_;
    // One for every material: how to filter and wrap does not depend on which
    // image is sampled.
    Sampler sampler_;
};

} // namespace lc1
