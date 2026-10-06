#pragma once

#include "lc1/vk/render/draw-item.hpp"

#include "lc1/scene/lights/light.hpp"
#include "lc1/vk/render/graphics-shaders.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"
#include "lc1/vk/resources/render-target.hpp"

#include <span>

namespace lc1 {

class Device;
class CommandBuffer;
struct FrameResources;

// Coverage for the legacy directional-light depth-map diagnostic, in metres.
// The caller explicitly chooses the region; ray-query rendering has no such input.
struct ShadowMapRegion {
    glm::vec3 center{};
    float half_extent = 96.0F;
};

// Independent diagnostic, not a lighting backend. Shaders/sampler belong to
// this pass; the lazily created depth image and descriptors belong to each slot.
class ShadowMapPreview {
  public:
    explicit ShadowMapPreview(Device const &device);
    void record(CommandBuffer &commands, FrameResources &resources, RenderTarget const &target,
                std::span<scene::Light const> lights, std::span<DrawItem const> draws,
                ShadowMapRegion const &region) const;

  private:
    void ensure_resources(FrameResources &resources) const;

    Device const *device_; // Non-owning, non-null; the device outlives this object.
    vk::Format depth_format_;
    GraphicsShaders depth_shaders_;
    GraphicsShaders preview_shaders_;
    vk::SamplerCreateInfo sampler_;
};

} // namespace lc1
