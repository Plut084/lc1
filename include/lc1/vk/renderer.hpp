#pragma once

#include "lc1/vk/buffer.hpp"
#include "lc1/vk/common.hpp"
#include "lc1/vk/image.hpp"
#include "lc1/vk/material.hpp"
#include "lc1/vk/mesh.hpp"
#include "lc1/vk/pipeline.hpp"
#include "lc1/vk/sampler.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace lc1 {

class Device;
class Texture;

// A single color attachment covering `extent`, with a format matching the
// renderer's pipeline. Its image must already be in COLOR_ATTACHMENT_OPTIMAL.
struct RenderTarget {
    // Non-owning reference: the wrapper must remain valid through record;
    // its view and image must stay alive until the GPU finishes using them.
    vk::raii::ImageView const &view;
    vk::Extent2D extent;
};

// Matches `UniformBuffer` in shaders/shader.slang, field for field.
struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

// Owns what drawing needs (the pipeline, plus the uniform buffers and
// descriptor sets its shaders read) and records into a command buffer the
// frame loop owns and submits. What to draw is decided here, so the frame loop
// never sees a pipeline.
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

    // Depth images are allocated lazily from the target extent in record.
    Renderer(Device const &device, std::vector<vk::Format> const &color_attachment_formats);

    // Allocates set 1 from this renderer's pool and points it at `texture`.
    // Here rather than in Material because only the renderer has the pool, the
    // sampler, and the pipeline whose layout the set must match. Throws once
    // max_materials sets are live.
    Material make_material(Texture const &texture);

    // `frame_index` picks the uniform buffer and descriptor set. It must be the
    // frame loop's slot, because only that slot's fence proves the GPU is done
    // reading them. The target is independent of that slot.
    // Records a rendering pass into an already recording command buffer,
    // outside any rendering pass. The caller owns begin/end and the target's
    // layout transitions; record leaves it in COLOR_ATTACHMENT_OPTIMAL.
    // `target.extent` must be nonzero and fit the attachment.
    void record(vk::raii::CommandBuffer const &command_buffer, std::uint32_t frame_index,
                RenderTarget const &target, UniformBufferObject const &ubo,
                std::span<DrawItem const> draws);

  private:
    // One per frame in flight: the CPU writes one slot's buffer while the GPU
    // may still be reading the other's.
    struct FrameData {
        Buffer uniform_buffer;
        // Set 0. Points at uniform_buffer.
        vk::raii::DescriptorSet descriptor_set;
        // Created on first use, replaced when this slot's target size changes.
        // Only this slot's fence is needed before replacing it.
        std::optional<Image> depth_image;
    };

    // The caller has waited for this frame slot's fence.
    Image const &ensure_depth_image(FrameData &frame, vk::Extent2D extent);

    Device const &device_;
    vk::Format depth_format_;
    Pipeline pipeline_;
    // Declared before frames_: each vk::raii::DescriptorSet frees itself back
    // into this pool, so the pool must outlive them -- and, for the same
    // reason, this Renderer must outlive every Material it made.
    vk::raii::DescriptorPool descriptor_pool_;
    // One for every material: how to filter and wrap does not depend on which
    // image is sampled.
    Sampler sampler_;
    // Sized to FrameLoop::frames_in_flight in the constructor.
    std::vector<FrameData> frames_;
};

} // namespace lc1
