#pragma once

#include "lc1/vk/render/draw-item.hpp"

#include "lc1/scene/lights/light.hpp"
#include "lc1/vk/render/pipeline.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"
#include "lc1/vk/resources/render-target.hpp"

#include <span>

namespace lc1 {

class Device;
struct FrameResources;

// Coverage for the legacy directional-light depth-map diagnostic, in metres.
// The caller explicitly chooses the region; ray-query rendering has no such input.
struct ShadowMapRegion {
    glm::vec3 center{};
    float half_extent = 96.0F;
};

// Independent diagnostic, not a lighting backend. Pipelines/sampler belong to
// this pass; the lazily created depth image and descriptors belong to each slot.
class ShadowMapPreview {
  public:
    ShadowMapPreview(Device const &device, vk::Format output_format);
    void record(vk::raii::CommandBuffer const &commands, FrameResources &resources,
                RenderTarget const &target, std::span<scene::Light const> lights,
                std::span<DrawItem const> draws, ShadowMapRegion const &region) const;

  private:
    void ensure_resources(FrameResources &resources) const;

    Device const &device_;
    vk::Format depth_format_;
    Pipeline depth_pipeline_;
    Pipeline preview_pipeline_;
    vk::raii::Sampler sampler_;
};

} // namespace lc1
