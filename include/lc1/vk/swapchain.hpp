#pragma once

#include "lc1/vk/common.hpp"

#include <cstdint>
#include <vector>

namespace lc1 {

class Device;
class Window;

// One swapchain image plus everything whose lifetime must match it.
//
// The present semaphore lives here rather than in the frame loop.
// vkGetSwapchainImagesKHR may return MORE images than requested, and the count
// can differ between the original and a recreated swapchain, so a separate
// semaphore array sized from minImageCount makes renderFinished[imageIndex] an
// out-of-bounds write. Tying the semaphore to the image makes "array sized
// wrong" and "recreation forgot a resource" the same bug, caught in one place.
struct SwapchainImage {
    // Non-owning: the swapchain owns this image and destroys it with itself.
    // vk::raii::Image would call vkDestroyImage on it, which is invalid for
    // swapchain images.
    vk::Image image{};
    vk::raii::ImageView view{nullptr};
    vk::raii::Semaphore render_finished{nullptr};
};

// Must not outlive the Device it was constructed from: its raii handles are
// destroyed through that device.
class Swapchain {
  public:
    Swapchain(Device const &device, Window const &window);

    // Rebuilds the swapchain after a resize, VK_ERROR_OUT_OF_DATE_KHR, or
    // VK_SUBOPTIMAL_KHR. Returns false if the window is minimized (zero
    // extent), in which case nothing was destroyed and the caller should block
    // on events rather than spin.
    bool recreate();

    vk::Format format() const { return format_; }
    vk::Extent2D extent() const { return extent_; }
    std::vector<SwapchainImage> const &images() const { return images_; }

    // For FrameLoop: acquireNextImage is a SwapchainKHR method.
    vk::raii::SwapchainKHR const &raii() const { return handle_; }

  private:
    Device const &device_;
    // Read only by recreate(), so the Window must outlive every call to it.
    Window const &window_;

    vk::raii::SwapchainKHR handle_{nullptr};
    vk::Format format_ = vk::Format::eUndefined;
    vk::Extent2D extent_{};
    // Declared last so it is destroyed first: the image views and semaphores in
    // here reference the swapchain's images and must not outlive it on the way
    // out of scope. recreate() has to repeat that ordering by hand.
    std::vector<SwapchainImage> images_;
};

} // namespace lc1
