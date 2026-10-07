#pragma once

#include "lc1/vk/render/draw-item.hpp"

#include "lc1/scene/scene.hpp"
#include "lc1/vk/core/common.hpp"
#include "lc1/vk/render/frame-resources.hpp"
#include "lc1/vk/render/graphics-shaders.hpp"
#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/render/material.hpp"
#include "lc1/vk/render/output-settings.hpp"
#include "lc1/vk/render/ray-query-shadows.hpp"
#include "lc1/vk/render/render-data.hpp"
#include "lc1/vk/resources/descriptor-heap.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"
#include "lc1/vk/resources/render-target.hpp"
#include "lc1/vk/resources/sampler.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace lc1 {

class CommandBuffer;
class Device;
class GpuTexture;

namespace scene {
class FpsCamera;
} // namespace scene

// Owns material rendering and ray-query shadows.
// Records using one
// caller-owned set of FrameResources; neither resource counts nor frame-slot
// indices are part of this interface.
//
// Takes the raii command buffer: a non-raii vk::CommandBuffer dispatches
// through VULKAN_HPP_DEFAULT_DISPATCHER, which this project never defines.
//
// Must not outlive the Device it was constructed from.
class Renderer {
  public:
    static constexpr vk::Format hdr_format = vk::Format::eR16G16B16A16Sfloat;

    // Color/depth images are allocated lazily from the target extent in record.
    Renderer(Device const &device, std::vector<vk::Format> const &color_attachment_formats,
             vk::SampleCountFlagBits samples);
    Renderer(Renderer const &) = delete;
    Renderer &operator=(Renderer const &) = delete;
    Renderer(Renderer &&) = delete;
    Renderer &operator=(Renderer &&) = delete;

    vk::SampleCountFlagBits sample_count() const { return main_state_.samples; }

    // Creates one frame slot with its own descriptor heap and buffer cache. The
    // caller decides how many sets to keep and when each is safe to reuse.
    // Capacity counts draws; it is fixed for the lifetime of this resource set.
    FrameResources make_frame_resources(std::uint32_t object_capacity) const;

    // resources must have been made by this renderer, and its previous GPU use
    // must have completed. The target format must match the constructor's output format.
    // Records lighting and tone-map passes into an already recording command buffer,
    // outside any rendering pass. The caller owns begin/end and the target's
    // layout transitions; record leaves it in COLOR_ATTACHMENT_OPTIMAL.
    // `target.extent` must be nonzero and fit the attachment.
    void record(CommandBuffer &cmd, FrameResources &resources, RenderTarget const &target,
                scene::FpsCamera const &camera, std::span<scene::Light const> lights,
                std::span<DrawItem const> draws, OutputSettings output = {});

    void record(CommandBuffer &cmd, FrameResources &resources, RenderTarget const &target,
                scene::Scene const &scene, OutputSettings output = {});

  private:
    // The caller has waited for the resources' previous GPU use.
    void ensure_attachments(FrameResources &resources, vk::Extent2D extent) const;
    void tone_map(CommandBuffer &commands, FrameResources &resources, RenderTarget const &target,
                  OutputSettings output) const;

    Device const *device_; // Non-owning, non-null; the device outlives this object.
    vk::Format color_format_;
    vk::Format depth_format_;
    GraphicsState main_state_;
    GraphicsShaders shaders_;
    GraphicsState tone_map_state_{};
    GraphicsShaders tone_map_shaders_;
    RayQueryShadows ray_query_shadows_;
    vk::SamplerCreateInfo default_sampler_info_;
    // Fallback textures for omitted material slots.
    GpuTexture white_srgb_;
    GpuTexture white_linear_;
    GpuTexture flat_normal_;
};

} // namespace lc1
