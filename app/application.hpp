#pragma once

#include "game-session.hpp"

#include "lc1/input.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/frame_loop.hpp"
#include "lc1/vk/instance.hpp"
#include "lc1/vk/loader.hpp"
#include "lc1/vk/surface.hpp"
#include "lc1/window.hpp"

#include <filesystem>
#include <functional>
#include <optional>

namespace lc1::app {

struct ApplicationConfig {
    bool enable_validation = true;
    std::filesystem::path asset_root = "assets";
};

// Owns the graphics lifetime and presentation loop. GameSession owns gameplay.
class Application {
  public:
    explicit Application(ApplicationConfig const &config);
    Application(Application const &) = delete;
    Application &operator=(Application const &) = delete;
    Application(Application &&) = delete;
    Application &operator=(Application &&) = delete;

    // Drains the GPU on both normal and exceptional exits, before any member dies.
    void run(std::function<bool()> const &stop_requested);

  private:
    bool prepare_swapchain();
    void run_loop(std::function<bool()> const &stop_requested);

    // Declaration order is the lifetime contract: the loader dies last, Window
    // outlives the surface, and all GPU objects die before Device/Instance.
    VulkanLoader loader_;
    Window window_;
    Instance instance_;
    Surface surface_;
    Device device_;
    vk::SurfaceFormatKHR const surface_format_;
    std::optional<Swapchain> swapchain_;
    Renderer renderer_;
    GameSession game_;
    FrameLoop<FrameResources> frame_loop_;
    InputRouter router_{default_bindings(), InputContext::Gameplay};
    bool recreate_requested_ = false;
};

} // namespace lc1::app
