#pragma once

#include "lc1/vk/common.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace lc1 {

class Device;
struct DrawItem;
class Renderer;
class Swapchain;
struct UniformBufferObject;

enum class FrameResult : std::uint8_t {
    Ok,
    // OUT_OF_DATE or SUBOPTIMAL. The caller recreates at the top of the next
    // frame -- never mid-frame.
    RecreateRequested,
    // Not recoverable in this milestone.
    SurfaceLost,
};

// Everything one frame in flight owns. The per-FRAME counterpart of
// SwapchainImage: grouping them makes "slot i's fence guards slot i's command
// buffer" a fact about the type rather than about three parallel indices.
struct Frame {
    vk::raii::CommandBuffer command_buffer;
    // Signaled by acquire, waited on by this frame's submit.
    vk::raii::Semaphore image_available;
    // Signaled when this frame's submit finishes on the GPU; waited on before
    // the slot is reused.
    vk::raii::Fence in_flight;
};

// The swapchain/WSI state machine: wait, acquire, submit, present.
//
// Owns only per-FRAME resources (fences, image-available semaphores, command
// buffers). Per-IMAGE resources live in SwapchainImage, so swapchain recreation
// never has to reach into this class -- which is what makes "recreation forgot
// to rebuild X" hard to write.
//
// Must not outlive the Device it was constructed from.
class FrameLoop {
  public:
    static constexpr uint32_t frames_in_flight = 2;

    // The queue comes from the Device: there is exactly one vk::raii::Queue in
    // the program, and it belongs to the Device.
    explicit FrameLoop(Device const &device);

    // `ubo` is this frame's uniform data and `draws` its list of what to draw;
    // see DrawItem. Both are handed to the renderer together with this slot's
    // index, once the slot's fence says the GPU is done with it.
    FrameResult draw_frame(Swapchain &swapchain, Renderer const &renderer,
                           UniformBufferObject const &ubo, std::span<DrawItem const> draws);

  private:
    Device const &device_;

    vk::raii::CommandPool command_pool_{nullptr};
    // A vector rather than std::array: raii handles have no default
    // constructor. Sized to frames_in_flight in the constructor.
    std::vector<Frame> frames_;

    std::uint32_t frame_index_ = 0;
};

} // namespace lc1
