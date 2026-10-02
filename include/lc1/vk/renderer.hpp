#pragma once

#include "lc1/scene/scene.hpp"
#include "lc1/vk/common.hpp"
#include "lc1/vk/frame-resources.hpp"
#include "lc1/vk/gpu-mesh.hpp"
#include "lc1/vk/gpu-texture.hpp"
#include "lc1/vk/material.hpp"
#include "lc1/vk/pipeline.hpp"
#include "lc1/vk/ray-query-shadows.hpp"
#include "lc1/vk/render-data.hpp"
#include "lc1/vk/render-target.hpp"
#include "lc1/vk/sampler.hpp"
#include "lc1/vk/shadow-map-preview.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace lc1 {

class Device;
class GpuTexture;

namespace scene {
class FpsCamera;
} // namespace scene

// Owns material rendering, ray-query shadows and an optional depth-map diagnostic.
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
    // Maximum simultaneously live materials; the pool is sized once and cannot grow.
    static constexpr std::uint32_t max_materials = 64;
    static constexpr vk::Format hdr_format = vk::Format::eR16G16B16A16Sfloat;

    // Color/depth images are allocated lazily from the target extent in record.
    Renderer(Device const &device, std::vector<vk::Format> const &color_attachment_formats,
             vk::SampleCountFlagBits samples);
    Renderer(Renderer const &) = delete;
    Renderer &operator=(Renderer const &) = delete;
    Renderer(Renderer &&) = delete;
    Renderer &operator=(Renderer &&) = delete;

    // Allocates set 2 from this renderer's pool and points it at `texture`.
    // Here rather than in Material because only the renderer has the pool, the
    // sampler, and the pipeline whose layout the set must match. Throws once
    // max_materials sets are live.
    Material make_material(GpuTexture const &texture);
    Material make_material(MaterialInfo const &info);
    vk::SampleCountFlagBits sample_count() const { return samples_; }

    // Creates one set of drawing resources with its own descriptor pool. The
    // caller decides how many sets to keep and when each is safe to reuse.
    // Capacity counts draws; it is fixed for the lifetime of this resource set.
    FrameResources make_frame_resources(std::uint32_t object_capacity) const;

    // resources must have been made by this renderer, and its previous GPU use
    // must have completed. The target format must match the constructor's output format.
    // Records lighting and tone-map passes into an already recording command buffer,
    // outside any rendering pass. The caller owns begin/end and the target's
    // layout transitions; record leaves it in COLOR_ATTACHMENT_OPTIMAL.
    // `target.extent` must be nonzero and fit the attachment.
    void record(vk::raii::CommandBuffer const &command_buffer, FrameResources &resources,
                RenderTarget const &target, scene::FpsCamera const &camera,
                std::span<scene::Light const> lights, std::span<DrawItem const> draws,
                OutputSettings output = {});

    void record(vk::raii::CommandBuffer const &command_buffer, FrameResources &resources,
                RenderTarget const &target, scene::Scene const &scene, OutputSettings output = {});

    // Separate diagnostic entry point. No ray queries, TLAS updates or temporal
    // passes run here. Returning to normal rendering starts fresh shadow history.
    void record_shadow_map_preview(vk::raii::CommandBuffer const &command_buffer,
                                   FrameResources &resources, RenderTarget const &target,
                                   std::span<scene::Light const> lights,
                                   std::span<DrawItem const> draws, ShadowMapRegion const &region);

  private:
    // The caller has waited for the resources' previous GPU use.
    void ensure_attachments(FrameResources &resources, vk::Extent2D extent) const;
    void tone_map(vk::raii::CommandBuffer const &commands, FrameResources &resources,
                  RenderTarget const &target, OutputSettings output) const;

    Device const &device_;
    vk::Format color_format_;
    vk::Format depth_format_;
    vk::SampleCountFlagBits samples_;
    Pipeline pipeline_;
    Pipeline tone_map_pipeline_;
    RayQueryShadows ray_query_shadows_;
    std::optional<ShadowMapPreview> shadow_map_preview_;
    // Material sets free themselves back into this pool: the caller must
    // destroy Materials before this renderer. FrameResources own separate pools.
    vk::raii::DescriptorPool material_pool_;
    // Default sampler and fallback textures are borrowed by materials.
    Sampler sampler_;
    GpuTexture white_srgb_;
    GpuTexture white_linear_;
    GpuTexture flat_normal_;
    std::uint32_t live_materials_ = 0;
};

} // namespace lc1
