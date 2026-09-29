#include "lc1/camera.hpp"
#include "lc1/game-clock.hpp"
#include "lc1/model.hpp"
#include "lc1/vk/common.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/frame_loop.hpp"
#include "lc1/vk/instance.hpp"
#include "lc1/vk/loader.hpp"
#include "lc1/vk/material.hpp"
#include "lc1/vk/mesh.hpp"
#include "lc1/vk/renderer.hpp"
#include "lc1/vk/swapchain-policy.hpp"
#include "lc1/vk/swapchain.hpp"
#include "lc1/vk/texture.hpp"
#include "lc1/window.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <optional>
#include <print>
#include <vector>

namespace {

// Set from a signal handler, so it must be sig_atomic_t:
// glfwSetWindowShouldClose is not async-signal-safe and must not be called from
// the handler itself.
std::sig_atomic_t volatile interrupted = 0;

constexpr double event_wait_timeout_seconds = 0.1;

void interrupt_handler(int /*unused*/)
{
    interrupted = 1;
}

} // namespace

int main()
{
    // Line-buffer stdout. The debug callback aborts on a validation error, and
    // abort() does not flush block-buffered streams -- so under a pipe the
    // startup logs would vanish in exactly the run where they matter most.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

#ifdef NDEBUG
    bool enable_validation = false;
#else
    bool enable_validation = true;
#endif
    if (std::getenv("LC1_NO_VALIDATION") != nullptr)
        enable_validation = false;

    spdlog::set_pattern("[%^%l%$] %v");

    // Graceful shutdown also applies while waiting for a usable framebuffer.
    std::signal(SIGINT, interrupt_handler);
    std::signal(SIGTERM, interrupt_handler);

    try {
        // Declared FIRST so that it is destroyed LAST: every Vulkan function
        // pointer below, GLFW's included, points into the library it dlopened.
        // It must also precede Window, because GLFW reads the loader it is
        // handed only in glfwInit.
        lc1::VulkanLoader loader;

        // Destroyed after every Vulkan object below it. That ordering is
        // load-bearing on Wayland: the VkSurfaceKHR is built from GLFW's
        // wl_surface, so glfwTerminate must not run while a surface still
        // refers to it.
        lc1::Window window{1280, 720, "lc1"};

        auto instance_extensions = window.required_instance_extensions();
        auto layers = std::vector<char const *>{};
        if (enable_validation) {
            constexpr auto validation_layer = "VK_LAYER_KHRONOS_validation";
            instance_extensions.push_back(vk::EXTDebugUtilsExtensionName);
            layers.push_back(validation_layer);
        }
        lc1::Instance instance{loader, instance_extensions, layers};
        if (enable_validation)
            instance.setup_debug_messenger();

        auto device_extensions = std::vector<char const *>{
            vk::KHRSwapchainExtensionName,
        };
        lc1::Device device{instance, window, device_extensions};

        // Format selection needs the surface, not an image extent. Use the
        // same choice for every swapchain creation and the renderer's pipeline.
        auto const formats = device.raii_physical().getSurfaceFormatsKHR(device.raii_surface());
        auto const surface_format = lc1::pick_surface_format(formats);

        // Empty means not constructed yet. A constructed Swapchain is ready
        // to use; first creation waits for a usable extent in the main loop.
        std::optional<lc1::Swapchain> swapchain;

        lc1::FrameLoop frame_loop{device};

        // The renderer's pipeline enables sample shading, so it draws every
        // sample of a pixel on its own; the sample count is the device's
        // maximum.
        auto const samples = device.max_sample_count();
        lc1::Renderer renderer{device, {surface_format.format}, samples};

        // The application pairs drawing resources with synchronization slots.
        // Renderer creates one set at a time and never needs the slot count.
        std::vector<lc1::FrameResources> frame_resources;
        frame_resources.reserve(frame_loop.frame_count());
        for (std::uint32_t slot = 0; slot < frame_loop.frame_count(); ++slot) {
            frame_resources.push_back(renderer.make_frame_resources());
        }

        // Resources: loaded once. Later these come from files and live in a
        // resource cache; objects refer to them by pointer.
        // std::vector<lc1::Vertex> const vertices = {
        //     {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        //     {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        //     {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        //     {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        //
        //     {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        //     {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        //     {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        //     {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}};
        //
        // std::vector<uint16_t> const indices = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};
        // lc1::Mesh const triangle(device, vertices, indices);

        auto model = lc1::Model::load_from_file(
            "/home/shelpam/Documents/projects/mine/lc1/assets/models/viking_room.obj");
        auto mesh = model.gen_mesh(device);
        auto tex = lc1::Texture(
            device, "/home/shelpam/Documents/projects/mine/lc1/assets/textures/viking_room.png");
        auto material = renderer.make_material(tex);

        // lc1::Texture const texture{device, "/home/shelpam/Pictures/zyx.png"};
        // // After texture, so destroyed before it; after renderer, whose pool its
        // // descriptor set is freed back into.
        // lc1::Material const material = renderer.make_material(texture);

        // Rebuilt every frame by walking the scene; one fixed item for now.
        std::vector<lc1::DrawItem> draws;

        lc1::Stopwatch stopwatch;
        lc1::FpsCamera camera;

        lc1::UniformBufferObject ubo{};
        // Construct the callback once; it borrows the current drawing inputs.
        std::function const record = [&](vk::raii::CommandBuffer const &command_buffer,
                                         std::uint32_t slot, lc1::RenderTarget const &target) {
            renderer.record(command_buffer, frame_resources[slot], target, ubo, draws);
        };

        bool recreate_requested = false;
        try {
            while (!window.should_close() && interrupted == 0) {
                window.poll_events();
                if (window.should_close() || interrupted != 0) {
                    break;
                }

                // Taken unconditionally; see Window::take_resize_event.
                bool const framebuffer_resized = window.take_resize_event();
                recreate_requested = recreate_requested || framebuffer_resized;

                // Creation and recreation happen here: at the top of the
                // frame, before acquire. That keeps the acquired-image window
                // from ever overlapping recreation, and it coalesces the flood
                // of resize events produced by dragging a window edge into one
                // rebuild per frame.
                if (!swapchain || recreate_requested) {
                    auto const caps =
                        device.raii_physical().getSurfaceCapabilitiesKHR(device.raii_surface());
                    auto const framebuffer = window.framebuffer_extent();
                    vk::Extent2D extent{};
                    if (!lc1::compute_extent(
                            {.width = framebuffer.width, .height = framebuffer.height}, caps,
                            &extent)) {
                        // Keep the request pending. A bounded wait also lets us
                        // observe shutdown signals without another window event.
                        window.wait_events(event_wait_timeout_seconds);
                        continue;
                    }

                    auto const modes =
                        device.raii_physical().getSurfacePresentModesKHR(device.raii_surface());
                    lc1::SwapchainConfig const config{
                        .surface_format = surface_format,
                        .present_mode = lc1::pick_present_mode(modes),
                        .min_image_count = lc1::pick_image_count(caps),
                        .composite_alpha = lc1::pick_composite_alpha(caps.supportedCompositeAlpha),
                    };
                    if (swapchain) {
                        swapchain->recreate(config, extent);
                    }
                    else {
                        swapchain.emplace(device, config, extent);
                    }
                    recreate_requested = false;
                    camera.set_aspect_ratio(static_cast<float>(extent.width) /
                                            static_cast<float>(extent.height));
                }

                stopwatch.tick();
                // spdlog::info("[Main] fps: {}", stopwatch.fps());

                ubo.model = glm::mat4(1.0F);
                ubo.model = glm::translate(
                    ubo.model, glm::vec3(glm::sin(glm::radians(10 * stopwatch.total_time())), 0,
                                         glm::cos(glm::radians(10 * stopwatch.total_time()))));
                ubo.model = glm::scale(ubo.model, glm::vec3(1, 1, 1));
                ubo.model = glm::rotate(ubo.model, glm::radians(15 * stopwatch.total_time()),
                                        glm::vec3(0.0F, 1, 0.0F));
                ubo.model =
                    glm::rotate(ubo.model, -glm::radians(90.0F), glm::vec3(1.0F, 0.0F, 0.0F));
                camera.set_position(glm::vec3(5.0F, 2.0F, 2.0F));
                camera.look_at(glm::vec3(0.0F, 0.0F, 0.0F));
                // Every frame, not once: a resize changes the swapchain's shape.
                // ubo.view = glm::lookAt(glm::vec3(2.0F, 2.0F, 2.0F), glm::vec3(0.0F, 0.0F, 0.0F),
                //                        glm::vec3(0.0F, 0.0F, 1.0F));
                ubo.view = camera.view_matrix();
                ubo.proj = camera.projection_matrix();

                draws.clear();
                draws.push_back({.mesh = &mesh, .material = &material});

                auto const result = frame_loop.draw_frame(*swapchain, record);
                switch (result) {
                case lc1::FrameResult::Ok:
                    break;
                case lc1::FrameResult::RecreateRequested:
                    recreate_requested = true;
                    break;
                case lc1::FrameResult::SurfaceLost:
                    lc1::fail("VK_ERROR_SURFACE_LOST_KHR: the presentation surface "
                              "is gone "
                              "and is not recoverable in this milestone");
                }
            }
        }
        catch (...) {
            // A callback may throw while another slot is still on the GPU.
            // Wait while all borrowed drawing resources are still alive.
            device.wait_idle();
            throw;
        }
        device.wait_idle();
    }
    catch (lc1::Error const &error) {
        std::println(stderr, "[lc1] fatal: {}", error.what());
        return EXIT_FAILURE;
    }
    // Everything else, vk::SystemError included: its message already names the
    // failing call and the VkResult. Without this, an escaping exception would
    // std::terminate with no guarantee that any destructor runs.
    catch (std::exception const &error) {
        std::println(stderr, "[lc1] fatal: {}", error.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
