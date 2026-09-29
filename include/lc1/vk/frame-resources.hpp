#pragma once

#include "lc1/vk/descriptor-set.hpp"
#include "lc1/vk/gpu-image.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace lc1 {

// One independently reusable set of drawing resources, made by
// Renderer::make_frame_resources. The caller owns it and must wait for its
// previous GPU use before recording with it again, replacing it or destroying it.
// Must not outlive the Device it was created from.
struct FrameResources {
    FrameResources(vk::raii::DescriptorPool descriptor_pool, DescriptorSet globals,
                   std::vector<DescriptorSet> objects, GpuImage shadow_image)
        : descriptor_pool{std::move(descriptor_pool)}, globals{std::move(globals)},
          objects{std::move(objects)}, shadow_image{std::move(shadow_image)}
    {
    }

    ~FrameResources() = default;
    FrameResources(FrameResources const &) = delete;
    FrameResources &operator=(FrameResources const &) = delete;
    FrameResources(FrameResources &&) = default;
    // Memberwise assignment would destroy the old pool before freeing its set.
    FrameResources &operator=(FrameResources &&) = delete;

    // Declared first so all descriptor sets are freed before the pool.
    vk::raii::DescriptorPool descriptor_pool;
    DescriptorSet globals;
    std::vector<DescriptorSet> objects;
    // Created lazily from the output extent. The color image is only needed
    // for MSAA; the single-sampled resolve destination belongs to the caller.
    GpuImage shadow_image; // One independent depth map per in-flight slot.
    std::optional<GpuImage> depth_image;
    std::optional<GpuImage> color_image;
};

} // namespace lc1
