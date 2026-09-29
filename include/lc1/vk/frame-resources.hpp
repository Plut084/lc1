#pragma once

#include "lc1/vk/buffer.hpp"
#include "lc1/vk/image.hpp"

#include <optional>
#include <utility>

namespace lc1 {

// One independently reusable set of drawing resources, made by
// Renderer::make_frame_resources. The caller owns it and must wait for its
// previous GPU use before recording with it again, replacing it or destroying it.
// Must not outlive the Device it was created from.
struct FrameResources {
    FrameResources(Buffer uniform_buffer, vk::raii::DescriptorPool descriptor_pool,
                   vk::raii::DescriptorSet descriptor_set)
        : uniform_buffer{std::move(uniform_buffer)}, descriptor_pool{std::move(descriptor_pool)},
          descriptor_set{std::move(descriptor_set)}
    {
    }

    ~FrameResources() = default;
    FrameResources(FrameResources const &) = delete;
    FrameResources &operator=(FrameResources const &) = delete;
    FrameResources(FrameResources &&) = default;
    // Memberwise assignment would destroy the old pool before freeing its set.
    FrameResources &operator=(FrameResources &&) = delete;

    Buffer uniform_buffer;
    // Declared before the set so that the set is freed before its pool.
    vk::raii::DescriptorPool descriptor_pool;
    vk::raii::DescriptorSet descriptor_set;
    // Created lazily from the output extent. The color image is only needed
    // for MSAA; the single-sampled resolve destination belongs to the caller.
    std::optional<Image> depth_image;
    std::optional<Image> color_image;
};

} // namespace lc1
