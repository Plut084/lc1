#pragma once

#include "lc1/vk/buffer.hpp"
#include "lc1/vk/common.hpp"
#include "lc1/vk/mesh.hpp"
#include "lc1/vk/pipeline.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace lc1 {

class Device;
class Swapchain;

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
class Renderer {
  public:
    Renderer(Device const &device, Swapchain const &swapchain);

    // `frame_index` picks the uniform buffer and descriptor set. It must be the
    // frame loop's slot, because only that slot's fence proves the GPU is done
    // reading them. `image_index` picks the swapchain image; the two are
    // unrelated.
    void record(vk::raii::CommandBuffer const &command_buffer, std::uint32_t frame_index,
                Swapchain const &swapchain, std::uint32_t image_index,
                UniformBufferObject const &ubo, std::span<DrawItem const> draws) const;

  private:
    // One per frame in flight: the CPU writes one slot's buffer while the GPU
    // may still be reading the other's.
    struct FrameData {
        Buffer uniform_buffer;
        // Points at uniform_buffer.
        vk::raii::DescriptorSet descriptor_set;
    };

    Pipeline pipeline_;
    // Declared before frames_: each vk::raii::DescriptorSet frees itself back
    // into this pool, so the pool must outlive them.
    vk::raii::DescriptorPool descriptor_pool_{nullptr};
    // Sized to FrameLoop::frames_in_flight in the constructor.
    std::vector<FrameData> frames_;
};

} // namespace lc1
