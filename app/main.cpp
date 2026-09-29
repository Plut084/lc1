#include "lc1/camera.hpp"
#include "lc1/game-clock.hpp"
#include "lc1/input.hpp"
#include "lc1/render-object.hpp"
#include "lc1/resource-manager.hpp"
#include "lc1/vk/common.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/frame_loop.hpp"
#include "lc1/vk/instance.hpp"
#include "lc1/vk/loader.hpp"
#include "lc1/vk/material.hpp"
#include "lc1/vk/mesh.hpp"
#include "lc1/vk/renderer.hpp"
#include "lc1/vk/surface.hpp"
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

// The application's whole control scheme, as data. This is the only place a key meets an
// action: rebinding, or giving one key a different meaning in another mode, is a row here
// and touches no other file.
//
// One row per (context, action, key), so an action can have several rows -- W and Up both
// mean MoveForward -- and one key can mean different things in different modes. Several
// rows may share an action; the action is held if any of them is.
//
// InputContext::Ui has no rows on purpose and does not need any: an unbound context
// resolves every action to false, which is exactly the gate for "a text field or ImGui
// owns the keyboard now".
constexpr std::array bindings{
    // 大地图: RPG movement, cursor captured so mouse motion turns the camera.
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::MoveForward, lc1::Key::W},
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::MoveForward, lc1::Key::Up},
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::MoveBackward, lc1::Key::S},
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::MoveBackward, lc1::Key::Down},
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::MoveLeft, lc1::Key::A},
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::MoveLeft, lc1::Key::Left},
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::MoveRight, lc1::Key::D},
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::MoveRight, lc1::Key::Right},
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::MoveUp, lc1::Key::Space},
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::MoveDown, lc1::Key::LeftControl},
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::Sprint, lc1::Key::LeftShift},

    // 小地图: top-down RTS controls, cursor visible. W is deliberately unbound here: the
    // arrows scroll the map, the left button draws a selection box, the right one gives an
    // order, and the mouse belongs to the map rather than to the camera.
    lc1::Binding{lc1::InputContext::Minimap, lc1::Action::MoveForward, lc1::Key::Up},
    lc1::Binding{lc1::InputContext::Minimap, lc1::Action::MoveBackward, lc1::Key::Down},
    lc1::Binding{lc1::InputContext::Minimap, lc1::Action::MoveLeft, lc1::Key::Left},
    lc1::Binding{lc1::InputContext::Minimap, lc1::Action::MoveRight, lc1::Key::Right},
    lc1::Binding{lc1::InputContext::Minimap, lc1::Action::Select, lc1::Key::MouseLeft},
    lc1::Binding{lc1::InputContext::Minimap, lc1::Action::OrderMove, lc1::Key::MouseRight},

    // Switching modes is possible from either one, so both carry the toggle.
    lc1::Binding{lc1::InputContext::WorldMap, lc1::Action::ToggleMapMode, lc1::Key::M},
    lc1::Binding{lc1::InputContext::Minimap, lc1::Action::ToggleMapMode, lc1::Key::M},
};

// Mouse look and walking, from this frame's resolved actions.
//
// Reads actions, never keys: nothing below names W, so moving a control is an edit to
// `bindings` and not to this function.
void update_camera(lc1::InputState const &input, lc1::InputRouter const &router,
                   bool cursor_captured, float delta_seconds, lc1::FpsCamera &camera)
{
    // Look only while the cursor is captured. In the RTS mode the pointer is visible and
    // its motion belongs to the map, not to the view.
    if (cursor_captured) {
        constexpr float look_sensitivity = 0.05F; // degrees per screen point
        glm::vec2 const look = input.cursor_delta() * look_sensitivity;
        camera.add_yaw(look.x);
        // Screen y grows downwards, so looking up is a negative pitch.
        camera.add_pitch(-look.y);
    }

    glm::vec3 local_move{0.0F};
    if (router.held(lc1::Action::MoveForward))
        local_move.z += 1.0F;
    if (router.held(lc1::Action::MoveBackward))
        local_move.z -= 1.0F;
    if (router.held(lc1::Action::MoveRight))
        local_move.x += 1.0F;
    if (router.held(lc1::Action::MoveLeft))
        local_move.x -= 1.0F;
    if (router.held(lc1::Action::MoveUp))
        local_move.y += 1.0F;
    if (router.held(lc1::Action::MoveDown))
        local_move.y -= 1.0F;

    if (glm::dot(local_move, local_move) <= 0.0F)
        return;

    constexpr float walk_speed = 4.0F; // units per second
    constexpr float sprint_multiplier = 4.0F;
    float const speed = walk_speed * (router.held(lc1::Action::Sprint) ? sprint_multiplier : 1.0F);
    // Normalised, or holding two directions would be faster than one. Scaled by the frame's
    // delta, so the speed is per second and the feel does not change with the frame rate.
    camera.move_groud(glm::normalize(local_move) * speed * delta_seconds);
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
        lc1::Surface surface{instance, window};
        lc1::Device device{instance, surface, device_extensions};

        // Format selection needs the surface, not an image extent. Use the
        // same choice for every swapchain creation and the renderer's pipeline.
        auto const formats = device.raii_physical().getSurfaceFormatsKHR(surface.raii());
        auto const surface_format = lc1::pick_surface_format(formats);

        // Empty means not constructed yet. A constructed Swapchain is ready
        // to use; first creation waits for a usable extent in the main loop.
        std::optional<lc1::Swapchain> swapchain;

        // The renderer's pipeline enables sample shading, so it draws every
        // sample of a pixel on its own; the sample count is the device's
        // maximum.
        auto const samples = device.max_sample_count();
        lc1::Renderer renderer{device, {surface_format.format}, samples};
        constexpr std::uint32_t object_capacity = 16;
        lc1::FrameLoop<lc1::FrameResources> frame_loop{
            device, [&] { return renderer.make_frame_resources(object_capacity); }};

        // Declared after Renderer, before its borrowers. The loop's normal and
        // exceptional exits wait for GPU completion before these resources die.
        std::string_view asset_root{"/home/shelpam/Documents/projects/mine/lc1/assets"};
        lc1::ResourceManager resources{device, asset_root};
        auto &texture = resources.load<lc1::Texture>("textures/viking_room.png");
        auto material = renderer.make_material(texture);
        auto const *const mesh_path = "models/viking_room.obj";

        // The OBJ is Z-up; both objects convert to our Y-up world.
        std::array objects{
            lc1::RenderObject{
                .transform = {.position = {-1.5F, 0.0F, 0.0F}, .rotation = {-90.0F, 0.0F, 0.0F}},
                .mesh = &resources.load_mesh(mesh_path),
                .material = &material},
            lc1::RenderObject{
                .transform = {.position = {1.5F, 0.0F, 0.0F}, .rotation = {-90.0F, 0.0F, 0.0F}},
                .mesh = &resources.load_mesh(mesh_path),
                .material = &material},
        };

        // Rebuilt every frame from CPU-side objects.
        std::vector<lc1::DrawItem> draws;

        lc1::Stopwatch stopwatch;
        lc1::FpsCamera camera;
        // Placed once, before the loop: the loop now moves the camera from input, so a
        // per-frame set_position/look_at would fight it.
        camera.set_position(glm::vec3(5.0F, 2.0F, 2.0F));
        camera.look_at(glm::vec3(0.0F, 0.0F, 0.0F));

        // Owned here, like Stopwatch and FpsCamera -- not a singleton, so there is no
        // lifetime question and no state that survives a run.
        lc1::InputRouter router{bindings, lc1::InputContext::WorldMap};
        window.set_cursor_captured(true);

        // Construct the callback once; it borrows the current drawing inputs.
        std::function const record = [&](vk::raii::CommandBuffer const &command_buffer,
                                         lc1::FrameResources &resources,
                                         lc1::RenderTarget const &target) {
            renderer.record(command_buffer, resources, target, camera, draws);
        };

        bool recreate_requested = false;
        try {
            while (!window.should_close() && interrupted == 0) {
                window.poll_events();
                // After polling and before anything reads an action: this is what makes
                // held()/pressed() describe this frame's events and nothing else.
                router.update(window.input());

                if (window.should_close() || interrupted != 0) {
                    break;
                }
                // ESC is this demo's exit shortcut, not an action. It is decided here
                // rather than in Window because a hardcoded ESC-to-close would make ESC
                // unusable for the pause and back keys every strategy game needs.
                if (window.input().pressed(lc1::Key::Escape)) {
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
                        device.raii_physical().getSurfaceCapabilitiesKHR(surface.raii());
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
                        device.raii_physical().getSurfacePresentModesKHR(surface.raii());
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
                        swapchain.emplace(device, surface, config, extent);
                    }
                    recreate_requested = false;
                    camera.set_aspect_ratio(static_cast<float>(extent.width) /
                                            static_cast<float>(extent.height));
                }

                float const delta_seconds = stopwatch.tick();
                // spdlog::info("[Main] fps: {}", stopwatch.fps());

                // Mode switch first: it decides whether this frame's mouse motion is a
                // camera input at all, and set_cursor_captured drops the delta the pointer
                // accumulated before the switch, so the view does not jump on the frame the
                // mode changes.
                if (router.pressed(lc1::Action::ToggleMapMode)) {
                    bool const to_minimap = router.active_context() == lc1::InputContext::WorldMap;
                    if (to_minimap)
                        router.push(lc1::InputContext::Minimap);
                    else
                        router.pop();
                    window.set_cursor_captured(!to_minimap);
                }

                update_camera(window.input(), router, window.cursor_captured(), delta_seconds,
                              camera);

                // RTS edges, so the mouse bindings are observable before the map exists.
                // Delete with the placeholders.
                if (router.pressed(lc1::Action::Select))
                    spdlog::info("[demo] minimap select");
                if (router.pressed(lc1::Action::OrderMove))
                    spdlog::info("[demo] minimap order move");

                // One stationary object and one moving object share mesh and material.
                objects[1].transform.position.y =
                    0.5F * glm::sin(static_cast<float>(stopwatch.total_time()));
                draws.clear();
                for (auto const &object : objects) {
                    draws.push_back(object.draw_item());
                }

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
