#include "game-session.hpp"

#include "lc1/image.hpp"
#include "lc1/input.hpp"
#include "lc1/model.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/scene/lights/sphere-light.hpp"
#include "lc1/scene/lights/spot-light.hpp"
#include "lc1/window.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace lc1::app {
namespace {

constexpr std::array lighting_names{"全部", "方向光", "点光", "聚光灯", "球形光", "仅发光材质"};

PlayerInput player_input(InputRouter const &router, glm::vec2 look_delta)
{
    return {.movement = {static_cast<float>(router.held(Action::MoveRight)) -
                             static_cast<float>(router.held(Action::MoveLeft)),
                         static_cast<float>(router.held(Action::MoveForward)) -
                             static_cast<float>(router.held(Action::MoveBackward))},
            .look_delta = look_delta,
            .running = router.held(Action::Sprint)};
}

// A visible, body-sized placeholder for the player now that the camera is above them.
Model make_player_model()
{
    std::vector<Vertex> vertices;
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
    constexpr scene::CharacterConfig config;
    box(config.half_width, 0.0F, 1.2F, {0.06F, 0.25F, 0.8F});
    box(0.22F, 1.2F, config.height, {0.95F, 0.65F, 0.15F});
    return Model{std::move(vertices), std::move(indices)};
}

} // namespace

GameSession::GameSession(Window &window, Device const &device, Renderer &renderer,
                         std::filesystem::path const &asset_root, OutputSettings output)
    : white_{device, Image::load_from_file(asset_root / "textures/prototype-white.png")},
      continent_{Continent::make_prototype(demo::PbrShowcase::spawn_blocks())},
      continent_view_{device, renderer, white_, continent_},
      showcase_{device, renderer, continent_.spawn()},
      player_mesh_{make_player_model().gen_mesh(device)},
      player_material_{renderer.make_material(white_)},
      player_{continent_,
              {.transform = {.position = continent_.spawn()},
               .mesh = &player_mesh_,
               .material = &player_material_}},
      player_view_{continent_, player_.position()}, output_{output}
{
    auto const terrain_draws = continent_view_.draw_items();
    draws_.assign(terrain_draws.begin(), terrain_draws.end());
    showcase_offset_ = draws_.size();
    draws_.insert(draws_.end(), showcase_.draws().begin(), showcase_.draws().end());
    draws_.push_back(player_.draw_item());

    scene::DirectionalLight const sun{
        .base = {.color = glm::vec3{1.0F, 0.95F, 0.9F}, .intensity = 0.3F},
        .direction = glm::normalize(glm::vec3{-1.0F, -1.0F, -1.0F})};
    scene::SphereLight const lamp{.base = {.color = glm::vec3{1.0F, 0.5F, 0.5F}, .intensity = 128},
                                  .position = continent_.spawn() + glm::vec3{3.0F, 10.0F, 0.0F},
                                  .radius = 1.0F,
                                  .shadow_sample_count = 2};
    scene::SpotLight const spotlight{
        .base = {.color = glm::vec3{0.5F, 0.5F, 1.0F}, .intensity = 64},
        .position = continent_.spawn() + glm::vec3{-3.0F, 10.0F, 0.0F},
        .direction = glm::normalize(glm::vec3{1.0F, -1.0F, 0.0F}),
        .inner_angle = glm::radians(15.0F),
        .outer_angle = glm::radians(30.0F)};
    lights_ = {to_light(sun), to_light(lamp), to_light(spotlight)};
    // Keep the player's flashlight last so updates can follow the player.
    lights_.insert(lights_.end() - 1, showcase_.lights().begin(), showcase_.lights().end());
    active_lights_ = lights_;

    window.set_cursor_captured(false);
    spdlog::info("[world] WASD: walk, wheel: zoom, Shift: run, Y: camera lock, Space: "
                 "recenter, M: first-person/oblique view, Esc: quit");
    spdlog::info("[showcase] material exhibits beside the spawn road; F4: lighting, "
                 "F5: pause motion, PageUp/PageDown: exposure");
    spdlog::info(
        "[world] {} regions, {} locations, {} roads, {} terrain chunks; fixed oblique view",
        continent_.regions().size(), continent_.locations().size(), continent_.roads().size(),
        terrain_draws.size());
    update_title(window);
}

void GameSession::update_showcase(InputRouter const &router, bool focused, float delta_seconds)
{
    if (focused) {
        if (router.pressed(Action::CycleLighting))
            lighting_mode_ = (lighting_mode_ + 1) % lighting_names.size();
        if (router.pressed(Action::ToggleShowcaseMotion))
            showcase_animated_ = !showcase_animated_;
        float const exposure_steps = static_cast<float>(router.pressed(Action::IncreaseExposure)) -
                                     static_cast<float>(router.pressed(Action::DecreaseExposure));
        output_.exposure =
            std::clamp(output_.exposure * std::exp2(exposure_steps * 0.5F), 0.03125F, 16.0F);
    }
    if (showcase_animated_)
        showcase_time_ += std::clamp(delta_seconds, 0.0F, 0.1F);
    showcase_.update(showcase_time_);
    std::copy(showcase_.draws().begin(), showcase_.draws().end(),
              draws_.begin() + showcase_offset_);
}

void GameSession::update_player(Window &window, InputRouter const &router, float delta_seconds)
{
    if (window.focused() && router.pressed(Action::ToggleCameraView)) {
        player_view_.toggle(player_);
        // Do this before reading the cursor delta: changing capture resets its baseline.
        window.set_cursor_captured(!player_view_.oblique());
    }
    auto const &input = window.input();
    player_view_.update(
        player_, player_input(router, player_view_.oblique() ? glm::vec2{} : input.cursor_delta()),
        {.enabled = router.active_context() == InputContext::Gameplay && window.focused(),
         .toggle_lock = router.pressed(Action::ToggleCameraLock),
         .recenter = router.held(Action::RecenterCamera),
         .scroll = input.scroll().y,
         .cursor = input.cursor(),
         .viewport_size = window.content_size(),
         .pointer_active = window.hovered() && window.focused() && !window.cursor_captured()},
        delta_seconds);
    draws_.back() = player_.draw_item();
}

void GameSession::update_lighting()
{
    // Hold the flashlight above ground and outside the mesh, on the aiming side.
    lights_.back().position = player_.position() + glm::vec3{0.0F, 1.3F, 0.0F} +
                              player_.heading() * 0.5F + player_.right() * 0.5F;
    lights_.back().direction = player_.heading();
    active_lights_.clear();
    for (auto const &light : lights_)
        if (lighting_mode_ == 0 || (lighting_mode_ <= 4 && light.type == lighting_mode_ - 1))
            active_lights_.push_back(light);
}

void GameSession::update_title(Window &window)
{
    // Location follows the character even while the camera is panned elsewhere.
    glm::vec2 const position{player_.position().x, player_.position().z};
    auto const *region = continent_.region_at(position);
    auto const *place = continent_.location_at(position);
    auto const *road = continent_.road_at(position);
    auto const local = player_.position() - continent_.spawn();
    bool const in_showcase = std::abs(local.x) < 24 && std::abs(local.z) < 23;
    auto const title = std::format("lc1 | {} | {} | 灯光: {} | 曝光: {:.2f}", region->name,
                                   in_showcase ? "材质展示区"
                                   : place     ? place->name
                                   : road      ? road->name
                                               : "野外",
                                   lighting_names[lighting_mode_], output_.exposure);
    if (title != current_title_) {
        window.set_title(title);
        spdlog::info("[world] {}", title);
        current_title_ = title;
    }
}

void GameSession::update(Window &window, InputRouter const &router, float delta_seconds)
{
    update_showcase(router, window.focused(), delta_seconds);
    update_player(window, router, delta_seconds);
    update_lighting();
    update_title(window);
}

} // namespace lc1::app
