#include "pbr-showcase.hpp"

#include "lc1/game-clock.hpp"
#include "lc1/game/continent-view.hpp"
#include "lc1/game/continent.hpp"
#include "lc1/game/map-camera-controller.hpp"
#include "lc1/game/player-controller.hpp"
#include "lc1/image.hpp"
#include "lc1/input.hpp"
#include "lc1/model.hpp"
#include "lc1/scene/camera.hpp"
#include "lc1/scene/character.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/scene/lights/light.hpp"
#include "lc1/scene/lights/sphere-light.hpp"
#include "lc1/scene/lights/spot-light.hpp"
#include "lc1/vk/common.hpp"
#include "lc1/vk/device.hpp"
#include "lc1/vk/frame_loop.hpp"
#include "lc1/vk/gpu-mesh.hpp"
#include "lc1/vk/gpu-texture.hpp"
#include "lc1/vk/instance.hpp"
#include "lc1/vk/loader.hpp"
#include "lc1/vk/material.hpp"
#include "lc1/vk/renderer.hpp"
#include "lc1/vk/surface.hpp"
#include "lc1/vk/swapchain-policy.hpp"
#include "lc1/vk/swapchain.hpp"
#include "lc1/window.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
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

lc1::PlayerInput player_input(lc1::InputRouter const &router, glm::vec2 look_delta = {})
{
    return {.movement = {static_cast<float>(router.held(lc1::Action::MoveRight)) -
                             static_cast<float>(router.held(lc1::Action::MoveLeft)),
                         static_cast<float>(router.held(lc1::Action::MoveForward)) -
                             static_cast<float>(router.held(lc1::Action::MoveBackward))},
            .look_delta = look_delta,
            .running = router.held(lc1::Action::Sprint)};
}

// Called only in first-person mode. Mouse-look policy is independent of cursor capture.
void update_player(lc1::Window const &window, lc1::InputRouter const &router, float delta_seconds,
                   lc1::FirstPersonCharacterController const &controller,
                   lc1::scene::Character &player, lc1::scene::FpsCamera &camera)
{
    if (window.focused() && router.active_context() == lc1::InputContext::Gameplay)
        controller.update(delta_seconds, player_input(router, window.input().cursor_delta()),
                          player);
    camera.set_position(player.eye_position());
    camera.look_at(player.eye_position() + player.look_direction());
}

// One oblique view; the camera controller decides whether its center follows the player.
void update_player2(lc1::Window const &window, lc1::InputRouter const &router, float delta_seconds,
                    lc1::Continent const &continent, lc1::scene::Character &player,
                    lc1::ThirdPersonCharacterController const &controller,
                    lc1::scene::FpsCamera &camera, lc1::MapCameraController &camera_controller)
{
    auto const &input = window.input();
    bool const enabled = router.active_context() == lc1::InputContext::Gameplay && window.focused();
    camera_controller.apply(camera);
    if (enabled)
        controller.update(delta_seconds, player_input(router), camera.heading(), player);

    camera_controller.update(
        camera, player.position(), continent.bounds(),
        {.enabled = enabled,
         .toggle_lock = router.pressed(lc1::Action::ToggleCameraLock),
         .recenter = router.held(lc1::Action::RecenterCamera),
         .scroll = input.scroll().y,
         .cursor = input.cursor(),
         .viewport_size = window.content_size(),
         .pointer_active = window.hovered() && window.focused() && !window.cursor_captured()},
        delta_seconds);
    if (enabled && router.pressed(lc1::Action::ToggleCameraLock))
        spdlog::info("[camera] {}",
                     camera_controller.locked() ? "locked to player" : "free camera");
}

// A visible, body-sized placeholder for the player now that the camera is above them.
lc1::Model make_player_model()
{
    std::vector<lc1::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    auto box = [&](float radius, float bottom, float top, glm::vec3 color) {
        // Separate vertices at hard edges so each box face has its own normal.
        std::array const points{
            glm::vec3{-radius, bottom, -radius}, glm::vec3{radius, bottom, -radius},
            glm::vec3{radius, top, -radius},     glm::vec3{-radius, top, -radius},
            glm::vec3{-radius, bottom, radius},  glm::vec3{radius, bottom, radius},
            glm::vec3{radius, top, radius},      glm::vec3{-radius, top, radius}};
        for (auto const face : std::array{std::array{4U, 5U, 6U, 7U}, std::array{1U, 0U, 3U, 2U},
                                          std::array{0U, 4U, 7U, 3U}, std::array{5U, 1U, 2U, 6U},
                                          std::array{3U, 7U, 6U, 2U}, std::array{0U, 1U, 5U, 4U}}) {
            auto const base = static_cast<std::uint32_t>(vertices.size());
            auto const normal = glm::normalize(
                glm::cross(points[face[1]] - points[face[0]], points[face[2]] - points[face[0]]));
            for (auto const index : face)
                vertices.push_back({points[index], normal, color, {0.5F, 0.5F}});
            for (auto const index : {0U, 1U, 2U, 0U, 2U, 3U})
                indices.push_back(base + index);
        }
    };
    constexpr lc1::scene::CharacterConfig config;
    box(config.half_width, 0.0F, 1.2F, {0.06F, 0.25F, 0.8F});
    box(0.22F, 1.2F, config.height, {0.95F, 0.65F, 0.15F});
    return lc1::Model{std::move(vertices), std::move(indices)};
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

        // Keep 4x sample shading for material/BRDF evaluation. Shadow rays run
        // separately in a single-sampled visibility pass.
        auto const samples = std::min(device.max_sample_count(), vk::SampleCountFlagBits::e4);
        lc1::Renderer renderer{device, {surface_format.format}, samples};
        lc1::OutputSettings output{.encoding =
                                       surface_format.format == vk::Format::eR8G8B8A8Unorm ||
                                               surface_format.format == vk::Format::eB8G8R8A8Unorm
                                           ? lc1::OutputEncoding::Srgb
                                           : lc1::OutputEncoding::Linear};
        char const *const configured_root = std::getenv("LC1_ASSET_ROOT");
        auto const asset_root = std::filesystem::path{configured_root ? configured_root : "assets"};
        lc1::GpuTexture const white{
            device, lc1::Image::load_from_file(asset_root / "textures/prototype-white.png")};
        auto const showcase_blocks = lc1::demo::PbrShowcase::spawn_blocks();
        lc1::Continent const continent = lc1::Continent::make_prototype(showcase_blocks);
        lc1::ContinentView const continent_view{device, renderer, white, continent};
        lc1::demo::PbrShowcase showcase{device, renderer, continent.spawn()};
        lc1::GpuMesh const player_mesh = make_player_model().gen_mesh(device);
        lc1::Material const player_material = renderer.make_material(white);
        lc1::scene::Character player{continent,
                                     {.transform = {.position = continent.spawn()},
                                      .mesh = &player_mesh,
                                      .material = &player_material}};
        lc1::FirstPersonCharacterController const first_person_controller{continent};
        lc1::ThirdPersonCharacterController const third_person_controller{continent};
        auto const terrain_draws = continent_view.draw_items();
        std::vector<lc1::DrawItem> draws{terrain_draws.begin(), terrain_draws.end()};
        auto const showcase_offset = draws.size();
        draws.insert(draws.end(), showcase.draws().begin(), showcase.draws().end());
        draws.push_back(player.draw_item());

        // Capacity follows the generated chunks. GPU resources die before their owners.
        auto const object_capacity = static_cast<std::uint32_t>(draws.size());
        lc1::FrameLoop<lc1::FrameResources> frame_loop{
            device, [&] { return renderer.make_frame_resources(object_capacity); }};
        lc1::Stopwatch stopwatch;
        lc1::scene::FpsCamera camera;
        camera.set_zfar(800.0F);

        lc1::scene::DirectionalLight sun{
            .base = {.color = glm::vec3{1.0F, 0.95F, 0.9F}, .intensity = 0.3F},
            .direction = glm::normalize(glm::vec3{-1.0F, -1.0F, -1.0F})};
        lc1::scene::SphereLight lamp{
            .base = {.color = glm::vec3{1.0F, 0.5F, 0.5F}, .intensity = 128},
            .position = continent.spawn() + glm::vec3{3.0F, 10.0F, 0.0F},
            .radius = 1.0F,
            .shadow_sample_count = 2};
        lc1::scene::SpotLight spotlight{
            .base = {.color = glm::vec3{0.5F, 0.5F, 1.0F}, .intensity = 64},
            .position = continent.spawn() + glm::vec3{-3.0F, 10.0F, 0.0F},
            .direction = glm::normalize(glm::vec3{1.0F, -1.0F, 0.0F}),
            .inner_angle = glm::radians(15.0F),
            .outer_angle = glm::radians(30.0F)};
        std::vector<lc1::scene::Light> lights{
            to_light(sun),
            to_light(lamp),
            to_light(spotlight),
        };
        // Keep the player's flashlight last; the update below follows the player.
        lights.insert(lights.end() - 1, showcase.lights().begin(), showcase.lights().end());
        std::vector<lc1::scene::Light> active_lights = lights;
        constexpr std::array lighting_names{"全部",   "方向光", "点光",
                                            "聚光灯", "球形光", "仅发光材质"};
        std::size_t lighting_mode = 0;
        bool showcase_animated = true;
        float showcase_time = 0.0F;

        lc1::MapCameraController camera_controller{player.position()};
        bool oblique_view = true;
        glm::vec2 first_person_angles{0.0F};
        lc1::InputRouter router{lc1::default_bindings(), lc1::InputContext::Gameplay};
        update_player2(window, router, 0.0F, continent, player, third_person_controller, camera,
                       camera_controller);
        spdlog::info("[world] WASD: walk, wheel: zoom, Shift: run, Y: camera lock, Space: "
                     "recenter, M: first-person/oblique view, Esc: quit");
        spdlog::info("[showcase] material exhibits beside the spawn road; F4: lighting, "
                     "F5: pause motion, PageUp/PageDown: exposure");
        spdlog::info(
            "[world] {} regions, {} locations, {} roads, {} terrain chunks; fixed oblique view",
            continent.regions().size(), continent.locations().size(), continent.roads().size(),
            terrain_draws.size());
        window.set_cursor_captured(false);

        // Identity comes from map data, including when the camera is panned away.
        std::string current_title;
        auto update_place = [&] {
            glm::vec2 const position{player.position().x, player.position().z};
            auto const *region = continent.region_at(position);
            auto const *place = continent.location_at(position);
            auto const *road = continent.road_at(position);
            auto const local = player.position() - continent.spawn();
            bool const in_showcase = std::abs(local.x) < 24 && std::abs(local.z) < 23;
            auto const title = std::format("lc1 | {} | {} | 灯光: {} | 曝光: {:.2f}", region->name,
                                           in_showcase ? "材质展示区"
                                           : place     ? place->name
                                           : road      ? road->name
                                                       : "野外",
                                           lighting_names[lighting_mode], output.exposure);
            if (title != current_title) {
                window.set_title(title);
                spdlog::info("[world] {}", title);
                current_title = title;
            }
        };
        update_place();

        // Construct the callback once; it borrows the current drawing inputs.
        std::function const record = [&](vk::raii::CommandBuffer const &command_buffer,
                                         lc1::FrameResources &resources,
                                         lc1::RenderTarget const &target) {
            renderer.record(command_buffer, resources, target, camera, active_lights, draws,
                            output);
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

                if (window.focused()) {
                    if (router.pressed(lc1::Action::CycleLighting))
                        lighting_mode = (lighting_mode + 1) % lighting_names.size();
                    if (router.pressed(lc1::Action::ToggleShowcaseMotion))
                        showcase_animated = !showcase_animated;
                    float const exposure_steps =
                        static_cast<float>(router.pressed(lc1::Action::IncreaseExposure)) -
                        static_cast<float>(router.pressed(lc1::Action::DecreaseExposure));
                    output.exposure = std::clamp(output.exposure * std::exp2(exposure_steps * 0.5F),
                                                 0.03125F, 16.0F);
                }
                if (showcase_animated)
                    showcase_time += std::clamp(delta_seconds, 0.0F, 0.1F);
                showcase.update(showcase_time);
                std::copy(showcase.draws().begin(), showcase.draws().end(),
                          draws.begin() + showcase_offset);

                if (window.focused() && router.pressed(lc1::Action::ToggleCameraView)) {
                    if (!oblique_view)
                        first_person_angles = {player.body_yaw() + player.look_yaw(),
                                               player.look_pitch()};
                    oblique_view = !oblique_view;
                    window.set_cursor_captured(!oblique_view);
                    if (oblique_view) {
                        player.set_look_angles(0.0F, 0.0F);
                        // Preserve zoom and the Y lock state when returning to the player.
                        camera_controller.update(camera, player.position(), continent.bounds(),
                                                 {.recenter = true}, 0.0F);
                    }
                    else {
                        player.set_body_yaw(first_person_angles.x);
                        player.set_look_angles(0.0F, first_person_angles.y);
                        camera.set_position(player.eye_position());
                        camera.look_at(player.eye_position() + player.look_direction());
                    }
                    spdlog::info("[camera] {}", oblique_view ? "update_player2: oblique"
                                                             : "update_player: first-person");
                }
                if (oblique_view)
                    update_player2(window, router, delta_seconds, continent, player,
                                   third_person_controller, camera, camera_controller);
                else
                    update_player(window, router, delta_seconds, first_person_controller, player,
                                  camera);
                // Hold the light above ground and outside the player mesh, on the aiming side.
                lights.back().position = player.position() + glm::vec3{0.0F, 1.3F, 0.0F} +
                                         player.heading() * 0.5F + player.right() * 0.5F;
                lights.back().direction = player.heading();
                active_lights.clear();
                for (auto const &light : lights)
                    if (lighting_mode == 0 ||
                        (lighting_mode <= 4 && light.type == lighting_mode - 1))
                        active_lights.push_back(light);
                update_place();
                draws.back() = player.draw_item();

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
