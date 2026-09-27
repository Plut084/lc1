#include "lc1/camera.hpp"
#include "lc1/game-clock.hpp"
#include "lc1/vk/common.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/frame_loop.hpp"
#include "lc1/vk/instance.hpp"
#include "lc1/vk/loader.hpp"
#include "lc1/vk/mesh.hpp"
#include "lc1/vk/renderer.hpp"
#include "lc1/vk/swapchain.hpp"
#include "lc1/window.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <print>
#include <vector>

namespace {

// Set from a signal handler, so it must be sig_atomic_t:
// glfwSetWindowShouldClose is not async-signal-safe and must not be called from
// the handler itself.
std::sig_atomic_t volatile interrupted = 0;

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

        lc1::Swapchain swapchain{device, window};

        lc1::FrameLoop frame_loop{device};

        lc1::Renderer renderer{device, swapchain};

        // Resources: loaded once. Later these come from files and live in a
        // resource cache; objects refer to them by pointer.
        std::vector<lc1::Vertex> const vertices = {
            {.position = {-0.5f, -0.5f}, .color = {1.0f, 0.0f, 0.0f}},
            {.position = {0.5f, -0.5f}, .color = {0.0f, 1.0f, 0.0f}},
            {.position = {0.5f, 0.5f}, .color = {0.0f, 0.0f, 1.0f}},
            {.position = {-0.5f, 0.5f}, .color = {1.0f, 1.0f, 1.0f}}};
        std::vector<uint16_t> const indices = {0, 1, 2, 2, 3, 0};
        lc1::Mesh const triangle(device, vertices, indices);

        // Rebuilt every frame by walking the scene; one fixed item for now.
        std::vector<lc1::DrawItem> draws;

        // Graceful shutdown on Ctrl-C / SIGTERM, so validation still runs its
        // leak checks at instance destruction.
        std::signal(SIGINT, interrupt_handler);
        std::signal(SIGTERM, interrupt_handler);

        lc1::Stopwatch stopwatch;
        lc1::FpsCamera camera;

        bool recreate_requested = false;
        while (!window.should_close() && interrupted == 0) {
            window.poll_events();

            // Taken unconditionally; see Window::take_resize_event.
            bool const framebuffer_resized = window.take_resize_event();

            // Recreation happens here and only here: at the top of the
            // frame, before acquire. That keeps the acquired-image window
            // from ever overlapping recreation, and it coalesces the flood
            // of resize events produced by dragging a window edge into one
            // rebuild per frame.
            if (recreate_requested || framebuffer_resized) {
                recreate_requested = false;
                if (!swapchain.recreate()) {
                    // Minimized: block rather than spin. On Wayland a
                    // minimize may not even generate a resize event.
                    window.wait_events();
                    continue;
                }
            }

            stopwatch.tick();
            // spdlog::info("[Main] fps: {}", stopwatch.fps());

            lc1::UniformBufferObject ubo{};
            ubo.model = glm::mat4(1.0F);
            // ubo.model = glm::translate(ubo.model, glm::vec3(glm::sin(stopwatch.total_time()),
            //                                                 glm::cos(stopwatch.total_time()),
            //                                                 0));
            // ubo.model = glm::scale(ubo.model, glm::vec3(0.5F, 0.5F, 0.5F));
            ubo.model = glm::rotate(ubo.model, glm::radians(30 * stopwatch.total_time()),
                                    glm::vec3(0.0F, 0.0F, 1.0F));
            camera.set_position(glm::vec3(2.0F, 2.0F, 2.0F));
            camera.look_at(glm::vec3(0.0F, 0.0F, 0.0F));
            // ubo.view = glm::lookAt(glm::vec3(2.0F, 2.0F, 2.0F), glm::vec3(0.0F, 0.0F, 0.0F),
            //                        glm::vec3(0.0F, 0.0F, 1.0F));
            ubo.view = camera.view_matrix();
            ubo.proj = camera.projection_matrix();

            draws.clear();
            draws.push_back({.mesh = &triangle});

            switch (frame_loop.draw_frame(swapchain, renderer, ubo, draws)) {
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

        // Explicit because ~vk::raii::Device does not wait for the GPU before
        // destroying the device.
        device.wait_idle();
    }
    catch (lc1::Error const &error) {
        std::println(stderr, "[lc1] fatal: {}", error.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
