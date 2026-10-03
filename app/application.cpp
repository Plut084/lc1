#include "application.hpp"

#include "lc1/game-clock.hpp"
#include "lc1/vk/swapchain-policy.hpp"

#include <algorithm>
#include <array>

namespace lc1::app {
namespace {

constexpr double event_wait_timeout_seconds = 0.1;
constexpr std::array device_extensions{vk::KHRSwapchainExtensionName};

Instance make_instance(VulkanLoader const &loader, Window const &window, bool enable_validation)
{
    auto extensions = window.required_instance_extensions();
    std::vector<char const *> layers;
    if (enable_validation) {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    Instance instance{loader, extensions, layers};
    if (enable_validation)
        instance.setup_debug_messenger();
    return instance;
}

OutputSettings output_settings(vk::Format format)
{
    return {.encoding = format == vk::Format::eR8G8B8A8Unorm || format == vk::Format::eB8G8R8A8Unorm
                            ? OutputEncoding::Srgb
                            : OutputEncoding::Linear};
}

} // namespace

Application::Application(ApplicationConfig const &config)
    : window_{1280, 720, "lc1"},
      instance_{make_instance(loader_, window_, config.enable_validation)},
      surface_{instance_, window_}, device_{instance_, surface_, device_extensions},
      surface_format_{
          pick_surface_format(device_.raii_physical().getSurfaceFormatsKHR(surface_.raii()))},
      renderer_{device_,
                {surface_format_.format},
                std::min(device_.max_sample_count(), vk::SampleCountFlagBits::e4)},
      game_{window_, device_, renderer_, config.asset_root,
            output_settings(surface_format_.format)},
      frame_loop_{device_,
                  [this] { return renderer_.make_frame_resources(game_.object_capacity()); }}
{
}

bool Application::prepare_swapchain()
{
    // Consume every resize event, even when recreation is already pending.
    bool const framebuffer_resized = window_.take_resize_event();
    recreate_requested_ = recreate_requested_ || framebuffer_resized;
    if (swapchain_ && !recreate_requested_)
        return true;

    auto const caps = device_.raii_physical().getSurfaceCapabilitiesKHR(surface_.raii());
    auto const framebuffer = window_.framebuffer_extent();
    vk::Extent2D extent{};
    if (!compute_extent({.width = framebuffer.width, .height = framebuffer.height}, caps,
                        &extent)) {
        // Keep creation/recreation pending. Bounded waits also observe shutdown
        // signals when no further window event arrives.
        window_.wait_events(event_wait_timeout_seconds);
        return false;
    }

    auto const modes = device_.raii_physical().getSurfacePresentModesKHR(surface_.raii());
    SwapchainConfig const config{
        .surface_format = surface_format_,
        .present_mode = pick_present_mode(modes),
        .min_image_count = pick_image_count(caps),
        .composite_alpha = pick_composite_alpha(caps.supportedCompositeAlpha),
    };
    if (swapchain_)
        swapchain_->recreate(config, extent);
    else
        swapchain_.emplace(device_, surface_, config, extent);
    recreate_requested_ = false;
    game_.set_aspect_ratio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
    return true;
}

void Application::run_loop(std::function<bool()> const &stop_requested)
{
    Stopwatch stopwatch;
    // Construct once; FrameLoop invokes the callback synchronously and never retains it.
    std::function const record = [this](vk::raii::CommandBuffer const &command_buffer,
                                        FrameResources &resources, RenderTarget const &target) {
        renderer_.record(command_buffer, resources, target, game_.camera(), game_.lights(),
                         game_.draws(), game_.output());
    };

    while (!window_.should_close() && !stop_requested()) {
        window_.poll_events();
        // Resolve this frame's events before any action is read, including skipped frames.
        router_.update(window_.input());
        if (window_.should_close() || stop_requested())
            break;
        // Exit is application policy; Window must not consume Escape.
        if (window_.input().pressed(Key::Escape))
            break;

        // Recreate only at the top of a frame, before acquiring an image.
        if (!prepare_swapchain())
            continue;
        game_.update(window_, router_, stopwatch.tick());
        switch (frame_loop_.draw_frame(*swapchain_, record)) {
        case FrameResult::Ok:
            break;
        case FrameResult::RecreateRequested:
            recreate_requested_ = true;
            break;
        case FrameResult::SurfaceLost:
            fail("VK_ERROR_SURFACE_LOST_KHR: the presentation surface is gone "
                 "and is not recoverable in this milestone");
        }
    }
}

void Application::run(std::function<bool()> const &stop_requested)
{
    try {
        run_loop(stop_requested);
    }
    catch (...) {
        // Another frame slot may still reference the game's meshes/materials.
        device_.wait_idle();
        throw;
    }
    device_.wait_idle();
}

} // namespace lc1::app
