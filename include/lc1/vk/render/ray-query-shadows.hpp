#pragma once

#include "lc1/vk/render/draw-item.hpp"

#include "lc1/vk/render/pipeline.hpp"
#include "lc1/vk/render/render-data.hpp"
#include "lc1/vk/resources/gpu-image.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace lc1 {

class Device;
struct FrameResources;
namespace scene {
class FpsCamera;
} // namespace scene

struct TemporalShadowData {
    glm::mat4 previous_view_projection{1.0F};
    glm::uvec4 frame_info{}; // valid, sequence index, history limit, light count
};
static_assert(sizeof(TemporalShadowData) == 80 && offsetof(TemporalShadowData, frame_info) == 64);

// Ray-query visibility, temporal accumulation and spatial filtering. The Device
// outlives this pass; callers keep per-slot resources alive until GPU completion.
class RayQueryShadows {
  public:
    RayQueryShadows(Device const &device, vk::Format depth_format);

    // Called after the slot fence. Uploads camera/light/object data shared with
    // the subsequent material pass, and leaves visibility ready for sampling.
    void record(vk::raii::CommandBuffer const &commands, FrameResources &resources,
                vk::Extent2D extent, scene::FpsCamera const &camera,
                std::span<scene::Light const> lights, std::span<DrawItem const> draws);
    void invalidate_history() { history_valid_ = false; }

  private:
    void ensure_attachments(FrameResources &resources, vk::Extent2D extent) const;
    void update_acceleration_structure(vk::raii::CommandBuffer const &commands,
                                       FrameResources &resources,
                                       std::span<DrawItem const> draws) const;
    void record_visibility(vk::raii::CommandBuffer const &commands, FrameResources &resources,
                           vk::Extent2D extent, std::span<DrawItem const> draws) const;
    void prepare_history(vk::raii::CommandBuffer const &commands, FrameResources &resources,
                         vk::Extent2D extent);
    void resolve_history(vk::raii::CommandBuffer const &commands, FrameResources &resources,
                         vk::Extent2D extent) const;
    void filter_history(vk::raii::CommandBuffer const &commands, FrameResources &resources,
                        vk::Extent2D extent) const;

    Device const &device_;
    vk::Format depth_format_;
    Pipeline visibility_pipeline_;
    Pipeline temporal_pipeline_;
    Pipeline spatial_pipeline_;

    struct HistoryFrame {
        GpuImage visibility;
        GpuImage surface;
    };
    // Consecutive submissions share history, independently of frame slots.
    // Replacing these images requires waiting for ALL outstanding GPU use.
    std::array<std::optional<HistoryFrame>, 2> history_;
    std::uint32_t history_write_index_ = 0;
    std::uint32_t sequence_ = 0;
    bool history_valid_ = false;
    glm::mat4 previous_view_projection_{1.0F};
    std::vector<DrawItem> previous_draws_; // Borrowed mesh identities, never dereferenced.
    std::array<scene::Light, max_num_lights> previous_lights_{};
    std::uint32_t previous_light_count_ = 0;
};

} // namespace lc1
