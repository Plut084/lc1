#pragma once

#include "lc1/vk/resources/acceleration-structure.hpp"
#include "lc1/vk/resources/descriptor-set.hpp"
#include "lc1/vk/resources/gpu-image.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace lc1 {

// Reused only after the owning frame slot's fence. The global set is also read
// by the material pass; it contains no legacy shadow-map bindings.
struct RayQueryShadowFrameResources {
    explicit RayQueryShadowFrameResources(DescriptorSet globals) : globals{std::move(globals)} {}

    DescriptorSet globals;
    std::optional<GpuBuffer> instances;
    std::optional<AccelerationStructure> scene;
    std::vector<vk::AccelerationStructureInstanceKHR> built_instances;
    std::optional<GpuImage> visibility;
    std::optional<GpuImage> surface;
    std::optional<GpuImage> depth;
};

struct ShadowMapFrameResources {
    GpuImage depth;
    DescriptorSet globals; // Freed before the depth view it references.
};

// One independently reusable set of drawing resources. The caller must wait
// for its previous GPU use before recording, replacing or destroying it.
// Device and Renderer must outlive these resources.
struct FrameResources {
    FrameResources(vk::raii::DescriptorPool descriptor_pool, DescriptorSet globals,
                   std::vector<DescriptorSet> objects)
        : descriptor_pool{std::move(descriptor_pool)}, ray_query{std::move(globals)},
          objects{std::move(objects)}
    {
    }

    ~FrameResources() = default;
    FrameResources(FrameResources const &) = delete;
    FrameResources &operator=(FrameResources const &) = delete;
    FrameResources(FrameResources &&) = default;
    // Memberwise assignment would destroy the old pool before freeing its sets.
    FrameResources &operator=(FrameResources &&) = delete;

    // Declared first so all descriptor sets are freed before the pool.
    vk::raii::DescriptorPool descriptor_pool;
    RayQueryShadowFrameResources ray_query;
    std::vector<DescriptorSet> objects;
    // Allocated only when this slot first records the legacy diagnostic.
    std::optional<ShadowMapFrameResources> shadow_map;
    // Main material pass attachments; created lazily from the output extent.
    std::optional<GpuImage> depth_image;
    std::optional<GpuImage> color_image;
    // Single-sampled linear HDR resolve, kept for tone mapping and debug readback.
    std::optional<GpuImage> hdr_image;
    // Declared after the image so the set is freed first. Allocated lazily.
    std::optional<DescriptorSet> tone_map;
};

} // namespace lc1
